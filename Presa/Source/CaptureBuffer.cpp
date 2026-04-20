#include "CaptureBuffer.h"

static void plog (const char* msg)
{
    if (auto* f = fopen ("/tmp/presa_debug.log", "a"))
    { fprintf (f, "%s\n", msg); fclose (f); }
}
static void plog (const juce::String& s) { plog (s.toRawUTF8()); }

CaptureBuffer::CaptureBuffer() {}

void CaptureBuffer::prepare (double sr, int numChannels)
{
    sampleRate   = sr;
    bufferLength = static_cast<int> (sr * kMaxSeconds);
    buffer.setSize (numChannels, bufferLength);
    buffer.clear();
    writeHead.store (0);
    samplesWritten.store (0);
    state.store (State::Idle);
}

void CaptureBuffer::pushSamples (const float* const* channelData, int numChannels, int numSamples)
{
    auto ch = juce::jmin (numChannels, buffer.getNumChannels());
    int wh = writeHead.load();

    // Compute RMS of incoming block (channel 0)
    float sumSq = 0.0f;
    if (ch > 0 && numSamples > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float s = channelData[0][i];
            sumSq += s * s;
        }
    }
    float rms = (numSamples > 0) ? std::sqrt (sumSq / numSamples) : 0.0f;
    captureRMS.store (rms);

    // Debug: log first non-silent audio
    if (!hasLoggedFirstAudio && rms > 0.001f)
    {
        plog ("CaptureBuffer: first audio received, RMS=" + juce::String (rms, 6)
             + " state=" + juce::String (static_cast<int> (state.load())));
        hasLoggedFirstAudio = true;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < ch; ++c)
            buffer.setSample (c, wh, channelData[c][i]);

        wh = (wh + 1) % bufferLength;
    }

    writeHead.store (wh);

    int sw = samplesWritten.load();
    samplesWritten.store (juce::jmin (sw + numSamples, bufferLength));
}

void CaptureBuffer::clear()
{
    buffer.clear();
    writeHead.store (0);
    samplesWritten.store (0);
    state.store (State::Idle);
}

void CaptureBuffer::readSamples (juce::AudioBuffer<float>& dest, int startSample, int numSamples) const
{
    auto ch = juce::jmin (dest.getNumChannels(), buffer.getNumChannels());
    int wh = writeHead.load();
    int sw = samplesWritten.load();

    for (int i = 0; i < numSamples; ++i)
    {
        int readIdx = (wh - sw + startSample + i + bufferLength) % bufferLength;
        for (int c = 0; c < ch; ++c)
            dest.setSample (c, i, buffer.getSample (c, readIdx));
    }
}

void CaptureBuffer::updateInputRMS (const float* data, int numSamples)
{
    if (data == nullptr || numSamples <= 0)
    {
        inputRMS.store (0.0f);
        return;
    }

    float sumSq = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        sumSq += data[i] * data[i];

    inputRMS.store (std::sqrt (sumSq / numSamples));
}

// ── State machine ──────────────────────────────────────────────────────────

void CaptureBuffer::arm()
{
    DBG ("CaptureBuffer::arm() - writeHead=" + juce::String (writeHead.load())
         + " samplesWritten=" + juce::String (samplesWritten.load()));
    state.store (State::Armed);
}

void CaptureBuffer::punch()
{
    punchInSample = writeHead.load();
    plog ("CaptureBuffer::punch() - punchIn=" + juce::String (punchInSample)
         + " samplesWritten=" + juce::String (samplesWritten.load())
         + " captureRMS=" + juce::String (captureRMS.load(), 6));
    state.store (State::Capturing);
}

void CaptureBuffer::stop()
{
    if (state.load() == State::Capturing)
    {
        punchOutSample = writeHead.load();
        int len = punchOutSample - punchInSample;
        if (len <= 0) len += bufferLength;
        plog ("CaptureBuffer::stop() - punchIn=" + juce::String (punchInSample)
             + " punchOut=" + juce::String (punchOutSample)
             + " sliceLen=" + juce::String (len)
             + " captureRMS=" + juce::String (captureRMS.load(), 6));
        sliceReady.store (true);
    }
    else
    {
        plog ("CaptureBuffer::stop() - WARNING: state was NOT Capturing! state="
             + juce::String (static_cast<int> (state.load())));
    }
    state.store (State::Idle);
}

bool CaptureBuffer::hasNewSlice()
{
    return sliceReady.exchange (false);
}

juce::AudioBuffer<float> CaptureBuffer::commitSlice()
{
    int ch = buffer.getNumChannels();

    // Calculate slice length, handling ring-buffer wrap
    int len = punchOutSample - punchInSample;
    if (len <= 0)
        len += bufferLength;

    // Clamp to buffer size
    if (len <= 0 || len > bufferLength)
        return {};

    juce::AudioBuffer<float> result (ch, len);

    for (int i = 0; i < len; ++i)
    {
        int srcIdx = (punchInSample + i) % bufferLength;
        for (int c = 0; c < ch; ++c)
            result.setSample (c, i, buffer.getSample (c, srcIdx));
    }

    return result;
}
