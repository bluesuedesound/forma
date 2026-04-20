#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// ══════════════════════════════════════════════════════════════════════════
// Forma / Presa shared design-language colour tokens
// ══════════════════════════════════════════════════════════════════════════

namespace Palette
{
    // ── Backgrounds ────────────────────────────────────────────────────────
    constexpr juce::uint32 BG_RAW        = 0xFF1A1814;
    constexpr juce::uint32 BG2_RAW       = 0xFF141210;
    constexpr juce::uint32 BG3_RAW       = 0xFF1E1C18;
    constexpr juce::uint32 BG4_RAW       = 0xFF0F0E0B;
    constexpr juce::uint32 DARK_RAW      = 0xFF262420;

    // ── Borders ────────────────────────────────────────────────────────────
    constexpr juce::uint32 BORDER_RAW    = 0xFF252318;
    constexpr juce::uint32 BORDER2_RAW   = 0xFF2A2820;

    // ── Accent ─────────────────────────────────────────────────────────────
    constexpr juce::uint32 ACCENT_RAW    = 0xFFC8875A;
    constexpr juce::uint32 ACCENT_DK_RAW = 0xFF5A3A20;

    // ── Text ───────────────────────────────────────────────────────────────
    constexpr juce::uint32 TXT_HI_RAW    = 0xFFE8E4DC;
    constexpr juce::uint32 TXT_MID_RAW   = 0xFF9A9590;
    constexpr juce::uint32 TXT_DIM_RAW   = 0xFF4A4840;
    constexpr juce::uint32 TXT_GHOST_RAW = 0xFF3A3830;
    constexpr juce::uint32 TXT_DARK_RAW  = 0xFF2A2820;

    // ── Interactive state ──────────────────────────────────────────────────
    constexpr juce::uint32 LINK_ACTIVE_RAW = 0xFF5AAA5A;

    // ── Convenience Colour objects ─────────────────────────────────────────
    inline juce::Colour BG        () { return juce::Colour (BG_RAW); }
    inline juce::Colour BG2       () { return juce::Colour (BG2_RAW); }
    inline juce::Colour BG3       () { return juce::Colour (BG3_RAW); }
    inline juce::Colour BG4       () { return juce::Colour (BG4_RAW); }
    inline juce::Colour DARK      () { return juce::Colour (DARK_RAW); }
    inline juce::Colour BORDER    () { return juce::Colour (BORDER_RAW); }
    inline juce::Colour BORDER2   () { return juce::Colour (BORDER2_RAW); }
    inline juce::Colour ACCENT    () { return juce::Colour (ACCENT_RAW); }
    inline juce::Colour ACCENT_DK () { return juce::Colour (ACCENT_DK_RAW); }
    inline juce::Colour TXT_HI    () { return juce::Colour (TXT_HI_RAW); }
    inline juce::Colour TXT_MID   () { return juce::Colour (TXT_MID_RAW); }
    inline juce::Colour TXT_DIM   () { return juce::Colour (TXT_DIM_RAW); }
    inline juce::Colour TXT_GHOST () { return juce::Colour (TXT_GHOST_RAW); }
    inline juce::Colour TXT_DARK  () { return juce::Colour (TXT_DARK_RAW); }
    inline juce::Colour LINK_ACTIVE () { return juce::Colour (LINK_ACTIVE_RAW); }

    // ── Mood thermal palettes (for StateDotField quadrants) ────────────────
    // Each mood has an inner (centre) and outer (edge) colour for gradients.

    // LIGHT + FIXED (bottom-left) — Nocturne
    inline juce::Colour NOCTURNE_INNER () { return juce::Colour (0xFF060610u); }
    inline juce::Colour NOCTURNE_OUTER () { return juce::Colour (0xFF4A4E6Bu); }

    // LIGHT + BREATHING (top-left) — Hollow / Shimmer
    inline juce::Colour HOLLOW_INNER () { return juce::Colour (0xFF050708u); }
    inline juce::Colour HOLLOW_OUTER () { return juce::Colour (0xFF506878u); }

    // WORN + FIXED (bottom-right) — Deep / Warm
    inline juce::Colour WARM_INNER () { return juce::Colour (0xFF090802u); }
    inline juce::Colour WARM_OUTER () { return juce::Colour (0xFFB88028u); }

    // WORN + BREATHING (top-right) — Tense / Bright
    inline juce::Colour TENSE_INNER () { return juce::Colour (0xFF0A0302u); }
    inline juce::Colour TENSE_OUTER () { return juce::Colour (0xFFB04020u); }
}
