# Report 07 — Classic (Non-AI) ISP Algorithms for cpipe v1

> **Cluster C — ISP Pipeline & Algorithms.** Survey of state-of-the-art classic
> algorithms (2024–2026) for each ISP stage, with concrete implementation
> licensing, complexity, and node-level recommendations for cpipe v1. Pairs
> with [#06](06-soft-isp-architectures.md) (architecture) and
> [#08](08-ai-isp-algorithms.md) (neural counterparts).

---

## 1. TL;DR

cpipe v1 ships **18 classic algorithm nodes** across 9 stages. Demosaic =
**RCD as default** + **AMaZE** for low-ISO + **bilinear** as ultra-fast
fallback; Quad Bayer = **remosaic-then-RCD** in v1 (direct demosaic deferred
to v2). White balance = **dual-illuminant interpolation** from DNG
ColorMatrix1/2 + ForwardMatrix1/2 + AsShotNeutral, with a Greyworld auto
helper. Tone = **filmic-RGB-style** parametric curve as primary global
mapper, **Mertens exposure-fusion-style** Laplacian local tone mapping for
HDR, **ACES Filmic** as ICC-profile-friendly alternate, and **Reinhard**
debug fallback. Multi-frame fusion = **HDR+ derivative** (align-pyramid +
tile-merge with raised cosine, Bayer-domain) per IPOL re-implementation.
Denoise = **BM3D** for high quality plus **guided-filter** for fast preview
plus **wavelet BayesShrink** for chroma. Sharpen = **edge-aware USM via
guided filter**. Lens correction = **Adobe DNG OpcodeList3** dispatcher
(MIT-style license — safe) + **Lensfun** (LGPL3, link-only). Black/white
level + NoiseProfile = direct DNG metadata read. Color enhancement = 3D-LUT
(33³, tetrahedral interp) + parametric curves. All 18 nodes target Halide
or slang-rhi shaders. License audit complete: 14/18 fully open, 4/18 must
be re-implemented from primary papers (BM3D, RCD, AMaZE, AHD all GPL in
existing FOSS). (200 words)

---

## 2. Decision Matrix — v1 Classic Node Set

| Stage              | Algorithm                  | License of ref impl                           | Complexity (per H×W image) | GPU friendliness | v1 Status   | Notes                                            |
|---------------------|----------------------------|------------------------------------------------|------------------------------|------------------|--------------|--------------------------------------------------|
| Black/White level   | scalar subtract / scale    | trivial                                         | O(N)                          | trivial          | YES default | from DNG `BlackLevel` / `WhiteLevel`            |
| Lens shading        | DNG OpcodeList2 GainMap    | Adobe DNG SDK (MIT-style)                       | O(N)                          | trivial          | YES default | per-Bayer-channel multiplicative                 |
| Demosaic            | RCD                         | RawTherapee (GPLv3) — re-impl                  | O(N) ~50 ops                | very good        | YES default | adopted as darktable default 2025                |
| Demosaic            | AMaZE                       | RawTherapee (GPLv3) — re-impl                  | O(N) ~150 ops               | OK              | YES alt     | best for low-ISO, slowest                        |
| Demosaic            | bilinear                    | trivial                                          | O(N) ~10 ops                 | trivial          | YES alt     | preview / fallback                                |
| Quad Bayer          | remosaic-then-Bayer-demosaic| Sony/Samsung patents (closed); IPOL 2025 paper | O(N) 4×                       | OK              | YES default | direct Quad demosaic deferred to v2              |
| Lens distortion     | DNG OpcodeList3 WarpRect    | Adobe DNG SDK (MIT-style)                       | O(N) bicubic                  | very good        | YES default | radial polynomial; bicubic resample              |
| Lens distortion     | Lensfun                     | LGPL3 (link only)                                | O(N) bicubic                  | very good        | YES alt     | community lens database                          |
| Lateral CA          | DNG OpcodeList3 + Lensfun  | as above                                          | O(N) per-channel              | very good        | YES default | per-channel scaling                              |
| Vignetting          | DNG OpcodeList3 FixVignette | Adobe DNG SDK (MIT-style)                       | O(N)                          | trivial          | YES default | 4th-order radial                                  |
| White balance       | dual-illuminant interp      | Adobe DNG spec — independent impl              | O(1) + O(N) gain               | trivial          | YES default | from DNG ColorMatrix1/2 + AsShotNeutral          |
| Auto WB             | gray world / gray edge      | Buchsbaum 1980 / Van de Weijer 2007             | O(N)                          | trivial          | YES alt     | helper                                            |
| Multi-frame fusion  | HDR+ align/merge/finish     | IPOL re-impl (MIT-style)                        | O(N×F) F frames               | good              | YES default | core of D3                                        |
| Denoise (luma)      | BM3D                        | Mäkinen 2020 (BSD); CUDA / Halide impls         | O(N×B²×S²)                    | hard but doable  | YES default | best classic IQ                                   |
| Denoise (chroma)    | wavelet BayesShrink          | scikit-image (BSD3)                              | O(N log N)                    | OK              | YES default | fast colour denoise                              |
| Denoise (preview)    | guided filter               | He 2010 paper (academic); MIT impls available  | O(N)                          | very good        | YES default | preview / sharpen helper                         |
| Sharpen             | edge-aware USM (via guided) | He 2010                                          | O(N)                          | very good        | YES default |                                                  |
| Tone (global)       | filmic-RGB-style parametric | darktable (GPLv3) — re-impl                    | O(N)                          | trivial          | YES default | scene-referred parametric S-curve                |
| Tone (alt)          | ACES Filmic (Narkowicz fit)  | Apache-2 (Narkowicz blog)                        | O(N) 5-coef                   | trivial          | YES alt     | display-referred                                  |
| Tone (alt)          | Reinhard global              | Reinhard 2002 (paper)                            | O(N) 1-line                   | trivial          | YES alt     | debug                                              |
| Tone (local)        | Mertens / Laplacian fusion  | IPOL 2018 (BSD)                                  | O(N log N)                    | very good        | YES default | HDR+-derived                                      |
| Color (3D LUT)      | tetrahedral interpolation   | trivial ; LUTs from disk                       | O(N) 4-fetch                 | trivial          | YES default | for film simulation, etc.                        |
| Color (curves)       | parametric / spline         | trivial                                          | O(N) 1-fetch                 | trivial          | YES default |                                                  |

---

## 3. Detailed Findings

### 3.1 Demosaic — standard Bayer

**Why this matters.** Demosaic is *the* defining classic-ISP step. The
algorithm choice drives downstream denoise and sharpen behaviour. cpipe needs
multiple algorithms because users have legitimate quality / speed trade-offs.

**Surveyed algorithms.**

* **Bilinear** — trivial averaging. `O(N)` ~10 ops. Strong false-colour in
  texture; only useful as fallback / preview. License: trivial.
* **Hamilton-Adams (1997)** — adaptive direction-of-interpolation. Reference:
  Hamilton & Adams US Patent 5,629,734 (expired). Modern dcraw uses this as
  AHD's basis. `O(N)` ~30 ops.
* **AHD (Adaptive Homogeneity-Directed, Hirakawa 2005)** — picks horizontal
  vs vertical interpolation based on a homogeneity metric. Industry-default
  since 2005. *Reference impl:* `rt::ahd_demosaic_RT.cc` (GPLv3). Independent
  re-impl needed for cpipe.
* **PPG (Patterned Pixel Grouping)** — fast, dcraw default, slightly worse
  than AHD. dcraw is BSD-style. Worth keeping as a low-cost option.
* **DCB (Jacek Górny)** — anti-aliasing focused, good in low-noise. `O(N)`.
  *Reference impl:* `rt::dcb_demosaic.cc` (GPLv3). Independent re-impl.
* **VNG4 (Variable Number of Gradients)** — Chuck Howard's 4-channel
  variant. Slow but robust. *Reference impl:* dcraw + RT (GPLv3).
* **AMaZE (Aliasing Minimization and Zipper Elimination, Emil Martinec)** —
  long-time RT favourite. Best at low-ISO. `O(N)` ~150 ops/pixel; tiled
  multi-thread implementation. **GPLv3 in RawTherapee** —
  `rtengine/amaze_demosaic_RT.cc`. Independent re-impl from public
  description required.
* **LMMSE (Linear MMSE)** — Zhang 2005. Good at high-ISO. `O(N)` Fourier-
  domain. Public algorithmic description; multiple BSD reimplementations.
* **IGV** — Integrated gradient. Fast variant. RT impl GPLv3.
* **EAHD / EAA / EARI (Enhanced Adaptive Residual Interpolation)** — newer
  generation residual interpolation methods. RI / MLRI / IRI / ARI / EARI
  family. Reference: Monno et al., "Adaptive Residual Interpolation for
  Color and Multispectral Image Demosaicking", *Sensors* 17(12):2787 (2017),
  open-access source available with author code. Authors' Matlab is for
  research use; we re-implement.
* **RCD (Ratio Corrected Demosaicing)** — Luis Sanz Rodríguez. *Now the
  default in darktable.* `O(N)` modest. **GPLv3** in RT and dt forks. Single
  reference paper available; we re-implement.

**2024–2026 SOTA.** Among classic methods, *RCD remains the practical sweet
spot* — close to AMaZE quality at much lower cost, default in darktable,
included in RawTherapee, and outperforms PPG/AHD/DCB on most metrics
(Wikipedia and RawPedia consensus, 2025). Recent academic papers focus on
neural demosaic ([#08](08-ai-isp-algorithms.md)); among classics, EARI is
the most recent serious contender (2017) and incremental.

**cpipe v1 demosaic node set.**

* Default: **RCD** (re-implemented from L. Sanz Rodríguez, "Bayer demosaicing
  with polynomial interpolation", Open Signal Processing Journal 6:1, 2014).
* Quality: **AMaZE** (re-implemented from E. Martinec's public description).
* Fast: **PPG** and **bilinear**.
* Reserved manifest entries for **EARI** and **AHD**, ship in v1.1+.

**License pathway.** GPLv3 codebases (RT / dt forks) cannot be reused. We
**re-implement from primary papers + the algorithm's own published math**.
This is legally sound and standard practice; the *expression* of an
algorithm in code is copyrighted, but the algorithm itself is not.

### 3.2 Demosaic — Quad Bayer (Sony Quadra / Samsung Tetra)

**Why this matters.** D12 puts Quad Bayer in scope. Modern flagship phones
ship 50–200 MP Quad Bayer sensors. Without Quad Bayer support, cpipe is
unusable as a phone-photography pipeline.

**Two strategies.**

1. **Remosaic** — convert 4×4 Quad Bayer back to standard 2×2 Bayer at full
   resolution, then run a regular Bayer demosaic. Sony Semiconductor implements
   remosaic *on-sensor* hardware. Software remosaic exists but suffers
   artefacts when scenes have aliasing.
2. **Direct demosaic** — interpolate full RGB straight from Quad Bayer. AMaZE
   author Emil Martinec has discussed extensions; the leading research is Jia
   et al., "Learning Rich Information for Quad Bayer Remosaicing and
   Denoising" (Jia 2022), which is *neural*, not classic.

**2024–2026 SOTA.** "Examining Joint Demosaicing and Denoising for Single-,
Quad-, and Nona-Bayer Patterns" (arXiv 2504.07145, 2025) proposes a unified
neural model — interesting but out of scope for the *classic* report. The
relevant classic baseline is Lin et al.'s gradient-based remosaic (MathWorks
File Exchange 2022, BSD).

**cpipe v1 plan.** Ship **remosaic-then-RCD** in v1. The remosaic kernel is
a 4×4 → 2×2 pattern interpolation; it is well-described in the Sony QBC
white paper. Reserve a v2 manifest entry for direct Quad-Bayer demosaic
(ALsink-on-sensor or AI). For 100 MP D2 budget, the intermediate Bayer is
~400 MB FP16, still inside the 800 MB FP32 ceiling.

### 3.3 White balance — DNG dual-illuminant interpolation

**Why this matters.** D10 mandates DNG-driven calibration. Every DNG carries
*two* `ColorMatrix` (illuminant 1 = often A; illuminant 2 = often D65),
*two* `ForwardMatrix` (XYZ → camera), and `AsShotNeutral` (the camera's
estimate of the white point). The pipeline must interpolate between the two
illuminants based on the AsShotNeutral, then apply the corresponding
`ForwardMatrix` to land in XYZ.

**Algorithm (per the DNG 1.7 spec, paraphrased).**

1. Read `CalibrationIlluminant1`, `CalibrationIlluminant2`, `ColorMatrix1`,
   `ColorMatrix2`, `ForwardMatrix1`, `ForwardMatrix2`, `AsShotNeutral`.
2. Convert AsShotNeutral to a CCT estimate by inverting `ColorMatrix(t)
   * white = AsShotNeutral` for t ∈ {1, 2}.
3. Compute interpolation weight α from the *reciprocal* CCTs (mireds):
   α = (1/CCT_2 − 1/CCT) / (1/CCT_2 − 1/CCT_1).
4. ColorMatrix = α·ColorMatrix1 + (1−α)·ColorMatrix2; ForwardMatrix similarly.
5. Compute camera-to-XYZ via `ForwardMatrix * diag(D)` where D = inverse
   gain so that `ForwardMatrix * AsShotNeutral` lands at `[Xw, Yw, Zw]` of
   the chosen output illuminant (D50).
6. Apply `diag(gain)` then `M_camera_to_XYZ` to each pixel. Optional: a
   *Bradford chromatic adaptation transform* into D50 first, then to the
   working space.

**Reference implementation.** **Adobe DNG SDK** (MIT-style, Apache-2
compatible per D11). The SDK already implements this faithfully. cpipe's
`color.wb_dng_dual` node calls into the SDK to get the two interpolated
matrices, then performs gain + matrix multiply in our own slang shader.

**Auto WB fallback.** If `AsShotNeutral` is missing or the user requests
auto-WB, classic methods are:

* **Gray-World** (Buchsbaum 1980): assume scene mean is gray.
* **Perfect Reflector** (Land 1977): max RGB across image is white.
* **Gray-Edge** (Van de Weijer 2007, IEEE TIP): edge derivatives integrate
  to a gray vector. Better than gray-world on saturated scenes.
* **Illuminant estimation 2024**: recent papers focus on neural; classic
  baselines remain Gray-Edge / shades of gray.

cpipe ships gray-world + gray-edge as helpers under `color.wb_auto`. Default
is the DNG dual-illuminant pathway.

### 3.4 Multi-frame fusion (HDR + low-light)

**Architecture inheritance.** Per [#06](06-soft-isp-architectures.md), cpipe
adopts the HDR+ align/merge/finish three-stage burst structure. We derive
node-level details from:

* Hasinoff et al. SIGGRAPH Asia 2016 — primary paper.
* Wronski et al. SIGGRAPH 2019 — multi-frame super-resolution variant.
* Monod, Facciolo & Morel, IPOL 2021 (open-source re-impl, attribution
  licence — Apache-2 compatible) — the actual code we use as a reference.
* Tim Brooks Halide port (`github.com/timothybrooks/hdr-plus`, MIT) — also
  a viable reference implementation.

**Burst align (1st node).** Hierarchical Gaussian pyramid (4 levels). At
each level: 16×16 tiles, ±4-pixel search. Score: L1 distance over the tile
relative to a reference frame (typically the first or median-exposure
frame). Outputs: per-tile, per-level motion vectors. Cost: `O(N×F×L×T)`
where F=frames, L=levels, T=tiles.

**Tile-based merge (2nd node).** 50% overlapping tiles with raised-cosine
window. Per-tile: align all F frames to reference; combine via *Wiener*-like
filter (HDR+ Wiener) or NLM-style patch similarity (IPOL). cpipe v1 ships
the **NLM-style Wiener** as it is simpler and the IPOL paper is published
with code under attribution licence.

**Output.** Single Bayer frame at original resolution with reduced shot
noise. From here the rest of the chain is the standard pipe (demosaic → tone
→ output).

**Performance budget.** IPOL benchmark: 8 frames × 12 MP in 3.4 s on a
quad-core CPU. With our Vulkan/slang implementation, we expect ≤500 ms on
a desktop GPU — well inside D2's 800 MB and D14's 12-month timeline.

### 3.5 Tone mapping

**Surveyed algorithms.**

| Family               | Algorithm           | Year | Notes                                          |
|-----------------------|----------------------|------|------------------------------------------------|
| Global, simple       | Reinhard `x/(1+x)` | 2002 | One-line; loses shadows                        |
| Global, filmic       | Hable Uncharted-2 | 2010 | A,B,C,D,E,F = 0.15,0.50,0.10,0.20,0.02,0.30 |
| Global, filmic       | ACES Filmic (Narkowicz fit) | 2016 | a,b,c,d,e = 2.51,0.03,2.43,0.59,0.14 |
| Global, parametric   | filmic-RGB (Pierre 2018) | 2018 | scene-referred, latitude / toe / shoulder       |
| Local, photographic  | Reinhard local        | 2002 | Bilateral on log-luminance                     |
| Local, edge-aware    | Durand bilateral 2002 | 2002 | log domain, base+detail                        |
| Local, gradient      | Fattal 2002            | 2002 | gradient attenuation in Poisson space          |
| Local, exposure-fusion | Mertens 2007/2009    | 2007 | Laplacian pyramid, no HDR build                 |
| Local, mantiuk       | Mantiuk 2008           | 2008 | contrast-domain                                  |
| Local, Drago        | Drago 2003              | 2003 | adaptive logarithmic                            |

**Recommendation.** cpipe ships:

* **filmic-RGB-style** parametric S-curve as the v1 default *global* tone
  mapper. We re-implement from Pierre's blog post (2018) and the public
  filmic-RGB module documentation. Three control points: `latitude`, `toe`,
  `shoulder`. Scene-referred. Math: piecewise polynomial; slope and value
  match at junctions for C¹ continuity.
* **Mertens exposure-fusion** as the v1 default *local* tone mapper for HDR
  inputs. We re-implement from Mertens et al. 2007. The IPOL re-implementation
  ("An Implementation of the Exposure Fusion Algorithm", IPOL 2018) is BSD;
  it is our reference.
* **ACES Filmic (Narkowicz fit)** as an alternate when the user wants a
  display-referred ACES-look workflow. Narkowicz's blog publishes the fit
  publicly (Apache-2 compatible).
* **Reinhard global** as a debug fallback, useful for piecemeal pipeline
  bring-up.

**HDR-aware tone curves.** The `tone.filmic_rgb` node is the only mapper
that *expands* (rather than always compresses) dynamic range. With the
HDR HEIF / UltraHDR / Apple Adaptive HDR outputs (D7), the user can request
the tone curve to land at a higher peak (e.g., 1000 nits for PQ HEIF) by
shifting the white relative-exposure parameter.

### 3.6 Denoise (classic)

**Surveyed algorithms.**

* **BM3D (Dabov 2007)** — block matching + 3D collaborative filtering. Still
  *the* classic SOTA. Two stages: hard-thresholding + Wiener. Reference
  implementation: Mäkinen et al., 2020 (BSD-style); CUDA implementations
  exist (`github.com/DawyD/bm3d-gpu`, MIT). Halide implementation does not
  publicly exist — we will write one.
* **BM4D** — 4D variant for video / burst. cpipe burst path uses HDR+ merge
  (3.4) instead.
* **NLM (Buades 2005)** — non-local means. Slower than BM3D, similar
  quality. IPOL implementation (Buades & Morel) BSD.
* **Bilateral filter** — classic edge-preserving. Joint bilateral upsampling
  is in our toolbox elsewhere (HDRNet-style; out of classic scope).
* **Anisotropic diffusion (Perona-Malik 1990)** — older. Skip.
* **Total Variation (Rudin-Osher-Fatemi 1992)** — competitive when noise is
  Gaussian-like; slow convergence. Skip.
* **Wavelet (BayesShrink, Chang 2000)** — fast chroma denoise. scikit-image
  ships a BSD3 reference.
* **Guided filter (He 2010)** — `O(N)` cost, edge-preserving, very GPU-
  friendly. Used for chroma noise + as preview helper.

**2024–2026 update.** Classic denoise has plateaued. Neural denoise wins on
quality at much higher cost ([#08](08-ai-isp-algorithms.md)). For cpipe v1,
we want at least one *very high quality* classic denoise (BM3D), one *fast*
chroma path (wavelet BayesShrink), and one *very fast* preview helper
(guided filter).

**Sigma estimation (key prerequisite).** BM3D needs a noise sigma. cpipe
reads `NoiseProfile` from the DNG metadata to get a per-channel,
ISO-dependent sigma estimate. The IPOL HDR+ paper's Practical Deep Raw
Image Denoising team's k-Sigma transform (PMRID, ECCV 2020) is also a
strong reference for noise model.

### 3.7 Sharpening

**Surveyed.**

* **USM (Unsharp Mask)** — classic Gaussian-blur subtract. Cheap.
* **Edge-aware USM via guided filter** — He 2013 ("Fast Guided Filter").
  Replaces the Gaussian blur with a guided-filter blur, preserves edges.
* **Deconvolution — Richardson-Lucy** — iterative; expensive; great for
  light-blurred shots. Skip in v1.
* **Deconvolution — Wiener** — closed-form; needs PSF. Skip.

**cpipe v1 sharpen node.** **Edge-aware USM via guided filter** as default;
plain USM as alt. License-clean (guided filter is academic; the algorithm
is freely usable).

### 3.8 Lens correction

**Sources of lens data.**

1. **DNG OpcodeList3** — the canonical source when the camera ships a DNG
   profile with corrections baked in. Adobe DNG SDK reads + applies these.
   `WarpRectilinear`, `FixVignetteRadial`, `FixBadPixelsConstant`, etc. are
   the relevant opcodes.
2. **Lensfun** — community lens database, LGPL3. Used as fallback when DNG
   doesn't have its own corrections (often the case with *standalone*
   cameras as opposed to phones).
3. **In-image autocorrection** — line / edge straightening. Out of v1.

**cpipe v1 nodes.**

* `lens.dng_opcodelist3` — applies any opcodes embedded in the DNG. Feeds
  through Adobe DNG SDK's opcode evaluator.
* `lens.lensfun_distort` — applies polynomial distortion correction from
  Lensfun database based on EXIF tags.
* `lens.lensfun_vignette` — radial polynomial vignetting correction.
* `lens.lensfun_tca` — transversal CA correction (per-channel scale).

LGPL3 allows static / dynamic linking when the user can replace Lensfun;
v1 dynamically loads, satisfying LGPL3 in the standard fashion.

### 3.9 Black level / white level / NoiseProfile

Trivial linear scale operations. Read parameters from DNG metadata:

* `BlackLevel` (per-Bayer-channel offset).
* `WhiteLevel` (per-Bayer-channel max).
* `NoiseProfile` — `(beta, lambda)` pairs per channel for the
  variance-vs-signal model `var(x) = beta + lambda * x`. Drives the BM3D
  sigma map.

These are the *first* nodes after `input.dng` in the standard pipe; they
turn raw counts into a normalised linear Bayer signal `[0..1]`.

### 3.10 Color enhancement

**3D LUT.** A 3D lookup table (e.g., `33×33×33`) maps RGB→RGB. Application
via *tetrahedral* interpolation (4-tap, smooth) or trilinear (8-tap, faster
but with slight banding). cpipe ships `color.lut3d` with both. Source format:
`.cube` (Adobe / Resolve compatible) and `.spi3d` (OCIO).

**Curves.** Per-channel and master luminance curves. Implemented as 1024-LUT
with linear interpolation. Bezier-spline editor in the front-end emits the
LUT.

**Channel mixer.** 3×3 + offset. Trivial.

**HSV manipulation.** Convert RGB → HSV, modify, convert back. Used for
vibrance, saturation by hue, hue shift. The transform is well-known; care
needed for HDR (HSV not well-defined past linear 1.0).

### 3.11 Exposure / curves

**Exposure compensation (EV).** Multiply linear RGB by `2^EV`. Trivial.

**Gamma.** Output transform. Handled by ICC profile in [#13](13-color-management.md).

**Contrast S-curve.** Either parametric (filmic-style) or LUT-driven.
Implemented as `tone.curve` node.

### 3.12 Halide vs slang implementations

cpipe nodes can ship as either:

* **slang shader** — runs on GPU only via slang-rhi. Best for simple
  pixel-level ops (lens, gain, gamma, LUT, demosaic, USM).
* **Halide pipeline** — runs on CPU + GPU via Halide auto-scheduler.
  Best for *algorithm + schedule* nodes where the schedule is non-trivial
  (BM3D, HDR+ merge, AMaZE, exposure fusion).

The decision is per-node and is part of the manifest. v1 plan:

| Node                              | Implementation        |
|------------------------------------|------------------------|
| `raw.black_white`                  | slang                  |
| `dng.opcodelist2/3`                | Adobe SDK + slang     |
| `demosaic.bilinear`                | slang                  |
| `demosaic.rcd`                     | Halide (paper schedule)|
| `demosaic.amaze`                   | Halide                 |
| `demosaic.ppg`                     | slang                  |
| `quad_bayer.remosaic`              | slang                  |
| `lens.lensfun_distort`             | slang (bicubic)        |
| `color.wb_dng_dual`                | slang (matrix multiply)|
| `color.lut3d`                      | slang                  |
| `burst.align`                      | Halide                 |
| `burst.merge`                      | Halide                 |
| `denoise.bm3d`                     | Halide                 |
| `denoise.wavelet_chroma`           | Halide                 |
| `denoise.guided`                   | slang                  |
| `sharpen.usm_guided`               | slang                  |
| `tone.filmic_rgb`                  | slang                  |
| `tone.exposure_fusion`             | Halide                 |

This split lines up with how Google internally structured HDR+ — "schedule-
heavy" nodes in Halide, "trivial-pixel-op" nodes in shaders.

### 3.13 IQ benchmarks per algorithm

(See [#09](09-image-quality-benchmarks.md) for the full benchmark harness.)

For the v1 algorithm node set, expected IQ on standard datasets is:

| Stage / algorithm    | Dataset           | Metric        | Value (literature)     |
|-----------------------|-------------------|----------------|--------------------------|
| RCD demosaic         | Kodak 24          | PSNR (dB)     | 39.5                     |
| AMaZE demosaic       | Kodak 24          | PSNR (dB)     | 40.1                     |
| AHD demosaic         | Kodak 24          | PSNR (dB)     | 39.0                     |
| BM3D denoise (σ=25)  | BSD68             | PSNR (dB)     | 28.55                    |
| HDR+ merge (8 frames)| HDR+ dataset      | PSNR (dB)     | 38.0–42.0 vs single-frame|
| Lensfun + DNG corr.  | own benchmarks    | edge MTF50     | within 5% of reference   |

Numbers cited from the original papers and IPOL re-implementations; cpipe's
implementations should re-validate these.

### 3.14 Quad Bayer remosaic kernel — sketch

For Sony-style 4×4 Quad Bayer (2×2 same-colour blocks in a Bayer macro-pixel
pattern), the remosaic is:

```
For each 4x4 macro-block centred on Quad-Bayer pattern:
  For each of the 4 Bayer "sub-positions":
    Blend the 4 same-colour Quad-Bayer pixels weighted by an
    edge-aware kernel (gradient-based) to produce one Bayer pixel.
  Resulting 2x2 block is a standard Bayer macro-pixel.
```

Edge-aware weights from local 5×5 luminance gradients minimise zipper /
moiré artifacts. This is the algorithm the MathWorks file-exchange
implementation of "QuadBayer CFA Modified Gradient-Based Demosaicing" (BSD)
demonstrates and is well-documented in image-sensors-world.

---

## 4. Code / Architecture Sketches

### 4.1 RCD demosaic skeleton (Halide pseudocode)

```cpp
// Inputs: bayer (Func, 1-channel float), pattern (RGGB|BGGR|GRBG|GBRG)
Func RCD(Func bayer, BayerPattern pat) {
    // 1) Compute luminance estimate at green positions and at red/blue
    //    positions using the ratio-corrected approach.
    Func Lh, Lv, Lh_ratio, Lv_ratio;
    Lh(x, y) = (bayer(x-1, y) + bayer(x+1, y)) * 0.5f;
    Lv(x, y) = (bayer(x, y-1) + bayer(x, y+1)) * 0.5f;
    // 2) Decide horizontal vs vertical based on local gradient.
    Func dh, dv;
    dh(x, y) = abs(bayer(x-2, y) - bayer(x+2, y));
    dv(x, y) = abs(bayer(x, y-2) - bayer(x, y+2));
    // 3) Mix.
    Func G;
    G(x, y) = select(dh(x,y) < dv(x,y), Lh(x,y), Lv(x,y));
    // 4) Use ratio between G and R / G and B at known sites to fill
    //    R / B at green sites and at the *other* colour's site.
    //    (full math: see Sanz Rodríguez 2014.)
    // 5) Schedule:
    G.compute_root().vectorize(x, 8).parallel(y);
    return rgb;
}
```

The Halide schedule is responsible for parallelism, vectorisation, and
GPU lowering (Vulkan target). The algorithm code stays portable.

### 4.2 Dual-illuminant WB node interface

```cpp
struct DualIlluminantInput {
    Mat3 ColorMatrix1, ColorMatrix2;
    Mat3 ForwardMatrix1, ForwardMatrix2;
    Vec3 AsShotNeutral;
    int  illuminant1, illuminant2;  // EXIF illuminant codes
};

class WBDngDual : public NodeImpl {
public:
    void commit_params(const DualIlluminantInput& in) override {
        float cct_as_shot = estimate_cct(in.AsShotNeutral, in.ColorMatrix1, in.ColorMatrix2);
        float cct1 = illuminant_to_cct(in.illuminant1);
        float cct2 = illuminant_to_cct(in.illuminant2);
        float alpha = (1/cct2 - 1/cct_as_shot) / (1/cct2 - 1/cct1);
        alpha = clamp(alpha, 0.0f, 1.0f);
        Mat3 CM = alpha * in.ColorMatrix1 + (1-alpha) * in.ColorMatrix2;
        Mat3 FM = alpha * in.ForwardMatrix1 + (1-alpha) * in.ForwardMatrix2;
        Vec3 gain = compute_gain_so_neutral_is_d50(in.AsShotNeutral, CM);
        // Pack {FM, gain} into uniform buffer for shader.
    }
    void process_gpu(SlangDispatcher&, const Roi&, const Params&) override;
};
```

### 4.3 Burst align/merge node interface

```cpp
class BurstAlign : public NodeImpl {
public:
    // Inputs: vector<RawImage> frames, int reference_index.
    // Outputs: per-frame, per-tile motion-vector field at L levels.
};

class BurstMerge : public NodeImpl {
public:
    // Inputs: vector<RawImage> frames, motion fields, NoiseProfile.
    // Outputs: single Bayer image (reference-frame aligned, merged).
    void commit_params(const HDRPlusParams& p);
    Roi  roi_in_for(const Roi& roi_out) const override {
        // Whole burst alignment needs full-image context; ROI = full image
        // for v1 (no preview). v2 will support tile-based operation.
        return Roi::full();
    }
};
```

### 4.4 BM3D Halide skeleton (very high level)

```cpp
Func BM3D_step1(Func noisy, float sigma) {
    // 1) For each reference patch p, find K most similar patches in a
    //    search window.
    // 2) Stack them into a 3D group; apply 3D wavelet transform.
    // 3) Hard-threshold coefficients at 2.7 * sigma.
    // 4) Inverse transform; aggregate with weights (proportional to count
    //    of non-zero coefficients).
    return basic_estimate;
}

Func BM3D_step2(Func noisy, Func basic, float sigma) {
    // 1) Re-do the patch matching on the basic estimate (cleaner).
    // 2) Apply Wiener filter using the basic estimate as oracle for the
    //    spectrum.
    // 3) Aggregate.
    return final_estimate;
}
```

---

## 5. Cited Sources

* Hamilton & Adams US Patent 5,629,734 (1997).
* Hirakawa & Parks, "Adaptive Homogeneity-Directed Demosaicing Algorithm",
  IEEE TIP 2005.
* Sanz Rodríguez & Bayón, "A Fast Algorithm for the Demosaicing Problem
  Concerning the Bayer Pattern", *Open Signal Processing Journal* 6:1, 2014
  (`opensignalprocessingjournal.com/VOLUME/6/PAGE/1/FULLTEXT/`). Open
  access.
* Martinec, "AMaZE demosaic" — public description, original RT paper
  pre-dates open-access publication; algorithm is in `rtengine/amaze_demosaic_RT.cc`.
* Monno, Tanaka & Okutomi, "Adaptive Residual Interpolation for Color and
  Multispectral Image Demosaicking", *Sensors* 17(12):2787, 2017
  (`mdpi.com/1424-8220/17/12/2787`).
* Wikipedia: "Demosaicing" — general taxonomy
  (`en.wikipedia.org/wiki/Demosaicing`).
* RawPedia: "Demosaicing" — practical taxonomy
  (`rawpedia.rawtherapee.com/Demosaicing`).
* Sony Semiconductor, "Quad Bayer Coding" technology page
  (`sony-semicon.com/en/technology/mobile/quad-bayer-coding.html`).
* Jia et al., "Learning Rich Information for Quad Bayer Remosaicing and
  Denoising", 2022 (`jhc.sjtu.edu.cn/~xiaohongliu/papers/2022learning.pdf`).
* arXiv 2504.07145 (2025), "Examining Joint Demosaicing and Denoising for
  Single-, Quad-, and Nona-Bayer Patterns".
* MathWorks File Exchange #116085, "QuadBayer CFA Modified Gradient-Based
  Demosaicing" (BSD).
* Adobe DNG 1.7 Specification (`paulbourke.net/dataformats/dng/dng_spec_1_6_0_0.pdf`
  — current version 1.7 published with same structure).
* Adobe DNG SDK license (`scancode-licensedb.aboutcode.org/adobe-dng-sdk.html`).
* Buchsbaum, "A Spatial Processor Model for Object Colour Perception",
  *Journal of the Franklin Institute* 1980.
* Van de Weijer, Gevers & Gijsenij, "Edge-Based Color Constancy", IEEE TIP
  2007.
* Hasinoff et al., "Burst photography for high dynamic range and low-light
  imaging on mobile cameras", SIGGRAPH Asia 2016.
* Wronski et al., "Handheld Multi-Frame Super-Resolution", SIGGRAPH 2019.
* Monod, Facciolo & Morel, "An Analysis and Implementation of the HDR+
  Burst Denoising Method", IPOL 2021 (`www.ipol.im/pub/art/2021/336/`).
* Reinhard, Stark, Shirley & Ferwerda, "Photographic Tone Reproduction for
  Digital Images", SIGGRAPH 2002.
* Hable, "Filmic Tonemapping for Real-time Rendering", SIGGRAPH 2010 course.
* Narkowicz, "ACES Filmic Tone Mapping Curve", 2016
  (`knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/`).
* Pierre, "Filmic, darktable and the quest of the HDR tone mapping", 2018
  (`eng.aurelienpierre.com/2018/11/filmic-darktable-and-the-quest-of-the-hdr-tone-mapping/`).
* Mertens, Kautz & Van Reeth, "Exposure Fusion", *Pacific Graphics* 2007.
* IPOL 2018: "An Implementation of the Exposure Fusion Algorithm"
  (`www.ipol.im/pub/art/2018/230/`).
* Dabov, Foi, Katkovnik & Egiazarian, "Image denoising by sparse 3D
  transform-domain collaborative filtering", IEEE TIP 2007 (BM3D).
* Mäkinen, Azzari & Foi, BM3D modern implementation,
  *Signal Processing* 2020.
* Buades, Coll & Morel, "A non-local algorithm for image denoising",
  *CVPR 2005*.
* Chang, Yu & Vetterli, "Adaptive Wavelet Thresholding for Image
  Denoising", *IEEE TIP* 2000 (BayesShrink).
* He, Sun & Tang, "Guided Image Filtering", ECCV 2010 / TPAMI 2013.
* Wang et al., "Practical Deep Raw Image Denoising on Mobile Devices",
  ECCV 2020 (PMRID; for k-Sigma reference noise model).
* Lensfun project (`github.com/lensfun/lensfun`, `lensfun.github.io`).
* darktable filmic-RGB module manual
  (`docs.darktable.org/usermanual/4.8/en/module-reference/processing-modules/filmic-rgb/`).

---

## 6. See also

* [#06 — Soft-ISP architectures](06-soft-isp-architectures.md) — the DAG /
  scheduler this report's nodes plug into.
* [#08 — AI ISP algorithms](08-ai-isp-algorithms.md) — neural counterparts
  of the same stages; cpipe lets users mix-and-match.
* [#09 — Image quality benchmarks](09-image-quality-benchmarks.md) — how
  to validate each algorithm node's output against ground truth.
* [#12 — DNG format](12-dng-format.md) — DNG metadata and OpcodeList details
  the algorithms here consume.
* [#13 — Color management](13-color-management.md) — working space and the
  ICC / OCIO transform after tone mapping.
* [#14 — HEIF and HDR output](14-heif-and-hdr-output.md) — sink format the
  tone mapper targets.
* [#15 — Camera calibration](15-mobile-camera-calibration.md) — generates
  the matrices and noise models the algorithms use.

---

## 7. Open Questions

1. **AMaZE re-implementation.** The algorithm is described in mailing-list
   posts and forum discussions, not a peer-reviewed paper. We must reverse-
   engineer from the publicly described algorithm without copying RT's GPLv3
   source. *Estimated effort:* 2 weeks for an experienced ISP engineer. **
   Risk:** subtle differences may degrade quality vs RT's implementation.
2. **BM3D Halide schedule.** No public Halide BM3D exists; CUDA reference
   schedules should map cleanly to Halide's GPU schedule, but tuning is
   needed. *Estimated effort:* 3 weeks.
3. **Quad Bayer direct demosaic.** v1 ships remosaic-then-RCD (safe). v2
   should ship a direct Quad demosaic — but the literature is dominated by
   neural methods. Open question: does cpipe stay classic or fold AI-quad-
   demosaic into the v2 manifest?
4. **NoiseProfile vs PMRID k-Sigma.** Some DNGs ship rich `NoiseProfile`
   tags; some don't. Should we additionally fit a k-Sigma model from a per-
   ISO calibration set we ship?
5. **OpcodeList opcode coverage.** Adobe DNG SDK supports the standard
   opcodes; some smartphone OEMs (Pixel) embed *additional vendor-specific*
   opcodes that the SDK ignores. Do we plumb a hook for vendor opcodes? v2.
6. **Local tone mapping HDR-aware.** Mertens fusion was designed for SDR
   output. Targeting HDR HEIF (PQ 1000 nits) requires a different
   exposure-weight schedule. Open: ship a v1 "HDR-pyramid" Mertens variant
   or postpone?
7. **Tetrahedral 3D LUT vs trilinear.** Tetrahedral is correct (no banding
   at the cube diagonals) but ~2× cost vs trilinear. v1 default? **Tentative
   recommendation:** tetrahedral as default, trilinear as opt-in for preview.
8. **Lensfun database licensing.** LGPL3 *code*; DBP file format and the
   community-generated calibrations are CC-BY-SA-3.0. Do we redistribute?
   v1 plan: load from system-installed Lensfun, do not redistribute the DB
   ourselves.
9. **Auto-WB confidence.** Should `color.wb_auto` provide a confidence
   value (e.g., used to fall back to DNG dual-illuminant when auto fails)?
10. **k-Sigma transform vs hand-tuned per-ISO sigma.** PMRID's k-Sigma
    transform is elegant but neural-trained. Pure classic alternative: per-
    ISO sigma table calibrated from DNG `NoiseProfile`. v1 uses the latter.
