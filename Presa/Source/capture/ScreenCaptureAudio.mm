#include "ScreenCaptureAudio.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>

#include <vector>

// ══════════════════════════════════════════════════════════════════════════
// File-based debug log — stderr is not attached when launched from Finder.
// ══════════════════════════════════════════════════════════════════════════
static void sckLog (NSString* msg)
{
    if (msg == nil) return;
    if (FILE* f = fopen ("/tmp/presa_debug.log", "a"))
    {
        fprintf (f, "[SCK] %s\n", msg.UTF8String);
        fclose (f);
    }
}

// Nil-safe + UTF-8-aware bridge from NSString to juce::String.
// Avoids the juce_String.cpp:327 assertion that fires when the
// juce::String (const char*) constructor sees bytes > 127 (which any
// non-ASCII localized error message will have).
static juce::String juceFromNSString (NSString* s, const char* fallback)
{
    if (s == nil) return juce::String (fallback);
    const char* utf8 = s.UTF8String;
    if (utf8 == nullptr) return juce::String (fallback);
    return juce::String::fromUTF8 (utf8);
}

// ══════════════════════════════════════════════════════════════════════════
// ObjC stream handler
//
// Owns the SCStream and forwards audio into the C++ CaptureBuffer held by
// the ScreenCaptureAudio instance whose pointer is stored in `owner`.
// The C++ destructor calls `invalidate` under `ownerLock` to prevent UAF
// if a late ScreenCaptureKit callback fires after the C++ object is gone.
// ══════════════════════════════════════════════════════════════════════════
API_AVAILABLE(macos(13.0))
@interface PresaSCStreamHandler : NSObject <SCStreamOutput, SCStreamDelegate>
{
    NSLock* ownerLock;
    ScreenCaptureAudio* owner;
}
@property (nonatomic, strong) SCStream* stream;
@property (nonatomic, strong) dispatch_queue_t audioQueue;

- (instancetype)initWithOwner:(ScreenCaptureAudio*)o;
- (void)invalidate;
- (ScreenCaptureAudio*)lockedOwner;  // must be paired with -unlockOwner
- (void)unlockOwner;
@end

@implementation PresaSCStreamHandler

- (instancetype)initWithOwner:(ScreenCaptureAudio*)o
{
    if ((self = [super init]))
    {
        ownerLock  = [[NSLock alloc] init];
        owner      = o;
        _audioQueue = dispatch_queue_create ("com.forma.presa.sck.audio",
                                             DISPATCH_QUEUE_SERIAL);
    }
    return self;
}

- (void)invalidate
{
    [ownerLock lock];
    owner = nullptr;
    [ownerLock unlock];
}

- (ScreenCaptureAudio*)lockedOwner
{
    [ownerLock lock];
    return owner;
}

- (void)unlockOwner
{
    [ownerLock unlock];
}

// ── SCStreamDelegate: stream stopped unexpectedly ─────────────────────────
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error
{
    @autoreleasepool
    {
        NSString* errText = (error != nil && error.localizedDescription != nil)
                            ? error.localizedDescription : @"unknown";
        NSString* msg = [NSString stringWithFormat:@"Stream stopped: %@", errText];
        sckLog (msg);

        ScreenCaptureAudio* o = [self lockedOwner];
        if (o != nullptr)
            o->reportError (juceFromNSString (msg, "stream stopped"));
        [self unlockOwner];
    }
}

// ── SCStreamOutput: audio sample buffer delivered on our serial queue ─────
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type
{
    @autoreleasepool
    {
        if (type != SCStreamOutputTypeAudio) return;
        if (sampleBuffer == nullptr) return;
        if (!CMSampleBufferIsValid (sampleBuffer)) return;

        CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription (sampleBuffer);
        if (fmt == nullptr) return;

        const AudioStreamBasicDescription* asbd =
            CMAudioFormatDescriptionGetStreamBasicDescription (fmt);
        if (asbd == nullptr) return;

        const UInt32 channels   = asbd->mChannelsPerFrame;
        const UInt32 bytesFrame = asbd->mBytesPerFrame;
        const bool   isFloat    = (asbd->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
        const bool   isPlanar   = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
        const UInt32 bitDepth   = asbd->mBitsPerChannel;

        if (!isFloat || bitDepth != 32 || channels == 0)
            return;

        const CMItemCount numFrames = CMSampleBufferGetNumSamples (sampleBuffer);
        if (numFrames <= 0) return;

        // Publish the real sample rate so PluginProcessor stops using the
        // host output rate as a proxy for the capture rate. Cheap to do every
        // call — it's an atomic store of a stable value.
        {
            ScreenCaptureAudio* o = [self lockedOwner];
            if (o != nullptr)
                o->setDeliveredSampleRate (asbd->mSampleRate);
            [self unlockOwner];
        }

        // First-callback diagnostic — confirms SCK's actual delivery format.
        {
            static std::atomic<bool> logged { false };
            bool expected = false;
            if (logged.compare_exchange_strong (expected, true))
            {
                fprintf (stderr,
                         "SCK format: sr=%.0f channels=%u bitsPerChannel=%u "
                         "bytesPerFrame=%u framesPerPacket=%u formatFlags=0x%x "
                         "isPlanar=%d isFloat=%d numFramesThisBuffer=%lld\n",
                         asbd->mSampleRate,
                         asbd->mChannelsPerFrame,
                         asbd->mBitsPerChannel,
                         asbd->mBytesPerFrame,
                         asbd->mFramesPerPacket,
                         asbd->mFormatFlags,
                         isPlanar, isFloat,
                         (long long) numFrames);
                fflush (stderr);

                if (FILE* f = fopen ("/tmp/presa_debug.log", "a"))
                {
                    fprintf (f,
                             "[SCK] format sr=%.0f ch=%u bits=%u bpf=%u fpp=%u "
                             "flags=0x%x planar=%d float=%d nframes=%lld\n",
                             asbd->mSampleRate,
                             asbd->mChannelsPerFrame,
                             asbd->mBitsPerChannel,
                             asbd->mBytesPerFrame,
                             asbd->mFramesPerPacket,
                             asbd->mFormatFlags,
                             isPlanar, isFloat,
                             (long long) numFrames);
                    fclose (f);
                }
            }
        }

        // AudioBufferList sizing: one mBuffer per channel when planar.
        AudioBufferList  abl;
        AudioBufferList* ablPtr  = &abl;
        size_t           ablSize = sizeof (AudioBufferList);
        std::vector<uint8_t> ablStorage;

        if (isPlanar && channels > 1)
        {
            ablSize = sizeof (AudioBufferList) + (channels - 1) * sizeof (AudioBuffer);
            ablStorage.resize (ablSize);
            ablPtr = reinterpret_cast<AudioBufferList*> (ablStorage.data());
        }

        CMBlockBufferRef blockBuffer = nullptr;
        OSStatus status =
            CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer (
                sampleBuffer,
                nullptr,
                ablPtr,
                ablSize,
                kCFAllocatorDefault,
                kCFAllocatorDefault,
                kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
                &blockBuffer);

        if (status != noErr || blockBuffer == nullptr || ablPtr->mNumberBuffers == 0)
        {
            if (blockBuffer) CFRelease (blockBuffer);
            return;
        }

        std::vector<float> leftScratch;
        std::vector<float> rightScratch;
        const float* chanPtrs[2] = { nullptr, nullptr };

        if (isPlanar)
        {
            const float* p0 = static_cast<const float*> (ablPtr->mBuffers[0].mData);
            if (p0 == nullptr) { CFRelease (blockBuffer); return; }
            chanPtrs[0] = p0;

            if (ablPtr->mNumberBuffers > 1)
            {
                const float* p1 = static_cast<const float*> (ablPtr->mBuffers[1].mData);
                chanPtrs[1] = (p1 != nullptr) ? p1 : p0;
            }
            else
            {
                chanPtrs[1] = p0; // mono → duplicate
            }
        }
        else
        {
            const float* src = static_cast<const float*> (ablPtr->mBuffers[0].mData);
            if (src == nullptr) { CFRelease (blockBuffer); return; }

            leftScratch.resize ((size_t) numFrames);
            rightScratch.resize ((size_t) numFrames);

            if (channels == 1)
            {
                for (CMItemCount i = 0; i < numFrames; ++i)
                    leftScratch[(size_t) i] = rightScratch[(size_t) i] = src[i];
            }
            else
            {
                const UInt32 stride = bytesFrame / sizeof (float);
                for (CMItemCount i = 0; i < numFrames; ++i)
                {
                    leftScratch[(size_t) i]  = src[i * stride + 0];
                    rightScratch[(size_t) i] = src[i * stride + 1];
                }
            }

            chanPtrs[0] = leftScratch.data();
            chanPtrs[1] = rightScratch.data();
        }

        // Deliver to the C++ side under the owner lock so a concurrent
        // destruction in ScreenCaptureAudio::~ScreenCaptureAudio cannot
        // free the CaptureBuffer out from under us mid-write.
        ScreenCaptureAudio* o = [self lockedOwner];
        if (o != nullptr && chanPtrs[0] != nullptr && chanPtrs[1] != nullptr)
        {
            CaptureBuffer& buf = o->getCaptureBuffer();
            buf.updateInputRMS (chanPtrs[0], (int) numFrames);
            buf.pushSamples (chanPtrs, 2, (int) numFrames);
        }
        [self unlockOwner];

        CFRelease (blockBuffer);
    }
}

@end

// ══════════════════════════════════════════════════════════════════════════
// Opaque container held by ScreenCaptureAudio::impl
// ══════════════════════════════════════════════════════════════════════════
struct SCKImpl
{
    PresaSCStreamHandler* handler = nil;
};

// ══════════════════════════════════════════════════════════════════════════
// Helper: finalise stream setup. Runs on the main queue so that async
// completion blocks don't contend with ScreenCaptureKit's internal XPC.
// Captures `self` (the ObjC handler) strongly to keep it alive through
// the async chain; the C++ owner is reached via [handler lockedOwner].
// ══════════════════════════════════════════════════════════════════════════
API_AVAILABLE(macos(13.0))
static void startStreamWithContent (PresaSCStreamHandler* handler,
                                    SCShareableContent*   content)
{
    @autoreleasepool
    {
        if (handler == nil) return;

        ScreenCaptureAudio* owner = [handler lockedOwner];
        if (owner == nullptr) { [handler unlockOwner]; return; }
        // Keep the lock held only long enough to publish status; release
        // before calling blocking ScreenCaptureKit APIs.
        [handler unlockOwner];

        if (content == nil || content.displays.count == 0)
        {
            ScreenCaptureAudio* o2 = [handler lockedOwner];
            if (o2 != nullptr) o2->reportError ("No displays available");
            [handler unlockOwner];
            return;
        }

        SCDisplay* display = content.displays.firstObject;
        if (display == nil)
        {
            ScreenCaptureAudio* o2 = [handler lockedOwner];
            if (o2 != nullptr) o2->reportError ("No primary display");
            [handler unlockOwner];
            return;
        }

        SCContentFilter* filter =
            [[SCContentFilter alloc] initWithDisplay:display
                                excludingApplications:@[]
                                     exceptingWindows:@[]];

        SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
        cfg.capturesAudio               = YES;
        cfg.excludesCurrentProcessAudio = YES;
        cfg.sampleRate                  = 48000;
        cfg.channelCount                = 2;
        cfg.minimumFrameInterval        = CMTimeMake (1, 1);
        cfg.width                       = 2;
        cfg.height                      = 2;

        SCStream* stream = [[SCStream alloc] initWithFilter:filter
                                              configuration:cfg
                                                   delegate:handler];

        NSError* addErr = nil;
        BOOL added = [stream addStreamOutput:handler
                                        type:SCStreamOutputTypeAudio
                          sampleHandlerQueue:handler.audioQueue
                                       error:&addErr];
        if (!added)
        {
            NSString* desc = (addErr != nil && addErr.localizedDescription != nil)
                             ? addErr.localizedDescription : @"addStreamOutput failed";
            sckLog ([NSString stringWithFormat:@"addStreamOutput failed: %@", desc]);

            ScreenCaptureAudio* o2 = [handler lockedOwner];
            if (o2 != nullptr)
                o2->reportError (juceFromNSString (desc, "addStreamOutput failed"));
            [handler unlockOwner];
            return;
        }

        handler.stream = stream;

        [stream startCaptureWithCompletionHandler:^(NSError* err)
        {
            @autoreleasepool
            {
                if (err != nil)
                {
                    NSString* desc = err.localizedDescription != nil
                                     ? err.localizedDescription : @"startCapture failed";
                    sckLog ([NSString stringWithFormat:@"startCapture error: %@", desc]);

                    ScreenCaptureAudio* o2 = [handler lockedOwner];
                    if (o2 != nullptr)
                        o2->reportError (juceFromNSString (desc, "startCapture failed"));
                    [handler unlockOwner];
                    return;
                }

                ScreenCaptureAudio* o2 = [handler lockedOwner];
                if (o2 != nullptr)
                {
                    o2->markRunning (true);
                    o2->setStatus ("System Audio");
                }
                [handler unlockOwner];
                sckLog (@"ScreenCaptureAudio: stream started");
            }
        }];
    }
}

// ══════════════════════════════════════════════════════════════════════════
// ScreenCaptureAudio — C++ façade
// ══════════════════════════════════════════════════════════════════════════
ScreenCaptureAudio::ScreenCaptureAudio (CaptureBuffer& buffer)
    : captureBuffer (buffer)
{
    impl = new SCKImpl();
}

ScreenCaptureAudio::~ScreenCaptureAudio()
{
    stop();

    if (impl != nullptr)
    {
        auto* sck = static_cast<SCKImpl*> (impl);

        // Sever the handler→owner back-reference before we tear down.
        // Any in-flight SCK callback will see owner==nullptr and bail.
        if (@available (macOS 13.0, *))
        {
            if (sck->handler != nil)
                [sck->handler invalidate];
        }

        sck->handler = nil;
        delete sck;
        impl = nullptr;
    }
}

juce::String ScreenCaptureAudio::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

void ScreenCaptureAudio::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    status = s;
}

void ScreenCaptureAudio::markRunning (bool r)
{
    running.store (r);
}

void ScreenCaptureAudio::reportError (const juce::String& message)
{
    setStatus ("Error: " + message);
    running.store (false);
}

juce::String ScreenCaptureAudio::start()
{
    if (@available (macOS 13.0, *))
    {
        @autoreleasepool
        {
            if (running.load()) return {};

            auto* sck = static_cast<SCKImpl*> (impl);
            if (sck == nullptr) return "Internal error: no impl";

            sckLog (@"ScreenCaptureAudio::start() — requesting shareable content");
            setStatus ("Requesting permission...");

            // Create (or reuse) the ObjC handler; it must outlive all the
            // async callbacks below, so we keep a strong ref in SCKImpl.
            if (sck->handler == nil)
                sck->handler = [[PresaSCStreamHandler alloc] initWithOwner:this];

            PresaSCStreamHandler* handler = sck->handler;

            // getShareableContent invokes its completion on an internal
            // private queue; we MUST NOT block the main queue waiting for it
            // because it uses NSXPC (to replayd) and that requires the main
            // runloop to drain. So: async all the way down, no semaphores.
            [SCShareableContent getShareableContentWithCompletionHandler:
             ^(SCShareableContent* content, NSError* error)
            {
                @autoreleasepool
                {
                    if (error != nil || content == nil)
                    {
                        NSString* desc = (error != nil && error.localizedDescription != nil)
                                         ? error.localizedDescription
                                         : @"Screen Recording permission required";
                        sckLog ([NSString stringWithFormat:@"getShareableContent error: %@", desc]);

                        ScreenCaptureAudio* o = [handler lockedOwner];
                        if (o != nullptr)
                            o->reportError (juceFromNSString (desc, "Screen Recording permission required"));
                        [handler unlockOwner];
                        return;
                    }

                    // Move to the main queue for stream setup so we interact
                    // with ScreenCaptureKit from a stable, drained runloop.
                    dispatch_async (dispatch_get_main_queue(), ^{
                        @autoreleasepool
                        {
                            startStreamWithContent (handler, content);
                        }
                    });
                }
            }];

            // Synchronous return: the caller will see status == "Requesting
            // permission..." until the async flow resolves to either
            // "System Audio" or "Error: ...".
            return {};
        }
    }

    setStatus ("macOS 13+ required");
    return "ScreenCaptureKit audio requires macOS 13 or later";
}

void ScreenCaptureAudio::stop()
{
    if (@available (macOS 13.0, *))
    {
        @autoreleasepool
        {
            running.store (false);

            if (impl == nullptr) return;
            auto* sck = static_cast<SCKImpl*> (impl);
            if (sck == nullptr || sck->handler == nil) return;

            SCStream* stream = sck->handler.stream;
            sck->handler.stream = nil;

            if (stream != nil)
            {
                // Fire and forget: do NOT block any thread here. A pending
                // startCapture callback may be in flight, and blocking on a
                // semaphore would reintroduce the exact deadlock we just fixed.
                [stream stopCaptureWithCompletionHandler:^(NSError* err)
                {
                    @autoreleasepool
                    {
                        if (err != nil)
                            sckLog ([NSString stringWithFormat:@"stopCapture error: %@",
                                                               err.localizedDescription]);
                    }
                }];
            }

            setStatus ("Idle");
        }
    }
}

#endif // JUCE_MAC
