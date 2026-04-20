# Forma Design Language

## Aesthetic Summary

Dark, sophisticated audio-production interface with organic, breathing energy. Deep warm blacks with burnt-orange accents. Monospace-dominant typography signals technical precision; smooth animated gradients and glowing state changes provide life and warmth. The feel is: **professional DAW tool meets living instrument**.

---

## Color Palette

### Backgrounds (dark → darkest)

| Token | Hex | Use |
|-------|-----|-----|
| BG | `#1A1814` | Primary background |
| BG2 | `#141210` | Side panel background |
| BG3 | `#1E1C18` | Secondary / hover background |
| BG4 | `#0F0E0B` | Top/bottom bar (darkest) |
| DARK | `#262420` | Dark accent fill |

### Borders

| Token | Hex |
|-------|-----|
| BORDER | `#252318` |
| BORDER2 | `#2A2820` |

### Accent

| Token | Hex | Use |
|-------|-----|-----|
| ACCENT | `#C8875A` | Primary interactive color (warm burnt orange) |
| ACCENT_DK | `#5A3A20` | Muted accent for borders/indicators |
| SUGGEST2 | `#6A8FAA` | Secondary accent (muted steel blue) |
| LINK_ACTIVE | `#5AAA5A` | Connected/active state (green) |

### Text

| Token | Hex | Use |
|-------|-----|-----|
| TXT_HI | `#E8E4DC` | Primary / high-contrast |
| TXT_MID | `#9A9590` | Secondary labels |
| TXT_DIM | `#4A4840` | Tertiary / disabled |
| TXT_GHOST | `#3A3830` | Barely visible hints |
| TXT_DARK | `#2A2820` | Embossed / inset text |

### Color Relationships

- Backgrounds are warm-tinted near-blacks (slight amber undertone in the hex values)
- Text colors share the same warm undertone, never pure gray
- Accent is a single warm hue (burnt orange ~38deg) used sparingly for focus/active states
- Secondary accent (steel blue) appears only for alternative suggestions

---

## Typography

| Context | Family | Size | Weight |
|---------|--------|------|--------|
| Logo | Mono | 15px | Plain |
| Section headers | Mono | 8px | Plain |
| Body labels | Mono | 9-10px | Plain |
| Mood names | Mono | 11px | Plain |
| Value displays | Mono | 10-11px | Plain |
| Chord names (featured) | Sans-serif | 15px | Bold |
| Key display (large) | Mono | 26px | Plain |
| Compass labels | Mono | 12px | Plain |
| Smallest text | Mono | 7px | Plain |

**Rule**: Monospace is the default for everything. Sans-serif bold is reserved for the single most prominent data point in a component (e.g. chord name).

---

## Dimensions & Spacing

| Property | Value |
|----------|-------|
| Window | 780 x 564 px |
| Top bar height | 40px |
| Status bar height | 24px |
| Standard border width | 1px |
| Button height | 18-20px |
| Item row height | 23px |
| Item gap | 2px |
| Card padding | ~15px |

---

## Border Radius

| Element type | Radius |
|-------------|--------|
| Small buttons / tags | 3-4px |
| Cards / panels | 6px |
| Pill buttons | 4px |
| Toggle switches | 8px |
| Key caps (large rounded) | 10px |

**Rule**: Corners are softened but never fully round. 3-6px is the common range.

---

## Effects

### Glows (accent feedback)

- Active/pressed elements get a **2px expanded stroke** of ACCENT at 10-15% alpha
- Toggles get a **6px expanded fill** of ACCENT at 30% alpha
- Multi-pass glow rings: 3 strokes at 7px/3px/1px width, 10%/18%/38% alpha

### Shadows

- Inset shadows on pressed elements: gradient from `#E0000000` → transparent
- Top bar casts a 1px shadow line of `#0A0908`

### Gradients

- **Thermal gradients**: 5-stop radial gradients unique per mood (void → inner → mid1 → mid2 → outer → edge)
- Feathered edges via alpha fade (1.0 → 0.0)
- No sharp drop-shadows; all depth is communicated through color shift

---

## Animation

| Property | Duration/Rate |
|----------|--------------|
| Mood color transition | 400ms cubic ease (t squared (3-2t)) |
| Dot position transition | 500ms same easing |
| Suggestion pulse | ~380ms full cycle |
| Wave deformation | Continuous, multi-frequency (0.2-0.7 Hz base) |
| Breathing phase | 0.7 rad/sec drift |

**Rule**: Motion is organic and continuous, never snappy or bouncy. Easing is always smooth cubic, never linear or spring-based.

---

## Mood Thermal Palettes (12 moods)

Each mood defines a 5-color radial gradient from center (void) to edge:

| Mood | Void | Edge | Hue |
|------|------|------|-----|
| Bright | `#080602` | `#C88838` | 38 (amber) |
| Warm | `#090802` | `#B88028` | 34 (gold) |
| Dream | `#060508` | `#7858A8` | 255 (indigo) |
| Deep | `#090702` | `#B06C22` | 24 (burnt amber) |
| Hollow | `#050708` | `#506878` | 205 (cold blue-gray) |
| Tender | `#080507` | `#9C5078` | 318 (plum) |
| Tense | `#0A0302` | `#B04020` | 10 (dark red) |
| Dusk | `#090802` | `#C49028` | 38 (warm gold) |
| Crest | `#040810` | `#87CEEB` | 200 (sky blue) |
| Nocturne | `#060610` | `#4A4E6B` | 240 (blue-violet) |
| Shimmer | `#060810` | `#B0C4DE` | 210 (silver-blue) |
| Static | `#100408` | `#FF69B4` | 330 (hot pink) |

---

## Design Principles

1. **Warmth in darkness** — Backgrounds are never pure black or neutral gray; always warm-tinted
2. **One accent, used sparingly** — Burnt orange marks interactive/active state only
3. **Monospace as identity** — Technical aesthetic without being cold
4. **Depth through transparency** — Alpha-blended glows and color shifts, never drop shadows
5. **Organic motion** — Multi-frequency waves, breathing phases; nothing mechanical
6. **Compact density** — Small fonts (7-11px), tight spacing; information-rich without clutter
7. **Subtle hierarchy** — States differ by 1-2 hex digits, not by stark contrast
