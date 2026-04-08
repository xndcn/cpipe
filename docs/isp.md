# ISP Node Reference and SOTA Survey

## Overview

cpipe's ISP strategy is **classical algorithms first, AI gradual replacement**. All processing nodes -- both classical and AI-based -- are implemented as plugins via the unified C ABI. Users can freely swap implementations in their pipeline JSON.

The MVP pipeline processes single-frame RAW data through a linear chain. Multi-frame processing (burst, HDR) and advanced AI nodes are added in later milestones.

## Pipeline Topology (MVP)

```
RAW (Bayer 16-bit)
 |
 v
[BLC] -----> Black Level Correction
 |
 v
[Bad Pixel] -> Dead/Hot Pixel Correction
 |
 v
[LSC] -----> Lens Shading Correction
 |
 v
[Demosaic] -> CFA Interpolation (Bayer -> RGB)
 |
 v
[AWB] -----> Auto White Balance
 |
 v
[CCM] -----> Color Correction Matrix (camera -> sRGB/Display P3)
 |
 v
[Gamma] ----> Tone Curve / Gamma Correction
 |
 v
HEIF (8/10-bit sRGB or HDR)
```

## Node Specifications

### RAW Preprocessing

#### Black Level Correction (BLC)

| Property | Value |
|----------|-------|
| Type | Classical (Halide) |
| Plugin ID | `cpipe.isp.blc` |
| Input | Raw Bayer 16-bit + DNG BlackLevel/BlackLevelDeltaH/V tags |
| Output | Bayer 16-bit (black-level subtracted) |
| Device | CPU or GPU |

**Implementation**: Per-channel subtraction using DNG `BlackLevel` tag values. Supports per-row delta (`BlackLevelDeltaH/V`) for sensors with row-dependent black levels. Clamp to zero after subtraction.

**Parameters** (JSON):
```json
{
  "black_level": [64, 64, 64, 64],
  "use_dng_metadata": true
}
```

**Reference**: DNG Specification 1.7, Section 5 (Opcode Processing).

#### Lens Shading Correction (LSC)

| Property | Value |
|----------|-------|
| Type | Classical (Halide) |
| Plugin ID | `cpipe.isp.lsc` |
| Input | Bayer 16-bit + shading gain map (from DNG OpcodeList3 or calibration file) |
| Output | Bayer 16-bit (shading corrected) |
| Device | CPU or GPU |

**Implementation**: Bilinear interpolation of per-channel gain maps (typically 17x13 grid) to full resolution, then per-pixel multiplication. Gain maps are either extracted from DNG `OpcodeList3` (GainMapOpcode) or loaded from a device calibration file.

**Parameters**:
```json
{
  "gain_map_source": "dng",
  "gain_map_file": null,
  "strength": 1.0
}
```

#### Bad Pixel Correction

| Property | Value |
|----------|-------|
| Type | Classical (Halide) |
| Plugin ID | `cpipe.isp.bad_pixel` |
| Input | Bayer 16-bit + optional defect map |
| Output | Bayer 16-bit (corrected) |
| Device | CPU or GPU |

**Implementation**: Two-pass approach:
1. **Detection**: Pixels deviating by more than a threshold from the median of same-color neighbors are flagged as defective.
2. **Correction**: Flagged pixels are replaced by the median of their same-color neighbors (3x3 or 5x5 window).

Optional static defect map from calibration can supplement dynamic detection.

**Parameters**:
```json
{
  "detection_threshold": 50,
  "window_size": 5,
  "static_map_file": null
}
```

#### CFA Pattern Detection (Utility)

| Property | Value |
|----------|-------|
| Type | Utility (no processing) |
| Plugin ID | N/A (engine-level) |

Reads `CFAPattern` tag from DNG metadata and provides CFA layout enum (`RGGB`, `BGGR`, `GRBG`, `GBRG`) to all Bayer-aware nodes. Not a pipeline node itself -- handled by the pipeline engine during initialization.

### Core ISP

#### Demosaicing

| Property | Value |
|----------|-------|
| Type | Classical (Halide); AI replacement planned |
| Plugin ID | `cpipe.isp.demosaic` |
| Input | Bayer 16-bit + CFA pattern |
| Output | RGB 16-bit (linear, full resolution) |
| Device | GPU preferred |

**Classical implementation**: Malvar-He-Cutler (2004) gradient-corrected bilinear interpolation. Good balance of quality and speed. Alternative: Adaptive Homogeneity-Directed (AHD) for higher quality at ~2x cost.

**AI target**: Hardware-implementable Joint Demosaicing-Denoising (JDD), 83% parameter reduction and 77% MACs reduction vs standard convolution approaches. Deployable via ExecuTorch with SNPE delegate on Qualcomm.

**Parameters**:
```json
{
  "algorithm": "malvar",
  "edge_threshold": 1.5
}
```

**References**:
- Malvar, He, Cutler. "High-Quality Linear Interpolation for Demosaicing of Bayer-Patterned Color Images." IEEE ICASSP, 2004.
- Hardware-implementable JDD with partial convolution (2024): 83% fewer parameters, mobile-friendly.

#### Auto White Balance (AWB)

| Property | Value |
|----------|-------|
| Type | Classical (Halide); AI replacement planned |
| Plugin ID | `cpipe.isp.awb` |
| Input | RGB 16-bit (linear) |
| Output | RGB 16-bit (white-balanced) + WB gains [R, G, B] |
| Device | CPU or GPU |

**Classical implementation**: Gray World assumption with White Patch constraint. Compute channel means, derive per-channel gains to equalize. White Patch adds a ceiling based on the brightest neutral region.

**AI target**: Time-aware AWB (~5K parameters, 0.25ms on mobile DSP). Uses contextual metadata (timestamp, geolocation) alongside image statistics. Validated on 3,224 images with ground-truth illuminants.

**Parameters**:
```json
{
  "algorithm": "gray_world",
  "manual_gains": null,
  "clip_percentile": 0.95
}
```

**References**:
- Gray World: Buchsbaum, 1980.
- Time-aware AWB (2025): ~5K params, 0.25ms DSP / 0.80ms CPU.
- Learnable Projection Matrices AWB (2024): 35x faster with equivalent quality.

#### Color Correction Matrix (CCM)

| Property | Value |
|----------|-------|
| Type | Classical (Halide) |
| Plugin ID | `cpipe.isp.ccm` |
| Input | RGB 16-bit (white-balanced) + 3x3 color matrix |
| Output | RGB 16-bit (in target color space) |
| Device | CPU or GPU |

**Implementation**: 3x3 matrix multiplication per pixel. Matrix sourced from DNG `ColorMatrix1`/`ColorMatrix2` tags (interpolated by correlated color temperature) or from a device calibration file. Supports sRGB and Display P3 as target color spaces.

**Parameters**:
```json
{
  "matrix_source": "dng",
  "target_colorspace": "srgb",
  "custom_matrix": null
}
```

**Reference**: DNG Specification 1.7, Section 6 (Mapping Camera Color Space to CIE XYZ).

#### Gamma / Tone Curve

| Property | Value |
|----------|-------|
| Type | Classical (Halide); AI replacement planned |
| Plugin ID | `cpipe.isp.gamma` |
| Input | RGB 16-bit (linear) |
| Output | RGB 8-bit or 10-bit (gamma-encoded) |
| Device | CPU or GPU |

**Classical implementation**: sRGB transfer function (IEC 61966-2-1). Linear segment below threshold, power function above. Alternative: parametric curve with user-adjustable contrast/brightness.

**AI target**: TGTM -- TinyML Global Tone Mapping (2024). Only 1K parameters, 9 kFLOPS. Takes 256-bin histogram as input, derives tone curve parameters. Resolution-independent. Outperforms SOTA by up to 5.85 dB higher PSNR with orders of magnitude less computation.

**Parameters**:
```json
{
  "curve": "srgb",
  "output_bits": 8,
  "brightness": 0.0,
  "contrast": 0.0
}
```

**References**:
- sRGB: IEC 61966-2-1:1999.
- TGTM (2024): 1K params, 9 kFLOPS, histogram-based tone mapping. arXiv:2405.05016.

## Future AI Nodes

### AI RAW Denoising

| Property | Value |
|----------|-------|
| Architecture | NAFNet-based (Nonlinear Activation Free Network) |
| Plugin ID | `cpipe.ai.denoise` |
| Target params | ~8M (pruned from original) |
| Quantization | INT8 for mobile, FP16 for desktop |
| Milestone | M4 |

**Approach**: U-Net structure without nonlinear activations (multiplication replaces ReLU). Input: noisy RAW Bayer (or post-demosaic RGB). Output: denoised image.

**SOTA context**:
- NAFNet: 40.30 dB PSNR on SIDD, half computation of prior SOTA.
- AIM 2025 winner (MR-CAS): 42.01 PSNR, 8.13M params, 67.36 GMac.
- DualDn (ECCV 2024): Dual-domain (RAW + sRGB) denoising via differentiable ISP. Plug-and-play with real cameras.

**Open source**: [DualDn](https://github.com/OpenImagingLab/DualDn)

### Learned AWB

| Property | Value |
|----------|-------|
| Architecture | Time-aware compact model |
| Plugin ID | `cpipe.ai.awb` |
| Target params | ~5K |
| Inference | 0.25ms (DSP), 0.80ms (CPU) |
| Milestone | M4 |

Uses timestamp + geolocation + image statistics. Trained on 3,224 images with ground-truth illuminants.

### NILUT Neural Color Mapping

| Property | Value |
|----------|-------|
| Architecture | Neural Implicit 3D LUT |
| Plugin ID | `cpipe.ai.nilut` |
| Size | Compact (<1MB) |
| Milestone | M4 |

Implicitly defines a continuous 3D color transformation via a small neural network. Can emulate real 3D LUTs, blend styles, and supports invertible color transforms.

**Open source**: [NILUT](https://github.com/mv-lab/nilut)

### AI Demosaicing (JDD)

| Property | Value |
|----------|-------|
| Architecture | Joint Demosaicing-Denoising with partial convolution |
| Plugin ID | `cpipe.ai.demosaic` |
| Reduction | 83% fewer parameters, 77% fewer MACs vs standard |
| Milestone | M4+ |

Hardware-friendly architecture using multi-scale feature extraction with partial convolution. Designed for on-device inference via SNPE on Snapdragon.

## Future Multi-frame Nodes

### Burst Alignment (M6)

**Approach**: Coarse-to-fine alignment pipeline:
1. Global homography estimation (fast, coarse)
2. Per-tile optical flow refinement (accurate, local)
3. Occlusion detection and masking

**Reference**: NTIRE 2025 Burst HDR challenge (flow-based deformable alignment).

### Multi-frame Fusion (M6)

**Approach**: Kernel prediction network for per-pixel merge weights. Fuses 3-9 aligned RAW frames for noise reduction (temporal averaging) or HDR (exposure bracketing).

**Target constraints**: <30M parameters, <4T FLOPs (per NTIRE 2025 challenge guidelines).

### HDR Tone Mapping (M6)

**Approach**: Two-stage pipeline:
1. **Merge**: Exposure-bracketed frames → HDR linear image
2. **Tone map**: HDR → display-ready image

Classical: Debevec merge + Reinhard tone mapping.
AI target: GMNet gain-map based approach (ICLR 2025), or ultra-fast LUT-based inverse tone mapping.

**Open source**: [GMNet](https://github.com/qtlark/GMNet)

## SOTA References Table

| Area | Method | Params | Latency | Key Metric | Year | Open Source |
|------|--------|--------|---------|------------|------|-------------|
| End-to-end ISP | MicroISP | 158KB | <1s/32MP | - | 2024 | - |
| Adaptive ISP | AdaptiveISP | Light RL | 1ms/stage | Scene-adaptive | 2024 | [GitHub](https://github.com/OpenImagingLab/AdaptiveISP) |
| RAW Denoising | NAFNet-based (AIM 2025) | 8.13M | - | 42.01 PSNR | 2025 | - |
| Dual-domain DN | DualDn | - | - | Plug-and-play | 2024 | [GitHub](https://github.com/OpenImagingLab/DualDn) |
| Low-light RAW | RetinexRAWMamba | - | - | SOTA on SID | 2024 | [GitHub](https://github.com/Cynicarlos/RetinexRawMamba) |
| AWB | Time-aware AWB | ~5K | 0.25ms | - | 2025 | - |
| AWB | Learnable Proj. Matrices | - | 35x faster | - | 2024 | - |
| Tone Mapping | TGTM | 1K | 9 kFLOPS | +5.85 dB PSNR | 2024 | - |
| HDR ITM | GMNet | - | - | Gain-map based | 2025 | [GitHub](https://github.com/qtlark/GMNet) |
| Demosaic | JDD (partial conv) | 83% reduction | - | HW-friendly | 2024 | - |
| Color | NILUT | Compact | - | Neural 3D LUT | 2024 | [GitHub](https://github.com/mv-lab/nilut) |
| Invertible ISP | InvISP | - | - | RAW recovery | 2021 | [GitHub](https://github.com/yzxing87/Invertible-ISP) |
| Unpaired ISP | Lightweight ISP | - | - | -0.3 dB vs paired | 2025 | [GitHub](https://github.com/AndreiiArhire/Learned-Lightweight-Smartphone-ISP-with-Unpaired-Data) |
| Multi-frame HDR | NTIRE 2025 | <30M | <4T FLOPs | 43.22 PSNR | 2025 | - |
| ISP Challenges | AISP Library | - | - | NTIRE/AIM ref | - | [GitHub](https://github.com/mv-lab/AISP) |
| Traditional ISP | openISP | - | - | Full simulation | - | [GitHub](https://github.com/cruxopen/openISP) |

## Image Quality Assessment (IQA)

### Toolchain

[IQA-PyTorch](https://github.com/chaofengc/IQA-PyTorch) -- comprehensive library covering full-reference and no-reference metrics.

### Metrics Used

**Full-Reference (FR)** -- require ground-truth reference image:

| Metric | Description | Use Case |
|--------|-------------|----------|
| PSNR | Peak Signal-to-Noise Ratio | Pixel-level fidelity |
| SSIM | Structural Similarity Index | Structural preservation |
| LPIPS | Learned Perceptual Patch Similarity | Perceptual quality |

**No-Reference (NR)** -- evaluate without ground truth:

| Metric | Description | Use Case |
|--------|-------------|----------|
| NIQE | Natural Image Quality Evaluator | Naturalness |
| MUSIQ | Multi-Scale Universal Perceptual Quality | Human-correlated quality |

### Evaluation Protocol

1. **Per-node evaluation**: Process reference DNG through pipeline, compare each node's output against reference processed by a known-good pipeline (e.g., darktable with default settings).
2. **Full-pipeline evaluation**: Compare final HEIF output against reference.
3. **A/B comparison**: Classical vs AI variant of same node, same input.
4. **Regression testing**: Store baseline IQA scores in CI; fail if PSNR drops >0.5 dB or LPIPS increases >0.01.

### Running IQA

```bash
# From repository root
python tools/iqa/evaluate.py \
    --input output.heif \
    --reference reference.heif \
    --metrics psnr ssim lpips niqe musiq
```

## Mobile Deployment Considerations

### Model Size Budget

| Priority | Budget per node |
|----------|----------------|
| Critical path (denoise, demosaic) | <5 MB |
| Optional enhancement (NILUT, AWB) | <1 MB |
| Total AI model footprint | <20 MB |

### Quantization Strategy

| Platform | Default | Latency-critical |
|----------|---------|-----------------|
| Desktop (ONNX Runtime) | FP32 | FP16 |
| Android GPU (Vulkan) | FP16 | FP16 |
| Android NPU (QNN) | INT8 | INT8 |
| Android CPU (XNNPACK) | FP32 | INT8 |

### Memory Constraints

- Peak allocation tracked by BufferPool profiler
- Target: <500 MB for full-resolution processing on mobile
- Tiling support for nodes that exceed memory budget on large images

### Thermal Budget

- Sustained processing: profile thermal throttling on target devices
- Preview pipeline: must maintain 30 fps within thermal envelope
- Capture pipeline: allowed to run hotter (single-shot, not sustained)
