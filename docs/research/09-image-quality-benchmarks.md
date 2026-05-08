# Report 09 — Image Quality Benchmarks for cpipe

> **Cluster C — ISP Pipeline & Algorithms.** Methodology, library, dataset, and
> harness recommendations for *both* image-quality (IQA) and performance
> benchmarking of cpipe. Pairs with [#06 — Soft-ISP architectures](06-soft-isp-architectures.md),
> [#07 — Classic ISP algorithms](07-classic-isp-algorithms.md),
> [#08 — AI ISP algorithms](08-ai-isp-algorithms.md), and feeds CI gates that
> guard merges into `main`. License-aware per **D11** (Apache-2.0): IQA-PyTorch
> is **CC-BY-NC-SA / NTU-S-Lab**, so it cannot be linked into the shipping
> binary; **piq** (Apache-2.0) is the in-binary metric kit. IQA-PyTorch is
> used out-of-process in CI only.

---

## 1. TL;DR

cpipe needs two parallel benchmark tracks. **Track A — Image Quality**:
in-binary `cpipe-bench-iq` runs **piq** (Apache-2.0) for PSNR / SSIM /
MS-SSIM / FSIM / VIFp / GMSD / HaarPSI / VSI / LPIPS / DISTS / BRISQUE +
custom ΔE2000, MTF50, and HDR-IQ. Out-of-process **`pyiqa`** (NTU-S-Lab,
non-commercial) runs MANIQA / TOPIQ / MUSIQ / CLIP-IQA / Q-Align via
subprocess in CI only — never linked into shipping artifacts. **Track B —
Performance**: **Google Benchmark** for per-stage micro / latency,
**nanobench** for inner-loop kernel A/B tests, plus a custom end-to-end
harness measuring wall, GPU, NPU, peak RSS, and (on Android) battery via
`dumpsys batterystats`. Reference dataset = curated 50-image v1 corpus
sampled from MIT-Adobe FiveK + SIDD + DND + HDR+ Burst + RAISE + Kalantari
HDR + Quad-Bayer phone shots — DNG inputs, golden HEIF references frozen
at `v1.0.0`. CI gates: `PSNR ≥ baseline − 0.10 dB`, `LPIPS ≤ baseline +
0.005`, end-to-end latency ≤ baseline × 1.10, peak RSS ≤ baseline × 1.05.
Bridge to Python via subprocess (not pybind11) — minimal coupling.
(200 words)

---

## 2. Decision Matrix — Tools, Metrics, Datasets, Gates

### 2.1 IQA library matrix

| Library                       | License                  | Apache-2 compat? | In-binary? | CI-only? | Coverage                                                                     | v1 role                       |
|--------------------------------|--------------------------|:----------------:|:----------:|:--------:|------------------------------------------------------------------------------|--------------------------------|
| **piq** (photosynthesis-team) | Apache-2.0               | YES             | YES       | YES     | PSNR, SSIM, MS-SSIM, IW-SSIM, FSIM, VIFp, GMSD, MS-GMSD, VSI, DSS, HaarPSI, MDSI, LPIPS, PieAPP, DISTS, BRISQUE, CLIP-IQA, IS, FID, KID | **default in-process metrics** |
| **pyiqa / IQA-PyTorch**       | NTU-S-Lab + CC-BY-NC-SA  | NO              | NO        | YES     | All of the above + MANIQA, TOPIQ, MUSIQ, CLIP-IQA+, Q-Align, NRQM, HyperIQA, DBCNN, NIMA, PaQ-2-PiQ, PieAPP | **CI-only via subprocess**    |
| **scikit-image**              | BSD-3                    | YES             | YES       | YES     | PSNR, SSIM, MS-SSIM (basic)                                                   | redundant with piq             |
| **OpenCV `quality` module**   | Apache-2.0               | YES             | YES       | YES     | PSNR, SSIM, GMSD, BRISQUE, MSE                                                | useful for spot-checks         |
| **DISTS-pytorch (official)**  | MIT                      | YES             | YES       | YES     | DISTS only                                                                    | piq's DISTS is sufficient      |
| **LPIPS (official)**          | BSD-2                    | YES             | YES       | YES     | LPIPS only (with torchvision backbone weights — license check needed)         | piq's LPIPS is sufficient      |
| **ST-LPIPS**                  | BSD-2                    | YES             | YES       | YES     | shift-tolerant LPIPS for camera-shake comparison                             | adopted as separate node       |
| **Imatest** (commercial)      | proprietary              | NO              | NO        | NO      | MTF50, ΔE, dynamic range, noise — gold standard but non-free                 | not used; Apache-2 alternatives below |

**Headline decision.** **piq** is the in-binary library: Apache-2.0 makes it
distributable inside cpipe; it covers 16 of the 18 metrics we need. **pyiqa**
ships in the `cpipe-iqa-ci` Python sidecar, invoked over **subprocess**, only
during CI / nightly regressions. We avoid `pybind11` embedding because it
would couple `pyiqa`'s non-commercial license into our process address space.

### 2.2 IQA metric matrix

| Metric             | Class | Type                   | Cost              | License (cpipe path)         | v1 status                  | Used for                                  |
|---------------------|:-----:|------------------------|-------------------|------------------------------|-----------------------------|-------------------------------------------|
| PSNR                | FR    | scalar pixel-error     | trivial           | Apache-2 (piq)              | YES default                | always reported                            |
| SSIM                | FR    | structural             | cheap             | Apache-2 (piq)              | YES default                | always reported                            |
| MS-SSIM             | FR    | multiscale structural  | cheap             | Apache-2 (piq)              | YES default                | always reported                            |
| FSIM                | FR    | feature-similarity     | medium            | Apache-2 (piq)              | YES default                | structural/edge                            |
| VIFp                | FR    | info-theoretic         | medium            | Apache-2 (piq)              | YES                        | natural-image fidelity                     |
| HaarPSI             | FR    | wavelet-based          | medium            | Apache-2 (piq)              | YES                        | sharp/blur regression                      |
| GMSD / MS-GMSD      | FR    | gradient-similarity    | cheap             | Apache-2 (piq)              | YES                        | edge / texture regression                  |
| MDSI                | FR    | multi-scale gradient   | cheap             | Apache-2 (piq)              | YES                        | sanity                                     |
| VSI                 | FR    | visual saliency        | medium            | Apache-2 (piq)              | YES                        | content-aware regression                   |
| LPIPS               | FR    | learned perceptual     | medium-GPU        | BSD-2 + AlexNet weights      | YES default                | perceptual regression                      |
| ST-LPIPS            | FR    | shift-tolerant LPIPS   | medium-GPU        | BSD-2                        | YES alt                    | hand-held burst comparison                 |
| DISTS               | FR    | deep struct-tex        | medium-GPU        | MIT (piq)                    | YES                        | semantic content match                      |
| BRISQUE             | NR    | natural-statistics     | cheap             | Apache-2 (piq)              | YES default                | sanity / visual quality                    |
| NIQE                | NR    | natural-statistics     | cheap             | LICENSE (NTU-SLAB) — pyiqa   | CI-only                    | sanity                                     |
| MANIQA              | NR    | multi-attention        | medium-GPU        | non-commercial (pyiqa)       | CI-only                    | high-end NR quality                        |
| TOPIQ               | NR    | top-down semantic      | medium-GPU        | non-commercial (pyiqa)       | CI-only                    | TIP 2024 SOTA                               |
| MUSIQ               | NR    | multi-scale transformer| medium-GPU        | Apache-2 (Google) via pyiqa  | CI-only                    | UHD images                                  |
| CLIP-IQA / +        | NR    | CLIP-feature based     | medium-GPU        | OpenAI CLIP / pyiqa          | CI-only                    | semantic-aware NR                           |
| Q-Align             | NR    | LMM-based scoring      | heavy-GPU         | research (pyiqa)             | CI-only nightly            | calibration against subjective MOS         |
| HyperIQA            | NR    | hyper-network          | medium-GPU        | research (pyiqa)             | CI-only                    | mixed-distortion benchmarking              |
| PaQ-2-PiQ           | NR    | patch quality          | medium-GPU        | research (pyiqa)             | CI-only                    | UGC reference                               |
| ΔE2000 (CIE)        | FR    | color difference       | cheap             | self-implemented (Apache-2) | YES default                | Macbeth color accuracy                      |
| MTF50 (slanted edge)| FR    | resolution             | cheap             | self (ISO 12233:2024)       | YES default                | sharpness                                   |
| Dynamic Range       | FR    | DR test                | cheap             | self (DXOMARK-style)        | YES                        | HDR pipeline regression                     |
| HDR-IQ              | FR    | HDR-VDP-3              | medium            | non-commercial (HDR-VDP)     | CI-only                    | HDR HEIF regression                         |
| Temporal noise      | NR    | temporal variance      | cheap             | self                          | YES                        | burst denoise sanity                        |

### 2.3 Performance benchmark library matrix

| Library                | License        | Strengths                                                                | Weaknesses                              | v1 role                                  |
|-------------------------|----------------|--------------------------------------------------------------------------|------------------------------------------|-------------------------------------------|
| **Google Benchmark**   | Apache-2.0     | Battle-proven; rich autotuning; counters; JSON output; CMake-friendly  | Slow autotune; one-process model         | **default per-stage micro-bench**         |
| **nanobench**          | MIT            | ~80 × faster autotune than GB; single header; built-in Markdown / JSON | No CPU counters; no fixtures             | inner-loop kernel A/B + CI smoke tests    |
| **Catch2 benchmark**   | BSL-1.0        | Co-located with unit tests; very low ceremony                          | Basic; no advanced statistics             | colocated regression tests                 |
| **`hyperfine`** (CLI)  | MIT/Apache-2.0 | Process-level wall-clock; easy in shell scripts                         | Coarse; no in-process counters           | end-to-end CLI runs from CI               |
| **`perf` (Linux)**     | GPL (kernel)   | HW counters, energy, IPC                                                 | Linux-only; per-process                  | desktop-only deeper investigations         |
| **NSight / Radeon GPU Profiler** | proprietary | Vendor GPU profiling                                                | Not in CI                                  | local performance work                    |

### 2.4 Dataset matrix

| Dataset                                | Modality            | Size           | License                     | v1 use                              |
|-----------------------------------------|---------------------|----------------|-----------------------------|-------------------------------------|
| MIT-Adobe FiveK                          | 5,000 RAW (DNG-able)| 50 GB          | research, non-commercial    | calibration + tone-mapping refs     |
| SIDD                                     | smartphone RAW noisy + GT | 380 GB    | research                     | denoise regression (NAFNet, BM3D)   |
| DND (Darmstadt Noise)                    | RAW noisy + GT       | 8 GB          | research                     | denoise sanity                       |
| RAW-NOD (Real-Noise Object Detection)   | RAW + labels         | 12 GB         | research (CVPR 2024)         | low-light corner cases               |
| HDR+ Burst dataset                       | bursts of RAW + ref  | 153 GB        | CC BY-SA 4.0                 | burst align/merge regression         |
| RAISE                                    | 8,156 RAW            | 350 GB         | research                     | demosaic regression                  |
| Kalantari HDR (CVPR 2017)                | bracketed HDR + GT   | 7 GB          | research                     | HDR fusion regression                 |
| NTIRE-HDR                                | HDR challenge sets    | varies         | research                     | HDR regression                        |
| CID2013 / KonIQ-10k / SPAQ / FLIVE       | rated NR datasets    | 1–50 GB       | research                     | NR-IQA model calibration              |
| **cpipe-curated v1 (50 images)**         | DNG + HEIF refs       | ~8 GB         | mixed (sourced w/ permission)| **primary regression set**           |

### 2.5 CI gate matrix

| Metric                         | Threshold (per-image)                  | Threshold (corpus-mean)               |
|--------------------------------|----------------------------------------|----------------------------------------|
| PSNR (sRGB)                    | ≥ baseline − 0.20 dB                   | ≥ baseline − 0.10 dB                  |
| SSIM                           | ≥ baseline − 0.005                     | ≥ baseline − 0.002                    |
| LPIPS                          | ≤ baseline + 0.010                     | ≤ baseline + 0.005                    |
| BRISQUE                        | informational                           | informational                           |
| ΔE2000 (Macbeth)               | mean ≤ baseline + 0.5                  | max patch ≤ 5.0                        |
| MTF50 (center)                 | ≥ 0.95 × baseline                      | ≥ 0.97 × baseline                      |
| Dynamic Range                  | ≥ baseline − 0.10 EV                   | ≥ baseline − 0.05 EV                  |
| End-to-end latency (per stage) | ≤ baseline × 1.15                      | ≤ baseline × 1.10                      |
| Peak RSS                       | ≤ baseline × 1.10                      | ≤ baseline × 1.05                      |
| GPU peak memory (NSight)       | ≤ D2 ceiling 800 MB                    | hard fail                               |

---

## 3. Detailed Findings

### 3.1 Image-quality assessment landscape (2024–2026)

IQA metrics divide cleanly into **full-reference (FR)** — comparing a test
image against a known ground truth, useful when our pipeline output is being
compared to a frozen reference — and **no-reference (NR)** — judging an
image in isolation, useful when the reference is unattainable (for example,
a real-world burst capture with no clean target).

The cpipe regression suite needs both: FR for pixel-equivalence regressions
on synthetic/curated inputs where we *do* have a frozen reference; NR for
trend-tracking on real-world bursts / phone captures where ground truth is
not available, and as a sanity check that the FR-frozen reference itself is
not going stale visually.

**Full-reference metric families.**

* **Pixel-error.** PSNR, MAE, MSE — classical, fast, deeply familiar to
  reviewers, but poorly correlated with subjective quality (a 1-pixel shift
  destroys PSNR while leaving the image perceptually identical). Always
  reported, but never the *only* gate.
* **Structural.** SSIM (Wang 2004) and MS-SSIM (Wang 2003) compare local
  luminance, contrast, and structure. MS-SSIM is the default in most
  ISP regression suites and tracks subjective scores moderately well.
  Variants in piq: SSIM, MS-SSIM, IW-SSIM (information-content-weighted),
  3-SSIM (with edge weighting). For HDR, a special *PU21* encoding is
  applied first (Mantiuk 2021) to make SSIM perceptually uniform across
  HDR luminance ranges.
* **Feature similarity.** FSIM (Zhang 2011) operates on phase-congruency
  and gradient features and outperforms SSIM on TID2013 by ~6%. VSI
  (Zhang 2014) adds visual saliency weighting. HaarPSI (Reisenhofer 2018)
  is wavelet-domain — sensitive to edge-and-texture distortions, used to
  sanity-check sharpening regressions.
* **Gradient similarity.** GMSD / MS-GMSD (Xue 2014) — fast, edge-sensitive,
  excellent for detecting subtle blurring or sharpening regressions. cpipe
  reports MS-GMSD on every release as a *direct* sharpening-regression
  metric.
* **Information-theoretic.** VIFp (Sheikh 2006) — natural-scene statistics
  pixel-domain variant; correlated well with subjective ratings on TID2013
  and LIVE.
* **Color difference.** ΔE76 (Euclidean Lab), ΔE94, ΔE2000 (CIEDE2000) —
  all defined in CIE publications, none patented. ΔE2000 is the modern
  industry default per ViewSonic's primer (2024) and DXOMARK's color
  protocol; ΔE2000 < 1.0 is generally below human perception threshold.
  cpipe ships ΔE2000 over a synthetic 24-patch Macbeth target as a
  color-pipeline regression metric.
* **Learned perceptual.** LPIPS (Zhang 2018) uses a frozen CNN backbone
  (AlexNet/VGG/SqueezeNet) and computes weighted L2 over deep features.
  Strongly correlated with human perception on the BAPPS dataset. ST-LPIPS
  (Ghildyal 2022) adds shift tolerance — important for hand-held bursts
  where slight pixel shifts are perceptually irrelevant. **cpipe uses LPIPS
  as the primary perceptual gate**: easier to interpret than SSIM, less
  brittle than PSNR.
* **Deep structural-textural.** DISTS (Ding 2020) and PieAPP (Prashnani
  2018) operate on deep features but with a different pooling. DISTS is
  resilient to texture-mismatch (good for film-simulation regressions
  where the texture should change but the *content* should not).

**No-reference metric families.**

* **Natural-statistics.** NIQE (Mittal 2013) and BRISQUE (Mittal 2012)
  fit a multivariate Gaussian to natural-image features and score
  deviations. Both are pre-trained on a small (~125-image) clean corpus
  and need no DB. They run cheaply on CPU and are good as sanity gates.
* **Learning-based, CNN.** DBCNN (Zhang 2018), CNNIQA (Kang 2014),
  HyperIQA (Su 2020), MANIQA (Yang CVPRW 2022), PaQ-2-PiQ (Ying CVPR 2020)
  are CNN/transformer regressors trained on large rated datasets
  (KonIQ-10k, SPAQ, FLIVE, KADID-10K). MANIQA won the NTIRE 2022 No-Ref
  IQA challenge. Outputs a quality score ∈ [0..1] correlating strongly
  with subjective MOS.
* **Multi-scale transformer.** MUSIQ (Ke ICCV 2021) handles UHD/4K
  natively without resizing — important for cpipe's 100 MP D2 inputs.
* **CLIP-based.** CLIP-IQA (Wang AAAI 2023), CLIP-IQA+ — operate on
  pretrained CLIP features. Good cross-dataset generalisation; inexpensive
  inference.
* **Top-down semantic.** TOPIQ (Chen IEEE TIP 2024) brings high-level
  semantics into IQA via a coarse-to-fine network. Best 2024 NR metric on
  multiple datasets per the IQA-PyTorch and Awesome-IQA leaderboards.
* **LMM-based.** Q-Align (Wu ICML 2024) uses a large multimodal model
  (LLaVA-derived) to predict discrete rating levels (poor / fair / good /
  excellent / perfect) and convert to a score. State-of-the-art on
  multiple datasets in 2024 but very expensive (multi-second per image on
  consumer GPU). cpipe runs Q-Align *only* on weekly nightlies, not per
  PR.

### 3.2 Library deep-dive — piq (Apache-2.0, in-binary)

**Repo.** `github.com/photosynthesis-team/piq` (v0.8.0, 2023 — the most
recent stable; minor updates through 2024–2026). **License.** Apache-2.0
— per D11 this is **directly linkable** into cpipe.

**Coverage.** PSNR, SSIM, MS-SSIM, IW-SSIM, VIFp, FSIM, SR-SIM, GMSD,
MS-GMSD, VSI, DSS, HaarPSI, MDSI, LPIPS, PieAPP, DISTS, TV (total
variation), BRISQUE, CLIP-IQA, IS, FID, GS, KID, MSID, PR. A
no-reference metric (BRISQUE) is implemented; the more recent NR metrics
(MANIQA / TOPIQ / MUSIQ / Q-Align) are not — those require IQA-PyTorch
or upstream repos.

**Usage pattern.** piq is a PyTorch library. cpipe's `cpipe-bench-iq`
binary is a small C++ wrapper that:

1. Reads a candidate HEIF (decoded back to RGB float per D17 — see
   [#14 — HEIF / HDR output](14-heif-and-hdr-output.md)) and a reference
   HEIF.
2. Spawns a Python interpreter via `python -m cpipe_iqa.fr` (a packaged
   wheel installed alongside cpipe). The wheel pins `piq==0.8.x` and
   `torch==2.5.x`.
3. Ships tensors over stdin (binary, fp32) — this avoids per-image
   subprocess startup cost amortised over the corpus run (one Python
   subprocess processes the whole corpus).
4. Reads JSON metric output back over stdout.

This is the *bridging architecture* discussed in the task brief. We
chose subprocess over **pybind11** for three reasons:

1. **License hygiene.** Subprocess keeps Python-side dependencies in their
   own address space and license boundary. If a future Python dep is
   GPLv3, it does not infect the cpipe binary.
2. **Build simplicity.** No CPython header-version pinning in our CMake;
   no torch C++ ABI matching.
3. **Failure isolation.** A torch crash kills the bench subprocess, not
   `cpipe`. CI can retry.
4. **Minor performance.** Subprocess startup is amortised (single
   process for the whole corpus); per-image IPC is a few KB/MB which is
   negligible against the metric compute (LPIPS forward = 200–500 ms).

A C++ port of the in-process metrics (PSNR / SSIM / MS-SSIM / GMSD / ΔE)
is written *in the cpipe core* (under 500 LOC, Apache-2.0 from scratch);
this lets `cpipe --self-bench` produce instant scalar metrics without any
Python at all. piq is consulted only for the deeper or learned metrics.

### 3.3 Library deep-dive — pyiqa / IQA-PyTorch (NOT in-binary)

**Repo.** `github.com/chaofengc/IQA-PyTorch` ; PyPI `pyiqa`. **License.**
Per the PyPI page (`pypi.org/project/pyiqa/`, 2026), the package uses NTU
S-Lab License plus CC BY-NC-SA 4.0 — explicitly *non-commercial*. Per D11
this is a **hard block** on linking it into the shipping binary, but we
are free to use it in CI for *internal* benchmarking — a tools-only,
non-redistributed use.

**Coverage that piq lacks.** MANIQA, TOPIQ, MUSIQ, CLIP-IQA, CLIP-IQA+,
Q-Align, NRQM, HYPERIQA, DBCNN, NIMA, PaQ-2-PiQ, PieAPP (also in piq),
WADIQAM, CKDN, AHIQ, AFINE, plus aesthetic-specific models (NIMA-vgg,
NIMA-mobilenet) and face quality models (Face-IQA).

**CI integration.** A separate `cpipe-iqa-ci` Python wheel installs
`pyiqa==0.1.x`. The benchmark runner invokes:

```bash
python -m cpipe_iqa.nr \
    --metric topiq_iaa \
    --input  benches/out/img-0001.heif \
    --device cuda:0 \
    --json   benches/out/img-0001.iqa.json
```

The output JSON is appended to the regression dashboard. cpipe's release
artifacts never include `pyiqa`; the CI sidecar is a separate
container image (`cpipe/iqa-ci:v1`) used only inside the test runner.

### 3.4 Color accuracy — ΔE2000 against Macbeth

**The Macbeth ColorChecker (now X-Rite ColorChecker Classic) is the
canonical 24-patch reference**: 18 colors + 6 grayscale steps. It is the
*only* color reference accepted by DXOMARK and image-engineering.de;
imatest's color tests likewise default to it.

**ΔE2000 method.**

1. Capture the chart at calibrated lighting (D65 LED nominally, with
   rated CRI ≥ 95). cpipe ships a *synthesised* DNG that simulates a
   Macbeth chart under D65 by applying the inverse of a known camera's
   forward matrix to known XYZ patch values, plus a per-patch noise
   profile drawn from `NoiseProfile`. This avoids needing physical
   calibration shots in CI.
2. Run cpipe to produce HEIF.
3. Decode HEIF to floating-point sRGB / Display-P3 / Rec.2020 (per
   project profile).
4. For each patch: convert pixel mean to CIE Lab, compare to the
   reference Lab (from the ColorChecker's official spectrophotometric
   measurements), report ΔE2000.
5. Pass / fail: corpus-mean ΔE2000 ≤ 2.0; max patch ≤ 5.0. Tighter than
   typical "consumer" displays but well within DSLR ISP norms.

**ΔE94 / ΔE76** are also reported for backwards compatibility with older
literature; ΔE2000 is the gate.

**Implementation.** ~80 lines of Apache-2 C++ code; reference values
loaded from a `colorchecker_d65.json` shipped with cpipe. The math is
public (CIE 142-2001, CIE 116-1995); no library dependency.

### 3.5 Sharpness — MTF50 / SFR via slanted edge (ISO 12233:2024)

**Method.** ISO 12233:2024 is the current standard for spatial frequency
response (SFR) measurement. The slanted-edge method uses a black/white
edge tilted ~5° to construct a super-resolution edge spread function
(ESF), differentiate to a line spread function (LSF), and Fourier-
transform to an MTF. **MTF50** = the spatial frequency at which the MTF
drops to 50% — the de-facto sharpness metric, used by DXOMARK and
Imatest.

**Apache-2-clean implementations.**

* **`mtf-edge`** — small Python tool, MIT.
* **OpenCV `quality::computeSFR`** — not yet upstream; pending PR. cpipe
  rolls its own ~150-line C++ implementation following the ISO algorithm.

**cpipe v1 plan.** The benchmark binary applies `mtf50_slanted_edge()`
to a synthesised slanted-edge DNG (one per pipeline release). Pass/fail:
center MTF50 ≥ 0.95 × baseline (a 5% sharpness regression triggers
investigation). The same routine reports MTF10, MTF30, edge profile
(ESF), and noise-power-spectrum-aware *acutance* (per ISO 12233:2024
Annex E).

### 3.6 Dynamic range / HDR-IQ

**DXOMARK DR test.** Captures a calibrated step-wedge target at fixed
illumination, varies exposure, finds the highest stop above which the
SNR per stop drops below a threshold (often SNR=1 for "engineering DR"
or SNR=10 for "perceptual DR"). cpipe replicates this with a synthetic
HDR step wedge (16 stops) injected as a synthesised RAW input.

**HDR-VDP-3.** Mantiuk et al. published HDR-VDP-3 in 2023; it is the
gold standard FR HDR-IQA metric, providing a quality value Q in
relative-luminance HDR space. **License.** Non-commercial — used CI-only.
Run via Octave / MATLAB Compiler runtime; output JSON.

**HDR-IQ pipeline-level test.** For UltraHDR / Apple Adaptive HDR / HDR
HEIF outputs (D7), the harness:

1. Runs cpipe with `--hdr=pq1000` / `--hdr=ultra-hdr` / `--hdr=adaptive`.
2. Decodes the HEIF using libheif and reads the gain map sidecar
   (UltraHDR) or the HDR primary image (PQ HEIF).
3. Reconstructs the HDR display image at 1000-nit peak.
4. Compares to a frozen-reference 1000-nit HDR EXR via PU21-encoded
   PSNR / SSIM and HDR-VDP-3 Q value.

Per-image gates: HDR-PSNR ≥ baseline − 0.5 dB, HDR-SSIM ≥ baseline −
0.005, HDR-VDP-3 Q ≥ baseline − 1.0.

### 3.7 Noise — temporal vs spatial

**Spatial noise.** For each output, compute residual against the
reference, then RMS within a uniform-color region (a Macbeth gray patch
or an ISO 15739 noise patch). Per-channel report. Also: noise spectral
density (NPS) — useful to flag over-aggressive denoising that biases
toward low frequencies.

**Temporal noise.** Capture (or synthesise) a burst of N=32 frames of a
fixed scene; run cpipe over each; compute per-pixel temporal variance
across outputs. Compare to the corresponding noise computed on the
input bursts; the ratio is the *denoising effectiveness*. Used to
regression-test that a NAFNet update or a BM3D parameter change has
not silently lost denoising effectiveness.

### 3.8 Mobile-photo specific frameworks (Lockheed, FOVEA, Apple's perceptual)

* **VCM (Visual Computing Module) by Adobe.** Internal; no public
  documentation. Not used.
* **Lockheed metrics.** Defense-imaging tradition (NIIRS — National
  Imagery Interpretability Rating Scale, published in IEEE / SPIE).
  Subjective; mostly used as a yardstick during specification, not in
  CI.
* **Google FOVEA.** Public CVPR-paper-level only; no deployable harness.
  Useful as a target for what *eventually* should drive cpipe perceptual
  testing, but not a v1 dependency.
* **Apple's Perceptual Image Difference (PID).** Mentioned in a 2023
  WWDC talk; no public SDK. Out of scope.

cpipe v1 therefore uses LPIPS + DISTS + MUSIQ + CLIP-IQA as our
*public-model* perceptual battery, and frozen ΔE / MTF / DR / HDR-VDP-3
as our *engineering* battery.

### 3.9 Performance benchmarking — Google Benchmark vs nanobench

**Google Benchmark (GB).** `github.com/google/benchmark`. Apache-2.0.
Industry standard. Provides `BENCHMARK()` macros, `BENCHMARK_TEMPLATE`,
fixtures, complexity analysis, JSON / CSV output, and integrates cleanly
into CTest. *Weakness*: autotuning loop is slow — a typical microbench
that needs 0.1 ms per iteration may consume 1–2 s of GB time before
settling. For CI smoke tests this is unacceptable.

**nanobench.** `github.com/martinus/nanobench`. MIT. Single-header.
Per the nanobench docs: ~80× faster autotune than Google Benchmark.
Output formats include Markdown, JSON, CSV, HTML. **`doNotOptimize()`**
and **`doNotOptimizeAway()`** built in. Lacks Google Benchmark's CPU
counters and complexity analysis.

**Catch2 benchmark.** `BENCHMARK("...")` colocates with unit tests in
the Catch2 (BSL-1.0) framework. Useful for *micro-regressions tied to
unit tests* (e.g., "the ColorMatrix multiply must run in ≤ 100 ns").
Not as feature-rich as GB.

**cpipe v1 plan.**

* **GB** for the headline `cpipe-bench` binary that runs in CI. Each
  ISP node has a microbench in `bench/<node>_bench.cpp` exercising it
  on a 12-MP synthesised input on CPU + GPU. Output JSON is consumed by
  the dashboard.
* **nanobench** for inner-loop kernel A/B comparisons — quick local
  iteration when tuning a new shader. Lower ceremony makes it
  developer-friendly.
* **Catch2 benchmark** for unit-test-adjacent regressions (e.g.,
  per-pixel color transform must stay below a constant cost).

GPU-side timing uses **`vkCmdWriteTimestamp` + `vkGetQueryPoolResults`**
(or Metal `MTLCounterSampleBuffer`), exposed via slang-rhi. NPU-side
timing wraps the QNN profiler / Core ML `MLPerformanceMetrics`.

### 3.10 Energy / battery on Android

Android 14+ exposes per-app energy via:

```bash
adb shell dumpsys batterystats --reset
# run cpipe burst over N images
adb shell dumpsys batterystats <package> > /tmp/stats.txt
adb shell dumpsys batterystats --history-create-csv > /tmp/history.csv
```

The report includes CPU foreground time (ms), GPU time (ms), wakelock
duration, and an estimated mAh per process. cpipe's CI nightly Android
job records mAh-per-100-MP-burst as a metric; regression > 10 % flags.

iOS uses **Energy Diagnostics** (Xcode Instruments — `Energy Log`); not
scriptable in headless CI as cleanly. v1 is Linux + Android, so iOS
energy is deferred (D14).

### 3.11 Thermal sustained throughput

Run a 30-image batch back-to-back on Android. Record latency-per-image.
The ratio of last-image-latency to first-image-latency reveals thermal
throttling. A target: ≤ 1.30 (Pixel 9 / S24 baseline). If a future
release pushes this above 1.50, the CI job fails. This catches subtle
thermal regressions invisible to a single-image bench.

### 3.12 Datasets — selection rationale

* **MIT-Adobe FiveK** — 5,000 RAW (CR2) photos with 5 expert retouches.
  Used for tone-mapping calibration, color-enhancement training, and
  general visual diversity. License is research-only; we use a subset
  for *internal* regression and do not redistribute.
* **SIDD** — paired noisy / clean smartphone RAW from 10 scenes × 5
  phones, ~30,000 images. Per `eecs.yorku.ca/~kamel/sidd/` (CVPR 2018,
  Abdelhamed et al.). Primary denoise regression set. Mediums (320
  pairs) used for CI; full set for nightly.
* **DND (Darmstadt Noise Dataset)** — 50 noisy / clean pairs at higher
  quality than SIDD; benchmark-only (no train split). Standard
  denoising-paper baseline.
* **HDR+ Burst dataset** (`hdrplusdata.org/dataset.html`) — 153 GB,
  bursts of 5–10 RAW + ground-truth merged. CC BY-SA 4.0 — usable in
  CI. Burst align / merge regressions.
* **RAISE** — 8,156 RAW shots in TIFF + DNG, no manipulation. Demosaic
  baseline.
* **Kalantari HDR (CVPR 2017)** — 74 bracketed HDR test scenes with
  ground-truth HDR EXRs; the canonical HDR fusion dataset.
* **NTIRE / AIM-HDR** challenge datasets — annual, varying — used for
  HDR-IQ regressions.
* **RAW-NOD** (CVPR 2024) — RAW images with object-detection labels;
  used to flag low-light corner-case regressions.
* **KonIQ-10k / SPAQ / FLIVE / TID2013 / KADID-10K** — *NR-IQA*
  reference datasets used to *calibrate* MANIQA / TOPIQ / MUSIQ
  models, not to test cpipe's outputs. We trust the published model
  weights and only verify they load cleanly.
* **cpipe-curated v1 corpus** (50 images, ~8 GB). Hand-picked from
  FiveK + SIDD + HDR+ + Kalantari + Quad-Bayer phone shots, balanced
  across: low / high ISO, daylight / tungsten / fluorescent, low /
  medium / high dynamic range, portrait / landscape / night /
  high-frequency texture / chart targets. **The reference HEIFs are
  produced by cpipe v1.0.0 on a known-good build, manually inspected
  by an ISP engineer, and frozen as `corpus/v1.0.0-refs/`**. CI compares
  every PR against this frozen set.

### 3.13 Bridging architecture: subprocess vs pybind11 vs C++ port

**Subprocess (chosen).** pros: license cleanliness, build simplicity,
crash isolation. cons: per-corpus startup ~3 s (loading torch). mitigated
by amortising over the corpus.

**Embedded Python via pybind11.** pros: lower IPC overhead;
in-process state (e.g., reuse a loaded model across images) without
serialization. cons: forces matching CPython ABI; ships Python interp
inside cpipe; couples Python license terms to cpipe address space.
**Rejected** for v1.

**C++ port of metrics.** Apache-2 ports of PSNR, SSIM, MS-SSIM, GMSD,
ΔE2000, BRISQUE are trivial (≤ 200 LOC each). LPIPS / DISTS / MANIQA /
TOPIQ are *not* trivial — they require the full inference engine plus
backbone weights. **cpipe ships in-process C++ for the trivial 6
metrics; everything else is subprocess-Python.** This is the practical
sweet spot.

### 3.14 Continuous benchmarking strategy

| Cadence            | What runs                                                                     | Gate                                   | Compute                       |
|---------------------|--------------------------------------------------------------------------------|------------------------------------------|--------------------------------|
| Per-PR (CI)        | C++ in-process metrics on the 50-image corpus + perf microbench (Google Bench) | Hard fail on threshold                 | x86_64 CI runner               |
| Per-PR (CI, Android)| Same on a Pixel 9 farm; energy + thermal sustained run                       | Soft fail (warn) initially             | ARM64 device farm              |
| Nightly             | Add pyiqa subprocess (MANIQA / TOPIQ / MUSIQ / CLIP-IQA), full SIDD + HDR+   | Soft fail; investigate next morning    | GPU runner (RTX A5000)         |
| Weekly             | Add Q-Align (very expensive); HDR-VDP-3                                       | Trends only                              | Larger GPU                     |
| Per-release         | Full ext: NTIRE-HDR; RAW-NOD; manual visual check by ISP engineer            | Manual sign-off                          | Various                        |

### 3.15 Visualization / dashboard

Output of every CI run is a JSON of `{image_id → {metric → value}}`. A
small static-site generator (the `cpipe-bench-dashboard` HTML target)
combines the latest run with the last 50 historical runs to produce:

* **Per-PR diff page**: 50 thumbnails, side-by-side current vs reference,
  per-image and per-metric deltas. Hosted on a `bench.cpipe.dev`-style
  GitHub Pages site, link posted as a CI comment on the PR.
* **Trend page**: per-metric line graph over the last 90 days; click a
  point to see the commit. Plot with Vega-Lite (per the Catch2-bench
  visualisation pattern from `joht.github.io/johtizen`).
* **Metric correlation page**: pairs PSNR vs LPIPS, MS-SSIM vs MANIQA,
  etc., to spot correlation breakdowns (a sign of perceptual /
  pixel-error divergence).

Format: `cpipe-bench-dashboard` is a static HTML / JS bundle generated
by the harness; no live server. Hosted on the same GitHub Pages site as
the React Flow editor (D1).

### 3.16 Regression detection — algorithmic detail

Each metric on each image has a **baseline** (median over the prior 30
runs of that metric on that image, ignoring outliers > 3 MAD). The CI
gate is a *paired one-sided test*: for the 50-image corpus, compute the
distribution of `metric_PR(i) − metric_baseline(i)`; if the median delta
is outside the threshold *and* a Wilcoxon signed-rank p < 0.05, fail
the build. This avoids false negatives on a single regressed image and
false positives from unrelated noise.

**Per-image gate** is independent and stricter (any single image
exceeding 0.20 dB PSNR delta fails). Both gates run; either tripping
fails the build.

**Bayesian calibration mode.** A separate "calibration" CI mode lets
the team explicitly accept a quality regression in exchange for a
performance improvement (e.g., a new fast demosaic algorithm). Operator
runs `cpipe-bench --recalibrate` on the new build, which writes a new
baseline; the recalibration commit is reviewed and merged with explicit
documentation of the trade-off.

---

## 4. Architecture / Code Sketches

### 4.1 Harness CLI

```
cpipe-bench [--config <yaml>] [--corpus <dir>] [--baseline <json>]
            [--out <dir>] [--filter <regex>] [--parallel N]
            [--metrics <comma>] [--report html|json|md]
            [--gate strict|warn|none] [--target cpu|gpu|npu|auto]
```

Examples:

```bash
# CI per-PR
cpipe-bench --config bench/v1.yaml --gate strict --report json,html

# Nightly (adds pyiqa)
cpipe-bench --config bench/nightly.yaml --gate warn --report html

# Recalibrate baseline (with-write)
cpipe-bench --config bench/v1.yaml --recalibrate --out corpus/v1.0.1-refs
```

### 4.2 YAML config schema

```yaml
# bench/v1.yaml
schema_version: 1
corpus:
  root: corpus/v1.0.0
  manifest: corpus/v1.0.0/manifest.json   # SHA256 + DNG path + ref HEIF path
  filter:
    - "*.dng"

pipeline:
  binary: build/cpipe
  args: ["--profile", "default", "--hdr", "ultra-hdr"]
  timeout_seconds: 60

metrics:
  in_process:                              # piq + cpipe C++ in-bin
    - psnr
    - ssim
    - ms_ssim
    - lpips_alex
    - dists
    - haarpsi
    - ms_gmsd
    - vifp
    - delta_e2000_macbeth
    - mtf50_slanted_edge
    - dynamic_range_dxomark
    - hdr_psnr_pu21
  subprocess:                              # pyiqa
    - manimqa
    - topiq_iaa
    - musiq
    - clipiqa
    - clipiqa_plus
    - hdr_vdp_3                            # weekly only

performance:
  microbench:
    - bench: build/cpipe-microbench
      filter: "BM_(Demosaic|Denoise|ToneMap)"
      out: bench-out/microbench.json
  end_to_end:
    - cmd: build/cpipe
      input_set: corpus/v1.0.0/perf_set
      iterations: 5
      metrics: [wall_ms, cpu_ms, gpu_ms, npu_ms, peak_rss, peak_vram]

energy:                                     # Android-only
  enabled: false                            # set true on the Android job
  device: "pixel-9-pro"
  package: "dev.cpipe.cli"

gates:
  global:
    fail_on_signrank_p_lt: 0.05
    require_n_at_least: 30
  per_metric:
    psnr:        { delta: -0.10, op: ">=" }
    ssim:        { delta: -0.002, op: ">=" }
    lpips_alex:  { delta:  0.005, op: "<=" }
    delta_e2000_macbeth: { delta: 0.5, op: "<=" }
    mtf50_slanted_edge: { ratio: 0.97, op: ">=" }
    wall_ms:     { ratio: 1.10, op: "<=" }
    peak_rss:    { ratio: 1.05, op: "<=" }
  per_image:                                # tighter, single-image
    psnr:        { delta: -0.20, op: ">=" }
    lpips_alex:  { delta:  0.010, op: "<=" }

reports:
  html_dir: bench-out/html
  json_path: bench-out/results.json
  trend_window_days: 90

runtime:
  parallel_images: 4
  device: auto                              # cpu | gpu | npu | auto
```

### 4.3 Harness skeleton (C++)

```cpp
struct BenchConfig { /* deserialised YAML */ };

class BenchRunner {
public:
    explicit BenchRunner(BenchConfig cfg) : cfg_(std::move(cfg)) {}
    Report run() {
        auto images = enumerate_corpus(cfg_.corpus);
        std::vector<ImageReport> per_image;
        per_image.reserve(images.size());

        TaskFlow tf;                                        // [#03]
        tf.parallel_for(images, [&](const auto& img) {
            auto out_path = run_cpipe(img.dng_path);
            auto m1 = compute_in_process_metrics(out_path, img.ref_heif);
            auto m2 = run_perf_bench(img.dng_path);
            per_image.emplace_back(img.id, std::move(m1), std::move(m2));
        });

        // Subprocess pyiqa (single python process, full corpus)
        if (!cfg_.subprocess_metrics.empty()) {
            auto sub = launch_pyiqa_subprocess(cfg_);
            for (auto& rep : per_image) {
                rep.subprocess = sub.fetch(rep.id);          // streaming JSON
            }
            sub.shutdown();
        }

        auto baseline = load_baseline(cfg_.baseline_path);
        auto gates    = evaluate_gates(per_image, baseline, cfg_.gates);
        write_html(cfg_.reports.html_dir, per_image, baseline);
        write_json(cfg_.reports.json_path, per_image);
        return Report{ per_image, baseline, gates };
    }
private:
    BenchConfig cfg_;
};
```

### 4.4 Python sidecar (subprocess endpoint)

```python
# cpipe_iqa/__main__.py
import sys, json, struct, torch, pyiqa

metrics = {name: pyiqa.create_metric(name).cuda()
           for name in sys.argv[1].split(",")}

def read_image_pair():
    """stream protocol: 4-byte length, then JSON request {paths, id}."""
    hdr = sys.stdin.buffer.read(4)
    if not hdr: return None
    length = struct.unpack("<I", hdr)[0]
    return json.loads(sys.stdin.buffer.read(length))

while True:
    req = read_image_pair()
    if req is None: break
    a = pyiqa.utils.imread2tensor(req["candidate"]).cuda()
    b = pyiqa.utils.imread2tensor(req["reference"]).cuda()
    out = {name: metrics[name](a, b).item() for name in metrics}
    sys.stdout.write(json.dumps({"id": req["id"], "metrics": out}) + "\n")
    sys.stdout.flush()
```

### 4.5 ΔE2000 implementation

```cpp
// CIE 142-2001. ~80 LOC; Apache-2 from scratch.
double DeltaE2000(const Lab& s, const Lab& r) {
    const double kL = 1.0, kC = 1.0, kH = 1.0;
    const double Cs = std::hypot(s.a, s.b);
    const double Cr = std::hypot(r.a, r.b);
    const double Cmean = (Cs + Cr) * 0.5;
    const double G = 0.5 * (1.0 - std::sqrt(std::pow(Cmean,7) /
                            (std::pow(Cmean,7) + std::pow(25.0,7))));
    const double a1p = (1.0 + G) * s.a;
    const double a2p = (1.0 + G) * r.a;
    // ... (full CIEDE2000 follows; see CIE 142-2001 §6).
    return dE;
}
```

### 4.6 MTF50 slanted-edge sketch

```cpp
double mtf50(const ImageF& roi) {
    auto edge = detect_slanted_edge(roi);                      // Hough or Sobel
    auto esf  = supersampled_edge_spread(roi, edge, 4);        // 4× over-sample
    auto lsf  = differentiate(esf);                              // 1D derivative
    apply_hamming(lsf);                                          // window
    auto mtf  = magnitude(fft(zero_pad(lsf, 1024)));
    normalize(mtf);
    return interpolate_freq_at(mtf, 0.5);                        // MTF50
}
```

### 4.7 Performance microbench (Google Benchmark)

```cpp
// bench/demosaic_bench.cpp
#include <benchmark/benchmark.h>
#include "cpipe/nodes/demosaic.h"

static void BM_Demosaic_RCD(benchmark::State& s) {
    cpipe::Bayer bayer = make_bayer(s.range(0), s.range(0));
    cpipe::DemosaicRCD node;
    for (auto _ : s) {
        auto out = node.process_cpu(bayer);
        benchmark::DoNotOptimize(out);
    }
    s.SetBytesProcessed(int64_t(s.iterations()) *
                        s.range(0) * s.range(0) * sizeof(uint16_t));
}
BENCHMARK(BM_Demosaic_RCD)->RangeMultiplier(2)->Range(1024, 8192);

BENCHMARK_MAIN();
```

### 4.8 Gate evaluation

```cpp
struct GateResult { bool passed; std::string reason; };

GateResult evaluate(const std::vector<double>& deltas, const Gate& g) {
    auto med  = median(deltas);
    auto p    = wilcoxon_signed_rank(deltas);
    bool pass = compare(med, g.delta, g.op) || p > g.fail_p;
    return { pass, format_reason(med, p, g) };
}
```

---

## 5. Cited Sources

* **piq** (PyTorch Image Quality), `github.com/photosynthesis-team/piq`,
  v0.8.0 (2023) — Apache-2.0. Coverage list confirmed via repo README
  (accessed 2026-05-08).
* **IQA-PyTorch / pyiqa**, `github.com/chaofengc/IQA-PyTorch`, PyPI
  `pypi.org/project/pyiqa/` (v0.1.15.post2, 2026-03-18). License: NTU
  S-Lab + CC-BY-NC-SA-4.0 (verified 2026-05-08).
* **Google Benchmark**, `github.com/google/benchmark`, Apache-2.0.
  Comparison docs at nanobench's `nanobench.ankerl.com/comparison.html`
  (accessed 2026-05-08).
* **nanobench**, `github.com/martinus/nanobench`, MIT, single-header,
  ~80 × faster autotune than GB per `src/docs/comparison.rst` (accessed
  2026-05-08).
* **Catch2 benchmarks**, `github.com/catchorg/Catch2/blob/devel/docs/benchmarks.md`,
  BSL-1.0 (accessed 2026-05-08).
* **MANIQA**, Yang et al., CVPRW 2022,
  `arxiv.org/abs/2204.08958`; repo `github.com/IIGROUP/MANIQA`. Won
  NTIRE 2022 NR-IQA challenge.
* **TOPIQ**, Chen et al., IEEE TIP 2024
  (`ieeexplore.ieee.org/document/...`); implementation in pyiqa.
* **MUSIQ**, Ke et al., ICCV 2021,
  `arxiv.org/abs/2108.05997` (Apache-2.0 reference);
  `github.com/google-research/google-research/tree/master/musiq`.
* **CLIP-IQA**, Wang et al., AAAI 2023
  (`arxiv.org/abs/2207.12396`).
* **Q-Align**, Wu et al., ICML 2024,
  `q-align.github.io`; `github.com/Q-Future/Q-Align`. Repo and project
  page accessed 2026-05-08.
* **MIT-Adobe FiveK** dataset,
  `data.csail.mit.edu/graphics/fivek/`.
* **SIDD** dataset, Abdelhamed et al., CVPR 2018
  (`www.eecs.yorku.ca/~kamel/sidd/`,
  `cse.yorku.ca/~mbrown/pdf/sidd_cvpr2018.pdf`).
* **DND** (Darmstadt Noise Dataset),
  `noise.visinf.tu-darmstadt.de/`.
* **HDR+ Burst** dataset, `hdrplusdata.org/dataset.html`, CC BY-SA 4.0.
* **RAW-NOD** dataset, CVPR 2024
  (`openaccess.thecvf.com/content/CVPR2024/papers/Flepp_Real-World_Mobile_Image_Denoising_Dataset_with_Efficient_Baselines_CVPR_2024_paper.pdf`).
* **Kalantari HDR** dataset, SIGGRAPH 2017,
  `cseweb.ucsd.edu/~viscomp/projects/SIG17HDR/`.
* **NTIRE-HDR** challenge sets, `cvlai.net/ntire/`.
* **RAISE** dataset, `loki.disi.unitn.it/RAISE/`.
* **KonIQ-10k**, `database.mmsp-kn.de/koniq-10k-database.html`.
* **SPAQ**, `github.com/h4nwei/SPAQ`.
* **FLIVE / PaQ-2-PiQ**, `github.com/baidut/PaQ-2-PiQ`.
* **TID2013**, `live.ece.utexas.edu/research/tid2013/`.
* **KADID-10K**,
  `database.mmsp-kn.de/kadid-10k-database.html`.
* **PSNR / SSIM**, Wang et al., IEEE TIP 2004 (DOI
  `10.1109/TIP.2003.819861`).
* **MS-SSIM**, Wang et al., Asilomar 2003 / ICIP 2003.
* **FSIM**, Zhang et al., IEEE TIP 2011.
* **VSI**, Zhang et al., IEEE TIP 2014.
* **HaarPSI**, Reisenhofer et al., 2018
  (`arxiv.org/abs/1607.06140`).
* **GMSD / MS-GMSD**, Xue et al., IEEE TIP 2014.
* **VIFp**, Sheikh & Bovik, IEEE TIP 2006.
* **LPIPS**, Zhang et al., CVPR 2018,
  `richzhang.github.io/PerceptualSimilarity/`.
* **ST-LPIPS**, Ghildyal & Liu, ECCV 2022,
  `github.com/abhijay9/ShiftTolerant-LPIPS`.
* **DISTS**, Ding et al., IEEE TPAMI 2020,
  `github.com/dingkeyan93/DISTS`.
* **PieAPP**, Prashnani et al., CVPR 2018.
* **NIQE**, Mittal et al., IEEE Signal Processing Letters 2013.
* **BRISQUE**, Mittal et al., IEEE TIP 2012.
* **DBCNN**, Zhang et al., IEEE TCSVT 2018.
* **HDR-VDP-3**, Mantiuk et al., 2023
  (`hdrvdp.sourceforge.net/`).
* **PU21 encoding**, Mantiuk 2021
  (`github.com/gfxdisp/pu21`).
* **CIEDE2000**, CIE 142-2001 (`cie.co.at`); Sharma G., et al.,
  Color Research & Application 2005 (`hajim.rochester.edu/ece/sites/gsharma/papers/CIEDE2000CRNAFeb05.pdf`).
* **ColorChecker / Macbeth chart**, X-Rite reference
  (`xritephoto.com`).
* **ISO 12233:2024** "Photography — Electronic still picture imaging
  — Resolution and spatial frequency responses",
  `iso.org/standard/77520.html`.
* **Imatest slanted-edge validation**,
  `imatest.com/imaging/validating_slanted_edge/` (accessed 2026-05-08).
* **DXOMARK protocol**,
  `dxomark.com/dxomark-camera-sensor-testing-protocol-and-scores/`,
  `dxomark.com/dxomark-lens-camera-sensor-testing-protocol/`,
  `corp.dxomark.com/wp-content/uploads/2017/11/2009_EI_TextureSharpness.pdf`.
* **DXOMARK e-SFR chart** (ISO 12233:2024 / IEEE 2020 compliant),
  `corp.dxomark.com/catalog/testing-tools/product-type/charts-equipements/e-sfr-chart-iso-12233-compliant/`.
* **Image Engineering — image quality factors**,
  `image-engineering.de/library/image-quality/factors/1055-resolution`.
* **ViewSonic ΔE primer**,
  `viewsonic.com/library/creative-work/what-is-delta-e-and-why-is-it-important-for-color-accuracy/`.
* **Imatest — what MTF50 means**,
  `mri-q.com/uploads/3/4/5/7/34572113/imatest_-_sharpness__what_is_it_and_how_is_it_measured_.pdf`.
* **Awesome-IQA**,
  `github.com/chaofengc/Awesome-Image-Quality-Assessment`.
* **CodSpeed Google Benchmark guide**,
  `codspeed.io/docs/guides/how-to-benchmark-cpp-with-google-benchmark`.
* **Bencher Google Benchmark guide**,
  `bencher.dev/learn/benchmarking/cpp/google-benchmark/`.
* **Vega-Lite for Catch2 visualization**,
  `joht.github.io/johtizen/data/2022/09/05/visualize-catch2-benchmarks-with-vega-lite.html`.
* **Markaicode performance regression in CI/CD**,
  `markaicode.com/performance-regression-testing-cicd/`.
* **Vorbrodt micro-benchmarks comparison**,
  `vorbrodt.blog/2019/03/18/micro-benchmarks/`.
* **Android `dumpsys batterystats`**,
  `developer.android.com/topic/performance/power/setup-battery-historian`.
* **Apple Energy Diagnostics**,
  `developer.apple.com/documentation/xcode/reducing-your-apps-power-consumption`.
* **MAI 2025 challenge**,
  `openaccess.thecvf.com/content/CVPR2025W/MAI/`.

---

## 6. See also

* [#04 — Mobile AI inference](04-mobile-ai-inference.md) — engines whose
  latency / memory the perf bench measures.
* [#06 — Soft-ISP architectures](06-soft-isp-architectures.md) — DAG /
  scheduler / memory plan that the perf bench profiles.
* [#07 — Classic ISP algorithms](07-classic-isp-algorithms.md) — node-by-
  node algorithms whose IQ this report regression-tests.
* [#08 — AI ISP algorithms](08-ai-isp-algorithms.md) — neural nodes whose
  IQ regression set this harness owns.
* [#13 — Color management](13-color-management.md) — color spaces that
  ΔE2000 and ICC-aware HDR-IQ depend on.
* [#14 — HEIF and HDR output](14-heif-and-hdr-output.md) — HEIF decoder
  the bench uses to read cpipe outputs back into RGB / RGBA, and the
  UltraHDR / PQ / Apple Adaptive HDR formats whose HDR-IQ this harness
  exercises.

---

## 7. Open Questions

1. **HDR-VDP-3 license.** Mantiuk's HDR-VDP-3 is *non-commercial*. v1
   uses it CI-only (acceptable). v2 may want a redistributable HDR
   metric — an Apache-2 PU21-encoded MS-SSIM is a candidate but the
   MS-SSIM author hasn't blessed PU21 wrap; we may need to publish our
   own validation.
2. **pyiqa upgrade cadence.** pyiqa releases roughly quarterly; new
   metrics arrive that we'd want (e.g., 2026's TOPIQ-AIGC variants).
   How often do we rev the CI pinned `pyiqa==0.1.x`? Tentative: every
   minor release, behind a dedicated PR that locks a new baseline.
3. **Subprocess startup amortisation.** Loading torch + pyiqa + 4
   models takes ~5 s. For our 50-image corpus this is fine. For per-PR
   dashboards on 1 image, it dominates. Mitigation: persistent CI
   daemon? Out of v1.
4. **Synthetic vs real Macbeth shots.** v1 uses *synthesised* DNG
   Macbeth charts (round-trip the patches through a known camera
   forward matrix). This avoids needing a calibrated photo studio in
   CI but loses the lens / sensor noise contributions. v2 should add a
   physical chart shot per supported sensor.
5. **Q-Align cost vs value.** A single Q-Align run on 50 images on
   RTX A5000 takes ~10 minutes. Run weekly only? Or run on a smaller
   "stress" subset of 5 images per PR? Tentative: 5 images per PR; full
   50 weekly.
6. **Burst regression fairness.** SIDD pairs are "noisy" + "clean"
   single-frame. cpipe's burst pipeline operates on N frames. Synthetic
   bursts (add temporal noise to clean GT) work but lose realistic
   motion. The HDR+ Burst dataset is the right answer; we may need to
   *augment* it for our specific phone sensors.
7. **Per-architecture reference baselines.** Apple Silicon, x86_64,
   and ARM64 may produce slightly different floating-point outputs
   even when running the same compute graph (FMA differences). Do we
   maintain three baselines, or pick a "canonical" architecture? Tentative:
   x86_64 canonical baseline; Apple / Android tested with looser per-
   metric tolerances.
8. **Energy regression on iOS.** Without a scriptable energy API, we
   can't gate on iOS energy in v1 CI. Hand-run nightlies via Instruments
   are expected; gating deferred to v2 (matches D14 timeline).
9. **AI artifact-specific metrics.** AI denoise / SR can hallucinate
   plausible-but-wrong details (especially with deep generative models).
   LPIPS / DISTS partly catch this; specialised "AI hallucination"
   metrics (e.g., detail-mismatch score, see arXiv 2403.07485 / AIGC-IQA
   2024 work) are a 2026+ development. v1 uses a `compare.split` node
   for human review of suspected hallucinations.
10. **GPU baseline reproducibility.** Vulkan / Metal driver updates can
    move PSNR by ~0.05 dB and latency by ~5 %. We pin driver versions
    on CI runners; baseline recalibration triggers on driver bumps.
    Open: how to expose this in the dashboard without spurious "regression"
    alerts.
