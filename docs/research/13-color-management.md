# Report 13 — Color Management

**Cluster:** E (Color, Format, Calibration)
**Status:** Research draft
**Date:** 2026-05-08
**Related decisions:** D7 (HDR HEIF + UltraHDR + Apple Adaptive HDR), D8 (ICC + OCIO), D9 (FP16), D11 (Apache 2.0)

---

## 1. TL;DR

Per **D8** the cpipe color stack is **OpenColorIO 2.4+** (BSD-3) for in-pipeline transforms and looks, **Little CMS 2 (lcms2 2.16+)** (MIT) for ICC profile parsing and embedding. The recommended **working color space is linear Rec.2020** (D65), with ACEScg available as an opt-in advanced mode for users coming from VFX.

Why linear Rec.2020:
1. It is the canonical HDR-aware color space (Rec.2100 PQ/HLG share its primaries) — simplest path to UltraHDR / HDR HEIF output (D7).
2. AI nodes (Cluster B) generally tolerate Rec.2020 input with a one-line LUT to/from sRGB; ACEScg has 2 % primaries-difference and is rarely what AI demosaic/denoise was trained on.
3. FP16 has just enough headroom for linear scene-referred data when normalised so that `1.0` = diffuse white (mid-gray ≈ 0.18); peak HDR up to ~25.6 stops above mid-gray fits in FP16 (max = 65 504, log2 ≈ 16).
4. OCIO ships built-in transforms BT.2020 → ACES → display.

For HEIF output color signalling, cpipe writes both an ICC profile (`prof` colour box) and CICP (`nclx`) — the spec allows both; HDR readers prefer CICP (`9 / 16 / 9` for BT.2020-PQ, `9 / 18 / 9` for BT.2020-HLG).

---

## 2. Decision Matrix

| Decision | Recommendation | Trade-off |
|---|---|---|
| Working color space | **linear Rec.2020 (D65)** as default; ACEScg AP1 as advanced opt-in | Rec.2020 ≈ HDR delivery primaries; ACEScg adds VFX heritage but slightly different primaries |
| ICC engine | **lcms2 2.16+** (MIT) via vcpkg | Mature; no Apache trap; does not understand CICP fully — supplement with manual CICP path |
| Pipeline color engine | **OpenColorIO 2.4** (BSD-3) via vcpkg | Industry standard; FP16-clean; GPU shader codegen for Vulkan |
| ICC profile spec target | **v4.4 (ICC.1:2022)** | Latest as of 2026; covers floatType LUTs needed for HDR |
| Working precision | **FP16** with `1.0 = diffuse white`, peak ≤ ~25 stops | Per D9; fits in FP16 range with normalisation |
| Display preview transform | **OCIO Display View** (per-monitor ICC + OCIO display look) | Native OCIO path; aligns with the editor preview ([#11](11-pipeline-editor-and-connectivity.md)) |
| HEIF output color signalling | **CICP (`nclx`) + ICC (`prof`)** dual-write | Maximum reader compatibility |
| HDR gamut mapping | **BT.2407 §A1.1 (perceptual gamut compression)** as default; soft-clip available | Reference-grade; expensive but quality-driven |

---

## 3. Detailed Findings

### 3.1 ICC Profile Specification (v4.4)

**ICC.1:2022 (Profile version 4.4.0.0)** is the current reference; published by the International Color Consortium and registered as ISO 15076-1:2010 / 15076-1:2024 update. The DNG ColorMatrix path produces XYZ-D50; ICC profiles are the way to encapsulate a device's "to PCS" (Profile Connection Space, XYZ or Lab D50) and "from PCS" transforms.

**Header fields cpipe must respect:**
- `magic = 'acsp'` — ICC magic.
- `profile class` — `mntr` (display monitor), `scnr` (input scanner), `prtr` (printer), `spac` (color space), `nmcl` (named color), `link` (device-link), `abst` (abstract), `cenc` (color encoding) ← v4.4 addition.
- `colour space` — `RGB ` / `XYZ ` / `Lab ` / `GRAY` / `CMYK` etc.
- `PCS` — `XYZ ` or `Lab ` (PCS is always D50 except for absolute-colorimetric).
- `creation date/time`, `platform signature`, `flags`, `device manufacturer`, `device model`, `attributes`, `rendering intent`.
- `illuminant of PCS` — D50 (X = 0.9642, Y = 1.0, Z = 0.8249).

**Tag table:** the body of an ICC profile is a list of tag records (`tag signature`, `offset`, `size`). Tags relevant to cpipe (sRGB-class display profile or matrix-shaper input profile):

- `desc` — profile description (textType / multiLocalizedUnicodeType).
- `cprt` — copyright.
- `wtpt` — media white point (XYZ).
- `bkpt` — black point.
- `chad` — chromatic adaptation matrix (Bradford D-illum → D50).
- `rXYZ`, `gXYZ`, `bXYZ` — primary chromaticities (XYZ tristimulus).
- `rTRC`, `gTRC`, `bTRC` — tone-reproduction curves (per channel; either `curveType` or `parametricCurveType`).
- `A2B0/1/2`, `B2A0/1/2` — full-blown LUT-based device→PCS / PCS→device transforms (lutAtoBType, lutBtoAType).
- `cicp` — **ColorEncoding (v4.4)**: 4-byte CICP signalling for device color encoding (color primaries / transfer / matrix coefficients, full-range flag). Allows an ICC profile to assert it is logically equivalent to "BT.2020 + PQ".

**Intent codes:**
- `0` Perceptual — preferred for photos, smooth gamut mapping.
- `1` Media-Relative Colorimetric — match tristimulus, allow clipping; preserve neutral.
- `2` Saturation — push to vivid; preferred for charts.
- `3` Absolute Colorimetric — match including white-point.

cpipe v1 default: **Perceptual** for photographic content; **Relative Colorimetric** for grayscale and chart workflows.

**lutAtoB types (for HDR-aware profiles):**
- `lutAtoB16Type` — 16-bit fixed (legacy).
- `lutAtoBfloatType` — IEEE-754 binary16 (FP16) — added in v4.4. **Critical for HDR**: lets a profile carry an FP16 3D LUT, avoiding the [0,1] clamp implied by 8/16-bit encodings. cpipe HDR pipelines read/write `lutAtoBfloatType`.

### 3.2 lcms2 (Little CMS) Integration

**lcms2 2.16+** (MIT license) is the de-facto open-source ICC engine. API surface used by cpipe:

```cpp
#include <lcms2.h>

cmsHPROFILE input_profile = cmsOpenProfileFromMem(icc_bytes, icc_size);
cmsHPROFILE working_profile = cmsCreate_LinearRec2020Profile();  // helper
cmsHPROFILE output_profile = cmsOpenProfileFromMem(...);        // BT.2020 PQ profile

cmsHTRANSFORM xform = cmsCreateTransform(
  input_profile,  TYPE_RGB_FLT,
  working_profile, TYPE_RGB_FLT,
  INTENT_PERCEPTUAL,
  cmsFLAGS_NOOPTIMIZE | cmsFLAGS_HIGHRESPRECALC);

cmsDoTransform(xform, src_pixels, dst_pixels, n_pixels);

cmsDeleteTransform(xform);
cmsCloseProfile(input_profile);
cmsCloseProfile(working_profile);
cmsCloseProfile(output_profile);
```

**Limitations of lcms2 in 2026:**
- Does not natively read/honour the `cicp` tag (Issue #439 on mm2/Little-CMS, Mar 2024). cpipe must parse CICP separately and override the lcms-derived TRC when CICP is present. This is important for HEIF reads where the ICC TRC may be a placeholder and CICP is authoritative.
- Best in CPU; for GPU we delegate to OCIO.

cpipe's role for lcms: **read embedded ICC** from input DNG/HEIF; **construct working-space ICC** programmatically; **embed output ICC** in HEIF.

### 3.3 OpenColorIO 2.4+

OCIO 2.4 was released **September 2024** and is the **VFX Reference Platform 2025** baseline. OCIO 2.5 (preview/beta as of mid-2025) adds preview ACES 2.0 transforms. cpipe targets OCIO 2.4 as a hard floor.

**License:** BSD-3-Clause — Apache 2.0 compatible (Apache and BSD-3 are permissive and combinable; D11 OK).

**Configuration model:** an OCIO config is a YAML-like document declaring:
- `roles` — semantic anchors: `scene_linear`, `compositing_log`, `color_picking`, `data`, `default`. cpipe sets `scene_linear: linear_rec2020` by default.
- `displays` — physical output targets: sRGB, Display P3, Rec.2020 PQ, Rec.2020 HLG.
- `views` — viewing transforms applied per display: "ACES 2.0 SDR-100", "Untouched", "Filmic Soft", etc.
- `looks` — optional creative grades.
- `colorspaces` — declared spaces (linear Rec.2020, ACEScg, ACES2065-1, sRGB, etc.) with from/to scene-linear transforms.

cpipe ships **two OCIO configs** out-of-box:
1. **`cpipe_default`** — minimalist config: working = linear Rec.2020; displays = sRGB / Display P3 / Rec.2020-PQ / Rec.2020-HLG; views = "Untouched" + "ACES 2.0 SDR" + "ACES 2.0 HDR-1000".
2. **`cpipe_aces`** — full ACES 2.0 config (`cg-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio` from OpenColorIO-Config-ACES) for users who need ACES interchange.

The active config is selected at runtime via `OCIO_ACTIVE_CONFIG` env var or the cpipe CLI / Editor "Color → Active config" menu.

**GPU shader generation:**
```cpp
OCIO::ConstConfigRcPtr config = OCIO::Config::CreateFromEnv();
OCIO::ConstProcessorRcPtr proc = config->getProcessor(
  "linear_rec2020", "Display P3 - sRGB");
OCIO::ConstGPUProcessorRcPtr gpu = proc->getDefaultGPUProcessor();

OCIO::GpuShaderDescRcPtr desc = OCIO::GpuShaderDesc::CreateShaderDesc();
desc->setLanguage(OCIO::GPU_LANGUAGE_VK_GLSL_4_50);
desc->setFunctionName("OCIODisplay");
gpu->extractGpuShaderInfo(desc);

const char* shader_text = desc->getShaderText();
// integrate into the cpipe scheduler as a Vulkan compute pass
```

OCIO 2.4 added Vulkan target. cpipe consumes the resulting GLSL fragment as part of compositing the Vulkan / Slang RHI graph from [#01 — compute frameworks](01-compute-frameworks.md).

**FP16 in OCIO:** OCIO operates in FP32 by default. The GPU pipeline can downcast to FP16 at the input edge of the OCIO compute kernel and back-to-FP32 on output if precision is needed; or keep everything FP16. Inspection of OCIO 2.4 source (CPU and GPU paths) shows OCIO's internal LUT-3D apply uses 32-bit float internally even when the final write is FP16 — adequate for HDR. **D9 alignment: OCIO is FP16-output-clean.**

### 3.4 ICC vs OCIO — When Each Applies

cpipe layers them:

```
                 ┌──────────────────────┐
DNG file ───►   │ [ICC] camera profile  │ →  XYZ D50
                 └──────────────────────┘
                            │
                 ┌──────────────────────┐
                │  XYZ D50 → working CS  │  (Bradford CAT + matrix; lcms or hand-coded)
                 └──────────────────────┘
                            │
                            ▼  (linear Rec.2020, FP16)
                 ┌──────────────────────┐
                │   <user DAG: tone,     │
                │    denoise, AI, ...>  │
                 └──────────────────────┘
                            │
                            ▼
                 ┌──────────────────────┐
                │  [OCIO] working → display preview   │  (Editor only)
                 └──────────────────────┘
                            │
                            ▼  (display refresh)
                 ┌──────────────────────┐
                │ [ICC + CICP] output    │ →  HEIF embed
                │  profile encoding      │
                 └──────────────────────┘
```

**Rule of thumb:** ICC at the **boundaries** (input, output, display monitor); OCIO **inside** the pipeline (looks, exposure stops, log encoding, ACES transforms). Both, layered.

### 3.5 Working Color Space Selection — Detailed Trade-off

The candidates from D8:

| Space | Primaries | White | Gamut size | FP16 fit | NPU/AI | Notes |
|---|---|---|---|---|---|---|
| linear Rec.709 / sRGB | BT.709 | D65 | Smallest | Excellent | Excellent (training data) | Too small for HDR or wide-gamut camera output |
| linear ProPhoto (ROMM) | Kodak ProPhoto | D50 | Wider than Rec.2020 | Risk of negative blue | Untrained territory | Photographic heritage; D50 mismatches HDR delivery (D65) |
| linear Rec.2020 | BT.2020 | D65 | Wide; matches HDR delivery | Excellent | Acceptable (close to sRGB) | **Recommended default** |
| ACEScg (AP1) | AP1 | D60 | Slightly larger than Rec.2020 | Excellent | Acceptable | VFX heritage; D60 ≠ delivery |
| ACES2065-1 (AP0) | AP0 | D60 | Encompasses spectral locus (incl. imaginary) | Headroom + risk of negative | Untrained | Archival only; not for active editing |
| Sensor-native RGB | per-camera | per-camera | Camera-defined | OK | Untrained | Skip color matrix; defer to editor — too volatile for v1 |

**Why linear Rec.2020 wins for cpipe:**

1. **HDR delivery alignment:** Rec.2100 (PQ and HLG) shares Rec.2020 primaries. Going from working → BT.2020 PQ is a `pure 1D OETF` on luminance — no gamut mapping. ACEScg → BT.2020 PQ involves a tiny matrix step plus OETF. ProPhoto → BT.2020 is a primary-conversion matrix that can clip if the camera fired colours outside Rec.2020.
2. **AI-friendly:** AI ISP nodes (denoise, demosaic, super-resolution, tone) are typically trained on sRGB or linear Rec.709 / Rec.2020 data. Rec.2020 is the closest "wide" working space to training distribution. ACEScg is 2 % off but observable.
3. **FP16 dynamic range:** with normalisation `1.0 = diffuse white`, Rec.2020 linear can encode -0.5 to +24,000 in FP16 (smallest FP16 = ~6e-5, largest = 65 504). 16 stops above mid-gray = ~12 000 in linear; HDR cinema clips to ~10 000 nits / 100 at mid-gray, all in-range.
4. **D65 = display white:** matches consumer display white-point; one less Bradford CAT step for SDR sRGB output.

**Why ACEScg is the "advanced" opt-in:**
- VFX users cross-cutting between cpipe and Nuke / Resolve expect ACEScg.
- ACES 2.0 Output Transforms in OCIO 2.4+ and 2.5 are a polished SDR + HDR delivery story.
- D60 white means Bradford CAT for any D65 delivery — small but not zero.

**Why ProPhoto loses:** D50 white is a meaningful CAT cost for HDR delivery, and ProPhoto's primaries include imaginary colors that can yield negative components inside the working space — a frequent cause of FP16 NaN/Inf when downstream nodes assume positivity. ACES2065-1 has the same issue but is intended for archival, not active processing. ProPhoto used as a working space for SDR JPEG output (Adobe Lightroom historical default) has no advantage in cpipe's HDR-first world.

**Sensor-native RGB** is interesting for theoretical perfection — color matrix is the only place where bit-precision is "lost" in early ISP — but each camera's native space is a unique manifold that AI nodes do not understand and OCIO cannot describe stably. cpipe **considers** sensor-native as a v2 power-user mode but ships v1 with the matrix applied as part of DNG ingest.

### 3.6 Color Matrix from DNG → Working CS

The math is canonical (see Adobe DNG Specification ch. 6.4.3):

**Step 1 — Determine scene illuminant CCT.** From `AsShotNeutral` (raw RGB triplet that ought to be neutral white), invert through the dual-illuminant `ColorMatrix1/2` to find the CCT of the illuminant under which the camera saw a neutral. Solving:

```
Given AsShotNeutral = (n_r, n_g, n_b)  (raw)
For a candidate CCT t:
  M(t) = inverse_lerp_color_matrix(CCT_1, CM1, CCT_2, CM2, t)
  XYZ(t) = M(t)^-1 * (n_r, n_g, n_b)
  xy(t)  = XYZ_to_xy(XYZ(t))
  err(t) = distance(xy(t), planckian_locus(t))
Find t* = argmin err(t)
```

This is a 1D search over CCT (typically Brent's method, 1500K–10000K). Result `t*` is the inferred scene CCT.

**Step 2 — Interpolate ForwardMatrix at t*:**

```
weight = compute_blend(CCT_1, CCT_2, t*)   // Adobe spec: linear blend in 1/CCT
FM(t*) = (1 - weight) * FM2 + weight * FM1
```

**Step 3 — White-balance the raw:**

```
WB = diag(1/n_r, 1/n_g, 1/n_b)  // multiplicative white balance
```

**Step 4 — Combine and apply per pixel:**

```
RGB_camera_wb = WB * raw_RGB
XYZ_D50 = FM(t*) * RGB_camera_wb
XYZ_D65 = M_chromatic_adaptation_D50_to_D65 * XYZ_D50  // Bradford
RGB_rec2020_linear = M_XYZ_D65_to_Rec2020 * XYZ_D65
```

If `ForwardMatrix1/2` are **absent** (older DNG, some Pixel XL pre-2018 DNGs), fall back to the legacy path:

```
M_inv(t*) = inverse(CM(t*))   // ColorMatrix maps XYZ→camera, invert
RGB_camera = AnalogBalance * raw_RGB
XYZ_D50 = M_inv(t*) * (RGB_camera / AsShotNeutral)
... continue as above
```

ForwardMatrix-based path is preferred because it makes the intent explicit: "given white-balanced camera, give me XYZ" — no inverse needed; better numerical conditioning.

### 3.7 Chromatic Adaptation (Bradford CAT)

D50 → D65 is a fixed 3×3 matrix (Bradford-cone-space transform):

```
M_D50_to_D65 = M_inv_bradford * diag(D65_XYZ / D50_XYZ in cone space) * M_bradford
```

cpipe pre-computes this matrix as a constant. The Bradford matrix:

```
M_bradford = | 0.8951  0.2664  -0.1614 |
             |-0.7502  1.7135   0.0367 |
             | 0.0389 -0.0685   1.0296 |
```

Other CATs (CAT02, CAT16) are options but Bradford is the DNG-mandated CAT; cpipe sticks to it for compatibility.

### 3.8 HDR Gamut Handling

When the working space (linear Rec.2020) is delivered to a smaller-gamut SDR display (sRGB / Display P3), out-of-gamut colors must be mapped. Two families:

**Hard clip (BT.2407 §A.2):** clip per channel. Cheapest; punitive on saturated colours; produces hue shifts.

**Perceptual gamut compression (BT.2407 §A.1.1, ACES 2.0 GMA):** compress the gamut smoothly with a bezel near the gamut boundary; preserves neutral and skin tones; touched only by saturated colours. **cpipe default for SDR output.**

**Soft clip / per-channel rolloff:** an intermediate. Used in Filmic curves (Blender, darktable filmic). cpipe can offer this as a "look".

**ACES 2.0 GMA:** the new ACES 2.0 Output Transforms include a sophisticated gamut mapping algorithm based on color appearance modeling (CAM16 / CAM02-like). When the user selects "ACES 2.0" view in the OCIO config, this is the GMA used.

### 3.9 SMPTE ST 2086 Mastering Display Metadata

For HDR HEIF output (D7), cpipe writes:

- **`mdcv` box (ST 2086)** — the chromaticities of the mastering display primaries, white point xy, and min/max luminance. The "mastering display" for an in-camera or in-app HDR image is often nominal: `BT.2020 primaries, D65, 0.01 nits min, 1000 nits max` is a defensible default.
- **`clli` box** — MaxCLL (max content light level) and MaxFALL (max frame-average light level). Computed at encode time by scanning the rendered HDR pixels.

For a single still image, MaxFALL is computed as the actual frame mean luminance; MaxCLL is the actual max pixel luminance. These are stored as integer nits.

The order of operations:
1. Render HDR linear Rec.2020 → BT.2020 PQ-encoded pixel grid.
2. Compute MaxCLL / MaxFALL on the linear (pre-PQ) pixels.
3. Encode HEVC main10 stream with CICP signalling.
4. Wrap in HEIF; embed `mdcv` and `clli` in `iprp` properties.

### 3.10 HEIF Output Color Signalling

HEIF (ISO/IEC 23008-12) carries color information via the `colr` (ColourInformationBox) inside the item's `iprp` (ItemPropertyContainer). The box has a 4-CC `colour_type` selector:

- `nclx` — CICP signalling (4 fields: color_primaries, transfer_characteristics, matrix_coefficients, full_range_flag). 8 bytes.
- `rICC` — restricted ICC profile (only matrix-shaper class profiles; for compatibility).
- `prof` — full ICC profile.

**Per HEIF spec, only one `colr` box of type `nclx` is allowed, and only one of type `rICC` or `prof`.** Multiple `colr` of different types may coexist — common practice is to write both `nclx` and `prof`. ImageMagick issue #7715 (2024) documents real-world breakage when `nclx` is omitted on HDR HEIF; libheif issue #995 reports buggy NCLX serialisation in older versions.

**CICP for HDR HEIF:**

| Output | color_primaries | transfer | matrix | full_range |
|---|---|---|---|---|
| sRGB SDR | 1 (BT.709) | 13 (sRGB) | 5/6 (BT.601) or 1 (BT.709) | 1 |
| Display P3 SDR | 12 (Display P3) | 13 (sRGB) | 5/6 | 1 |
| BT.2020 SDR | 9 | 14 (BT.2020 10-bit) | 9 | 1 |
| BT.2020 PQ HDR | 9 | **16 (PQ ST 2084)** | 9 | 1 |
| BT.2020 HLG HDR | 9 | **18 (HLG)** | 9 | 1 |

`matrix_coefficients = 0` (Identity / RGB) is used when the file is encoded in raw RGB rather than YCbCr; common for HEIF still images stored in RGB. cpipe writes `matrix_coefficients = 9` (BT.2020 non-constant luminance) when the HEVC stream is YCbCr — which is the usual path because HEVC encoders are tuned for YCbCr.

cpipe **always writes both `nclx` and `prof`** to maximise reader compatibility:
- iOS 16+, Android 13+, macOS 12+, Windows 11+ all read `nclx`.
- Older readers and many third-party apps fall back to `prof`.

**lcms2 issue #439** identifies that lcms ignores ICC's `cicp` tag — cpipe's CICP path is hand-rolled (small; ~50 LoC).

### 3.11 Tone Mapping Interaction

Tone mapping converts scene-referred linear (open-ended, can exceed `1.0`) → display-referred (bounded). cpipe's tone-mapping nodes operate on linear Rec.2020 inputs.

Key principle: **tone-map in linear scene-referred working space, then OETF-encode for delivery.** This is the OCIO pattern and the ACES Output Transform pattern:

```
linear Rec.2020 (scene)
  ─► [Tone Map: SDR-100] ─► linear Rec.2020 (display, [0,1] for SDR)
  ─► [Rec.2020 OETF: BT.2020] ─► nonlinear Rec.2020 SDR
  ─► [HEVC encode + colr=nclx 9/14/9]

linear Rec.2020 (scene)
  ─► [Tone Map: HDR-1000] ─► linear Rec.2020 (display, up to 10.0 = 1000 nits)
  ─► [PQ OETF] ─► PQ Rec.2020
  ─► [HEVC main10 encode + colr=nclx 9/16/9]
```

**Per-display vs per-output-format:** the editor preview applies a "display tone map" specific to the user's monitor (using the monitor's ICC). The export pipeline applies a "delivery tone map" specific to the chosen output format (PQ-1000 nits, HLG, SDR-100). Both are OCIO Display+View combinations.

### 3.12 GPU Implementation

For each transform stage, cpipe choose between:

1. **1D LUT** — gamma curves, OETFs, EOTFs. Cheap. Implemented as a Vulkan storage buffer or a `R32_FLOAT` 1D image; sampled with `texture()` in the shader.
2. **3D LUT** — large nonlinear transforms (e.g. ACES Output Transform). Typical sizes: 33×33×33 or 65×65×65. Stored as a Vulkan `R16G16B16A16_SFLOAT` 3D image; sampled with linear filtering. **65³ = 274 625 samples × 8 bytes (FP16x4) = 2.1 MB** — fits comfortably in GPU memory.
3. **Procedural matrices** — simple ones like XYZ→RGB or CAT. Run inline in shaders; no lookup.

OCIO 2.4 GPU codegen handles all three intelligently: a Display View transform may compile to "LUT-3D + matrix + LUT-1D" with three texture binds and a few ops.

**Precision:** OCIO 2.4 supports `R32F` 3D LUTs and `R16F` 3D LUTs. cpipe defaults to FP16 LUTs for memory bandwidth (and per D9). For perceptual transforms (gamma 2.2 etc.), 1D LUT at 1024 entries is sufficient; for HDR PQ OETF, prefer **procedural** PQ formula for full precision (the closed-form is cheap):

```glsl
vec3 pq_oetf(vec3 L)  // L in [0, 1] where 1.0 = 10000 nits
{
  const float m1 = 0.1593017578125;
  const float m2 = 78.84375;
  const float c1 = 0.8359375;
  const float c2 = 18.8515625;
  const float c3 = 18.6875;
  vec3 Lm = pow(max(L, 0.0), vec3(m1));
  return pow((c1 + c2 * Lm) / (1.0 + c3 * Lm), vec3(m2));
}
```

### 3.13 Reference Repositories Inspected

1. **AcademySoftwareFoundation/OpenColorIO** — 2.4 release source. Inspected GPU shader codegen path (`src/OpenColorIO/GpuShaderUtils.cpp`); confirmed Vulkan target works and Slang can transpile. License BSD-3 (D11 OK).
2. **mm2/Little-CMS** — lcms2 2.16. Confirmed CICP tag is unimplemented in transform engine (issue #439) — design implication: cpipe must hand-handle CICP separately. License MIT (D11 OK).
3. **AcademySoftwareFoundation/OpenColorIO-Config-ACES** — ships `cg-config-v4.0.0_aces-v2.0_ocio-v2.5.ocio`. License BSD-3. cpipe loads as the ACES advanced config.

---

## 4. Concrete Code Sketches

### 4.1 ColorContext API

```cpp
namespace cpipe::color {

enum class WorkingSpace {
  LinearRec2020,   // default
  ACEScg,          // advanced
};

struct DisplayInfo {
  std::string ocio_display;     // "Display P3 - sRGB" / "BT.2020 PQ" / ...
  std::string ocio_view;        // "Untouched" / "ACES 2.0 SDR-100" / ...
  std::optional<IccProfile> monitor_profile;  // calibrated monitor ICC, optional
};

class ColorContext {
public:
  // Active config (selected at construction)
  static std::unique_ptr<ColorContext> create_default();   // cpipe_default
  static std::unique_ptr<ColorContext> create_aces();      // cpipe_aces

  WorkingSpace working_space() const;

  // Camera profile -> working CS transform; built from DNG ColorMatrix/ForwardMatrix
  Transform build_dng_to_working(const dng::ColorCalibration&,
                                 const Vec3& as_shot_neutral) const;

  // Working CS -> output CS for HEIF embed
  Transform build_working_to_output(const OutputProfile&) const;

  // GPU shader for a transform (Vulkan GLSL via OCIO codegen)
  std::string compile_gpu_shader(const Transform&) const;

  // For Editor preview
  Transform build_display_preview(const DisplayInfo&) const;

  // ICC profile bytes for HEIF embedding
  std::vector<std::byte> generate_output_icc(const OutputProfile&) const;

  // CICP triplet for HEIF nclx box
  CicpTriple cicp_for(const OutputProfile&) const;
};

struct CicpTriple {
  uint16_t color_primaries;
  uint16_t transfer_characteristics;
  uint16_t matrix_coefficients;
  bool     full_range;
};

} // namespace cpipe::color
```

### 4.2 OCIO config snippet (cpipe_default)

```yaml
# cpipe_default.ocio (extract)
ocio_profile_version: 2.4

roles:
  scene_linear:    linear_rec2020
  color_picking:   sRGB - Display
  data:            raw
  default:         linear_rec2020

displays:
  sRGB:
    - !<View> {name: "Untouched", colorspace: sRGB - Display}
    - !<View> {name: "ACES 2.0 SDR-100", colorspace: sRGB - Display, look: ACES_2_SDR}
  Display P3:
    - !<View> {name: "Untouched", colorspace: Display P3 - Display}
    - !<View> {name: "ACES 2.0 SDR-100", colorspace: Display P3 - Display, look: ACES_2_SDR}
  Rec.2020 PQ:
    - !<View> {name: "PQ-1000", colorspace: Rec.2020 - PQ - Display}
    - !<View> {name: "ACES 2.0 HDR-1000", colorspace: Rec.2020 - PQ - Display, look: ACES_2_HDR_1000}
  Rec.2020 HLG:
    - !<View> {name: "HLG", colorspace: Rec.2020 - HLG - Display}

active_displays: [sRGB, Display P3, Rec.2020 PQ, Rec.2020 HLG]
active_views:    [Untouched, ACES 2.0 SDR-100, ACES 2.0 HDR-1000, PQ-1000, HLG]

colorspaces:
  - !<ColorSpace>
    name: linear_rec2020
    family: scene
    bitdepth: 32f
    isdata: false
    encoding: scene-linear
  - !<ColorSpace>
    name: sRGB - Display
    bitdepth: 8ui
    encoding: sdr-video
    from_scene_reference: !<GroupTransform>
      children:
        - !<MatrixTransform> {matrix: [Rec2020->sRGB primaries matrix...]}
        - !<ColorSpaceTransform> {dst: srgb_oetf}
  # ... Rec.2020 - PQ - Display, etc.
```

### 4.3 HEIF colr-box writer

```cpp
struct HeifColr {
  // Always emit nclx; emit prof when ICC bytes are present
  enum class Type { Nclx, Prof, RestrictedIcc };
  Type type;

  // For Nclx
  CicpTriple cicp;

  // For Prof / RestrictedIcc
  std::vector<std::byte> icc_bytes;
};

void write_colr(HeifWriter& w, const HeifColr& c) {
  w.box_start("colr");
  switch (c.type) {
    case HeifColr::Type::Nclx:
      w.write_4cc("nclx");
      w.write_u16_be(c.cicp.color_primaries);
      w.write_u16_be(c.cicp.transfer_characteristics);
      w.write_u16_be(c.cicp.matrix_coefficients);
      w.write_u8(c.cicp.full_range ? 0x80 : 0x00);
      break;
    case HeifColr::Type::Prof:
      w.write_4cc("prof");
      w.write_bytes(c.icc_bytes);
      break;
    case HeifColr::Type::RestrictedIcc:
      w.write_4cc("rICC");
      w.write_bytes(c.icc_bytes);
      break;
  }
  w.box_end();
}
```

---

## 5. Cited Sources

- ICC Specifications (ICC.1:2022 / v4.4 entry).
  https://www.color.org/v4spec.xalter
- ICC open-source tools list.
  https://www.color.org/opensource.xalter
- mm2/Little-CMS issue #439 (CICP tag handling).
  https://github.com/mm2/Little-CMS/issues/439
- LittleCMS unbounded mode reference (Elle Stone).
  https://ninedegreesbelow.com/photography/lcms2-unbounded-mode.html
- OCIO 2.4 release notes.
  https://opencolorio.readthedocs.io/en/v2.4.0/releases/ocio_2_4.html
- OCIO 2.5 release notes.
  https://opencolorio.readthedocs.io/en/latest/releases/ocio_2_5.html
- OCIO Using OCIO documentation 2.4.
  https://opencolorio.readthedocs.io/en/v2.4.0/guides/using_ocio/using_ocio.html
- OpenColorIO-Config-ACES Releases.
  https://github.com/AcademySoftwareFoundation/OpenColorIO-Config-ACES/releases
- OpenColorIO official site.
  https://opencolorio.org/
- ACES 2.0 implementation March 12 2025 meeting notes.
  https://community.acescentral.com/t/aces-2-0-implementation-optimization-meeting-march-12th-2025/5739
- ACEScg specification.
  https://docs.acescentral.com/specifications/acescg/
- ACES vs ProPhoto discussion.
  https://community.acescentral.com/t/aces-vs-prophoto-for-still-images-and-archiving/4513
- darktable forum on linear Rec.2020 working space.
  https://discuss.pixls.us/t/linear-rec-2020-rgb-why/50611
- ITU-R BT.2407 gamut conversion report.
  https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2407-2017-PDF-E.pdf
- ITU-R BT.2408-7 guidance for HDR operational practices.
  https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-7-2023-PDF-E.pdf
- ITU-R BT.2446-1 HDR-to-SDR conversion methods.
  https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2446-1-2021-PDF-E.pdf
- Wikipedia: Rec. 2100.
  https://en.wikipedia.org/wiki/Rec._2100
- Wikipedia: Hybrid log–gamma.
  https://en.wikipedia.org/wiki/Hybrid_log%E2%80%93gamma
- Wikipedia: Academy Color Encoding System.
  https://en.wikipedia.org/wiki/Academy_Color_Encoding_System
- HDR10 / SMPTE ST 2086 explainer.
  https://ff.de/st-2086-demystified-from-codec-constraints-to-metadata-mastery-with-hdrmaster/
- Wikipedia: HDR10.
  https://en.wikipedia.org/wiki/HDR10
- AVIF/AV1 CICP wiki on AOMediaCodec.
  https://github.com/AOMediaCodec/libavif/wiki/CICP
- AOMediaCodec/av1-avif issue #84 (ICC vs CICP).
  https://github.com/AOMediaCodec/av1-avif/issues/84
- Strukturag/libheif issue #119 (color profile support).
  https://github.com/strukturag/libheif/issues/119
- libheif issue #995 (NCLX serialisation).
  https://github.com/strukturag/libheif/issues/995
- ImageMagick issue #7715 (NCLX/CICP corruption on HDR).
  https://github.com/ImageMagick/ImageMagick/issues/7715
- Adobe DNG Spec 1.6.0.0 (color matrix math).
  https://paulbourke.net/dataformats/dng/dng_spec_1_6_0_0.pdf
- colour-hdri DNG model (Python reference).
  https://github.com/colour-science/colour-hdri/blob/develop/colour_hdri/models/dng.py
- Greg Benz photography on color spaces.
  https://gregbenzphotography.com/photoshop/which-colorspace-should-you-use-for-photography/
- VFX Reference Platform 2025.
  https://www.aswf.io/blog/industry-support-for-ocio-v2/

---

## 6. See Also

- [#01 — Compute frameworks](01-compute-frameworks.md) — Vulkan / Slang RHI host the OCIO-generated shaders.
- [#02 — Zero-copy buffers](02-zero-copy-buffer-architecture.md) — color buffers carry FP16 data; the working-space tag travels with the buffer.
- [#06 — Soft ISP architectures](06-soft-isp-architectures.md) — color stages slot into the DAG.
- [#07 — Classic ISP algorithms](07-classic-isp-algorithms.md) — tone-mapping algorithms that run in the working space.
- [#11 — Pipeline editor](11-pipeline-editor-and-connectivity.md) — display preview uses the same OCIO transform.
- [#12 — DNG format](12-dng-format.md) — input ColorMatrix/ForwardMatrix.
- [#14 — HEIF and HDR output](14-heif-and-hdr-output.md) — output side of color signalling.
- [#15 — Mobile camera calibration](15-mobile-camera-calibration.md) — calibration data feeds the camera-to-working transform.

---

## 7. Open Questions

1. **ACEScg as default for users coming from VFX?** Open the question to early Beta users; instrument config-selector telemetry. v1 ships Rec.2020 default but allow swap via OCIO env var; collect data; revisit for v1.1.
2. **Apple Adaptive HDR colour signalling**: does an Adaptive-HDR HEIF carry CICP `9/16/9` plus a gain-map, or `9/13/9` plus a gain-map? Tracker: see [#14](14-heif-and-hdr-output.md). Need to read iPhone 16 ProRAW outputs.
3. **AI demosaic / denoise color-space sensitivity**: train models on linear Rec.2020 vs sRGB; a small benchmark is needed (cross-link [#08](08-ai-isp-algorithms.md)).
4. **OCIO config evolution**: ACES 2.0 ships in OCIO 2.5; cpipe targets 2.4. Should cpipe pin OCIO 2.5 if available at v1 release? Yes, with a 2.4 fallback path.
5. **Display calibration**: for the Editor's display preview, do we ship a per-monitor ICC ingest (drag-and-drop a `.icc` file)? v1 plan: yes — small UX win, leans on lcms.
6. **Wide-gamut sensor support beyond Rec.2020**: some smartphone sensors (Sony IMX989-class) measurably exceed Rec.2020 primaries on red. ACEScg has slightly more coverage. Worth profiling; currently we accept the small clip on extreme reds. Document in release notes.
7. **lcms2 + CICP**: track lcms 2.17+ for built-in CICP support; if shipped, replace the hand-rolled CICP path.
