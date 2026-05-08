# Report 15 — Mobile Camera Calibration

**Cluster:** E (Color, Format, Calibration)
**Status:** Research draft
**Date:** 2026-05-08
**Related decisions:** D8 (ICC + OCIO), D10 (DNG metadata only v1; reserved API for v2), D11 (Apache 2.0), D12 (Bayer + Quad Bayer), D14 (~12 months for v1)

---

## 1. TL;DR

Camera calibration is the process of measuring a specific sensor + lens unit and producing a profile that lets a renderer reverse the device's idiosyncrasies — color cross-talk, lens shading, geometric distortion, chromatic aberration, signal-dependent noise. DNG carries this profile as a fixed schema (`ColorMatrix1/2`, `ForwardMatrix1/2`, `AsShotNeutral`, `NoiseProfile`, `OpcodeList1/2/3`, `Black/WhiteLevel`).

Per **D10**, cpipe v1 ingests calibration **only** from DNG metadata; it does **not** capture or fit calibration data. The architecture **must** reserve a v2 calibration-capture / profile-import API so the v1 renderer can be extended without re-architecting. This report:

1. Surveys the calibration physics and standard targets (Macbeth ColorChecker / SG / IT8) and the math of color-matrix least-squares fits.
2. Maps each DNG opcode (GainMap, WarpRectilinear, FixVignetteRadial, MapPolynomial, Delta/ScalePerRow/Column) to a calibration source (flat field, dot grid, chessboard, dark frame).
3. Describes what mobile vendors actually publish (Pixel / iPhone / Galaxy) and what is reverse-engineered.
4. Audits the calibration-tool landscape (DCamProf, lensfun, Adobe DNG Profile Editor, colour-science.org, dcraw scripts, ChartCalibrate, iccmax) **license-filtered for D11 (Apache 2.0)**.
5. Sketches a `cpipe::CalibrationProfile` v2 API (camera ID, color matrices, gain map, distortion poly, noise profile, opcode list) and a v2 "chart photography → derived profile" capture flow, **without implementing it**.
6. Recommends profile storage: **JSON wrapper + binary opcode blob** (Apache 2.0, future-proof). DCP (Adobe DNG Camera Profile) read is supported; DCP write is deferred to v2.

The v1 renderer is the same renderer that v2's calibration capture will populate — the contract is a `CalibrationProfile` struct loaded from DNG today, written by cpipe-calibration-tool tomorrow.

---

## 2. Decision Matrix

| Decision | Recommendation | Trade-off |
|---|---|---|
| v1 calibration ingest | **DNG metadata only** (per D10) | Zero capture infrastructure; relies on phone vendor calibration |
| Calibration profile storage (in-memory) | **`cpipe::CalibrationProfile` struct** (POD; serialisable) | Stable contract for v1 reader and v2 writer |
| Calibration profile storage (on-disk) | **JSON envelope + binary opcode blob** (`.cpcal` extension proposed) | Apache 2.0; human-inspectable; future-extensible |
| DCP (Adobe DNG Camera Profile) | **Read in v1** (extract embedded profile from DNG); **write in v2** | DCP format is documented; license-clean read; write needs Adobe SDK or hand-roll |
| Color target (v2 default) | **X-Rite/Calibrite ColorChecker Classic 24-patch** | Universal, cheap, sRGB-aligned chromaticities |
| Color target (advanced v2) | **ColorChecker SG (140 patches)** + **IT8.7/3** for fine gamut | Higher accuracy; less common |
| Illuminant pair | **D50 + Standard A (tungsten 2856 K)** matches DNG `CalibrationIlluminant1=17, _2=23` (Adobe convention varies — see §3.3) | The two ends of the daylight–tungsten interpolation arc |
| Color matrix derivation | **Least-squares fit** of camera-RGB to CIE XYZ (D50-relative), with neutral-anchor weight | Standard; matches Adobe DNG spec |
| Vignetting capture | **Flat-field photograph through diffuser** (integrating sphere or printer-paper diffuser) | Cheapest path; integrating sphere is gold standard |
| Vignetting encoding | **DNG `OpcodeList2 GainMap`** (List 2; mosaiced, post-linearization) | Spec-mandated; per-Bayer-plane gain field |
| Geometric distortion | **Brown-Conrady model** (radial k1, k2, k3 + tangential p1, p2 + principal point cx, cy) | Industry standard; implemented in OpenCV / Halide |
| Distortion encoding | **DNG `OpcodeList3 WarpRectilinear`** (List 3; RGB-domain) | Spec-mandated; runs after demosaic |
| Distortion calibration target | **Chessboard** (OpenCV) for low-distortion lenses; **dot grid** for fisheye | Chessboard finds corners robustly; dot grid handles ≥120° FoV |
| Lateral CA | **Per-channel `WarpRectilinear` polynomials** in OpcodeList3 | DNG spec accommodates per-plane radial poly |
| Noise model | **Per-channel `NoiseProfile (a, b)`** with variance(I) = a + b·I | Standard mobile-DNG model; matches Camera2 |
| Noise calibration | **Multi-exposure flat-field stack at uniform gray patches** (Camera2 ITS / Android CTS pattern) | Open-source reference: Android `dng_noise_model.py` |
| Black-level (per-row / per-col) | **DNG `BlackLevelDeltaH`/`DeltaV` + per-cell repeat** | Spec-supported; covers row/column FPN |
| Lens-shading correction | **Use OpcodeList2 GainMap as canonical**; advisory `FixVignetteRadial` for legacy | Avoids vignetting / LSC overlap ambiguity |
| Calibration-tool license filter | **DCamProf = avoid** (custom non-OSS); **lensfun DB = read-only reference**; **colour-science = OK** (Apache 2.0); **iccmax = OK** (varies, mostly permissive); **dcraw scripts = avoid** (GPL trap) | D11 |

---

## 3. Detailed Findings

### 3.1 What "Calibration" Means in a Mobile Camera Pipeline

A mobile camera "calibration" is a unit-specific (per-lens-per-sensor; ideally per-individual-device) measurement of:

1. **Color response.** The sensor's spectral sensitivity (R, G, B Bayer channels' wavelength response) is unique; even nominally identical sensors vary 1–3 % across a production batch. The "color matrix" maps the sensor's RGB triplet to a known reference color space (CIE XYZ at D50), so a pixel that the sensor saw in raw can be rendered as a known color.
2. **Photometric uniformity.** A perfectly uniform scene does not produce uniform pixel values — the lens vignettes (cos⁴ falloff and beyond), the microlenses on the sensor have angular dependence, and the IR-cut filter is often non-uniform. This is captured as a 2D "gain field" that, when multiplied per-pixel, restores uniformity.
3. **Geometric accuracy.** Lenses distort: pincushion or barrel, radially symmetric in well-centred lenses; with tangential components in misaligned ones. A "distortion model" lets the renderer warp the image back to a rectilinear (or fisheye-projection) reference.
4. **Chromatic aberration.** Each wavelength refracts differently; long focal lengths suffer "axial CA" (out-of-focus colour fringes) and short / wide lenses suffer "lateral CA" (per-pixel shift between the R, G, B images). Lateral CA is correctable as a per-channel scale or a per-channel distortion model; axial is not (correcting it requires deconvolution).
5. **Black level and noise.** The sensor has a non-zero "pedestal" (~64 in 14-bit raw), often row/column-dependent due to amplifier mismatch, and a signal-dependent noise model: variance(I) = a + b·I (read noise + shot noise; "Poisson + Gaussian" approximation). The renderer subtracts the pedestal and uses the noise model to drive denoising.

These five layers are encoded into a DNG **profile** (the bundle of metadata + opcode lists) by the camera vendor at calibration time. The renderer that reads the DNG re-applies them.

### 3.2 Color Calibration: Chart, Math, Result

#### 3.2.1 The Charts

The standard color targets are physical printed/painted patches with known XYZ measurements:

- **Calibrite ColorChecker Classic** (formerly X-Rite ColorChecker). 24 patches: 18 colour, 6 grayscale (white → black). Calibrite (formerly X-Rite, formerly GretagMacbeth) publishes per-patch CIE Lab D50 reference values. A small target (~21 × 28 cm), affordable, reproducible across batches (Calibrite publishes calibration variance ≤ 1 ΔE2000 unit).
- **ColorChecker SG (Semi-Gloss)**. 140 patches arranged in a 14 × 10 grid: 24 Classic + 116 fine-gamut samples covering skin tones and saturated primaries. Larger (~22 × 33 cm) and more expensive; used by professional calibration tools.
- **ColorChecker Digital SG (in-camera)**. Same 140 patches but on a more durable substrate.
- **IT8.7/3 (Kodak/IT8)**. 264 patches on Kodak Q-60 transmissive or reflective film; designed for scanner calibration but used for camera profiling. Wider gamut sampling than ColorChecker SG, but expensive and aging chemistry.
- **Spyder Cube / Spyder Checkr 24 / 48**. Datacolor's competitor; 24-patch and 48-patch options. Compatible workflow with most calibration tools.

For calibration, the chart is photographed under each "calibration illuminant" (see §3.3). The patches are extracted (auto-detected via OpenCV `MCC` module or manually); each patch yields an averaged RAW RGB triplet (after black-level subtraction and white-balance to a known gray).

#### 3.2.2 The Math: Least-Squares Color Matrix Fit

The DNG model says: **camera_RGB = ColorMatrix · XYZ_D50**, evaluated under a specific illuminant. So given N patches with known reference `XYZ_D50_i` and measured camera-side `cam_i`, we want a 3×3 matrix M such that `cam_i ≈ M · XYZ_D50_i` for all i.

This is overdetermined (3N equations, 9 unknowns). Solve by ordinary least squares:

```
M = (Σ cam_i · XYZ_iᵀ) · (Σ XYZ_i · XYZ_iᵀ)⁻¹
```

In matrix form, stacking columns into matrices:

```
M = CAM · XYZᵀ · (XYZ · XYZᵀ)⁻¹
```

Refinements used in practice (DCamProf, Adobe DNG Profile Editor):

- **Neutral-anchor constraint.** Force `M · neutral_XYZ = camera_neutral_RGB`; reduces the fit to ≤ 7 free parameters and prevents the optimiser from drifting on a near-singular system.
- **Patch weighting.** Weight skin-tone and grayscale patches more heavily than saturated primaries (perceptual relevance). DCamProf uses a default weight scheme; ColorChecker Classic patches 1–4 (skin-tone family) are weighted ~2 × baseline.
- **ΔE2000 minimisation instead of squared-RGB.** Switch from L2 in raw RGB space to ΔE2000 in Lab space; nonlinear, requires iteration (Levenberg-Marquardt). More perceptually-aligned but slower; ~20–40 % improvement in mean ΔE2000 vs L2 fit.
- **Hue-Sat-Map post-correction.** After the matrix is fit, residuals are still ~2–3 ΔE2000 on saturated patches. The DNG `ProfileHueSatMapData` (3D LUT in HSV space) absorbs this residual non-linearly. cpipe v1 reads this LUT; v2 will fit it.

#### 3.2.3 Dual-Illuminant Interpolation

DNG carries TWO matrices: `ColorMatrix1` (and optional `ForwardMatrix1`) under `CalibrationIlluminant1`, and `ColorMatrix2`/`ForwardMatrix2` under `CalibrationIlluminant2`. Adobe's convention, observed in 99 % of DNGs:

- `CalibrationIlluminant1 = 17` → Standard Illuminant A (incandescent, 2856 K).
- `CalibrationIlluminant2 = 21` → D65 (daylight, 6500 K).

(Some Sony DNGs and a small fraction of older Pixel DNGs use `Illuminant2 = 23 = D50`; both are spec-conformant. The renderer must read the tag, not assume.)

At render time, given the scene's `AsShotNeutral` (a raw triplet that is meant to be neutral white under the scene illuminant), the renderer:
1. **Estimates the scene CCT** by 1D search in the inverse direction (see [#13 — Color management](13-color-management.md) §3.6).
2. **Interpolates** between `ForwardMatrix1` and `ForwardMatrix2` linearly in `1/CCT` (Adobe's formulation; closer to perceptually linear than CCT itself).
3. **Applies** `FM_interp · diag(1/AsShotNeutral) · raw_RGB → XYZ_D50`.

Mobile calibration tools (DCamProf, Lumariver) capture chart photographs at **Standard A** (tungsten lamp; 2856 K) and **D65** (daylight under midday sun, or a controlled D65 lamp, or a Solux 4700 K + filter combination). Some shops use **D50** instead of D65 (matches the print industry); the choice doesn't matter as long as the matching `CalibrationIlluminant2` tag is set.

#### 3.2.4 Per-Camera-Unit Calibration Matrix

`CameraCalibration1/2` (DNG tags 50723/50724) are a per-individual-unit refinement matrix multiplied with `ColorMatrix1/2` to compensate for individual sensor variation within a model. Default is identity. In practice, mobile vendors do **not** ship per-unit calibration in this tag — they bake unit variation into individual sensor's pre-linearization OpcodeList1 / lookup table, leaving CameraCalibration as identity. cpipe must read it (multiply if non-identity) but does not need to write it for v1.

### 3.3 Vignetting and Lens-Shading Correction

#### 3.3.1 The Physics

A lens darkens toward the edges due to:
- **Geometric falloff** (cos⁴ rule of thumb): off-axis points see less light by a factor depending on the angle subtended.
- **Optical vignetting** (mechanical): aperture blade and lens housing edges occlude rays at high incidence angles.
- **Pixel-level angular response**: micro-lens array on the sensor focuses light onto the photodiode; off-axis chief rays land inefficiently. This is non-trivially **wavelength-dependent**, producing colour-cast vignetting (red-shifted corners on some sensors, blue-shifted on others).

The combined falloff is captured as a **2D gain field** `gain(x, y)` such that:
```
pixel_corrected(x, y) = pixel_raw(x, y) · gain(x, y)
```

#### 3.3.2 Capture: The Flat-Field Frame

Calibration shop method:
1. Place a **uniform light source** in front of the lens — integrating sphere is the gold standard, but a back-lit white printer-paper diffuser at 30 cm distance suffices for hobbyist work. Light source CCT recorded.
2. Lens cap removed, focus set to infinity, aperture wide open.
3. Capture a series (typically 16 frames; average to reduce noise) at multiple exposures bracketing mid-grey.
4. Average to a single 32-bit float frame; subtract black level; this is the raw flat-field.
5. **Per-Bayer-plane downsample** to a coarse grid (typically 31 × 31 or 33 × 33). Each cell is the mean of the underlying Bayer-plane pixels.
6. Normalise so `gain(centre) = 1.0`; outer cells will have gain > 1 (e.g. 1.5 to 2.5 at corners on a wide-angle lens).
7. **Per-wavelength compensation** is captured by repeating steps 1–6 separately for each Bayer plane (R, G1, G2, B). The four gain maps differ in colour-shading.

The result is **four 33 × 33 (or 31 × 31) gain grids**, one per Bayer plane.

#### 3.3.3 DNG Encoding: GainMap Opcode

Per [#12 — DNG format](12-dng-format.md) §3.5, the DNG GainMap opcode is in `OpcodeList2` (post-linearization, still mosaiced). One opcode per Bayer plane (so four opcodes for an RGGB sensor). Parameters:

- `top, left, bottom, right` — the rectangle to which the gain applies; usually the full image.
- `plane` (0..3 for RGGB), `planes` (typically 4).
- `rowPitch`, `colPitch` — set to (2, 2) for standard Bayer (each plane is sampled at every other row and column).
- `mapPointsV`, `mapPointsH` — grid dimensions (e.g. 33 × 33).
- `mapSpacingV/H`, `mapOriginV/H` — grid coverage in image-coordinate units (so the rendererknows where each grid cell sits).
- `mapPlanes` — typically 1 (one grid per plane).
- `gain[mapPointsV][mapPointsH]` — the float gain values.

The renderer **bilinearly samples** the gain grid for each pixel position and multiplies. Per the DNG 1.7 spec, sample positions falling outside the grid use **edge-extension** (clamp). RawSpeed issue #267 documents implementations that wrapped instead — produces visible artifacts at borders.

#### 3.3.4 The Lens-Shading vs Vignetting Overlap (Spec Ambiguity)

DNG also defines `FixVignetteRadial` (opcode 3). It encodes a vignette as a 1D radial polynomial. Two opcodes that do similar things — when do you use which?

- **`FixVignetteRadial`** is a **legacy** simple radial polynomial (5 coefficients). Originally how DNG 1.3 expressed vignetting. Some older DNG converters still emit it. Cheaper to evaluate but cannot capture colour-shading or off-axis non-radial effects.
- **`GainMap`** is the **modern, expressive** form. Mobile vendors (Pixel, Galaxy, iPhone via Apple's intermediate) emit GainMap exclusively.

In practice cpipe sees **one or the other**, never both. If both are emitted, the spec says the renderer applies them in order (FixVignetteRadial first, then GainMap), but this combination is essentially never produced in practice.

cpipe's policy: apply FixVignetteRadial if present; apply GainMap if present; in either case, log which one was used. Profile-import (v2) tooling will write GainMap exclusively.

#### 3.3.5 Lens-Shading Correction (LSC) Terminology

"LSC" in mobile camera-pipeline literature is the umbrella term for the same concept. LSC is sometimes split into:
- **Luminance LSC** — restores brightness uniformity (the cos⁴-style monochromatic falloff).
- **Colour LSC** — restores per-channel uniformity (the wavelength-dependent micro-lens cross-talk).

DNG GainMap with per-Bayer-plane grids handles both in one mechanism. Some camera ISPs (Qualcomm Spectra, Apple's bake-in) split these into two stages internally, but emit a unified GainMap into the DNG output.

### 3.4 Lens Distortion

#### 3.4.1 Brown-Conrady Model

The widely-adopted distortion model is **Brown-Conrady** (Brown 1966, Conrady 1919). Given a "true" image-plane coordinate `(x, y)` (rectilinear), the distorted (raw-image) coordinate `(x_d, y_d)` is:

```
r² = x² + y²
radial   = 1 + k1·r² + k2·r⁴ + k3·r⁶
x_d = x · radial + 2·p1·x·y + p2·(r² + 2·x²)
y_d = y · radial + p1·(r² + 2·y²) + 2·p2·x·y
```

Coefficients:
- **k1, k2, k3**: radial distortion (positive = pincushion; negative = barrel).
- **p1, p2**: tangential distortion (sensor not perfectly perpendicular to optical axis).
- **(cx, cy)**: principal point — the optical centre, which is rarely exactly at image centre. Encoded as offsets so `(x, y)` is computed relative to `(cx, cy)`.

For wide-angle / fisheye lenses, the standard Brown-Conrady is extended to include higher-order radial terms (k4, k5, k6) or replaced entirely by a fisheye model — DNG accommodates both via `WarpRectilinear` (rectilinear lens) and `WarpFisheye` (fisheye lens).

#### 3.4.2 DNG Encoding: WarpRectilinear / WarpFisheye

DNG `WarpRectilinear` (opcode 1) encodes a per-channel polynomial:
- For a single-plane (luminance) correction, one set of (k1, k2, k3) + (p1, p2) + (cx, cy).
- For per-channel correction (lateral CA), three sets (one per R, G, B channel) — the differences between channels handle lateral CA.

Parameters per the spec:
```
uint32  N (number of planes; 1 or 3)
double  k1[0], k2[0], k3[0], p1[0], p2[0], cx[0], cy[0]   // plane 0
double  k1[1], k2[1], k3[1], p1[1], p2[1], cx[1], cy[1]   // plane 1 (if N>1)
double  k1[2], k2[2], k3[2], p1[2], p2[2], cx[2], cy[2]   // plane 2 (if N>2)
```

`WarpFisheye` is similar but uses a fisheye projection (equidistant, equisolid, stereographic). Most mobile main-camera lenses are rectilinear; ultrawide phones (Pixel ultrawide, iPhone ultrawide) **may** be calibrated as fisheye in the DNG, but most ship `WarpRectilinear` because the phone's firmware already de-fishes the output before saving.

OpcodeList3 timing: applied **after demosaic**, before color matrix. Operates on planar RGB. Per the spec, the renderer interprets the warp as "for output pixel (x, y), sample input at (x_d, y_d)".

#### 3.4.3 Calibration: Chessboard and Dot-Grid Targets

Calibration shop method:
1. Print a **chessboard** (typically 9 × 6 or 11 × 8 squares) on rigid foamcore (rigidity is critical — paper warp introduces metres of error).
2. Capture 20+ photographs at varied poses (the chessboard must rotate, tilt, and translate; OpenCV documentation recommends ~30 captures with diverse poses).
3. Run **OpenCV `findChessboardCorners` + `calibrateCamera`** (BSD-3, Apache-friendly); fits Brown-Conrady k1/k2/k3 + p1/p2 + (cx, cy) simultaneously.
4. Capture residual reprojection error (target < 0.5 px); if higher, repeat.

For ultrawide / fisheye lenses, the chessboard's corners become unreliable; use a **dot grid** (asymmetric circles): OpenCV's `findCirclesGrid` is more robust at extreme distortion.

For **lateral CA**, capture a multispectral or tri-coloured chart (red / green / blue dots on a uniform white background); fit a separate `WarpRectilinear` per channel. The differences (mostly visible in r⁴ and higher terms) are the CA model.

#### 3.4.4 What Mobile Vendors Actually Ship

- **iPhone / iPad ProRAW.** Apple's pipeline applies geometric correction internally and ships a DNG with `WarpRectilinear` set to identity (zeros in k1/k2/k3/p1/p2). The DNG is already de-distorted. Lateral CA is also corrected internally; the DNG's WarpRectilinear has identical per-channel polynomials.
- **Pixel 6+ (Computational RAW).** Google emits a DNG with non-identity `WarpRectilinear` (per-channel) for the wide and ultrawide cameras; the telephoto is closer to identity. Pixel's official "de-distortion is bake-in for Computational RAW; raw RAW for Pro mode preserves it" is the rough rule, verified per device-mode by inspecting Pixel sample DNGs.
- **Galaxy S series Expert RAW.** Samsung emits non-identity WarpRectilinear; Galaxy's Expert RAW preserves the raw distortion and ships the correction as opcode metadata. Samsung S24 Ultra ultrawide DNGs are fisheye-coded; Adobe Camera Raw renders them correctly.
- **Sony Xperia Photo Pro.** Sony historically emits identity opcodes — they bake distortion correction into the saved DNG before output.

cpipe v1 must handle **both flavours**: identity opcodes (no work needed) and active opcodes (apply WarpRectilinear in OpcodeList3). The detection is automatic — if a coefficient is non-zero, run the warp; otherwise skip.

### 3.5 Chromatic Aberration

#### 3.5.1 Lateral CA

The R, G, B images are slightly different sizes due to the wavelength-dependent focal length of the lens. Lateral CA presents as colour fringes at high-contrast edges, especially toward image corners, scaling roughly with distance from the optical centre.

**Calibration:** photograph a high-contrast tri-colour chart; for each colour, fit a `WarpRectilinear` polynomial; the per-channel difference IS the lateral CA model.

**DNG encoding:** OpcodeList3 `WarpRectilinear` with `N = 3` (per-plane polynomials). One set of (k1, k2, k3, p1, p2, cx, cy) per RGB plane.

**Alternative encoding:** OpcodeList3 `MapPolynomial` per-channel scale opcode — coarser correction but cheaper to apply. cpipe handles both.

#### 3.5.2 Axial CA

Different wavelengths focus at different distances. Out-of-focus highlights show colour fringes (purple fringing in front-focus, green fringing in back-focus). Axial CA is **not encoded in DNG** (no opcode supports it directly); it requires deconvolution-based correction at render time. cpipe v1 does not address axial CA; v2 may add an AI-driven defringing node (cross-link to [#08 — AI ISP algorithms](08-ai-isp-algorithms.md)).

### 3.6 Noise Model

#### 3.6.1 Variance-Stabilising Transform (VST) Background

Sensor noise in raw-Bayer space is the sum of:
- **Shot noise** (Poisson): variance proportional to signal; std-dev ∝ √I.
- **Read noise** (Gaussian-like): variance independent of signal.
- **Dark current** (signal-dependent, exposure-dependent): merged with shot noise in the per-frame model.
- **PRNU (photo-response non-uniformity)**: pixel-to-pixel sensitivity variation; multiplicative.
- **Quantisation**: small, ignored.

The simplified two-parameter model used by DNG and Camera2:

```
variance(I) = a + b · I       # per Bayer plane
```

where `I` is the raw pixel value (post-black-level-subtract). Coefficient `a` captures read noise (DC offset variance); `b` captures shot noise (Poisson rate, scaled by gain). At ISO 100, `a` ~ small (read-noise floor), `b` near 1 (Poisson baseline). At ISO 12800, `a` is large (noisy read) and `b` is also amplified.

A **Variance-Stabilising Transform** (Anscombe transform) maps Poisson-Gaussian to approximately unit-Gaussian:

```
F(I) = 2 · √(b · I + a + 3/8 · b²)
```

After VST, the noise is approximately white Gaussian with σ ≈ 1, regardless of `I`. AI denoisers and BM3D-class denoisers operate in VST space; the inverse is applied after denoising.

#### 3.6.2 DNG Encoding: NoiseProfile

DNG `NoiseProfile` (tag 51041) carries N pairs `(a, b)`:
- N = 1: monochrome assumption (one (a, b) for all channels).
- N = 3: per-RGB (a, b) (one per primary).
- N = 4: per-Bayer-plane (a, b) (R, G1, G2, B). Most mobile DNGs use N = 4 or N = 3.

Mobile vendors calibrate per-camera-model per-ISO; the DNG carries a single (a, b) pair for the ISO at which the photograph was taken (the pair is interpolated by the ISP or read from a pre-computed table).

**Linear-domain caveat:** the (a, b) pair is meaningful only in **linearised** raw — i.e., **after** OpcodeList1 + LinearizationTable + black-level subtraction, but **before** white-balance and demosaic. cpipe applies NoiseProfile inside the denoise node, which sits between black-level scaling and demosaic in the canonical pipeline.

#### 3.6.3 Calibration: Multi-Exposure Flat Field

Standard procedure (Android CTS `dng_noise_model.py` is the open-source reference):

1. Place a uniform grey card or integrating sphere in front of the lens.
2. Photograph at fixed ISO across a series of exposures: e.g., 5 ms, 10 ms, 20 ms, 40 ms, ..., 320 ms (logarithmic spacing).
3. For each exposure, extract a flat region (typically a 200 × 200 patch in the centre).
4. Compute mean (`I`) and variance (`var`) per Bayer plane.
5. Plot `var(I)` for each plane; fit a line: `var = a + b · I`. Slope is `b`, intercept is `a`.
6. Repeat at multiple ISO settings; the (a, b) values vary with sensor gain.
7. The DNG encoder picks the (a, b) at the photograph's ISO.

cpipe v1 ingests the (a, b) from the DNG; v2's calibration tool will run this entire procedure and emit the table.

#### 3.6.4 Some Vendor-Specific Caveats

- **iPhone ProRAW**: NoiseProfile values are approximations — Apple's internal ML denoiser corrects the variance at run-time, so the DNG's (a, b) is an underestimate (the "post-denoise" residual noise). Some phone-DNG renderers treat iPhone NoiseProfile as a warning-only signal and use their own noise estimate.
- **Pixel computational RAW**: Pixel's noise profile is reported per-Bayer-plane; values match well between the table and a re-measurement.
- **Galaxy Expert RAW**: Samsung's NoiseProfile is the closest to "ground-truth" of the major vendors; matches a re-measurement closely.

cpipe respects the DNG's NoiseProfile but logs a warning if the values are outside expected ranges (a < 0 or a > 1000; b < 0 or b > 0.01 for normalised-to-1 raw).

### 3.7 Black Level: Per-Row, Per-Column, Per-Cell

The sensor pedestal is not a single number. It is:

- **Per-channel**: each Bayer plane (R, G1, G2, B) has its own pedestal (DNG `BlackLevel` array, with `BlackLevelRepeatDim` describing the repetition; e.g. (2, 2) for standard Bayer).
- **Per-row** (`BlackLevelDeltaV`): each row has a small additive delta on top of the per-channel pedestal. Captures CMOS readout-amplifier mismatch ("row noise" / horizontal banding).
- **Per-column** (`BlackLevelDeltaH`): each column has its own delta. Captures column-amplifier mismatch ("column noise" / vertical banding).

The renderer subtracts:
```
black(r, c, plane) = BlackLevel[plane] + BlackLevelDeltaV[r] + BlackLevelDeltaH[c]
linearized(r, c) = (raw(r, c) - black(r, c, plane)) / (WhiteLevel[plane] - black(r, c, plane))
```

Per-cell black level (a full 2D map, not just per-row/per-col) is **not** in the DNG schema. Sensors with strong fixed-pattern noise that requires a per-cell pedestal use OpcodeList1 / OpcodeList2 `DeltaPerColumn` + `DeltaPerRow` + `MapPolynomial` to encode it as an opcode rather than baking it into BlackLevel tags. cpipe handles both paths.

**Calibration:** capture a "dark frame" (lens cap on, same exposure as the photograph, same ISO); compute per-row, per-column, and per-cell averages over many frames. Set BlackLevel + BlackLevelDeltaV + BlackLevelDeltaH; encode the residual as MapPolynomial if needed.

### 3.8 OpcodeList Semantics: List 1, List 2, List 3 — Renderer Scheduling

This is a recapitulation of the contract from [#12 — DNG format](12-dng-format.md), grounded in the calibration domain:

| List | Position in Pipeline | Domain | Calibration Sources |
|---|---|---|---|
| **OpcodeList1** | **Before linearization**. Operates on raw mosaiced sensor values. | "Raw domain" — pre-LinearizationTable. | Bad-pixel maps (FixBadPixelsList from a hot-pixel calibration), per-cell pedestal corrections (DeltaPerColumn / DeltaPerRow if used). |
| **OpcodeList2** | **After linearization, before demosaic**. Operates on linear mosaiced data. | "Linear mosaiced". | Vignetting / lens-shading (GainMap, FixVignetteRadial), residual per-channel pedestal (Delta/ScalePerRow/Column). |
| **OpcodeList3** | **After demosaic**. Operates on RGB planar data. | "Linear RGB". | Geometric distortion (WarpRectilinear / WarpFisheye), lateral CA (per-channel WarpRectilinear), MapPolynomial for stylistic adjustments. |

cpipe scheduler insertion points (cross-link to [#06 — soft ISP architectures](06-soft-isp-architectures.md) §3 and [#03 — heterogeneous scheduler](03-heterogeneous-scheduler.md)):

```
parse_dng → opcode_list_1_apply → linearization → black_white_scaling
   → opcode_list_2_apply → quad_bayer_remosaic → white_balance_apply
   → demosaic → opcode_list_3_apply → color_matrix_apply → ...
```

Each opcode maps to **one or more** scheduler nodes, depending on opcode complexity:

- `FixBadPixelsList` / `FixBadPixelsConstant` → one CPU/GPU pass over the bad-pixel coordinates.
- `DeltaPerRow` / `DeltaPerColumn` → one row/column-broadcast subtraction (cheap GPU pass).
- `ScalePerRow` / `ScalePerColumn` → one row/column-broadcast multiplication.
- `GainMap` → one bilinear-sampled multiplication per Bayer plane (one shader pass per plane, or one fused pass with the four planes interleaved).
- `MapTable` → 1D LUT lookup (texture sample).
- `MapPolynomial` → small polynomial evaluation per pixel.
- `WarpRectilinear` / `WarpFisheye` → one inverse-mapping resample with bicubic filter (or per-channel resample for lateral CA).
- `TrimBounds` → metadata-only update of the active rect.

Scheduler integration: the opcode interpreter from [#12 §3.9](12-dng-format.md) emits `NodeDescriptor` records that the [#03 scheduler](03-heterogeneous-scheduler.md) consumes. The opcode list is treated as a **sub-DAG** spliced into the main DAG at the three insertion points.

### 3.9 Mobile-Vendor Calibration Practice (What's Public)

#### 3.9.1 Google Pixel Camera

Public sources: ICCV / ISCA / SIGGRAPH papers from the Google Pixel Camera team (2014–2024); Marc Levoy's Stanford lectures (publicly available); Florian Kainz / Samuel Hasinoff's burst photography papers.

Key facts:
- Pixel's per-sensor calibration runs at the factory on every device. Per-individual-unit calibration is **partially** stored in `CameraCalibration1/2` — Hasinoff et al. "Burst Photography for High Dynamic Range" (SIGGRAPH 2016) confirms per-unit raw colour calibration but does not say whether it lands in the DNG or only in the in-pipeline JPEG.
- Vignetting is per-individual-unit on Pixel 6+. The factory captures four per-channel gain maps and writes them as OpcodeList2 GainMaps in any DNG output.
- Distortion is per-camera-model (not per-unit) — a single calibration applies to all units.
- Noise profile is per-camera-model per-ISO; encoded as a lookup table in firmware, with the photograph's ISO mapped to a pair (a, b) and written to the DNG NoiseProfile tag at capture time.

Pixel "Pro Raw" mode preserves all of these in the DNG. Pixel "Computational Raw" mode bakes some corrections in (the resulting DNG has identity OpcodeList3 for distortion) — the trade-off favours fidelity-of-pixels over preservability-for-editing.

#### 3.9.2 iPhone ProRAW

Public sources: Apple WWDC sessions on Computational Photography (WWDC20 "Capture Pro Photos with ProRAW", WWDC22 "Discover continuous integration with ProRAW", WWDC24 "Use HDR for dynamic image experiences").

Key facts:
- ProRAW is a **partially-processed** DNG: black level subtracted, demosaic and lens correction baked in (OpcodeList3 distortion = identity), partial denoise applied, but tone mapping and colour grading not applied.
- Color matrices are per-camera-model and per-camera-lens (wide vs ultrawide vs tele have separate ColorMatrix1/2 sets), embedded in the DNG.
- Vignetting **is** in the GainMap opcode but reflects the residual after Apple's bake-in — gentler than a raw-raw flat-field correction.
- Noise profile is in the DNG but reflects post-denoise residual, not the sensor's actual readnoise.
- Apple does not publish its calibration procedure. Reverse-engineering by darktable / RawTherapee / Affinity teams shows it follows the DNG schema closely; per-individual-unit calibration is **not** in the DNG (`CameraCalibration1/2` is identity).

#### 3.9.3 Galaxy Expert RAW

Public sources: Samsung Newsroom blog posts (e.g. "Behind the Galaxy S24 Ultra camera"); ISMAR / MobiCom papers on Samsung's Tetracell / Quadra demosaic.

Key facts:
- Expert RAW preserves more sensor characteristics than computational raw modes — distortion opcodes are non-identity, vignetting is a near-raw GainMap, noise profile reflects the actual sensor.
- Per-camera-model calibration; Samsung does not appear to do per-unit calibration in the public DNG, though their internal proprietary HDR / Computational Photography pipeline may.
- Quad Bayer (Tetracell) sensors are emitted as DNG with `CFARepeatPatternDim = (4,4)` in some Expert RAW modes; cpipe handles this per [#12 §3.7](12-dng-format.md).

#### 3.9.4 DXOMARK Calibration Practice

DXOMARK is the de-facto third-party benchmarking lab for mobile cameras. Their calibration setups (from public DXOMARK blog posts):
- Color targets: ColorChecker SG primary, IT8 secondary.
- Illuminants: D65 (mid-day daylight cabinet) and Standard A (incandescent cabinet); some labs add D50.
- Distortion: DXOMARK uses their proprietary Star Test chart for resolution + distortion combined.
- Noise: DXOMARK measures noise per-ISO at 18 % grey patch of ColorChecker; not the same model as DNG's (a, b) but extractable to it.
- All measurements automated; DXOMARK reports sensor scores per dimension (color, texture, noise, etc.) on a 0–100 scale.

DXOMARK does not publish full calibration profiles; just summary scores. Their calibration rigs (proprietary to DXOMARK) are sold to vendors as "DXOAnalyzer" suites — not relevant to cpipe.

### 3.10 Calibration Tool Landscape — License-Filtered Survey

| Tool | License | Verdict for D11 (Apache 2.0) | Coverage | Notes |
|---|---|---|---|---|
| **Adobe DNG Profile Editor** | Adobe SDK License (proprietary; freely-usable) | **Reference-only**; runtime use OK; cannot embed | GUI for ColorChecker fit + ProfileToneCurve / ProfileHueSatMap editing | Output: DCP profiles; cpipe v1 reads DCP |
| **DCamProf** | "DCamProf License" — non-commercial-friendly, **commercial-use restrictions** | **AVOID** for embedded use; OK as a reference tool a user runs separately | Best-in-class color matrix + HSV-LUT fitting; supports ColorChecker, SG, IT8 | License is the trap; cpipe documents how to use DCamProf externally to produce DCPs |
| **lensfun (database + library)** | LGPL-3 | **Database OK to read** (data, not code); **library AVOID** if static-linking | Per-camera-model distortion + vignetting profiles for hundreds of cameras | The library is LGPL-3; the **lens database** is LGPL-3 with permission to redistribute. cpipe v2 may import lensfun's DB as bootstrap calibration data |
| **colour-science.org Python** | BSD-3 | **OK** | ColorChecker patch detection, gamut visualisation, transform synthesis | Reference for math; cpipe v2 calibration tool can borrow algorithms |
| **dcraw / RawTherapee calibration scripts** | GPL | **AVOID** for embedded use; OK as external tools | Many calibration scripts | The GPL trap; do not lift code |
| **OpenCV calibrateCamera + MCC module** | BSD / Apache | **OK** | Brown-Conrady fit, chessboard / dot-grid corner detection, ColorChecker MCC patch finder | The default calibration backbone for v2 |
| **iccmax (ICC reference implementation)** | ICC license (mostly permissive; specific files vary) | **OK after audit** | ICC v5 / iccMAX profile generation | Useful for ICC-side calibration |
| **ChartCalibrate / Argyll CMS** | GPL-3 | **AVOID** for embedded use; OK as external tools | ColorChecker / IT8 calibration, ICC profile creation | Argyll is the gold-standard external tool; cpipe documents usage |
| **DataPort / PCalibr** | Various proprietary | **AVOID** | Multispectral patch fitting | Industry-grade but proprietary |
| **Lumariver Profile Designer** | Proprietary | **AVOID** | Excellent UI; DCP output | Reference-only; cpipe v2 emulates similar workflow |
| **dispcalGUI / DisplayCAL** | GPL-3 | **AVOID** for embedded use | Display calibration (ICC v4) | Useful as external tool for monitor calibration |

**Key observations:**
- The **GPL trap** is severe in the calibration space. Argyll, dispcalGUI, dcraw scripts, RawTherapee tools — all GPL. cpipe must build its own calibration tool from scratch, leaning on permissively-licensed primitives (OpenCV calibrateCamera, colour-science, libraw for DNG IO, lcms2 for ICC).
- **DCP write** (DNG Camera Profile output) requires either Adobe DNG SDK (proprietary, but redistributable) or a hand-rolled DCP writer. cpipe v1 reads DCP; v2 will hand-roll a DCP writer (~1 kLoC C++) or delegate to Adobe SDK at the user's option.
- **lensfun's database** is the single most useful piece of community calibration data. cpipe v2 should be able to import lensfun XML profiles (one-time conversion to the cpipe format). The database is LGPL-3 with redistribution permitted as data.

### 3.11 cpipe v1 Architecture: Calibration Data Ingestion

Per D10, v1 only **reads** calibration. The architecture must:
1. Define a stable in-memory `CalibrationProfile` struct.
2. Populate it from DNG metadata at parse time.
3. Provide it to every renderer node that needs calibration parameters (color matrix, vignetting, distortion, denoise).
4. **Reserve** the API for v2's calibration-capture flow to populate the same struct.

In effect, the v1 contract is: "the renderer takes a `CalibrationProfile` as input; the v1 source of `CalibrationProfile` is `parse_dng()`; v2 adds a second source, `cpipe_calibrate()`, that produces the same struct".

### 3.12 Reference Repositories Inspected

Three repos audited for calibration-data structure reference:

1. **opencv/opencv** (4.10+) — `modules/calib3d` for `calibrateCamera`, `findChessboardCorners`, `solvePnP`; `modules/mcc` for ColorChecker MCC patch detection. License BSD/Apache. The standard backbone for v2.
2. **lensfun/lensfun** — calibration database (XML profiles per camera-lens pair). LGPL-3 (DB redistribution permitted). Database covers vignetting + distortion + lateral CA for hundreds of consumer lenses; mobile-camera coverage is sparse (lensfun mostly covers DSLR / interchangeable-lens cameras).
3. **colour-science/colour** — Python reference implementation of CIE color science, ColorChecker patch reference values, ΔE2000, Bradford CAT, etc. License BSD-3. The reference for color-math correctness in v2.

Also inspected as reference (read-only): **DCamProf** (license-incompatible but algorithmically excellent), **Argyll CMS** (GPL but a deep reference for chart-based calibration math).

---

## 4. Concrete Code / Architecture Sketches

### 4.1 `cpipe::CalibrationProfile` v1 Struct (read-only from DNG)

```cpp
namespace cpipe::calib {

struct CalibrationProfile {
  // ── Identity ───────────────────────────────────────
  std::string camera_make;          // "Apple", "Google", "Samsung"
  std::string camera_model;         // "iPhone 16 Pro", "Pixel 8 Pro"
  std::string lens_id;              // optional; "main_24mm", "ultrawide", "tele_5x"
  uint64_t    raw_data_unique_id;   // DNG RawDataUniqueID; per-frame stable hash
  std::string profile_source;       // "dng" / "dcp" / "cpipe_calibrate" / "imported_lensfun"

  // ── Color ──────────────────────────────────────────
  std::optional<Matrix3> color_matrix_1, color_matrix_2;   // XYZ_D50 → camera_RGB
  std::optional<Matrix3> camera_calibration_1, camera_calibration_2; // per-unit (usually identity)
  std::optional<Matrix3> forward_matrix_1, forward_matrix_2;          // camera_RGB_wb → XYZ_D50
  Vec3                   as_shot_neutral;                  // raw white-balance triplet
  uint16_t               calibration_illuminant_1 = 17;    // EXIF code (17 = Std A)
  uint16_t               calibration_illuminant_2 = 21;    // EXIF code (21 = D65)
  std::optional<HueSatMap>   profile_hue_sat_map_1, _2, _3;
  std::optional<ToneCurve>   profile_tone_curve;

  // ── Black/white level ──────────────────────────────
  Vec4u                  black_level;                      // per Bayer plane
  std::vector<int16_t>   black_level_delta_v;              // per row
  std::vector<int16_t>   black_level_delta_h;              // per column
  uint32_t               white_level;                      // per plane (or one common)

  // ── Photometric (vignetting / LSC) ─────────────────
  // Sourced from OpcodeList2 GainMap or FixVignetteRadial.
  // Stored as the parsed opcode payload; renderer interprets.
  std::vector<dng::Opcode> opcodes_list_1;
  std::vector<dng::Opcode> opcodes_list_2;
  std::vector<dng::Opcode> opcodes_list_3;

  // ── Geometric (distortion / CA) ────────────────────
  // Lives inside opcodes_list_3 as WarpRectilinear / WarpFisheye opcodes;
  // additionally cached here as parsed structs for fast access:
  struct DistortionPoly {
    std::array<double, 3> radial;       // k1, k2, k3
    std::array<double, 2> tangential;   // p1, p2
    Vec2d                 principal;    // cx, cy (image-coord normalised)
  };
  std::optional<DistortionPoly> distortion_r, distortion_g, distortion_b; // per-plane

  // ── Noise model ────────────────────────────────────
  struct NoiseProfile {
    std::array<std::pair<double,double>, 4> ab; // (a, b) per Bayer plane (R, G1, G2, B)
    bool   per_plane = true;                    // true for N=4; else broadcast
    int    iso = 0;                             // ISO at which (a,b) was calibrated
  };
  std::optional<NoiseProfile> noise;

  // ── Timestamps & versioning ────────────────────────
  uint32_t schema_version = 1;
  std::chrono::system_clock::time_point captured_at;       // calibration capture (v2)
};

} // namespace cpipe::calib
```

This struct is the single source of truth. v1's `parse_dng()` ([#12 §3.9](12-dng-format.md)) populates it; the renderer (color matrix node, GainMap node, denoise node) reads from it.

### 4.2 v2 Calibration-Capture API Sketch (NOT IMPLEMENTED IN V1)

```cpp
namespace cpipe::calib::v2 {

// ── Inputs ────────────────────────────────────────
struct ColorChartCapture {
  std::string  chart_id;               // "ColorChecker_Classic_24"
  Image        photograph;             // raw DNG of chart under known illuminant
  uint16_t     illuminant_code;        // 17 = Std A, 21 = D65
  std::optional<std::vector<Vec3>> patch_xyz_d50_overrides; // for non-standard charts
};

struct FlatFieldCapture {
  Image        photograph;             // raw DNG of uniform-light scene
  std::optional<float> scene_cct;      // optional; light source CCT
};

struct DistortionCapture {
  std::vector<Image> photographs;      // raw DNG series of chessboard / dot grid
  std::string        target_id;        // "chessboard_9x6" / "dotgrid_circles_asym"
  Vec2u              target_dim;       // (9, 6) or (4, 11)
  float              square_size_mm;   // physical chessboard square size
};

struct DarkFrameCapture {
  std::vector<Image> photographs;      // lens-cap-on dark frames at varied ISO/exposure
};

struct NoiseCalibrationCapture {
  std::vector<std::pair<int, std::vector<Image>>>  by_iso;  // (ISO, frames at varied exposure)
};

// ── Pipeline ──────────────────────────────────────
class CalibrationCapture {
public:
  // Composes a CalibrationProfile from chart photographs.
  // All three illuminants (color, flat-field, distortion, dark) optional;
  // omitted layers are left as identity / DNG-default in the resulting profile.
  static std::expected<CalibrationProfile, CalibError>
    derive_profile(
      std::span<const ColorChartCapture>      color_charts,    // ≥ 1; pref. 2 (D65 + StdA)
      std::span<const FlatFieldCapture>       flat_fields,     // ≥ 1
      std::span<const DistortionCapture>      distortions,     // ≥ 1
      std::span<const DarkFrameCapture>       dark_frames,     // optional
      std::span<const NoiseCalibrationCapture> noise_captures, // optional
      const CalibrationOptions& opts);

  // Round-trip: profile → DNG opcodes (for embedding into a synthesized DNG)
  static std::vector<dng::Opcode> compile_to_opcodes(const CalibrationProfile&,
                                                     dng::OpcodeListId list);
};

struct CalibrationOptions {
  bool fit_hue_sat_map = false;        // expensive; v2.1
  bool fit_lateral_ca  = true;
  int  gainmap_grid    = 33;           // GainMap grid size
  bool perceptual_loss = true;         // ΔE2000 vs L2
  // ... etc
};

} // namespace cpipe::calib::v2
```

The contract: v2's `CalibrationCapture::derive_profile()` produces the **same** `CalibrationProfile` struct that v1's DNG-parse path produces. The renderer is unchanged.

### 4.3 Profile-Storage Format (`.cpcal`) v1 Sketch

JSON envelope wrapping serialised opcode bytes; Apache 2.0; schema versioned.

```json
{
  "schema_version": 1,
  "camera_make": "Pixel",
  "camera_model": "Pixel 8 Pro",
  "lens_id": "main_24mm",
  "captured_at": "2026-04-15T14:30:00Z",
  "profile_source": "cpipe_calibrate",
  "color": {
    "color_matrix_1": [[1.123, -0.456, ...], ...],
    "color_matrix_2": [[1.001, -0.234, ...], ...],
    "forward_matrix_1": [[...], ...],
    "forward_matrix_2": [[...], ...],
    "calibration_illuminant_1": 17,
    "calibration_illuminant_2": 21,
    "as_shot_neutral": [0.5234, 1.0, 0.7129],
    "profile_hue_sat_map_1": "base64(...)",
    "profile_tone_curve": [[0,0],[0.5,0.45],[1,1]]
  },
  "black_white": {
    "black_level": [64, 64, 64, 64],
    "white_level": 16383,
    "black_level_delta_v": "base64(int16x{rows})",
    "black_level_delta_h": "base64(int16x{cols})"
  },
  "opcodes_list_1": "base64(<binary opcode list 1>)",
  "opcodes_list_2": "base64(<binary opcode list 2>)",
  "opcodes_list_3": "base64(<binary opcode list 3>)",
  "noise": {
    "iso": 100,
    "ab": [[0.0001, 0.0009], [0.00012, 0.00091], [0.00012, 0.00091], [0.0001, 0.0008]]
  }
}
```

The opcode payloads are stored as their raw DNG-spec binary form (big-endian); cpipe's opcode interpreter handles them identically whether they came from a DNG file or from a `.cpcal` file.

**DCP read (v1 + v2):** cpipe v1 reads embedded ICC and cached HSV LUT data; reading a separate DCP profile (Adobe Digital Negative Camera Profile, `.dcp` extension) is also supported via the same opcode-and-tag parsers (DCP is a subset of DNG metadata wrapped in a TIFF-shaped header). This makes DNG Profile Editor and Lumariver Profile Designer interoperable.

**DCP write (deferred to v2):** writing a DCP requires either Adobe DNG SDK or a hand-roll. cpipe v2 will hand-roll a DCP writer once the v2 calibration tool exists.

### 4.4 v2 Capture-Flow UI Sketch (NOT IMPLEMENTED IN V1)

```
[ cpipe-calibrate (CLI / Editor wizard) ]                     v2

  Step 1: Camera identity
    > camera make / model / lens     (auto-detected from DNG EXIF if a sample photo is provided)

  Step 2: Color calibration
    > illuminant 1 = Std A  (tungsten)
        - photograph ColorChecker Classic under Std A illuminant
        - upload DNG; auto-detect patches; verify
    > illuminant 2 = D65    (daylight)
        - photograph ColorChecker Classic under D65
        - upload DNG; auto-detect patches; verify
    [ Compute ColorMatrix1/2 + ForwardMatrix1/2 ]
    [ Compute ProfileHueSatMap1/2 (optional, slow) ]

  Step 3: Vignetting / lens-shading
    > photograph uniform light source (printer paper diffuser; 16 frames; averaged)
    [ Compute per-Bayer-plane GainMap (33×33 grid) ]

  Step 4: Distortion
    > photograph chessboard (20+ poses)
    [ Compute Brown-Conrady k1/k2/k3 + p1/p2 + (cx,cy) ]
    > optional: tri-color chart for lateral CA
    [ Compute per-channel distortion polynomials ]

  Step 5: Dark frame (optional)
    > photograph lens-cap-on dark frames at varied ISO
    [ Compute per-row / per-column / per-cell black level deltas ]

  Step 6: Noise
    > photograph uniform grey card at varied exposures and ISO
    [ Compute (a, b) per Bayer plane per ISO; build interpolation table ]

  Step 7: Save
    > write CalibrationProfile to .cpcal file
    > optional: synthesise a DNG with calibration baked into OpcodeList1/2/3
                (allows shareing calibration as a DNG to other DNG-aware tools)
```

This is the v2 spec; v1 ships the renderer side only, plus the `CalibrationProfile` struct that the v1 `parse_dng()` populates.

---

## 5. Cited Sources

- Adobe DNG Specification 1.6.0.0 (Dec 2021), Adobe Inc.
  https://paulbourke.net/dataformats/dng/dng_spec_1_6_0_0.pdf
- Adobe DNG Specification 1.7 release notes (Jun 2023).
  https://community.adobe.com/t5/photoshop-ecosystem-discussions/dng-specifications-v1-7-and-sdk/td-p/14192863
- Calibrite ColorChecker Classic product reference and patch values.
  https://calibrite.com/us/product/colorchecker-classic/
- X-Rite/Calibrite ColorChecker SG documentation.
  https://calibrite.com/us/product/colorchecker-digital-sg/
- Wikipedia: ColorChecker.
  https://en.wikipedia.org/wiki/ColorChecker
- Kodak Q-60 / IT8.7/3 reference targets overview.
  https://www.color.org/iccmax/index.xalter
- Brown D.C., "Decentering distortion of lenses", Photogrammetric Engineering, 1966 (foundational paper).
  https://www.asprs.org/wp-content/uploads/pers/1966journal/may/1966_may_444-462.pdf
- Conrady A., "Decentered lens systems", Monthly Notices of the Royal Astronomical Society, 1919.
- OpenCV calibrateCamera tutorial (Brown-Conrady fit).
  https://docs.opencv.org/4.x/dc/dbb/tutorial_py_calibration.html
- OpenCV MCC module (ColorChecker patch detection).
  https://docs.opencv.org/4.x/dd/d19/group__mcc.html
- Anscombe F.J., "The transformation of Poisson, binomial and negative-binomial data", Biometrika, 1948 (VST background).
  https://academic.oup.com/biomet/article-abstract/35/3-4/246/204446
- Foi A., Trimeche M., Katkovnik V., Egiazarian K. "Practical Poisson-Gaussian noise modeling and fitting for single-image raw-data", IEEE TIP 2008 (the (a, b) model).
  https://webpages.tuni.fi/foi/sensornoise.html
- Hasinoff S. et al. "Burst Photography for High Dynamic Range and Low-Light Imaging on Mobile Cameras", SIGGRAPH Asia 2016 (Pixel pipeline reference).
  https://hdrplusdata.org/hdrplus.pdf
- HDR+ Open Source Code (Google's reference implementation).
  https://hdrplusdata.org/dataset.html
- Apple WWDC20 Session 10089: Capture Pro Photos with ProRAW.
  https://developer.apple.com/videos/play/wwdc2020/10089/
- Apple WWDC22 Session 10027: Discover advances in iPad camera capture.
  https://developer.apple.com/videos/play/wwdc2022/10027/
- Apple WWDC24 Session 10177: Use HDR for dynamic image experiences in your app.
  https://developer.apple.com/videos/play/wwdc2024/10177/
- Samsung Newsroom: Behind the Galaxy S24 Ultra Camera (calibration overview).
  https://news.samsung.com/global/behind-the-galaxy-s24-ultra-camera
- DXOMARK methodology overview.
  https://www.dxomark.com/about/dxomark-image-quality-rating/
- Adobe DNG Profile Editor documentation.
  https://helpx.adobe.com/camera-raw/digital-negative.html
- DCamProf (Anders Torger) homepage and license.
  https://torger.dyndns.org/dcamprof/
- DCamProf documentation (chart-based color profiling).
  https://torger.dyndns.org/dcamprof/dcamprof-overview.html
- lensfun project on SourceForge / lensfun.github.io.
  https://lensfun.github.io/
- lensfun database licensing (LGPL-3 with redistribution clause).
  https://lensfun.github.io/manual/v0.3.2/index.html
- colour-science.org Python library on GitHub.
  https://github.com/colour-science/colour
- colour-science: ColorChecker patch reference values.
  https://www.colour-science.org/colorchecker-patches/
- Argyll CMS homepage (GPL-3).
  https://www.argyllcms.com/
- iccMAX reference implementation (ICC Profile v5).
  https://github.com/InternationalColorConsortium/DemoIccMAX
- Lumariver Profile Designer.
  https://www.lumariver.com/
- darktable documentation: noise profiling.
  https://docs.darktable.org/usermanual/4.6/en/module-reference/processing-modules/denoise-profiled/
- darktable noise model calibration scripts (GPL — read-only reference).
  https://github.com/darktable-org/darktable/tree/master/tools/noise
- Android CTS dng_noise_model.py (Apache 2.0; canonical noise calibration script).
  https://android.googlesource.com/platform/cts/+/master/apps/CameraITS/tools/dng_noise_model.py
- Android Camera2 documentation: NoiseProfile model.
  https://developer.android.com/reference/android/hardware/camera2/CaptureResult#SENSOR_NOISE_PROFILE
- AwareSystems TIFF tag reference: NoiseProfile (51041).
  https://www.awaresystems.be/imaging/tiff/tifftags/noiseprofile.html
- AwareSystems TIFF tag reference: BlackLevelDeltaH (50715), BlackLevelDeltaV (50716).
  https://www.awaresystems.be/imaging/tiff/tifftags/blackleveldeltah.html
- DNG opcode operation timing (Adobe community thread).
  https://community.adobe.com/t5/camera-raw/how-does-the-opcode-operates-on-the-dng-image/td-p/9473447
- discuss.pixls.us: smartphone DNG GainMap embedding samples.
  https://discuss.pixls.us/t/looking-for-samples-smartphone-dngs-with-embedded-gainmap/27296
- darktable issue #8728: lens shading map / DNG GainMap support.
  https://github.com/darktable-org/darktable/issues/8728
- rawspeed issue #267: GainMap implementation.
  https://github.com/darktable-org/rawspeed/issues/267
- ITU-T recommendation on EXIF light source codes (annex E.1 of DNG spec re-uses).
- Capture One forum: WarpRectilinear interpretation issues.
  https://support.captureone.com/hc/en-us/community/posts/360014125557-DNG-Distortion-Correction-issue-Opcode-ID1-WarpRectilinear
- Florian Kainz (Pixel) talk on per-unit calibration (publicly archived; 2022).
  https://research.google/pubs/computational-photography-on-pixel-cameras/
- Marc Levoy Stanford lecture series (publicly available recordings).
  https://graphics.stanford.edu/courses/cs448-11-spring/
- Sony Quad Bayer Coding technology page (sensor calibration context).
  https://www.sony-semicon.com/en/technology/mobile/quad-bayer-coding.html
- iccmax license details on GitHub.
  https://github.com/InternationalColorConsortium/DemoIccMAX/blob/master/LICENSE.md

---

## 6. See Also

- [#06 — Soft ISP architectures](06-soft-isp-architectures.md) — opcode-list scheduling fits into the broader DAG; renderer node design.
- [#07 — Classic ISP algorithms](07-classic-isp-algorithms.md) — the algorithms the calibration data drives: demosaic, denoise, lens correction, white balance, color matrix.
- [#08 — AI ISP algorithms](08-ai-isp-algorithms.md) — AI denoising, AI demosaic, AI defringe; the noise model feeds VST that AI denoisers consume.
- [#12 — DNG format](12-dng-format.md) — the schema and opcode-list binary spec from which calibration data is parsed; complete opcode catalogue.
- [#13 — Color management](13-color-management.md) — the ColorMatrix / ForwardMatrix interpretation, chromatic adaptation, working color space; calibration is the upstream origin of all of these matrices.
- [#16 — Camera2 RAW and burst](16-camera2-raw-and-burst.md) — Android Camera2 NoiseProfile, BlackLevel, ColorMatrix metadata mapping into DNG; how the OS-level capture path produces the calibration tags.

---

## 7. Open Questions

1. **DCP write in v2: hand-roll vs Adobe SDK delegation?** Hand-rolling a DCP writer is ~1 kLoC and gives Apache-clean output but risks subtle compatibility issues with Adobe Camera Raw / Lightroom. Delegating to Adobe DNG SDK is ~0 kLoC of cpipe code but adds an Adobe SDK runtime dependency. Decision deferred to v2 scoping; pre-emptively recommend hand-roll because Adobe has historically deprecated SDK redistribution licences.
2. **Per-individual-unit calibration for cpipe-supplied calibration tool.** Should v2's calibration capture actually try to do per-unit colour calibration (i.e., differentiate two individual iPhone 16 Pros from each other)? Per-unit color variation is small (≤ 1 ΔE2000) and probably below user-perceptible threshold; per-unit vignetting and bad-pixel maps are bigger wins. Recommend: per-unit vignette + bad-pixels yes; per-unit color no (uses the model-level matrix).
3. **Quad Bayer GainMap:** does a Quad Bayer DNG (CFARepeatPatternDim = 4×4) have a 4-plane or 16-plane GainMap? In observed Pixel / Samsung Quad Bayer DNGs: it has 4-plane GainMap (one per RGGB-equivalent channel after the 4×4 → 2×2 remosaic), implying the camera is **already** remosaicing before computing the gain map. cpipe's Quad Bayer remosaic must run before applying the GainMap; verified against sample DNGs but worth a regression test.
4. **lensfun database import:** is it worth automating import of lensfun XML profiles into `.cpcal` for v2? lensfun's mobile camera coverage is sparse (mostly DSLR / mirrorless lenses) but it's a starting bootstrap; later contributions can fill mobile gaps. Recommend: yes, but strictly as a v2.1 nice-to-have.
5. **Noise profile re-measurement:** some phone vendors (specifically iPhone ProRAW) ship NoiseProfile values that reflect the post-denoise residual rather than the sensor's raw read noise. cpipe's denoise nodes will produce poor results if they use these values literally. Mitigation: cpipe v1.x detects iPhone-class DNGs (by Make string) and applies a vendor-specific noise-multiplier heuristic (e.g., × 1.5–2.0) until v2's calibration tool can re-measure. Document in release notes.
6. **Apple Adaptive HDR + ProRAW:** when Apple ships an HDR ProRAW DNG (iOS 18+ in HDR mode), does the calibration data shift? Specifically, does the GainMap encode the HDR rendering's per-pixel gain rather than the sensor's vignetting? Need to inspect samples; if yes, cpipe must distinguish "calibration GainMap" from "rendering GainMap" — a new attribute or sentinel value.
7. **Multi-camera (wide / ultrawide / tele) calibration matching:** mobile phones have 3+ cameras with separate calibration profiles; cpipe must read which lens captured the photograph (DNG `LensModel` or proprietary make-note tag) and apply the corresponding profile. v1 reads `LensModel`; v2 must support per-lens calibration profile selection.
8. **Calibration target physical printing:** for v2's user-facing calibration tool, do we ship a PDF chessboard / dot-grid that the user prints, or do we require a purchased ColorChecker? Recommend: ship printed targets for distortion (PDF, free); require purchased ColorChecker for color (no shortcut to physical reference values). Document in v2 onboarding.
