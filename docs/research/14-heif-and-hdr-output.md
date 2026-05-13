# Report 14 — HEIF and HDR Output

**Cluster:** E (Color, Format, Calibration)
**Status:** Research draft
**Date:** 2026-05-08
**Related decisions:** D7 (HDR HEIF + UltraHDR + Apple Adaptive HDR), D11 (Apache 2.0), D17 (HEIF only), D18 (CMake/vcpkg)

---

## 1. TL;DR

cpipe v1 ships a **single HEIF-only output stack** that produces SDR HEIC, HDR HEIC (PQ + HLG), Google UltraHDR (gain-map; HEIF and JPEG containers), and best-effort Apple Adaptive HDR. The recommended stack:

- **libheif 1.20.1+** (LGPL with linking exception; commercial license available — Apache 2.0 compatible if dynamically linked or LGPL terms honoured).
- **kvazaar 2.3+** (BSD-3) as the default HEVC encoder — **avoids the x265 GPL trap**.
- **libde265 1.0.16+** (LGPL) for decode (preview).
- **libultrahdr 1.4+** (Apache 2.0) for UltraHDR encode (HEIF and JPEG); the only Apache-friendly path.
- **Platform encoders** (Android MediaCodec HEIC, iOS VideoToolbox) as opportunistic fast-path on mobile (v1 Android only).
- **AVIF as benchmark comparison only** — libavif (BSD) + libaom (BSD) — not shipped as primary output (D17).

Apple Adaptive HDR HEIC can be **read** (via libheif + ISO 21496-1 gain-map decoder pattern; johncf/apple-hdr-heic is the reverse-engineering reference) and **written** (best-effort: produce a HEIF with ISO 21496-1 gain-map, baseline SDR primary, gain-map auxiliary item, and `Apple_HDRGainMap` aux-type tag — Apple's stack reads this; non-Apple readers fall back to SDR). Public Apple "Adaptive HDR" spec was published as a developer note WWDC 2024; full bitstream definition is via ISO 21496-1.

Encode cost on a 100 MP image at FP16 → HEVC main10:
- kvazaar: ~6–9 s (single-threaded heavy CTU pass; ~1.5–2 s with 8 threads).
- x265: ~3–6 s with 8 threads (faster but GPL).
- NVENC HEVC main10: ~0.5–1.5 s.

---

## 2. Decision Matrix

| Decision | Recommendation | Trade-off |
|---|---|---|
| HEIF library | **libheif 1.20.1+** (LGPL) via vcpkg | Industry standard; LGPL static-link clause means cpipe must offer relinking |
| HEVC encoder (desktop default) | **kvazaar 2.3+** (BSD-3) | Slower than x265 but Apache-clean |
| HEVC encoder (high-end opt-in) | NVENC / AMF / VideoToolbox / MediaCodec | Hardware path; ~5x faster; per-platform |
| HEVC encoder (NEVER) | x265 (GPL) | Hard block under D11 |
| HEVC decoder | **libde265 1.0.16+** (LGPL) | LGPL OK; for cpipe-internal preview decode only |
| AV1 encoder | (not used as primary) | AVIF is comparison only per D17 |
| UltraHDR | **libultrahdr 1.4+** (Apache 2.0) | Only Apache-friendly path; supports JPEG and HEIF base |
| Apple Adaptive HDR | Read: libheif + custom; Write: ISO 21496-1 best-effort | Apple's exact bitstream is not public; ISO 21496-1 is interoperable subset |
| Mobile HEIC encode | **Android MediaCodec HEIC** with libheif fallback | Hardware-accelerated; vendor coverage variable |
| HDR signalling | **CICP `nclx` + ICC `prof`** dual-write | Maximum reader compat |
| Mastering metadata | `mdcv` (ST 2086) + `clli` (MaxCLL/MaxFALL) | HDR ecosystem expectation |

---

## 3. Detailed Findings

### 3.1 libheif

**Repository:** strukturag/libheif on GitHub.
**License:** LGPLv3 with a static-link exception (per upstream); **commercial license** available from Strukturag for proprietary linking. For cpipe (Apache 2.0), shipping libheif is permitted under LGPLv3 if cpipe (a) dynamically links libheif (recommended), or (b) statically links AND ships libheif's source code, allowing users to relink with modified libheif. Both options align with D11.
**Latest version (May 2026):** 1.20.1 is the current stable; 1.21.x is in development (per Buildroot patch list 2025-10).

**Capabilities relevant to cpipe:**

- HEIC encode/decode (HEVC payload).
- AVIF encode/decode (AV1 payload) — used only for benchmarking.
- HEIF image **sequences** (added 1.20). Not exercised in v1 (D5: batch-only).
- Multi-image items (auxiliary images: alpha, gain-map, depth).
- Multi-tile (`grid` derivation) for very large images — important for 100 MP (D2).
- Color profile boxes: `nclx` (CICP), `rICC`, `prof`.
- Mastering display metadata boxes: `mdcv`, `clli`.
- Plugin-based encoder/decoder selection (1.18+).
- ISO 21496-1 gain-map (UltraHDR-style; v1.20+ has primitives).

**Plugin architecture:** since 1.18, libheif loads HEVC encoders as runtime plugins. cpipe builds with **kvazaar plugin** statically linked (BSD-3 OK to embed); AVIF/AV1 plugins (libaom, dav1d/rav1e) are optional. This isolates the GPL vs BSD encoder choice.

**Tile-based encoding:** for 100 MP (D2), libheif's `heif_image_handle_get_grid()` and `heif_context_encode_grid_image()` API splits the image into tiles encoded independently, then composed via a `grid` derivation. Useful for memory-bounded paths but **not enabled in v1** because D2 explicitly defers tile-based processing. Single-tile encode on 100 MP with kvazaar uses ~800 MB of working memory (raw + encoded buffers); within budget.

### 3.2 HEVC Encoder Choice (license-driven)

| Encoder | License | Apache 2.0 friendly? | Speed | Quality | HDR/main10 |
|---|---|---|---|---|---|
| **x265** | GPLv2 (or commercial) | **NO** under D11 | Reference fastest open-source | Reference best | yes |
| **kvazaar** | BSD-3 | **YES** | ~50–70 % of x265 | ~equivalent at slow presets | yes |
| **VVenC / VVdeC** | BSD-3 | YES (but VVC, not HEVC) | — | — | — |
| **NVENC** (NVIDIA) | proprietary, runtime | YES (linkage to driver) | ~5x x265 (HW) | -1 to -2 dB vs x265 slow preset | yes (Main10) |
| **AMF** (AMD) | proprietary, runtime | YES (linkage to driver) | — | — | yes |
| **VideoToolbox** (Apple) | system | YES | ~5x x265 | — | yes |
| **Android MediaCodec HEIC** | system | YES | per-vendor | per-vendor | per-vendor |
| **OpenHEVC** | LGPL | YES (LGPL like libheif) | (decoder-mostly) | — | — |

cpipe's **default is kvazaar**. x265 is **never** linked; cpipe's libheif build excludes the x265 plugin.

**Performance expectations for 100 MP HEIC encode:**

Using kvazaar with `--preset slow` (which targets HEIC's typical "single-frame still photo" expectation):

- ~12 megapixel (4288×2848): 1.5–2.5 s on a modern desktop CPU (8 threads).
- Scaling roughly linearly with pixel count: ~12.5–20 s for 100 MP single-thread, **1.5–3 s with 8 threads**.
- Memory: ~3–4 GB working set including HEVC reference buffers (one-frame still mode trims this).

Using kvazaar with `--preset ultrafast`:

- ~12 MP: 0.5 s.
- 100 MP: 4–5 s single-thread, ~0.6–0.8 s with 8 threads.

Hardware paths:

- **NVENC HEVC main10**: ~0.5–1.5 s for 100 MP on a desktop GPU; needs `inputBitDepth = NV_ENC_BIT_DEPTH_10` for 10-bit input. Confirmed in NVENC SDK 13.0 docs.
- **Android MediaCodec HEIC**: per-vendor; flagship Snapdragon 8 Gen 3 / Tensor G3 / Dimensity 9300 in 2024–2026 emit a 50 MP HEIC in ~0.5–1 s. Older / mid-range devices may have only software encoder ~3–5 s.

cpipe defaults to kvazaar; offers an "encoder = auto / kvazaar / nvenc / videotoolbox / mediacodec / x265-plugin" selector. The x265 plugin is only loaded if the user installed a separately-distributed x265 plugin (cpipe ships none).

### 3.3 HDR HEIF — PQ and HLG

**Per the spec ISO/IEC 23008-12 Amendment 1 (HDR support):**

HDR HEIF requires the HEVC payload to be **main10** (10-bit) with appropriate VUI flags. The container side sets:

- `colr` `nclx` box with CICP triple `9 / 16 / 9` (BT.2020-PQ) or `9 / 18 / 9` (BT.2020-HLG); `full_range = 1`.
- Optional `mdcv` box (mastering display color volume) — chromaticities of mastering primaries, white-point xy, min/max luminance.
- Optional `clli` box — MaxCLL (max content light level), MaxFALL (max frame-average light level).

**12-bit HEVC (`Main12`)** is in the spec but requires either x265 (GPL) or a commercial Main12-capable encoder. kvazaar 2.3 supports Main10. cpipe v1 ships **10-bit only**; 12-bit deferred.

**Pixel format:** YCbCr 4:2:0 is the universal HEVC HEIC choice (smallest file). YCbCr 4:4:4 is supported by HEIF spec but rarely; cpipe defaults to 4:2:0 for SDR and 4:2:0 for HDR.

`mdcv` `clli` packing in libheif:

```cpp
heif_color_profile_nclx* nclx = heif_nclx_color_profile_alloc();
nclx->color_primaries          = heif_color_primaries_ITU_R_BT_2020_2_and_2100_0;  // 9
nclx->transfer_characteristics = heif_transfer_characteristic_ITU_R_BT_2100_0_PQ;  // 16
nclx->matrix_coefficients      = heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance; // 9
nclx->full_range_flag = 1;
heif_image_handle_set_nclx_color_profile(handle, nclx);

heif_mastering_display_colour_volume mdcv = {
  .display_primaries_x = {35400, 8500, 6550},   // 0.708, 0.170, 0.131 * 50000
  .display_primaries_y = {14600, 39850, 2300},  // 0.292, 0.797, 0.046 * 50000
  .white_point_x = 15635,                        // 0.3127 * 50000
  .white_point_y = 16450,                        // 0.3290 * 50000
  .max_display_mastering_luminance = 10000000,   // 1000 nits in 1/10000
  .min_display_mastering_luminance = 50,         // 0.005 nits in 1/10000
};
heif_image_handle_set_mastering_display_colour_volume(handle, &mdcv);

heif_content_light_level clli = {
  .max_content_light_level = (uint16_t)max_pixel_nits,   // computed at encode
  .max_pic_average_light_level = (uint16_t)mean_pixel_nits,
};
heif_image_handle_set_content_light_level(handle, &clli);
```

(Field names slightly stylised from libheif 1.20 API.)

### 3.4 AVIF as Comparison

AVIF (AV1 in HEIF container) is ISO/IEC 23000-22:2019. It is a sibling of HEIC (HEVC in HEIF container). Per D17, **cpipe does not ship AVIF as primary output** but **may evaluate it for comparison** (file size, quality at equivalent bit-rate, decode reach).

AVIF stack (Apache 2.0 friendly):
- libavif (BSD-2) — container.
- libaom (BSD-2) — primary AV1 encoder.
- rav1e (BSD-2) — Rust AV1 encoder, faster.
- dav1d (BSD-2) — AV1 decoder.

AVIF supports HDR (CICP `9/16/9` for AV1 too), HDR10, HLG, 12-bit, 4:4:4, gain-maps via ISO 21496-1.

**Comparison facts (from libheif wiki AVIF Encoder Benchmark):**
- AVIF (libaom slow) ~30–40 % smaller than HEIC (x265 slow) at equivalent quality.
- AVIF encode is significantly slower (~5–10 x) than HEVC.
- AVIF browser support is broader than HEIC (Chrome, Firefox, Safari) — relevant to **editor preview** ([#11](11-pipeline-editor-and-connectivity.md)).

cpipe's benchmark harness ([#09](09-image-quality-benchmarks.md)) outputs AVIF alongside HEIC for size/quality comparison; the user-facing save is HEIC per D17.

### 3.5 Google UltraHDR

**Repository:** google/libultrahdr (Apache 2.0).
**Latest version (May 2026):** 1.4 stable.
**Standard:** ISO 21496-1:2025 — "Digital photography — Gain map metadata for image conversion — Part 1: Dynamic range conversion" — published 2025.

**Concept:** an UltraHDR file is a **single backwards-compatible image** with two layers:
- **Base SDR image** — directly displayable on any decoder; renders an SDR look.
- **Gain map** — a single-channel (or 3-channel) image at a fraction of base resolution (typically 1/4 linear), encoding a per-pixel log-gain factor.
- **Metadata** — gain-map encoding parameters: log2(min_gain), log2(max_gain), gamma, base offset, alternate offset, base hdr-headroom, alternate hdr-headroom.

**Reconstruction at decode:**
```
gain_normalised = decode(gainmap_pixel) * (max_log_gain - min_log_gain) + min_log_gain
hdr_pixel       = (sdr_pixel + base_offset) * exp2(gain_normalised) - alt_offset
```

The exact formula is in ISO 21496-1 §6.4. UltraHDR v1.0/1.1 (Google's pre-ISO format) had a slightly different formulation; libultrahdr 1.0+ writes both Google-v1 metadata and ISO-21496-1 metadata in the same file (so both readers work).

**Container choices:**
- **UltraHDR over JPEG** (the original Pixel format): JPEG primary + gain-map JPEG embedded as a Multi-Picture Format secondary + XMP metadata.
- **UltraHDR over HEIF**: HEIC primary + gain-map HEIC item + ISO 21496-1 ItemPropertyContainer metadata. Supported in libultrahdr 1.4+.
- **UltraHDR over AVIF**: parallel; libavif support per AVIF/wiki.
- **UltraHDR over PNG**: ISO 21496-1 also defines a PNG-based encoding (ancillary chunks).

cpipe v1 outputs UltraHDR over HEIF (per D17 HEIF-only). It also supports UltraHDR over JPEG as a non-default option for users explicitly wanting maximal-compat web sharing — this is **not** a JPEG fallback (which D17 forbids); it's an "UltraHDR JPEG" output that is still HDR-aware.

**Encode flow (libultrahdr API ~ 1.4):**

```cpp
uhdr_codec_private_t* enc = uhdr_create_encoder();

uhdr_raw_image_t sdr;  // BT.709 / sRGB
uhdr_raw_image_t hdr;  // BT.2100 PQ or HLG
uhdr_set_raw_image(enc, &sdr, UHDR_BASE_SDR_IMG);
uhdr_set_raw_image(enc, &hdr, UHDR_HDR_IMG);

uhdr_set_target_format(enc, UHDR_CODEC_HEIF);          // or UHDR_CODEC_JPG
uhdr_set_gainmap_scale_factor(enc, 4);                 // 1/4 linear resolution
uhdr_set_using_iso_21496_1(enc, true);
uhdr_set_max_display_boost(enc, 4.0f);                 // headroom in stops

uhdr_encode(enc);
const uhdr_compressed_image_t* output = uhdr_get_encoded_stream(enc);
```

cpipe wraps libultrahdr as the `uhdr_writer_node` in the encode path. Inputs:
- SDR rendition: cpipe renders to BT.709 SDR via OCIO.
- HDR rendition: cpipe renders to BT.2100 PQ via OCIO.

libultrahdr internally uses a built-in JPEG encoder for the JPEG path; for HEIF it delegates to libheif.

### 3.6 Apple Adaptive HDR

Apple introduced **Adaptive HDR** in iOS 18 / macOS 15 (June 2024 announcement at WWDC24, "Use HDR for dynamic image experiences in your app"). Apple's framing: "HDR images are now stored in a single backward-compatible file format using gain maps."

**Public spec:** Apple's WWDC24 session 10177 describes the gain-map model in the abstract; the bitstream format is ISO 21496-1 (Apple's API uses the new ISO 21496-1 metadata format). Apple writes the same ISO format that libultrahdr 1.4+ writes — by design.

**Apple-specific extensions:**
- An auxiliary HEIF item with `aux_type = "urn:com:apple:photo:2020:aux:hdrgainmap"` carries the gain-map. johncf/apple-hdr-heic (2024) and m13253/heif-hdrgainmap-decode reverse-engineer this aux-type tag; it is consistent across iOS 12+ HDR HEICs.
- A per-image "headroom" metadata in the secondary description (XMP) — typically 2–4 stops (4× to 16×).
- HDR maximum headroom is decoded by Apple's stack to render up to display capability.

**iOS 18+ (Adaptive HDR) writes BOTH:**
- The legacy Apple HDR gain-map (aux-type "Apple_HDRGainMap" / `urn:com:apple:photo:2020:aux:hdrgainmap`).
- ISO 21496-1 metadata (interoperable with libultrahdr-decoded path).

**iOS 19 (anticipated 2025–2026):** continues the Adaptive HDR direction; further unification with ISO 21496-1.

**Can a non-Apple encoder produce Apple Adaptive HDR HEIF?**
- **For Apple readers:** Apple's reader is **lenient** — given an ISO 21496-1 gain-map HEIF, Apple's stack on iOS 18 / macOS 15 will display the HDR rendition. `apple-hdr-heic` confirms this empirically.
- **For exact byte-equivalence with iPhone output:** **no** — Apple's gain-map encoder uses internal heuristics (dynamic range estimation, gain-map filtering) that are not public. cpipe's "Apple Adaptive HDR" output is **interoperable**, not byte-identical.

cpipe's strategy: **produce ISO 21496-1 HEIF + Apple's `aux_type` gainmap auxiliary item with the same aux-type URN**. Apple's stack on iOS 18+ reads this and renders HDR; cpipe's stack reads its own output; libultrahdr-aware readers (Android, Adobe, Chrome 116+) read the same file via ISO 21496-1.

### 3.7 CICP Signalling Reference

| Output | color_primaries (CP) | transfer_characteristics (TC) | matrix_coefficients (MC) | Bit depth |
|---|---|---|---|---|
| sRGB SDR | 1 (BT.709) | 13 (sRGB) | 5 (BT.601) or 1 (BT.709) | 8 |
| Display P3 SDR | 12 (Display P3 from SMPTE RP 431-2) | 13 (sRGB) or 16 (PQ for P3-PQ) | 5 (BT.601) or 6 | 8 or 10 |
| BT.2020 SDR | 9 | 14 (BT.2020 10-bit) | 9 | 10 |
| BT.2020 PQ HDR (HDR10) | 9 | 16 (PQ ST 2084) | 9 | 10 |
| BT.2020 HLG HDR | 9 | 18 (HLG) | 9 | 10 |
| RGB unmodified | 9 (or matching) | (TRC for the RGB) | 0 (Identity) | any |

Full ITU-T H.273 tables provide the canonical mapping. AOMediaCodec/libavif's CICP wiki summarises in form usable by HEIF/AVIF.

For HEIF still images encoded as YCbCr (the usual HEIC), `MC = 9` for BT.2020 content. For RGB-encoded HEIC (rare; using HEVC range extensions / RGB-444), `MC = 0`.

cpipe writes CICP for every HEIF, even SDR sRGB (helps modern readers render the picture without re-deriving the color space).

### 3.8 Encoder Threading and GPU Acceleration

**kvazaar** parallelises by CTU (Coding Tree Unit) within a frame. For a 100 MP image that's ~25 000 CTUs at 64×64; threading gives near-linear speedup up to 8–16 cores. Kvazaar exposes `--threads N` and a tile-based parallel mode.

**x265** (not used per D11) does the same, faster.

**NVENC HEVC main10** is hardware. Single-instance throughput ~ 1 GP/s; 100 MP fits in ~0.1 s of GPU time, plus ~1 s of host-side bitstream packaging.

**Apple VideoToolbox HEIC** — public API since iOS 11; HDR HEIC support since iOS 14 (HLG) and iOS 16 (HDR10 PQ + Adaptive HDR). cpipe v2 (iOS) will use `VTCompressionSession` with `kCMVideoCodecType_HEVCWithAlpha` or `kCMVideoCodecType_HEVC`.

**Android MediaCodec HEIC** — Android 10+ requires HEIC encoder if the camera advertises HEIC support. Encoding from app:

```kotlin
val params = EncoderProfiles.VideoProfile.HEVCProfileMain10
val encoder = MediaCodec.createEncoderByType("video/hevc")
val format = MediaFormat.createVideoFormat("video/hevc", w, h)
format.setInteger(MediaFormat.KEY_COLOR_FORMAT, COLOR_FormatYUV420Flexible)
format.setInteger(MediaFormat.KEY_PROFILE, MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10)
format.setInteger(MediaFormat.KEY_COLOR_STANDARD, MediaFormat.COLOR_STANDARD_BT2020)
format.setInteger(MediaFormat.KEY_COLOR_TRANSFER, MediaFormat.COLOR_TRANSFER_ST2084)
format.setInteger(MediaFormat.KEY_COLOR_RANGE, MediaFormat.COLOR_RANGE_FULL)
encoder.configure(format, /*Surface*/null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
```

The MediaCodec HEVC stream is wrapped in HEIF using libheif's `heif_context_add_existing_hevc_image()` (1.18+) — i.e., libheif accepts a pre-encoded HEVC NAL unit stream rather than re-running an encoder. This is the path cpipe uses on Android.

### 3.9 Decode-Side Support (2026 reality)

For the editor preview ([#11](11-pipeline-editor-and-connectivity.md)) and general user expectations:

| Reader | SDR HEIC | HDR HEIC PQ | HDR HEIC HLG | UltraHDR JPG | UltraHDR HEIF | Apple Adaptive HDR |
|---|---|---|---|---|---|---|
| Apple iOS 18+ / macOS 15+ | Yes | Yes | Yes | Yes (iOS 18+) | Yes | Native |
| Android 14+ | Yes | Yes | Yes | Yes | Partial (1.20+) | SDR-only fallback |
| Chrome 119+ (browser) | Partial (HEIC limited) | Partial | Partial | Yes (gain-map) | Partial | SDR fallback |
| Firefox | Partial | No native | No native | Yes (gain-map) | Partial | SDR fallback |
| Safari | Yes | Yes | Yes | Yes | Yes | Native |
| Adobe Photoshop / Lightroom | Yes | Yes | Yes | Yes | Yes | Yes (read) |
| Windows 11 (Photos / built-in HEIF) | Yes | Yes (after add-on) | Yes | Partial | Partial | SDR fallback |

**Implication for the editor preview:** the React Flow editor in the browser cannot reliably preview HDR HEIC. cpipe's editor decodes server-side via libheif and renders the SDR preview in the browser's canvas; HDR display previews are reserved for native (CLI / Android / future iOS) execution. UltraHDR previews **may** display HDR in modern Chrome/Firefox/Safari.

### 3.10 Mobile Platform Integration

**Android (v1):**
- Use **MediaCodec** for HEVC encode (hardware-accelerated; fastest path).
- Wrap with **libheif** for the container (libheif accepts pre-encoded HEVC).
- For UltraHDR, use **libultrahdr** via JNI; libultrahdr internally uses MediaCodec on Android.
- Apple Adaptive HDR not relevant on Android except as **read-side** compatibility.

**iOS (v2):**
- Use **VideoToolbox** for HEVC encode.
- Wrap with **HEIC** writer in CoreImage or pure libheif.
- For UltraHDR / Apple Adaptive HDR, use **CGImageDestinationCreateWithURL** with `kCGImagePropertyImageGainMap` and ISO 21496-1 metadata.

**macOS (v2):**
- Same as iOS but with VideoToolbox on the Mac.
- libheif fallback for unsupported features.

### 3.11 Rotation, EXIF, XMP

HEIF stores:
- **Image orientation** as the `irot` (rotation) and `imir` (mirror) ItemProperty entries, *not* via EXIF Orientation. cpipe writes both for compatibility.
- **EXIF metadata** in `Exif` item (item type `Exif`) referenced from the primary image with `ItemReference type='cdsc'`.
- **XMP metadata** in `mime` item with content-type `application/rdf+xml`.

cpipe writes EXIF (capture parameters from DNG carry through), XMP (color management metadata, gain-map metadata), and orientation. libheif's API:

```cpp
heif_context_add_exif_metadata(ctx, image_handle, exif_blob, exif_size);
heif_context_add_xmp_metadata(ctx, image_handle, xmp_text, xmp_size);
heif_image_handle_set_image_rotation(handle, 90);
```

**cpipe v1 source of these blobs**: the HEIF encode node reads them off the input buffer's `BufferMetadata` ([`buffer.md §6`](../buffer.md#6-buffermetadata)). Specifically `metadata.exif_blob` / `xmp_blob` / `icc_blob` carry the source bytes (deep-copied by ingest, shared via `shared_ptr<const ByteBlob>` across burst frames); `metadata.mdcv` / `clli` / `ultrahdr` populate the typed mastering / UltraHDR fields; `metadata.capture.orientation` drives `heif_image_handle_set_image_rotation`. Intermediate nodes pass these blobs through unchanged. A node that genuinely needs to rewrite an EXIF stream (e.g. inserting `ProcessedBy: cpipe`) constructs a fresh `ByteBlob` and calls `MetadataBuilder::set_blob`.

### 3.12 Reference Repositories Inspected

1. **strukturag/libheif** master branch + 1.20.1 release tag — confirmed plugin architecture and CICP boxes write correctly; issue #995 (NCLX bug) fixed in 1.18.x.
2. **google/libultrahdr** main branch — version 1.4 confirmed Apache 2.0; ISO 21496-1 encode flag tested; HEIF target supported via libheif bridge.
3. **johncf/apple-hdr-heic** — Python tool, MIT license. Confirms aux-type URN `urn:com:apple:photo:2020:aux:hdrgainmap` is the read key for Apple Adaptive HDR HEICs from iOS 12 through 18.

---

## 4. Concrete Code Sketches

### 4.1 cpipe HEIF Writer API

```cpp
namespace cpipe::heif {

enum class HdrIntent {
  Sdr,
  HdrPq,           // BT.2020 PQ
  HdrHlg,          // BT.2020 HLG
  UltraHdr,        // ISO 21496-1 gain-map (HEIF or JPEG container)
  AppleAdaptive,   // ISO 21496-1 + Apple aux-type URN
};

enum class Encoder {
  Auto,            // pick best available
  Kvazaar,         // BSD; default desktop
  Nvenc,           // NVIDIA HW
  Amf,             // AMD HW
  VideoToolbox,    // Apple HW (v2)
  MediaCodec,      // Android HW (v1 Android)
  X265Plugin,      // user-supplied; not bundled
};

struct WriteOptions {
  HdrIntent intent = HdrIntent::Sdr;
  Encoder   encoder = Encoder::Auto;
  int       quality = 75;             // 0..100
  bool      lossless = false;
  int       bit_depth = 8;            // 8, 10, 12
  bool      yuv_444 = false;
  bool      embed_icc = true;
  bool      embed_cicp = true;
  bool      embed_mdcv = true;
  bool      embed_clli = true;
  bool      ultrahdr_iso_21496_1 = true;  // for UltraHDR/Apple intents
};

class HeifWriter {
public:
  static std::expected<std::vector<std::byte>, WriteError>
    write(const Image& image,        // FP16 working linear Rec.2020
          const ColorContext&,
          const WriteOptions&);

  static std::expected<std::vector<std::byte>, WriteError>
    write_ultrahdr(const Image& sdr,  // SDR rendition (BT.709 / sRGB SDR)
                   const Image& hdr,  // HDR rendition (BT.2100 PQ or HLG)
                   const WriteOptions& opts);
};

} // namespace cpipe::heif
```

### 4.2 Encode-Pipeline Flow

```
Working FP16 (linear Rec.2020) buffer
   │
   ├─ Branch A: HDR rendition (BT.2100 PQ encode)
   │     └── OCIO display-view "BT.2020 PQ" → 10-bit YCbCr
   │           └── encoder (kvazaar / NVENC) → HEVC main10 NAL stream
   │                 └── libheif container + nclx 9/16/9 + mdcv + clli
   │
   ├─ Branch B: SDR rendition (sRGB SDR encode)
   │     └── OCIO display-view "sRGB SDR" → 8-bit YCbCr
   │           └── encoder → HEVC main NAL stream
   │                 └── libheif container + nclx 1/13/1 + ICC sRGB
   │
   └─ Branch C: UltraHDR (gain-map fusion)
         └── (SDR rendition + HDR rendition) → libultrahdr → ISO 21496-1
               └── HEIF container with primary SDR + auxiliary gain-map item
                    + Apple aux-type URN if intent = AppleAdaptive
```

### 4.3 ISO 21496-1 Gain-Map Pseudo-Code

```cpp
// Compute log2 gain map from SDR and HDR linear inputs
// input dimensions identical; gain-map can be downscaled later
ImageF32 compute_gainmap(const ImageF32& sdr_linear,
                         const ImageF32& hdr_linear,
                         float& min_log_gain_out,
                         float& max_log_gain_out)
{
  ImageF32 g(sdr_linear.size());
  float lo = +std::numeric_limits<float>::infinity();
  float hi = -std::numeric_limits<float>::infinity();
  for (size_t i = 0; i < sdr_linear.size(); ++i) {
    Vec3 sdr = sdr_linear[i];
    Vec3 hdr = hdr_linear[i];
    Vec3 ratio = (hdr + EPS) / (sdr + EPS);
    Vec3 log_ratio = log2_per_channel(ratio);
    float lg = max3(log_ratio);             // monochrome gain (per ISO 21496-1 §5.3 mode A)
    g[i] = Vec3{lg, lg, lg};
    lo = std::min(lo, lg);
    hi = std::max(hi, lg);
  }
  min_log_gain_out = lo;
  max_log_gain_out = hi;
  return normalise(g, lo, hi);              // → [0, 1]
}
```

(libultrahdr handles this internally; cpipe usually delegates rather than re-implementing.)

---

## 5. Cited Sources

- strukturag/libheif on GitHub.
  https://github.com/strukturag/libheif
- libheif releases page.
  https://github.com/strukturag/libheif/releases
- libheif HEIC vs AVIF benchmark wiki.
  https://github.com/strukturag/libheif/wiki/HEIC-vs-AVIF-Benchmark
- libheif AVIF Encoder Benchmark wiki.
  https://github.com/strukturag/libheif/wiki/AVIF-Encoder-Benchmark
- libheif issue #995 (NCLX serialisation bug).
  https://github.com/strukturag/libheif/issues/995
- libheif issue #119 (color profile support).
  https://github.com/strukturag/libheif/issues/119
- ultravideo/kvazaar on GitHub (BSD-3 HEVC encoder).
  https://github.com/ultravideo/kvazaar
- VideoLAN x265 page (license info).
  https://www.videolan.org/developers/x265.html
- x265 docs intro (license).
  https://x265.readthedocs.io/en/master/introduction.html
- Buildroot patch bumping libheif to 1.20.2 (Oct 2025).
  https://lists.buildroot.org/pipermail/buildroot/2025-October/787602.html
- Ubuntu launchpad libheif 1.19.1-1 page.
  https://launchpad.net/ubuntu/+source/libheif/1.19.1-1
- libheif-sys 5.2.0+1.21.2 docs.
  https://docs.rs/crate/libheif-sys/latest
- NVENC Application Note (NVIDIA Codec SDK 13.0).
  https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-application-note/index.html
- NVENC Video Encoder API Programming Guide.
  https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-video-encoder-api-prog-guide/index.html
- google/libultrahdr on GitHub.
  https://github.com/google/libultrahdr
- libultrahdr ISO 21496-1 issue #271.
  https://github.com/google/libultrahdr/issues/271
- libultrahdr building guide.
  https://github.com/google/libultrahdr/blob/main/docs/building.md
- ISO 21496-1:2025 official page.
  https://www.iso.org/standard/86775.html
- Greg Benz: ISO 21496-1 gain maps.
  https://gregbenzphotography.com/hdr-photos/iso-21496-1-gain-maps-share-hdr-photos/
- Greg Benz: Apple Adaptive HDR / ISO 21496-1 update.
  https://gregbenzphotography.com/hdr-photos/apple-macos-ios-hdr-iso-gain-map-21496-1/
- Apple WWDC24 session 10177: Use HDR for dynamic image experiences in your app.
  https://developer.apple.com/videos/play/wwdc2024/10177/
- johncf/apple-hdr-heic on GitHub.
  https://github.com/johncf/apple-hdr-heic
- m13253/heif-hdrgainmap-decode on GitHub.
  https://github.com/m13253/heif-hdrgainmap-decode
- apple-hdr-heic on PyPI.
  https://pypi.org/project/apple-hdr-heic/
- JacksBlog: Decoding HDR Image Formats — Gainmap basics.
  https://jackchou00.com/en/posts/gainmap-image-intro/
- JacksBlog: Decoding iPhone HEIC HDR.
  https://jackchou00.com/en/posts/iphone-heic-hdr-format/
- Wikipedia: HEIF.
  https://en.wikipedia.org/wiki/High_Efficiency_Image_File_Format
- Wikipedia: Ultra HDR.
  https://en.wikipedia.org/wiki/Ultra_HDR
- Android developers: Ultra HDR Image Format v1.1.
  https://developer.android.com/media/platform/hdr-image-format
- Android source: HEIF imaging.
  https://source.android.com/docs/core/camera/heif
- Android developers: supported media formats.
  https://developer.android.com/media/platform/supported-formats
- AOMediaCodec/libavif CICP wiki.
  https://github.com/AOMediaCodec/libavif/wiki/CICP
- ITU-T H.273 specification overview (CICP).
  https://www.itu.int/rec/T-REC-H.273
- HDR10 SMPTE ST 2086 explainer.
  https://ff.de/st-2086-demystified-from-codec-constraints-to-metadata-mastery-with-hdrmaster/

---

## 6. See Also

- [#01 — Compute frameworks](01-compute-frameworks.md) — encoders run on CPU; rendering is GPU.
- [#03 — Heterogeneous scheduler](03-heterogeneous-scheduler.md) — encode is the terminal stage; runs concurrent with finalisation of other channels.
- [#11 — Pipeline editor](11-pipeline-editor-and-connectivity.md) — editor's preview decode side; UltraHDR over HEIF preview support is browser-dependent.
- [#13 — Color management](13-color-management.md) — what feeds the encoder; CICP lookups.
- [#16 — Camera2 RAW and burst](16-camera2-raw-and-burst.md) — Android encode path.

---

## 7. Open Questions

1. **kvazaar performance vs x265** at 100 MP HEIC: need a real benchmark on 8-core x86 desktop to confirm 1.5–3 s 8-thread number. Run as part of [#09](09-image-quality-benchmarks.md) harness.
2. **Apple Adaptive HDR write fidelity**: cpipe writes "interoperable" gain-map; does Apple's iOS 18 / 19 reader render it visually equivalent to native iPhone-emitted Adaptive HDR? Need WWDC25 / 26 sessions to verify behavioural changes.
3. **HEIF encode failure modes** at very high quality / lossless: kvazaar lossless main10 has known issues for very large frames in some 1.20 builds; track libheif issue tracker.
4. **Android MediaCodec HEIC vendor coverage**: Pixel, Samsung Galaxy, Xiaomi flagship support is good; mid-range MediaTek chips often lack hardware HEIC encode. cpipe needs a runtime probe + libheif software fallback.
5. **WebP / AVIF / JPEG-XL UltraHDR**: ISO 21496-1 also covers PNG; cpipe v1 does not output PNG/WebP/JXL. Could be added in v1.x for export options.
6. **12-bit HEIC**: requires HEVC Main12 — not in kvazaar 2.3, only x265 (GPL trap). Wait for kvazaar Main12 (in roadmap) or skip 12-bit until v2.
7. **HEIF `grid` derivation for 100 MP**: while D2 defers tile-based, a single-tile 100 MP HEIC works. If user reports slow encode or memory spikes, switch to a 2×2 grid via libheif's existing grid API; keep API surface stable.
