# Report 08 — AI ISP Algorithms (2024–2026)

> **Cluster C — ISP Pipeline & Algorithms.** Survey of state-of-the-art neural
> ISP algorithms ready to ship as cpipe nodes through the inference layer
> from [#04](04-mobile-ai-inference.md). Scope, sizing, latency, license, and
> recommendations for v1's small set of "AI nodes" complementing the classic
> ones in [#07](07-classic-isp-algorithms.md).

---

## 1. TL;DR

cpipe v1 ships **3 AI nodes**: (1) **NAFNet-width32** for high-quality raw
denoise (17 M params, 16 GMACs/MP, 40 dB SIDD PSNR; quantises to INT8 with
≤0.4 dB drop), (2) **AdaInt 3D-LUT** for AI photo color enhancement (<600 K
params, <2 ms / 4K on desktop GPU; perfect mobile GPU/NPU fit), (3)
**HDR+-Wronski-style burst neural denoise** as an optional drop-in for the
classic merge node, behind a feature flag. We **defer** end-to-end neural
ISPs (DeepISP, CameraNet, RMFA-Net) to v2 — they are tied to specific sensor
calibration data and undermine the modular architecture in
[#06](06-soft-isp-architectures.md). Restormer is a strong-quality alt
denoise (26 M params, 40.02 dB SIDD) but Halide schedule + INT8 path is less
mature; we ship as a v1.x option. SwinIR (11.8 M params) is a credible
super-resolution candidate but SR is **not** a v1 must-have. AHDRNet
remains our reference if we add neural multi-frame HDR fusion later.
Conversion path: PyTorch → ONNX → ExecuteTorch / ONNX RT for desktop;
PyTorch → ONNX → Qualcomm QNN for Hexagon HTP; PyTorch → Core ML for ANE.
Quantisation calibrated against MIT-Adobe FiveK + SIDD reference sets.
(200 words)

---

## 2. Decision Matrix — v1 AI Node Set

| Node                | Network             | Params | GMACs/MP | Disk (FP16/INT8)    | Target latency (Hexagon HTP) | Target latency (ANE) | License (orig)        | v1 Status           | Notes                                                |
|----------------------|---------------------|--------|----------|----------------------|------------------------------|------------------------|------------------------|----------------------|------------------------------------------------------|
| `denoise.nafnet`    | NAFNet-width32      | 17 M   | ~16     | 33 MB / 17 MB       | ~120 ms / 12 MP             | ~80 ms / 12 MP         | MIT (Megvii)            | v1 default AI denoise | Best quality/cost ratio in 2024–2026 lit            |
| `denoise.nafnet_w64` | NAFNet-width64      | 67 M   | ~64     | 134 MB / 67 MB      | ~600 ms / 12 MP             | ~400 ms / 12 MP        | MIT (Megvii)            | v1 alt              | High-quality desktop only                           |
| `denoise.restormer` | Restormer            | 26 M   | ~140    | 52 MB / 26 MB       | ~600 ms / 12 MP             | ~400 ms / 12 MP        | NTU-NTU-MBZUAI custom    | v1.x alt             | Best PSNR among classics on SIDD (40.02 dB)          |
| `color.adaint`       | AdaInt 3D LUT       | <0.6 M | ~0.05    | 1.2 MB / 0.6 MB     | ~2 ms / 4K                  | ~1 ms / 4K            | non-commercial?         | v1 default AI color | tiny model; CNN predicts LUT params                  |
| `color.lut3d_basis` | Image-Adaptive 3D LUT (Zeng) | 0.6 M | ~0.05 | 1.2 MB / 0.6 MB | ~2 ms / 4K | ~1 ms / 4K | research              | v1 alt            | Predecessor to AdaInt; basis fusion                |
| `tone.hdrnet`       | HDRNet               | 0.5 M  | ~0.5    | 1 MB / 0.5 MB        | ~10 ms / 4K                | ~5 ms / 4K           | Apache-2 (Google)       | v1 alt             | Bilateral grid; needs custom slice op              |
| `burst.hdrplus_ml` | HDR+-Wronski neural merge | ~10 M | ~50 | 20 MB / 10 MB | ~600 ms / 8×12 MP | ~400 ms | research                | v1 alt             | Drop-in upgrade for classic burst merge            |
| `demosaic.swinir_d` | SwinIR-tiny demosaic | 0.9 M  | ~10     | 1.8 MB / 0.9 MB    | ~80 ms / 12 MP            | ~50 ms / 12 MP       | Apache-2                 | v2 candidate         | Joint demosaic+denoise; transformer                 |
| `sr.swinir`          | SwinIR-base (×2 SR)  | 11.8 M | ~120    | 24 MB / 12 MB       | ~600 ms / 12 MP           | ~400 ms / 12 MP      | Apache-2                 | DEFERRED            | super-res not v1 must-have                          |
| `hdr.ahdrnet`       | AHDRNet              | ~1.5 M | ~12      | 3 MB / 1.5 MB        | ~80 ms / 3×12 MP          | ~50 ms                | author code (research)  | DEFERRED            | exposure-bracketed multi-frame HDR; v1 deferred     |
| `e2e.deepisp`        | DeepISP / CameraNet  | varies | varies   | varies               | varies                       | varies                  | research                  | NO                  | tied to specific sensor; v1 stays modular          |

### How latencies were estimated

Hexagon HTP (Snapdragon 8 Gen 3) ≈ 12 TOPS INT8. 12 MP = 12.6 M pixels ≈
12.6 GMAC at 1 MAC/pixel; typical ISP nets are 16 GMACs/MP. So a model
running on full HTP throughput would take ≈ (16 × 12.6) / 12 000 = 16.8 ms.
Real-world overhead is 5–10 × that for memory I/O and partitioning. ANE
(Apple A17 Pro) is ≈ 35 TOPS INT8; multiply factors below by 0.5–0.7.
**Numbers above are best-effort estimates; they must be re-measured** —
[#05](05-npu-backends-zero-copy.md) covers actual NPU benchmarking.

---

## 3. Detailed Findings

### 3.1 AI Denoise — the most mature category

This is where AI ISP delivers the largest delta over classical methods,
and where shipped mobile cameras in 2024–2026 already use AI.

#### 3.1.1 NAFNet (Chen et al., ECCV 2022)

* **Repo.** `github.com/megvii-research/NAFNet`. **License.** MIT (Megvii's
  recent restoration releases are MIT). Apache-2 compatible.
* **Architecture.** UNet-shape encoder–decoder with "Nonlinear Activation
  Free" blocks: SimpleGate (linear gating without GELU/sigmoid) +
  Simplified Channel Attention. Width-64 model is 67 M params; width-32
  is 17 M.
* **Performance.** SIDD PSNR 40.30 dB (width-64), 39.97 dB (width-32),
  beating prior SOTA by 0.28 dB at less than half the FLOPs. GoPro deblur
  PSNR 33.71 dB. Source: NAFNet paper (`arxiv.org/abs/2204.04676`).
* **Quantisation.** Megvii has published INT8 weights — quantises cleanly
  thanks to absent nonlinear activations. Expect ≤0.4 dB drop at INT8.
* **cpipe v1 plan.** **NAFNet-width32 is the v1 default AI denoise**.
  Width-64 is shipped as desktop-only alt. INT8 + Hexagon path = primary
  mobile target.

#### 3.1.2 Restormer (Zamir et al., CVPR 2022)

* **Repo.** `github.com/swz30/Restormer`. **License.** custom non-
  commercial-friendly clause; *we must verify before ship*. Several forks
  re-license as MIT for the implementation; the paper authors are NTU /
  MBZUAI.
* **Architecture.** Multi-scale hierarchical transformer with two key
  blocks: MDTA (Multi-Dconv head Transposed Attention) — attention across
  channels not space, so cost is O(C²) per pixel rather than O(N²) per
  channel; GDFN (Gated-Dconv Feed-forward Network).
* **Parameters.** 26.13 M.
* **Performance.** SIDD PSNR 40.02 dB (only model in 2022 to break 40 dB
  on SIDD); DND 40.03 dB; deblurring strong on GoPro / RealBlur / HIDE.
* **Quantisation.** Transformer attention is harder to quantise than NAFNet's
  conv blocks. Expect ≥1 dB drop at INT8 without QAT.
* **cpipe v1 plan.** v1.x alt; not default due to license uncertainty +
  quantisation difficulty.

#### 3.1.3 MIRNet-v2 (Zamir et al., TPAMI 2022)

* **Repo.** `github.com/swz30/MIRNetv2`. **License.** likely the same as
  Restormer; verify.
* **Architecture.** Multi-resolution parallel streams, info exchange across
  streams, non-local + multi-scale attention. Lighter than Restormer.
* **Performance.** SIDD vs. CycleISP +0.32 dB; DND vs. DAGL +0.11 dB.
* **cpipe.** Skipped — NAFNet covers the same niche with a more permissive
  license.

#### 3.1.4 SwinIR (Liang et al., ICCV 2021)

* **Repo.** `github.com/JingyunLiang/SwinIR`. **License.** Apache-2.0
  (declared in repo). Apache-2 compatible.
* **Architecture.** Shallow + deep feature extract + reconstruction. Deep
  stage = stack of Residual Swin Transformer Blocks (RSTBs). 11.8 M params
  (base), considerably smaller than IPT (115 M).
* **Performance.** Up to 0.45 dB PSNR on Set5/14/BSD100/Urban100/Manga109.
  Strong on Gaussian denoise, JPEG artefact removal, classical SR.
* **cpipe.** Apache-2 attractive; transformer is moderately quantisation-
  unfriendly. v2 candidate for joint demosaic + denoise; not v1.

#### 3.1.5 PMRID (Wang et al., ECCV 2020)

* **Repo.** `github.com/MegEngine/PMRID`. **License.** Apache-2.0 (Megvii
  policy).
* **Architecture.** Light-weight UNet + k-Sigma transform front-end. ~70 ms /
  MP on Snapdragon 855 (which is ~ 2× the throughput per pixel of NAFNet-
  width32 on a faster chip).
* **Performance.** Author claims it is the basis of "night shot" feature in
  several flagship phones (2019). Excellent quality / cost.
* **cpipe.** Strong contender for the v1 *fast* AI denoise node — even
  faster than NAFNet-width32. Open question: include both?

#### 3.1.6 Self-supervised denoising — Noise2Noise / Neighbor2Neighbor

These are training-time tricks rather than network architectures. Relevant
for cpipe only insofar as we want to *retrain* an existing architecture on
our own paired-burst data without ground-truth pairs. Scope: out of v1.

### 3.2 AI Multi-frame Fusion — HDR + Burst

#### 3.2.1 DeepHDR / Kalantari 2017

* **Paper.** Kalantari & Ramamoorthi, "Deep High Dynamic Range Imaging of
  Dynamic Scenes", SIGGRAPH 2017. First CNN HDR work. Optical-flow align +
  three-network compare.
* **Status.** Historical; superseded by attention-based methods.

#### 3.2.2 AHDRNet (Yan et al., CVPR 2019)

* **Repo.** `github.com/qingsenyangit/AHDRNet`.
* **Architecture.** Attention-guided U-Net. Highlights motion / saturation
  prior to the merge net.
* **Performance.** On the Kalantari 17 dataset, SOTA at the time (PSNR
  improvements over Kalantari 17 of ~1.5 dB).
* **cpipe.** Defer to v2; v1 burst path uses HDR+ classic + optional
  Wronski neural drop-in (3.2.4).

#### 3.2.3 HDR-Transformer (Liu et al., ECCV 2022)

* **Architecture.** Transformer feature encoding instead of CNN; outperforms
  Sen / Hu / Kalantari / DeepHDR / AHDRNet / NHDRRNet / HDR-GAN / SwinIR.
* **cpipe.** Defer to v2. Same niche as AHDRNet.

#### 3.2.4 Wronski Multi-Frame Super-Resolution (SIGGRAPH 2019)

* **Paper.** `arxiv.org/abs/1905.03277` (Pixel 3 / 4 burst pipeline).
* **Architecture.** *Not strictly a neural network* in the traditional
  sense — Wronski's contribution is a data-driven robustness model and
  kernel regression that runs in 4 s on-device for 12 MP. There is a
  non-official PyTorch port (`github.com/Jamy-L/Handheld-Multi-Frame-Super-Resolution`).
* **cpipe v1 plan.** Reference implementation for the *classic* burst
  merge node ([#07 §3.4](07-classic-isp-algorithms.md)). v1 ships the IPOL
  HDR+ derivative; Wronski's robustness extensions are an optional
  research-quality alt.

#### 3.2.5 BurstSR / DeepRep / BPN (Bhat et al., CVPR/ICCV 2021–22)

* **Repos.** `github.com/goutamgmb/deep-burst-sr`,
  `github.com/goutamgmb/deep-rep`. **License.** Apache-2.0 (declared).
* **Architectures.** PWC-Net based optical-flow alignment + reparametrized
  MAP fusion + learned image priors. Burst denoise + burst SR jointly.
* **cpipe.** Strong candidate for *AI burst denoise+SR* in v2. v1 deferred.

### 3.3 AI Color Enhancement / Tone

#### 3.3.1 Image-Adaptive 3D LUT (Zeng et al., 2020 / TPAMI 2022)

* **Repo.** `github.com/HuiZeng/Image-Adaptive-3DLUT`. **License.**
  *Author-restricted academic*; commercial use ambiguous. ECCV 2024
  follow-up (`github.com/WontaeaeKim/LUTwithBGrid`) re-licensed Apache-2.0.
* **Architecture.** Tiny CNN (~600 K params) ingests low-res input, predicts
  weights to fuse N basis 3D LUTs into one image-adaptive LUT. Inference
  cost: <2 ms / 4K on Titan RTX.
* **Performance.** Outperforms HDRNet, DPED, etc. by large PSNR / SSIM /
  ΔE on FiveK.
* **cpipe v1 plan.** **Image-Adaptive 3D LUT (or AdaInt) is the v1 default
  AI color enhancement node.** It hits the perfect mobile NPU sweet spot:
  trivial cost, large IQ improvement.

#### 3.3.2 AdaInt (Yang et al., CVPR 2022)

* **Repo.** `github.com/ImCharlesY/AdaInt`. **License.** unclear; verify.
* **Architecture.** Plug-and-play module for learnable 3D LUTs;
  introduces *image-adaptive sampling intervals* (non-uniform LUT layout)
  via a differentiable AiLUT-Transform.
* **Performance.** Beats Zeng 2020 with negligible overhead increase.
* **cpipe v1 plan.** Drop-in upgrade of `color.lut3d_basis` once licence
  cleared.

#### 3.3.3 SepLUT (Yang et al., ECCV 2022)

* **Architecture.** Separable 1D + 3D LUTs for ultra-low memory.
* **cpipe.** Worth tracking; not v1.

#### 3.3.4 HDRNet (Gharbi et al., SIGGRAPH 2017)

* **Repo.** `github.com/google/hdrnet`. **License.** Apache-2.0.
* **Architecture.** Bilateral guided upsampling. Low-res CNN predicts a
  coefficient grid in bilateral space; the coefficients are sliced and
  applied to full-resolution image via a custom GPU op.
* **Performance.** Real-time on smartphone (sub-ms 1080p) circa 2017
  hardware.
* **cpipe.** v1 alt to AdaInt. The custom slice op is an issue: it must
  exist on every backend (Vulkan slang / Metal / Hexagon). Investment
  required. **Tentative**: ship as v1.x once the slice op is up on slang-rhi.

#### 3.3.5 CSRNet (He et al., ECCV 2020)

* **Architecture.** Conditional sequential retouching network. Smaller than
  HDRNet on FiveK. Not as well-studied for HDR.

### 3.4 AI Demosaic

* **DemoNet / DDFN / Joint Demosaic-Denoise** — academic literature
  (Ehret 2019, Liu 2020, Ehret-Facciolo 2020). Quality matches or exceeds
  classic demosaic on noisy inputs.
* **Quad Bayer specific** — Jia 2022 (`learning-rich-information-quad-bayer-remosaicing-and-denoising`).
* **cpipe v1.** Defer to v2. Classic RCD ([#07 §3.1](07-classic-isp-algorithms.md))
  is sufficient for v1; AI demosaic is a v2 quality improvement.

### 3.5 AI Super-Resolution — out of v1 scope

Mentioned for completeness:

* **SwinIR** — see 3.1.4. 11.8 M params (base) / 30 M (large).
* **Real-ESRGAN** — `github.com/xinntao/Real-ESRGAN`, BSD. Pure synthetic
  data training. Best practical results on phone-camera artefact restoration.
* **BSRGAN** — Zhang et al. ICCV 2021, BSD. Practical degradation-aware
  blind SR.
* **Real-CUGAN** — anime / illustration focused.
* **cpipe.** Super-resolution is **not** part of v1's contract (D1: read
  DNG, write HEIF). v2.

### 3.6 End-to-End Neural ISPs — explicit "no" for v1

* **DeepISP (Schwartz et al., TIP 2018)** — full Bayer→sRGB CNN.
* **CameraNet (Liang et al., 2019)** — restoration + enhancement two-stage.
* **RMFA-Net (2024)** — modern raw→RGB neural ISP.
* **Survey:** "ISP Meets Deep Learning" (ACM Computing Surveys 2025, DOI
  `10.1145/3708516`).

**Why cpipe says no for v1.**

1. **Modularity loss.** End-to-end ISPs blur the boundary between stages,
   undermining the modular DAG architecture in [#06](06-soft-isp-architectures.md)
   and the user's ability to mix-and-match nodes.
2. **Calibration coupling.** End-to-end ISPs are typically trained for a
   specific sensor (often the MAI / Mobile-AI-Workshop Sony IMX586). Out-of-
   sensor performance is much worse, often worse than classic ISP.
3. **HDR support.** End-to-end ISPs trained on SDR input cannot produce HDR
   HEIF output without retraining. D7 mandates HDR.
4. **Editor friendliness.** A monolithic `e2e_isp` node has 0 editable
   parameters that map to user intent — defeats the editor in
   [#11](11-pipeline-editor-and-connectivity.md).

We track this category for v2 / v3 *not* because it is wrong but because it
is incompatible with cpipe's modular value proposition.

### 3.7 MAI / Mobile AI ISP Challenge ecosystem

The MAI (Mobile AI) workshop at CVPR runs an annual challenge that
benchmarks AI ISPs on real mobile hardware (Snapdragon, MediaTek, ARM).
Highlights from MAI 2024 / 2025:

* MAI 2025 challenge (`openaccess.thecvf.com/content/CVPR2025W/MAI/`):
  PUNET as baseline; Fujifilm UltraISP dataset; runtime measured on Adreno
  GPU + Mali GPU.
* AISP library (`github.com/mv-lab/AISP`): collected official reference
  implementations from MAI / NTIRE / AIM challenges.
* RMFA-Net (`arxiv.org/abs/2406.11469`) — strong neural ISP benchmark
  performer 2024.
* CoDISP (CVPR 2024 W) — compressed-domain ISP.

**Status for cpipe.** AISP library is a useful *reference* for evaluating
neural ISP candidates. We will not productise it directly (research-grade
quality, sensor-coupled, license uncertainties), but will use it to bench-
mark our v1 modular pipeline against on the same datasets.

### 3.8 Conversion + deployment path

For each AI node, the deployment chain is:

```
PyTorch checkpoint
  ├──► ONNX (opset 17, dynamic shape on H,W)
  │      ├──► ONNX Runtime (desktop CPU / GPU; v1 Linux CLI default)
  │      ├──► ExecuteTorch (.pte) for Apple Silicon (mac/iOS later)
  │      └──► QNN .dlc (Hexagon HTP via Qualcomm tools)
  └──► Core ML (.mlpackage; ANE on macOS/iOS later)
```

Calibration data: 100 representative images from each of MIT-Adobe FiveK +
SIDD + a small cpipe-curated phone-burst set. INT8 calibration via
percentile / MSE in Qualcomm AIMET or PyTorch FX quantisation.

### 3.9 Hexagon HTP latency model

For each node we derive a back-of-envelope latency on Hexagon HTP V73 /
Snapdragon 8 Gen 3 ≈ 45 INT8 TOPS sustained:

```
latency_HTP_ms ≈ (model_GMACs * MP) * 1000 / (HTP_TOPS * 1e12 * util)
```

with `util ≈ 0.20–0.30` for typical UNet-shaped restoration nets (memory-
bound, partitioning overhead). The cpipe scheduler should treat NPU
performance as bursty — a single inference launch has 5–10 ms overhead;
batching multiple frames into one launch is preferred where possible.
[#05 — NPU backends](05-npu-backends-zero-copy.md) carries the actual
benchmarks.

### 3.10 ANE latency model

Apple A17 Pro NE ≈ 35 INT8 TOPS with **better** utilisation than Hexagon
(typically 0.4–0.6 for Core ML compiled models). So FP16 / INT8 latencies
on ANE should be ~2 × faster per pixel than on Hexagon for the same model,
notwithstanding partitioning differences. v1 (Linux CLI) doesn't target
ANE; v2 must.

### 3.11 IQ regression risk

A standing risk when shipping AI nodes: model artefacts (over-smoothed,
hallucinated detail, unnatural colour). cpipe's mitigation:

1. **Strict reference set** — every AI node has a pinned regression set
   in [#09](09-image-quality-benchmarks.md). Numerical drift between releases
   is flagged.
2. **Side-by-side comparison node** — `compare.split` outputs left half
   classic, right half AI, for visual A/B in the editor.
3. **Strength control** — every AI node has an `intensity` slider (0..1)
   that linearly blends with the classic baseline. Defaults to 0.7 on
   ship; users can tune.

### 3.12 Memory considerations

Runtime memory for AI nodes is on top of the pipeline's existing peak. For
NAFNet-width32 on a 12 MP image:

| Component                         | Memory (FP16)  |
|------------------------------------|----------------|
| Input tensor (1×3×H×W FP16)       | ~75 MB        |
| Output tensor                      | ~75 MB        |
| Activation peak (UNet)            | ~280 MB        |
| Weights                            | ~33 MB        |
| **Total peak**                     | **~460 MB**   |

For 100 MP D2 budget, NAFNet's activation peak hits ~2.3 GB FP16 — exceeds
the 800 MB ceiling. **Therefore: AI denoise on 100 MP images must be tiled.**
Tiling breaks D2's "no tiling in v1" rule, but the architecture in
[#06](06-soft-isp-architectures.md) accommodates per-node tiling internally
(the *node* tiles, not the *pipeline*). We pass through a 256×256 tile size
for the AI denoise node; tile boundaries are seamed via the existing
overlap support in NAFNet's UNet inference.

---

## 4. Code / Architecture Sketches

### 4.1 AI node interface in the cpipe DAG

```cpp
class NafnetDenoise : public NodeImpl {
public:
    void commit_params(const Params&) override;

    // Routes to the inference engine; the engine is selected per Device.
    void process_npu(InferenceContext&, const Roi&, const Params&) override;
    void process_gpu(SlangDispatcher&, const Roi&, const Params&) override; // ONNX-RT GPU
    void process_cpu(const Roi&, const Params&) override;                    // ONNX-RT CPU

    Roi  roi_in_for(const Roi& roi_out) const override {
        // No spatial dependency outside the tile + halo.
        return roi_out.expand(/*halo*/16);
    }
};
```

The same node body has three implementations dispatched by device, each
calling out to the right backend through [#04](04-mobile-ai-inference.md).

### 4.2 Manifest declaration

```cpp
CPIPE_NODE("denoise.nafnet", "AI Denoise — NAFNet", 1)
    CPIPE_INPUT_RGB("in", 16)            // FP16 RGB linear
    CPIPE_OUTPUT_RGB("out", 16)
    CPIPE_PARAM_FLOAT("intensity", 0.0f, 1.0f, 0.7f)
    CPIPE_PARAM_ENUM ("model", {"width32","width64"}, 0)
    CPIPE_DEVICES(GPU | NPU | CPU)
    CPIPE_MODEL_ASSET("denoise.nafnet.width32", "nafnet_w32_int8.dlc")
    CPIPE_MODEL_ASSET("denoise.nafnet.width64", "nafnet_w64_fp16.dlc")
CPIPE_NODE_END
```

The manifest carries the *list of model assets* that ship with the node;
the inference layer resolves the right one for the device at runtime.

### 4.3 Tile loop for 100 MP inputs

```cpp
void NafnetDenoise::process_gpu(SlangDispatcher& d, const Roi& roi, const Params& p) {
    constexpr int TILE = 256;
    constexpr int HALO = 16;
    for (int ty = 0; ty < roi.h; ty += TILE - 2*HALO) {
        for (int tx = 0; tx < roi.w; tx += TILE - 2*HALO) {
            Roi tile{tx-HALO, ty-HALO, TILE, TILE, roi.scale};
            tile.clamp(roi);
            d.run(model_, tile, p);  // engine handles inputs/outputs.
        }
    }
}
```

### 4.4 Quantisation calibration script (sketch)

```python
import torch, onnx, onnxruntime as ort
from torch.ao.quantization import quantize_fx

model = NAFNet(width=32).eval()
# Calibration loop with FiveK + SIDD samples
for img in calib_loader:
    model(img.to(torch.float16))
quant_model = quantize_fx.convert_fx(model)
torch.onnx.export(
    quant_model, dummy_input,
    'nafnet_w32_int8.onnx', opset_version=17,
    dynamic_axes={'input': {2: 'H', 3: 'W'}}
)
```

For Hexagon HTP, follow up with `qnn-onnx-converter` →`.dlc`.
For ANE, use `coremltools.convert` with `ComputeUnit.CPU_AND_NE`.

### 4.5 Strength blending

```cpp
// AI node output is merged with classic baseline:
output(x, y) = (1 - intensity) * classic(x, y) + intensity * ai(x, y);
```

At `intensity = 0.7` (default), the AI denoise contributes 70% of the
denoised result. At `0` the AI node is a no-op (useful for users who want
a 100% deterministic classic pipeline). At `1` the AI node fully replaces
the classic.

---

## 5. Cited Sources

* Chen, Chu, Wang & Mei, "Simple Baselines for Image Restoration"
  (NAFNet), *ECCV 2022* (`arxiv.org/abs/2204.04676`); repo
  `github.com/megvii-research/NAFNet` (MIT).
* Zamir, Arora, Khan, Hayat, Khan & Yang, "Restormer: Efficient Transformer
  for High-Resolution Image Restoration", *CVPR 2022 Oral*
  (`arxiv.org/abs/2111.09881`); repo `github.com/swz30/Restormer`.
* Zamir et al., "Learning Enriched Features for Fast Image Restoration and
  Enhancement", *TPAMI 2022* (MIRNet-v2,
  `github.com/swz30/MIRNetv2`).
* Liang, Cao, Sun, Zhang, Van Gool & Timofte, "SwinIR: Image Restoration
  Using Swin Transformer", *ICCV 2021W*
  (`arxiv.org/abs/2108.10257`); repo `github.com/JingyunLiang/SwinIR`
  (Apache-2).
* Wang et al., "Practical Deep Raw Image Denoising on Mobile Devices"
  (PMRID), *ECCV 2020* (`arxiv.org/abs/2010.06935`); repo
  `github.com/MegEngine/PMRID` (Apache-2).
* Kalantari & Ramamoorthi, "Deep High Dynamic Range Imaging of Dynamic
  Scenes", *SIGGRAPH 2017* (`cseweb.ucsd.edu/~viscomp/projects/SIG17HDR/`).
* Yan et al., "Attention-guided Network for Ghost-free High Dynamic Range
  Imaging" (AHDRNet), *CVPR 2019*; repo
  `github.com/qingsenyangit/AHDRNet`.
* Liu et al., "Ghost-free High Dynamic Range Imaging with Context-aware
  Transformer" (HDR-Transformer), *ECCV 2022*.
* Wronski et al., "Handheld Multi-Frame Super-Resolution", *SIGGRAPH 2019*
  (`arxiv.org/abs/1905.03277`); non-official PyTorch port
  `github.com/Jamy-L/Handheld-Multi-Frame-Super-Resolution`.
* Bhat et al., "Deep Burst Super-Resolution", *CVPR 2021*
  (`arxiv.org/abs/2101.10997`); repo `github.com/goutamgmb/deep-burst-sr`
  (Apache-2).
* Bhat et al., "Deep Reparametrization of Multi-Frame Super-Resolution and
  Denoising", *ICCV 2021* (`arxiv.org/abs/2108.08286`); repo
  `github.com/goutamgmb/deep-rep`.
* Zeng, Cai, Li, Cao & Zhang, "Learning Image-adaptive 3D Lookup Tables for
  High Performance Photo Enhancement in Real-time", *TPAMI 2022*
  (`arxiv.org/abs/2009.14468`); repo
  `github.com/HuiZeng/Image-Adaptive-3DLUT`.
* Yang et al., "AdaInt: Learning Adaptive Intervals for 3D Lookup Tables on
  Real-time Image Enhancement", *CVPR 2022* (`arxiv.org/abs/2204.13983`);
  repo `github.com/ImCharlesY/AdaInt`.
* Yang et al., "SepLUT: Separable Image-adaptive Lookup Tables", *ECCV 2022*.
* Gharbi, Chen, Barron, Hasinoff & Durand, "Deep Bilateral Learning for
  Real-Time Image Enhancement" (HDRNet), *SIGGRAPH 2017*
  (`groups.csail.mit.edu/graphics/hdrnet/`); repo
  `github.com/google/hdrnet` (Apache-2).
* He, Bao & Lau, "Conditional Sequential Modulation for Efficient Global
  Image Retouching" (CSRNet), *ECCV 2020*.
* Schwartz, Giryes & Bronstein, "DeepISP", *TIP 2018*.
* Liang et al., "CameraNet: A Two-Stage Framework for Effective Camera ISP
  Learning", 2019.
* RMFA-Net (`arxiv.org/abs/2406.11469`), 2024.
* "ISP Meets Deep Learning: A Survey on Deep Learning Methods for Image
  Signal Processing", *ACM Computing Surveys 2025*, DOI
  `10.1145/3708516`.
* MAI 2025 challenge proceedings,
  `openaccess.thecvf.com/content/CVPR2025W/MAI/`.
* AISP library, `github.com/mv-lab/AISP`.
* Jia et al., "Learning Rich Information for Quad Bayer Remosaicing and
  Denoising", 2022 (`jhc.sjtu.edu.cn/~xiaohongliu/papers/2022learning.pdf`).
* arXiv 2504.07145, "Examining Joint Demosaicing and Denoising for Single-,
  Quad-, and Nona-Bayer Patterns", 2025.

---

## 6. See also

* [#04 — Mobile AI inference](04-mobile-ai-inference.md) — engine of choice
  for each AI node above.
* [#05 — NPU backends + zero-copy](05-npu-backends-zero-copy.md) — Hexagon /
  ANE deployment paths and benchmarking.
* [#06 — Soft-ISP architectures](06-soft-isp-architectures.md) — the DAG
  the AI nodes plug into.
* [#07 — Classic ISP algorithms](07-classic-isp-algorithms.md) — classic
  baselines AI nodes blend with via `intensity`.
* [#09 — Image quality benchmarks](09-image-quality-benchmarks.md) — the
  IQ regression suite each AI node ships with.
* [#13 — Color management](13-color-management.md) — colour space at AI
  node boundaries (linear FP16).

---

## 7. Open Questions

1. **Restormer license.** The original repo's license is "Custom" — verify
   commercial use before shipping. If unclear, fall back to NAFNet only.
2. **AdaInt vs Image-Adaptive 3D LUT vs HDRNet for default AI color node.**
   AdaInt is the technical winner; HDRNet has the cleanest licence (Apache-2);
   Image-Adaptive 3D LUT has the cleanest mobile NPU mapping. *Tentative*:
   ship Image-Adaptive 3D LUT in v1 (cleanest LUT primitive); upgrade to
   AdaInt in v1.x once licence is cleared.
3. **PMRID vs NAFNet-width32 for default AI denoise.** PMRID is roughly 2 ×
   faster; NAFNet has 1–2 dB higher PSNR. *Tentative*: ship NAFNet as the
   primary; PMRID as `denoise.nafnet_fast` alternate.
4. **Tiling AI nodes within the static topology rule (D6).** D6 says the
   topology is static after load; the AI node *internally* tiles. This is
   compatible — the *DAG* is static; what the node does inside is opaque.
   But we must clearly document that `roi_in_for` returns the per-tile
   halo, not per-pipeline ROI.
5. **Can we mix INT8 NAFNet with FP16 surrounding pipeline?** Yes — the
   conversion ops in [#06 §4.8](06-soft-isp-architectures.md) handle this.
   But INT8 needs scale + zero-point metadata that the precision plan must
   carry.
6. **AI HDR fusion (AHDRNet / HDR-Transformer) for v1?** Tempting because
   it would directly produce HDR HEIF output. *Tentative*: defer. v1's HDR
   path is the classic HDR+ merge + filmic-RGB tone curve targeted at PQ
   1000 nits.
7. **End-to-end ISP "exception" for benchmark mode.** Even though we don't
   ship e2e ISPs as production nodes, the benchmark harness in
   [#09](09-image-quality-benchmarks.md) should be able to run an e2e
   network as a comparison baseline.
8. **Strength-blending defaults.** 0.7 is a safe default for denoise;
   probably too high for color enhancement (over-saturated risk). Per-node
   defaults to be tuned via the regression suite.
9. **Model versioning + signing.** Model assets must be versioned and
   ideally checksummed. Reserved manifest field `model_sha256`. Supply chain
   security is in scope for the plugin architecture even if v1 ships only
   built-in models.
10. **Quantisation calibration set ownership.** MIT-Adobe FiveK + SIDD are
    license-clean for academic use; commercial use of FiveK is debated.
    cpipe ships its own *additional* curated calibration set so we are never
    blocked on third-party data.
