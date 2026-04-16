# Exposure Triangle — Porting Notes for TouchDesigner

State Library of NSW / Max Dupain Exhibition  
Phase 1 prototype documented for TouchDesigner port.

---

## 1. Serial Message Format

**Baud rate:** 115200

**Continuous encoder message** (sent whenever any encoder value changes):
```
A:127,S:64,I:200\n
```

**Button press message** (sent on press only, not release or hold):
```
A:127,S:64,I:200,BTN:2\n
```

- `A` = Aperture encoder (0–255)
- `S` = Shutter Speed encoder (0–255)
- `I` = ISO encoder (0–255)
- `BTN` = 1-indexed button number (1–4). **Only present on press events.**
- `BTN:0` is never transmitted.
- Line terminated with `\n` (LF only, no CR).

---

## 2. Encoder → Effect Mathematics

All encoder values are clamped integers in the range **0–255**.

### Normalised value
```
t = encoderValue / 255.0       // range 0.0 to 1.0
```

### 2.1 Aperture (f/1.4 → f/16)

```
apertureT    = encoderValue / 255.0
blurAmount   = (1.0 - apertureT)                  // 0=sharp, 1=max blur
blurRadius   = depthValue × blurAmount × MAX_BLUR  // per-pixel, depth-weighted

// MAX_BLUR expressed as a fraction of image height, e.g. 0.028 (28px at 1080p)

// Exposure shift (applied in final composite pass):
apertureBrightness = lerp(+0.15, -0.10, apertureT)
// → wide open: +0.15 stops brighter; stopped down: −0.10 stops darker
// Applied as: colour = pow(colour, 1.0 − apertureBrightness)
```

**Photographic mapping (display only, 8 stops):**

| Encoder | f-stop |
|---------|--------|
| 0       | f/1.4  |
| 36      | f/2    |
| 73      | f/2.8  |
| 109     | f/4    |
| 146     | f/5.6  |
| 182     | f/8    |
| 219     | f/11   |
| 255     | f/16   |

Formula: `index = round((v / 255) × 7)`, then look up `[1.4, 2, 2.8, 4, 5.6, 8, 11, 16][index]`

### 2.2 Shutter Speed (1/15s → 1/500s)

```
shutterT       = encoderValue / 255.0
blurStrength   = (1.0 - shutterT) × 0.05     // max 5% of image width
// Direction: sample from current pixel toward image centre (0.5, 0.5)
// 12 radial samples, weight tapered: w = 1.0 − t × 0.7

// Exposure shift:
shutterBrightness = lerp(+0.25, -0.20, shutterT)
// → slow: +0.25 stops (overexposed); fast: −0.20 stops (underexposed)
```

**Photographic mapping (6 stops):**

| Encoder | Shutter |
|---------|---------|
| 0       | 1/15s   |
| 51      | 1/30s   |
| 102     | 1/60s   |
| 153     | 1/125s  |
| 204     | 1/250s  |
| 255     | 1/500s  |

Formula: `index = round((v / 255) × 5)`, then look up `[15, 30, 60, 125, 250, 500][index]`

### 2.3 ISO (64 → 12800)

```
isoT           = encoderValue / 255.0
grainIntensity = isoT × 0.18           // max amplitude ±0.18 per channel
grainSize      = lerp(1.0, 3.5, isoT)  // pixel cell size: 1px fine → 3.5px coarse

// Grain implementation: cell-based hash (no texture needed)
//   scaledUV = uv × (resolution / grainSize)
//   frame    = floor(time × 24.0)            ← discrete 24fps ticks
//   grain    = hash(floor(scaledUV) + vec2(frame × 127.1, frame × 311.7))
//   grain    = (grain − 0.5) × 2.0 × grainIntensity

// Exposure shift (most dramatic — nearly full range):
isoBrightness = lerp(-0.50, +0.50, isoT)
// → ISO 64: −0.50 stops (crushed blacks); ISO 12800: +0.50 stops (blown highlights)
```

**Photographic mapping (9 stops):**

| Encoder | ISO   |
|---------|-------|
| 0       | 64    |
| 32      | 100   |
| 64      | 200   |
| 96      | 400   |
| 128     | 800   |
| 160     | 1600  |
| 192     | 3200  |
| 224     | 6400  |
| 255     | 12800 |

Formula: `index = round((v / 255) × 8)`, then look up `[64, 100, 200, 400, 800, 1600, 3200, 6400, 12800][index]`

### 2.4 Combined Exposure Composite

The three brightness shifts are summed and applied as a single gamma curve:

```
totalBrightness = apertureBrightness + shutterBrightness + isoBrightness
gamma           = clamp(1.0 − totalBrightness, 0.1, 3.5)
outputColour    = pow(inputColour, gamma)
```

Intensity ranking (brief spec):
- ISO: dramatic — full range ~±0.50
- Shutter: moderate — ~±0.25
- Aperture: subtle — ~±0.15

---

## 3. Depth Map Convention

### Convention
```
dark pixel (value ≈ 0.0)  = near / foreground = stays sharp at wide aperture
white pixel (value ≈ 1.0) = far  / background = receives maximum blur at wide aperture
```

### Shader inversion note
The brief (Section 3) states `"Shader uses (1.0 – depthValue) as blur weight"`.  
**This is incorrect for the intended effect.** The correct formula is:

```glsl
// CORRECT (blurs background at wide aperture):
blurRadius = depthValue × maxBlur × (1.0 - apertureT)

// INCORRECT — would blur foreground instead:
// blurRadius = (1.0 - depthValue) × maxBlur × (1.0 - apertureT)
```

The asset `Dupain_ExampleImg-DepthMap.jpg` encodes: the corridor interior (far) as bright/white, and nearby architectural elements as dark. Applying `depthValue` directly (without inversion) blurs the correct areas.

---

## 4. Correct Exposure — Data Structure

```javascript
{
  id:    'dupain_01',
  image: 'assets/Dupain_ExampleImg.jpg',
  depth: 'assets/Dupain_ExampleImg-DepthMap.jpg',
  label: "f/11 · 1/125s · ISO 100 — Dupain's choice for hard midday light",
  targets: {
    aperture: { value: 219, tolerance: 20 },  // encoder units
    shutter:  { value: 153, tolerance: 20 },
    iso:      { value: 32,  tolerance: 20 }
  }
}
```

Target encoder values are chosen so the displayed photographic labels (f/11, 1/125s, ISO 100) match the label string when the visitor reaches them.

---

## 5. Proximity Score & Trigger Logic

### Proximity per axis (0.0 – 1.0)

```
dist  = |encoderValue − target.value|

if dist ≤ tolerance:
    score = 1.0
else:
    score = max(0, 0.5 − 0.5 × (dist − tolerance) / (tolerance × 2))
    // linear ramp: 1.0 at boundary, 0.5 at 3×tolerance, 0 beyond
```

### Combined score
```
combinedScore = (apertureScore + shutterScore + isoScore) / 3.0
```

### HUD feedback bands
| Score | Visual |
|-------|--------|
| < 0.5 | No feedback; image looks wrong |
| 0.5–0.8 | Dots warm toward amber |
| > 0.8 | Amber glow intensifies |
| all three = 1.0 | Trigger correct-exposure event |

### Trigger condition
```
trigger fires when:
    proximityScore(aperture) == 1.0
    AND proximityScore(shutter) == 1.0
    AND proximityScore(iso) == 1.0
    AND triggered == false
```

### Hysteresis (prevents re-triggering boundary hovering)
```
triggered = true  on trigger

triggered = false  only when ALL three axes leave tolerance:
    proximityScore(aperture) < 1.0
    AND proximityScore(shutter) < 1.0
    AND proximityScore(iso) < 1.0
```

A single axis back inside tolerance does not re-arm. All three must move outside.

---

## 6. Correct Exposure Event Sequence

On trigger:

1. Play `shutter_click.mp3` (audio)
2. White flash overlay fades in (alpha 1.0) and decays at 1.5/second
3. HUD triangle briefly spikes to full opacity, then returns to normal
4. Label text fades in over 0.8s — holds 4 seconds — fades out over 0.8s
5. `triggered = true` (hysteresis armed)
6. HUD returns to dim active state

---

## 7. Idle / Attract State

**Timeout:** 60 seconds with no encoder input.

On timeout:
- Mode switches to `idle`
- HUD fades to invisible (alpha → 0)
- Encoder values replaced by sinusoidal oscillation:

```
// Three incommensurable frequencies — pattern never visibly repeats
// Centred at 128, amplitude ±48/40/44 — stays in mid-range, avoids extremes

aperture = 128 + 48 × sin(time × 0.11)
shutter  = 128 + 40 × sin(time × 0.17 + 1.2)
iso      = 128 + 44 × sin(time × 0.13 + 2.4)
```

On next encoder input:
- Mode switches to `active`
- Oscillation stops immediately
- Current encoder position takes over
- HUD fades in

---

## 8. Image Selection

- 4–6 illuminated momentary buttons, 1-indexed (BTN:1 → image 0)
- On button press:
  1. Crossfade to new image (0.5s, exponential)
  2. Encoder values randomised to mid-range start:
     - Each axis independently random in range 70–185
     - Avoids all three values simultaneously near their targets for the new image
- `triggered` flag reset on image change

**Randomisation to avoid obvious starting position:**
```
aperture = randomInt(70, 185)
shutter  = randomInt(70, 185)
iso      = randomInt(70, 185)
// If all three happen to land within tolerance of targets, re-randomise aperture
```

---

## 9. HUD Triangle Geometry

### Layout
Equilateral triangle, vertices computed to fill the canvas vertically:

```
MARGIN   = 24px
SIDE     = (canvasHeight − MARGIN × 2) × 2 / sqrt(3)    ≈ 1201px at 1080p
HEIGHT   = SIDE × sqrt(3) / 2                            ≈ 1040px

v[0] top apex       = (canvasWidth/2,       MARGIN)
v[1] bottom-left    = (canvasWidth/2 − S/2, MARGIN + H)
v[2] bottom-right   = (canvasWidth/2 + S/2, MARGIN + H)
```

At 1920×1080: v[0]=(960,24), v[1]=(360,1056), v[2]=(1560,1056)

### Edge → parameter assignments
| Edge | Vertices | Parameter | t = 0 | t = 1 |
|------|----------|-----------|-------|-------|
| Bottom | v[1]→v[2] | Aperture | f/1.4 (wide) | f/16 (stopped) |
| Right  | v[0]→v[2] | Shutter  | 1/15s (slow) | 1/500s (fast) |
| Left   | v[0]→v[1] | ISO      | ISO 64 (low) | ISO 12800 (high) |

### Dot position
```
dotX = v_a.x + (v_b.x − v_a.x) × t
dotY = v_a.y + (v_b.y − v_a.y) × t
where t = encoderValue / 255.0
```

### Visual states
- **Baseline:** lines at ~30% opacity, cool grey `#d0d0d0`
- **Approaching (score > 0.5):** dot warms toward amber `rgb(255, 120, 30)`  
  `glowFactor = (score − 0.5) / 0.5` → linear ramp 0..1
- **On trigger:** full white flash, then return to baseline
- **Idle:** alpha → 0 (invisible)

---

## 10. Render Pipeline Summary (WebGL)

Five draw calls per frame:

| Pass | Input | Output | Effect |
|------|-------|--------|--------|
| 1-H | imageA+B, depthMap | FBO-A | Aperture — horizontal Gaussian blur (depth-masked) |
| 1-V | FBO-A, depthMap | FBO-B | Aperture — vertical Gaussian blur (depth-masked) |
| 2 | FBO-B | FBO-C | Shutter — radial blur from centre |
| 3 | FBO-C | Canvas | ISO grain + all exposure shifts |
| 4 | — | Canvas | White flash quad (only during trigger event) |

Crossfade between images handled in Pass 1-H: `mix(imageA, imageB, crossfadeAlpha)`.  
FBOs are RGBA UNSIGNED_BYTE at 1920×1080.  
All textures use `CLAMP_TO_EDGE`, `LINEAR` filtering (no mipmaps — non-power-of-2 textures).

---

## 11. Asset Specifications

Per image, two JPEG files at 1920×1080px:

| File | Content |
|------|---------|
| `image_N.jpg` | Original B&W photograph |
| `image_N_depth.jpg` | Greyscale depth mask — single channel, dark=near, white=far |

The depth map should be a smooth, painterly approximation of scene depth — not a raw sensor depth map. Hard edges in the depth map produce bokeh artefacts at depth boundaries.

---

## 12. Arduino Wiring Reference

```
Encoder pinout (each encoder):
  CLK → Arduino digital pin (see sketch)
  DT  → Arduino digital pin
  GND → Arduino GND
  VCC → Arduino 5V

Buttons:
  One leg → Arduino digital pin (INPUT_PULLUP configured)
  Other leg → GND
  Active LOW when pressed

Baud: 115200
```

See `arduino/exposure_triangle.ino` for full sketch with pin assignments and debounce logic.
