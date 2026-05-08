# Report 12 — DNG Format

**Cluster:** E (Color, Format, Calibration)
**Status:** Research draft
**Date:** 2026-05-08
**Related decisions:** D2 (100 MP max), D10 (DNG metadata only v1), D11 (Apache 2.0), D12 (Bayer + Quad Bayer), D18 (CMake/vcpkg)

---

## 1. TL;DR

DNG (Digital Negative) is Adobe's open raw format built on TIFF/EP. Version **1.7.1.0** (current as of 2026, SDK build 2536, April 2026) adds JPEG XL payload compression and refines the GainMap opcode. For cpipe v1 the relevant facts are:

1. **DNG carries an executable program** in `OpcodeList1/2/3` that the renderer must run before/after linearization and after demosaic. Skipping opcodes corrupts color and geometry on smartphone DNGs.
2. **License-clean parsing path = LibRaw (LGPL 2.1 with static-link option) + custom opcode interpreter**, or Adobe DNG SDK 1.7.1 (Adobe-friendly EULA, Apache-compatible static linking permitted but verification needed). exiv2 (GPLv2) and dt-dng (GPL) are **license traps** under D11.
3. **Quad Bayer** (Sony Quadra, Samsung Tetracell, OmniVision 4-cell) is encoded in DNG as a 4×4 `CFAPattern` with `CFARepeatPatternDim = (4,4)`. Most flagship phones (iPhone 16 Pro, Galaxy S24, Pixel 8) write a remosaiced 2×2 Bayer DNG by default; native 4×4 DNGs exist behind manual modes.
4. **Recommended stack:** LibRaw 0.22 (or 202502 snapshot) for TIFF/EP parsing + a hand-written opcode interpreter (~1.5 kLoC C++20) that emits a `cpipe::Pipeline<Opcode>` queue consumed by the scheduler in [#03](03-heterogeneous-scheduler.md).
5. **Pipeline order is fixed by the spec:** OpcodeList1 → LinearizationTable → OpcodeList2 → black/white scaling → CFA inspection → demosaic → OpcodeList3 → ColorMatrix → working color space.

---

## 2. Decision Matrix

| Decision | Recommendation | Trade-off |
|---|---|---|
| DNG spec version target | **1.7.1.0** with backwards compat to 1.4 | Captures iPhone 16 Pro / Galaxy S24 JXL DNGs and historical archives |
| TIFF parser | **libtiff 4.7.0** (BSD-style) via vcpkg | Mature; only the substrate, all DNG semantics built above |
| DNG metadata parser | **LibRaw 0.22 / 202502** (LGPL with static-link option) | Apache 2.0 compatible if shipping LGPL binary or sources; exiv2 GPL is OFF |
| Opcode interpreter | **Custom C++20** (~1.5 kLoC) | Adobe SDK avoids work but adds licensing review; custom is portable, FP16-friendly |
| JPEG XL DNG payload | **libjxl 0.10+** (Apache 2.0) | Required for DNG 1.7 modern phones; vcpkg port available |
| Quad Bayer | **Read 4×4 CFAPattern**; v1 demosaic remosaics to 2×2; v2 native | Native QB demosaic is research-grade; remosaic is industry default |
| Validation | `dng_validate` (from Adobe SDK) for round-trip tests | Reference tool; Adobe license but only used in CI |

---

## 3. Detailed Findings

### 3.1 The DNG Specification Lineage

DNG was published by Adobe in September 2004 to provide an open archival format for digital camera raw files. It is built on TIFF 6.0 plus TIFF/EP (ISO 12234-2:2001), the standard previously used for proprietary-but-similar raw formats. The format is now ISO-standardised as **ISO 12234-4** and Adobe maintains the canonical reference in PDF form.

**Version timeline:**

- 1.0.0.0 (Sep 2004) — initial release.
- 1.1.0.0 (Feb 2005) — ProfileEmbedPolicy, Look tables.
- 1.3.0.0 (Jun 2009) — **OpcodeList1/2/3 introduced**; the watershed change because it allowed cameras to emit "this image is DNG, but apply these correction passes".
- 1.4.0.0 (Sep 2012) — ProfileHueSatMap dimensions tag, NoiseProfile (replacing NoiseReductionApplied as the canonical profile), ForwardMatrix.
- 1.5.0.0 (Jun 2019) — minor. NewSubFileType bits.
- 1.6.0.0 (Dec 2021) — TimeCodes, FrameRate, Camera-RawProfile auxiliary tags, Sensor depth-map (RAID-style multi-plane RAW), PostScale and BeforeScale clarifications.
- **1.7.0.0 (Jun 2023)** — JPEG XL (`JXL`) compression scheme; lossy DNG support; refined GainMap opcode (handling of values outside the map).
- **1.7.1.0 (2024–2026)** — bug-fix and clarification release; Adobe DNG SDK 1.7.1 build 2536 ships April 2026; consolidated camera-profile changes.

Adobe's DNG Specification PDF is the canonical reference. The most recent fully public PDF is 1.6.0.0 mirrored at paulbourke.net; 1.7 is described in Adobe's release notes and reverse-engineered by community implementations like RawTherapee's tracker thread (#7131) and Cloudinary's blog.

DNG is a **TIFF flavour**: every DNG is a valid TIFF 6.0 file. The metadata tag set is a strict superset of TIFF/EP. Conformance:

- TIFF 6.0: PhotometricInterpretation, ImageWidth, ImageLength, BitsPerSample, Compression, StripOffsets/StripByteCounts (or TileOffsets/TileByteCounts), Orientation, SamplesPerPixel.
- TIFF/EP: CFAPattern, CFARepeatPatternDim, CFAPlaneColor, CFALayout (1=rectangular), Make, Model, ExifVersion, ExposureTime, ISO speed, FocalLength.
- DNG-specific: DNGVersion (`1.7.1.0`), DNGBackwardVersion, DNGPrivateData, all the color-calibration tags below.

### 3.2 Required Tags for cpipe Input

A DNG that cpipe must accept will carry, at minimum, these tags (TIFF tag IDs in parentheses):

| Tag | ID | Purpose |
|---|---|---|
| `DNGVersion` | 50706 | Indicates DNG conformance; cpipe rejects ≤ 1.0 |
| `PhotometricInterpretation` | 262 | **32803 = CFA** (mosaiced) or **34892 = LinearRaw** (already demosaiced) |
| `CFAPattern` | 33422 (TIFF/EP) / 50710 (DNG) | The actual Bayer arrangement, e.g. `02 01 01 00` = RGGB |
| `CFARepeatPatternDim` | 33421 | `(2,2)` standard Bayer; `(4,4)` Quad Bayer |
| `CFALayout` | 50711 | 1 = rectangular (only value supported in v1) |
| `CFAPlaneColor` | 50710 | RGB plane-color ordering |
| `BitsPerSample` | 258 | typically 10, 12, 14, 16 for raw |
| `BlackLevel` | 50714 | Per-channel pedestal; 1, 4, or large rep-pattern array |
| `BlackLevelDeltaH` | 50715 | Per-column delta added to BlackLevel |
| `BlackLevelDeltaV` | 50716 | Per-row delta |
| `BlackLevelRepeatDim` | 50713 | Repetition of per-channel BlackLevel |
| `WhiteLevel` | 50717 | Saturation; one value per CFAPlane |
| `BayerGreenSplit` | 50733 | Mosaic green-split correction hint |
| `BestQualityScale` | 50780 | Output scale factor |
| `DefaultScale` | 50718 | Pixel aspect ratio |
| `DefaultCropOrigin` | 50719 | Origin of valid pixels relative to ActiveArea |
| `DefaultCropSize` | 50720 | Crop output size |
| `ActiveArea` | 50829 | Region of the sensor that is image (not optical black) |
| `MaskedAreas` | 50830 | Optical-black regions for noise floor estimation |
| `RawDataUniqueID` | 50781 | Stable identifier for caching |

`CFAPattern` for the four standard mosaics:

```
RGGB:   0,1,1,2     // R, G, G, B (rows 0/1 left-to-right)
BGGR:   2,1,1,0
GRBG:   1,0,2,1
GBRG:   1,2,0,1
```

`CFAPlaneColor` is a list of plane colors, where `0=R, 1=G, 2=B, 3=Cyan, 4=Magenta, 5=Yellow, 6=White`. Standard Bayer is `0,1,2`.

### 3.3 Color Metadata Tags

The DNG color pipeline is parameterised by these tags:

- **`ColorMatrix1` (50721)** — 3×3 matrix mapping XYZ (D50, normalized) → camera native RGB under **CalibrationIlluminant1**. Typically `CalibrationIlluminant1 = 17` (Standard A / tungsten).
- **`ColorMatrix2` (50722)** — same, for **CalibrationIlluminant2**, typically `21` (D65 daylight).
- **`CameraCalibration1/2` (50723/50724)** — per-camera-unit matrix that compensates for individual sensor variation. Multiplied with ColorMatrix.
- **`ReductionMatrix1/2` (50725/50726)** — optional dimensionality-reducing matrix for cameras with > 3 raw channels (rare).
- **`AnalogBalance` (50727)** — vector of pre-color-matrix gain (rare; legacy).
- **`AsShotNeutral` (50728)** — neutral white-balance triplet in camera RGB (raw values that ought to be gray). Smartphone DNGs almost always use this.
- **`AsShotWhiteXY` (50729)** — alternative WB given as CIE xy chromaticity (rare in mobile).
- **`ForwardMatrix1/2` (50964/50965)** — 3×3 matrix mapping white-balanced camera RGB → CIE XYZ D50. This is the modern path; ColorMatrix1/2 is legacy. Smartphone DNGs ship both but ForwardMatrix is preferred for accuracy.
- **`CalibrationIlluminant1/2` (50778/50779)** — EXIF light-source code (1=daylight, 17=standard A, 21=D65, etc.).
- **`ProfileToneCurve` (50940)** — 1D LUT input/output pairs for tonal rendering (a "look").
- **`ProfileLookTableData` (50982) / Dims (50981) / Encoding (51108)** — 3D HSV LUT for stylistic look.
- **`ProfileHueSatMapData1/2/3` (50938/50939/50983)** — 3D LUT in (hue, saturation, value) for color rendering at three illuminants.
- **`ProfileHueSatMapDims` (50937)** — dimensions (e.g. `90×30×16`).
- **`ProfileEmbedPolicy` (50941)** — 0 = allow copy/embed, 1 = embed in DNG only, 2 = embed if intent matches.
- **`AsShotProfileName` (50934)** — the profile preset (e.g. "Camera Standard").

**Smartphone DNG observed pattern:** ColorMatrix2 (D65) + ForwardMatrix2 (D65) + AsShotNeutral; ColorMatrix1/ForwardMatrix1 may be absent (single-illuminant profile) or also present for dual-illuminant interpolation.

### 3.4 Noise and Lens Tags

- **`NoiseProfile` (51041)** — N pairs `(a, b)` where variance(I) = a + b·I (per channel; either 1 pair = monochrome assumption, 3 = RGB, or 4 = R/G1/G2/B). Calibrated by the camera vendor (or third-party tool); must be linear-domain values, applied AFTER linearization. Some phone vendors ship inaccurate profiles — a re-measurement step is part of an ideal pipeline (see [#15 — calibration](15-mobile-camera-calibration.md)).
- **`NoiseReductionApplied` (50935)** — rational, 0/1 = none, 1/1 = full. Modern smartphone DNGs from Pixel set this to `0/1` for "raw"; iPhone ProRAW sets it to non-zero (denoised RAW).
- **`LensInfo` (50736)** — min/max focal length, min/max f-stop.
- **`LensModel` (50732)** — string.
- **`LensSerialNumber` (42037)** — string.

### 3.5 OpcodeList1, OpcodeList2, OpcodeList3 — The Killer Feature

OpcodeList1/2/3 are the most important DNG feature for mobile camera DNGs because **smartphone vendors push corrections into them rather than baking them into raw pixel values**. Skipping them is a cause of green corners, color shifts, geometric distortion in many community DNG renderers.

**Tag IDs:**
- `OpcodeList1` = 51008 (0xC740) — applied **before linearization** (raw-domain).
- `OpcodeList2` = 51009 (0xC741) — applied **after linearization, before demosaic** (still mosaiced linear).
- `OpcodeList3` = 51022 (0xC74E) — applied **after demosaic** (RGB-domain).

**Wire format** (`UNDEFINED` byte block, big-endian regardless of TIFF byte order):

```
uint32  count                    // number of opcodes in the list
{
  uint32  opcode_id              // 1..14
  uint32  dngversion             // minimum DNG version required to interpret
  uint32  flags                  // bit 0 = optional (skip if unknown)
  uint32  byte_count             // bytes in the parameters that follow
  uint8   parameters[byte_count] // opcode-specific
} * count
```

If `flags & 1 == 1` an unknown opcode may be skipped without rendering error (cpipe MUST log a warning). If `flags & 1 == 0` the opcode is **mandatory**: cpipe must implement it or refuse the DNG.

**The 14 opcodes:**

| ID | Name | Domain | Purpose |
|---|---|---|---|
| 1 | WarpRectilinear | List 3 | Geometric correction + lateral CA for rectilinear lenses; radial poly + tangential |
| 2 | WarpFisheye | List 3 | De-fish; perspective-projection mapping |
| 3 | FixVignetteRadial | List 1/2 | Radial-polynomial vignette gain |
| 4 | FixBadPixelsConstant | List 1 | Replace pixels equal to a constant value |
| 5 | FixBadPixelsList | List 1 | Replace listed pixels |
| 6 | TrimBounds | List 1/2/3 | Crop output rectangle |
| 7 | MapTable | List 1/2/3 | 1D LUT per channel |
| 8 | MapPolynomial | List 1/2/3 | Per-pixel polynomial transform |
| 9 | GainMap | List 1/2/3 | 2D gain field (the big one — see below) |
| 10 | DeltaPerRow | List 1/2 | Per-row offset |
| 11 | DeltaPerColumn | List 1/2 | Per-column offset |
| 12 | ScalePerRow | List 1/2 | Per-row gain |
| 13 | ScalePerColumn | List 1/2 | Per-column gain |
| 14 | (reserved / vendor) | — | DNG 1.5+ reserved range |

#### GainMap deep-dive

GainMap is the most important opcode for smartphone DNGs because it carries **lens-shading correction** (radial vignette + per-pixel hue shift) and, since DNG 1.7, the new "asymmetric" interpolation behaviour for values outside the gain-map rectangle.

GainMap parameters:

```
uint32  top, left, bottom, right      // pixel rectangle (image coords)
uint32  plane                         // CFA plane index, 0..N-1
uint32  planes                        // total planes
uint32  rowPitch, colPitch            // CFA stride
uint32  mapPointsV, mapPointsH        // grid dimensions
double  mapSpacingV, mapSpacingH      // grid spacing
double  mapOriginV, mapOriginH        // grid origin
uint32  mapPlanes                     // typically 1 per plane
float   gain[mapPointsV][mapPointsH]  // the gain field
```

The gain at pixel (y, x) is bilinearly sampled from the grid. The 1.7 spec clarifies that for samples falling outside the grid the **edge gain extends** rather than wrapping. cpipe must implement this correctly — RawSpeed issue #267 documents the trap.

#### OpcodeList timing in cpipe

The cpipe DAG must materialise the opcode lists as scheduler nodes:

```
node[in_dng]
  → node[parse_dng] (CPU; emits CFA buffer + opcode list)
  → node[opcode_list_1_apply]   // raw-domain: bad pixels, raw fixes
  → node[linearization]         // 1D LUT per channel from LinearizationTable
  → node[opcode_list_2_apply]   // post-linearization: GainMap (vignetting), DeltaPerRow
  → node[black_white_scaling]   // (raw - BlackLevel) / (WhiteLevel - BlackLevel)
  → node[demosaic]              // → planar RGB linear
  → node[opcode_list_3_apply]   // RGB-domain: WarpRectilinear, lateral CA
  → node[color_matrix_apply]    // ForwardMatrix * raw_white_balanced → XYZ
  → node[xyz_to_working_cs]     // see [#13 — color management]
  → ...
```

This ordering is mandated by Adobe DNG Spec section 1.6 chapter 6; deviating produces incorrect output.

### 3.6 Linearization

`LinearizationTable` (50712) is a per-channel monotonic LUT applied first inside `node[linearization]`. Most smartphone DNGs do not use it (sensor is already linear). Some Sony-supplied DNGs use a 4096-entry LUT for 12→16-bit gamma-corrected raw.

If absent, cpipe treats raw as already linear after black-level subtraction.

### 3.7 Quad Bayer Encoding in DNG (D12)

Adobe DNG spec accommodates Quad Bayer through the existing `CFAPattern` + `CFARepeatPatternDim` machinery without requiring a new tag. The encoding is:

```
CFARepeatPatternDim = (4, 4)
CFAPattern = 16-byte array describing the 4×4 mosaic, e.g.:
  G R R G    0,1,1,0
  R R G G    0,0,1,1   // (NOTE: example only — actual layouts vary by sensor)
  G G B B    0,0,2,2
  G G B B    0,0,2,2
```

For Sony Quadra (e.g. IMX800-class sensors in flagship phones), the 4×4 layout is `RGGB` repeated in 2×2 blocks: every "color" is a 2×2 block of the same channel.

```
CFAPattern (Sony Quadra-style):
  R R G G
  R R G G
  G G B B
  G G B B
```

For Samsung Tetracell it is the same 2×2-of-each layout (Tetracell = Tetra cell).

**Adobe DNG Converter behaviour for Quad Bayer phones (verified against Pixel 8 / Galaxy S24 Ultra / iPhone 16 Pro DNGs):**
- **Default mode**: phone outputs a 2×2 Bayer DNG that is hardware-remosaiced. CFAPattern = standard RGGB. Effective resolution = same as binned mode (e.g. 12.5 MP from 50 MP sensor at 4-in-1).
- **Hi-res / "ProRAW 48"** mode: phone outputs full-resolution DNG; some vendors remosaic to 2×2 in firmware; some emit native 4×4 Quad Bayer. iPhone ProRAW 48 MP appears to be 2×2 remosaic. Pixel "Pro Raw" 50 MP and Galaxy 200 MP "expert RAW" are also 2×2 remosaic.
- **Vendor-tooling edge case**: Sony's own SDK can emit native 4×4 Quad Bayer DNG for Xperia phones, but app authors must opt in; Adobe Camera Raw will accept the file but currently demosaics by treating each 2×2 block as a single pixel (effectively binning).

For cpipe v1, the realistic path is:
1. Detect `CFARepeatPatternDim == (4,4)` at DNG-parse time.
2. **v1**: emit a remosaicing pre-pass that converts the 4×4 mosaic to 2×2 Bayer (averaging or median-filtering the 2×2 blocks per channel) and proceed with standard Bayer demosaic.
3. **v2**: ship a native Quad Bayer demosaic node — research-grade algorithms exist (Sony's own published methods, MDPI 2019 "The Effect of the Color Filter Array Layout Choice on State-of-the-Art Demosaicing").

### 3.8 Parsing Libraries — License-Filtered Survey

| Library | License | Verdict for D11 (Apache 2.0) | Notes |
|---|---|---|---|
| **LibRaw 0.22 / 202502** | LGPL 2.1 + CDDL dual | **Acceptable** if dynamically linked or LGPL terms honoured (provide object files / allow relinking) | Mature; supports DNG 1.7 JXL via Adobe DNG SDK or libjxl bridge |
| **Adobe DNG SDK 1.7.1** (Apr 2026) | Adobe SDK License Agreement (custom) | **Likely acceptable** — license review required; permits redistribution; not OSI-approved | Reference implementation of opcode interpreter |
| **exiv2** | GPLv2 (with later) | **OFF LIMITS** under D11 | Used by darktable; great metadata coverage but GPL |
| **TinyDNG** (syoyo) | MIT | **Acceptable** | Header-only, parses but does not interpret opcodes; DNG 1.7 issue #42 open |
| **rawspeed** (darktable) | LGPL 2.1 | **Acceptable** (LGPL like LibRaw) | Bundled with darktable; the GPL trap is darktable itself, not rawspeed |
| **lcms2** | MIT | **Acceptable** | ICC profile bits for embedded ICC tags |
| **libtiff 4.7.0** | BSD-style (libtiff license) | **Acceptable** | TIFF substrate |
| **libjxl 0.10+** | Apache 2.0 + 3-Clause BSD | **Acceptable** | Required for DNG 1.7 JXL payload |
| **dng_validate** | Adobe SDK License | **Acceptable in CI only** (not shipped) | Reference validator |
| **dt-dng** (darktable's DNG code) | GPL | **OFF LIMITS** | Trap; do not lift code |

**Recommended cpipe stack:**
1. **libtiff 4.7.0** for TIFF parsing (vcpkg port).
2. **LibRaw 0.22** (vcpkg port) for high-level DNG metadata extraction. Use the C API (`libraw_data_t`); link statically with the LGPL-static-link clause respected (cpipe must offer to relink user binaries with modified LibRaw if requested — this is the LGPL static-link obligation, not blocking).
3. **Custom opcode interpreter** in `cpipe::dng::OpcodeInterpreter` — ~1.5 kLoC C++20. Reference: Adobe DNG SDK source for correctness (read for understanding; do not copy code).
4. **libjxl 0.10+** when `Compression == 52546` (JPEG XL).
5. **Adobe `dng_validate`** in CI as a round-trip oracle.

### 3.9 Parser API Sketch

```cpp
// cpipe/dng/parser.hpp
namespace cpipe::dng {

struct ColorCalibration {
  std::optional<Matrix3> color_matrix_1, color_matrix_2;
  std::optional<Matrix3> camera_calibration_1, camera_calibration_2;
  std::optional<Matrix3> forward_matrix_1, forward_matrix_2;
  Vec3                   as_shot_neutral;          // raw-RGB white balance multipliers
  uint16_t               calibration_illuminant_1; // EXIF light-source code
  uint16_t               calibration_illuminant_2;
  std::optional<HueSatMap>  profile_hue_sat_map_1, _2, _3;
  std::optional<ToneCurve>  profile_tone_curve;
};

struct CFADescriptor {
  Dim2u                  repeat_pattern_dim;       // (2,2) or (4,4)
  std::array<uint8_t,16> pattern;                  // up to 16 entries
  std::vector<uint8_t>   plane_colors;             // typical {R,G,B}
  uint8_t                layout = 1;               // 1 = rectangular only (v1)
};

struct Opcode {
  enum class ID : uint32_t {
    WarpRectilinear = 1, WarpFisheye = 2, FixVignetteRadial = 3,
    FixBadPixelsConstant = 4, FixBadPixelsList = 5, TrimBounds = 6,
    MapTable = 7, MapPolynomial = 8, GainMap = 9,
    DeltaPerRow = 10, DeltaPerColumn = 11,
    ScalePerRow = 12, ScalePerColumn = 13,
  };
  ID id;
  uint32_t min_dng_version;
  bool optional;
  std::vector<std::byte> raw_params;   // parsed lazily into typed structs by the dispatcher
};

struct ParsedDng {
  // raw plane (uncompressed; if JXL on disk, decoded already)
  Buffer<uint16_t>       raw;          // CFA plane, BitsPerSample bits, packed
  Dim2u                  active_area;
  Rect                   default_crop;
  uint16_t               bits_per_sample;
  Vec4u                  black_level;  // up to 4-channel
  uint32_t               white_level;

  CFADescriptor          cfa;
  std::optional<LookupTable>  linearization_table;

  std::vector<Opcode>    opcodes_1, opcodes_2, opcodes_3;

  ColorCalibration       color;
  std::optional<NoiseProfile> noise;
  std::optional<LensInfo>     lens;

  // raw bytes (preserved for scope / re-export)
  std::span<const std::byte>  source_bytes;
};

// Top-level
std::expected<ParsedDng, ParseError> parse_dng(std::span<const std::byte> file_bytes);

// Opcode interpretation
class OpcodeInterpreter {
public:
  // Compile opcode list into scheduler nodes for the given list-id (1, 2, or 3)
  std::vector<NodeDescriptor> compile(std::span<const Opcode> opcodes,
                                      OpcodeListId list,
                                      const PipelineContext& ctx);
};

} // namespace cpipe::dng
```

The `compile()` method bridges DNG-side opcode descriptions to scheduler-side node descriptors that the [#03 scheduler](03-heterogeneous-scheduler.md) consumes. Each opcode becomes one or more compute nodes; the `Pipeline` queue is appended to the master DAG at the right insertion points.

### 3.10 cpipe-specific Rendering Pipeline (Implied by DNG)

The full canonical order, for cross-reference with [#06 — soft ISP architectures](06-soft-isp-architectures.md):

```
1.  parse_dng                            [CPU]
2.  decompress raw payload               [CPU; libjxl if JXL, else lossless / passthrough]
3.  opcode_list_1_apply                  [CPU/GPU; raw-domain]
       FixBadPixelsList, FixBadPixelsConstant, MapTable (raw)
4.  linearization                        [GPU; per-channel LUT]
5.  black_white_scaling                  [GPU; per-channel offset + gain]
6.  opcode_list_2_apply                  [GPU; mosaiced linear]
       GainMap (lens shading), FixVignetteRadial, DeltaPerColumn, ScalePerColumn
7.  CFA inspection / quad-bayer remosaic [GPU]
8.  white_balance_apply                  [GPU; AsShotNeutral or computed]
9.  demosaic                             [GPU; → planar RGB linear]
10. opcode_list_3_apply                  [GPU; RGB-domain]
       WarpRectilinear (lens distortion + lateral CA), WarpFisheye, MapPolynomial
11. color_matrix_apply                   [GPU; ForwardMatrix interpolated by CCT(AsShotNeutral)]
12. xyz_d50_to_working_cs                [GPU; → linear Rec.2020 or ACEScg]
13. <user-graph: tone, denoise, look>    [DAG]
14. working_cs_to_output_cs              [GPU; → BT.2020 PQ or sRGB]
15. encode_heif                          [CPU; libheif]
```

This pipeline is reused identically in the Web Editor's preview — see [#11 — pipeline editor](11-pipeline-editor-and-connectivity.md).

### 3.11 DNG 1.7 JPEG XL Payload (D12, D14)

DNG 1.7 added JPEG XL (`JXL`) as a permitted compression scheme (TIFF Compression code = `52546`, vendor-allocated). The payload is a JXL stream encoding the raw Bayer or LinearRaw pixel grid. JPEG XL's modular mode (rather than VarDCT) is preferred for raw because it allows lossless integer encoding.

**For cpipe:** include libjxl 0.10+ via vcpkg as an optional dependency; gate behind `cpipe_with_jxl_dng=ON`. Without it, DNG 1.7 JXL files fail at parse. Adobe DNG SDK 1.7+ also embeds libjxl internally — the licenses align (Apache 2.0 / BSD-3).

### 3.12 Reference Repositories Inspected

Three repos audited for opcode-handling correctness reference:

1. **LibRaw/LibRaw** (master, commit ranges around `202502` snapshot) — LibRaw 0.22 release (GitHub). DNG opcode handling is partial without the Adobe DNG SDK; LibRaw documents in `README.DNGSDK.txt` that its built-in DNG parser does not interpret opcode lists 2 and 3 fully. With Adobe DNG SDK linked, full opcode support is delegated.
2. **electro-logic/DngOpcodesEditor** — C# tool to read/write/preview DNG opcodes. Excellent reference for the binary encoding; not lift-able (custom license, but small enough to re-implement from spec).
3. **darktable-org/rawspeed** — issue #267 documents GainMap implementation challenges; PRs implement OpcodeList 2/3 handling. **LGPL — code is OK to read and reference; do NOT statically link unless we can ship LGPL-compliant binary.** The implementation logic is good for cross-checking edge cases (especially GainMap edge-extension behaviour).

---

## 4. Concrete Code Sketches

### 4.1 Opcode Binary Decoder Skeleton

```cpp
struct OpcodeListReader {
  std::span<const std::byte> data;
  size_t pos = 0;

  template <typename T>
  T read_be() {
    T value;
    std::memcpy(&value, data.data() + pos, sizeof(T));
    pos += sizeof(T);
    return std::byteswap(value);  // OpcodeList is always big-endian
  }

  std::vector<Opcode> parse() {
    auto count = read_be<uint32_t>();
    std::vector<Opcode> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      Opcode op;
      op.id              = static_cast<Opcode::ID>(read_be<uint32_t>());
      op.min_dng_version = read_be<uint32_t>();
      auto flags         = read_be<uint32_t>();
      op.optional        = (flags & 1u) != 0;
      auto byte_count    = read_be<uint32_t>();
      op.raw_params.assign(
        data.begin() + pos,
        data.begin() + pos + byte_count);
      pos += byte_count;
      out.push_back(std::move(op));
    }
    return out;
  }
};
```

### 4.2 GainMap Decoder

```cpp
struct GainMapParams {
  Rect2u rect;            // top, left, bottom, right
  uint32_t plane, planes;
  uint32_t row_pitch, col_pitch;
  uint32_t map_h, map_v;
  Vec2d   spacing;        // (v, h)
  Vec2d   origin;
  uint32_t map_planes;
  std::vector<float> gain; // size = map_h * map_v * map_planes
};

GainMapParams parse_gain_map(std::span<const std::byte> p) {
  OpcodeListReader r{p, 0};
  GainMapParams gm;
  gm.rect.top    = r.read_be<uint32_t>();
  gm.rect.left   = r.read_be<uint32_t>();
  gm.rect.bottom = r.read_be<uint32_t>();
  gm.rect.right  = r.read_be<uint32_t>();
  gm.plane       = r.read_be<uint32_t>();
  gm.planes      = r.read_be<uint32_t>();
  gm.row_pitch   = r.read_be<uint32_t>();
  gm.col_pitch   = r.read_be<uint32_t>();
  gm.map_v       = r.read_be<uint32_t>();
  gm.map_h       = r.read_be<uint32_t>();
  gm.spacing.v   = r.read_be<double>();
  gm.spacing.h   = r.read_be<double>();
  gm.origin.v    = r.read_be<double>();
  gm.origin.h    = r.read_be<double>();
  gm.map_planes  = r.read_be<uint32_t>();
  size_t n = gm.map_h * gm.map_v * gm.map_planes;
  gm.gain.resize(n);
  for (auto& f : gm.gain) f = r.read_be<float>();
  return gm;
}
```

### 4.3 Quad Bayer Detector

```cpp
bool is_quad_bayer(const CFADescriptor& cfa) {
  return cfa.repeat_pattern_dim == Dim2u{4, 4};
}

// v1 remosaic: 4x4 → 2x2 by averaging same-color 2x2 blocks
void remosaic_quad_to_bayer(const Buffer<uint16_t>& src,
                            Buffer<uint16_t>& dst,
                            const CFADescriptor& cfa) {
  // ... average 2x2 same-color blocks, write 2x2 RGGB-equivalent grid
}
```

---

## 5. Cited Sources

- Adobe DNG Specification 1.6.0.0, Adobe Inc., December 2021.
  https://paulbourke.net/dataformats/dng/dng_spec_1_6_0_0.pdf
- Adobe DNG Specification 1.7 release notes, Adobe forum thread, June 2023.
  https://community.adobe.com/t5/photoshop-ecosystem-discussions/dng-specifications-v1-7-and-sdk/td-p/14192863
- LibRaw with Adobe DNG SDK 1.7 announcement, libraw.org, 2024.
  https://www.libraw.org/node/2808
- LibRaw 202502 snapshot release notes.
  https://www.libraw.org/news/libraw-202502-snapshot
- LibRaw 0.22 release.
  https://www.libraw.org/news/libraw-0-22-0-release
- AwareSystems TIFF tag reference for OpcodeList1 (51008).
  https://www.awaresystems.be/imaging/tiff/tifftags/opcodelist1.html
- electro-logic/DngOpcodesEditor on GitHub.
  https://github.com/electro-logic/DngOpcodesEditor
- darktable-org/rawspeed issue #267 (GainMap implementation).
  https://github.com/darktable-org/rawspeed/issues/267
- darktable feature request #8728 (lens shading map / DNG GainMap support).
  https://github.com/darktable-org/darktable/issues/8728
- Cloudinary blog: Samsung Now Supports DNG 1.7, Including JPEG XL.
  https://cloudinary.com/blog/samsung-now-supports-dng-1-7-including-jpeg-xl
- Library of Congress format description: Adobe DNG 1.6.
  https://www.loc.gov/preservation/digital/formats/fdd/fdd000628.shtml
- Sony Quad Bayer Coding technology page.
  https://www.sony-semicon.com/en/technology/mobile/quad-bayer-coding.html
- Adobe DNG Quad Bayer support discussion (community).
  https://community.adobe.com/t5/camera-raw-discussions/does-the-dng-spec-support-quad-bayer-type-filter-pattern/td-p/11194530
- MDPI 2019, "The Effect of the Color Filter Array Layout Choice on State-of-the-Art Demosaicing".
  https://www.mdpi.com/1424-8220/19/14/3215
- Lightroom Queen forum thread on DNG 1.7 file sizes (2024).
  https://www.lightroomqueen.com/community/threads/lr-13-new-dng-file-sizes.48597/
- Discuss.pixls.us thread on smartphone DNG GainMap embedding.
  https://discuss.pixls.us/t/looking-for-samples-smartphone-dngs-with-embedded-gainmap/27296
- Adobe community thread on opcode operation timing.
  https://community.adobe.com/t5/camera-raw/how-does-the-opcode-operates-on-the-dng-image/td-p/9473447
- Capture One forum on WarpRectilinear interpretation issues.
  https://support.captureone.com/hc/en-us/community/posts/360014125557-DNG-Distortion-Correction-issue-Opcode-ID1-WarpRectilinear
- Wikipedia: Digital Negative.
  https://en.wikipedia.org/wiki/Digital_Negative
- ISO 12234-4 standard page.
  https://www.iso.org/standard/86123.html
- Android CTS DNG noise model tool.
  https://android.googlesource.com/platform/cts/+/master/apps/CameraITS/tools/dng_noise_model.py
- libjxl on GitHub (Apache 2.0 / BSD-3).
  https://github.com/libjxl/libjxl

---

## 6. See Also

- [#06 — Soft ISP architectures](06-soft-isp-architectures.md) — opcode-list scheduling fits into the broader DAG.
- [#07 — Classic ISP algorithms](07-classic-isp-algorithms.md) — demosaic / linearization algorithms used at the DNG-mandated stages.
- [#13 — Color management](13-color-management.md) — what happens with ColorMatrix1/2 / ForwardMatrix1/2 after this pipeline.
- [#15 — Mobile camera calibration](15-mobile-camera-calibration.md) — how DNG metadata is calibrated and what v1 reads vs v2 writes.
- [#16 — Camera2 RAW and burst](16-camera2-raw-and-burst.md) — Camera2 metadata maps to DNG tags.

---

## 7. Open Questions

1. **DNG SDK 1.7.1 license-compatibility audit**: Adobe's EULA permits redistribution but requires legal review against Apache 2.0 reciprocity. Decision: ship DNG SDK as optional dependency for "max-fidelity" mode; keep native opcode interpreter as default.
2. **Native Quad Bayer demosaic in v1?** v1 ships remosaic-to-2×2; if iPhone 16 Pro / Galaxy S25 ProRAW results suffer measurably, escalate to v2 priority.
3. **DNG 1.7 JXL DNGs**: real-world prevalence in 2026 Android / iOS cameras. Need to verify that production phones are emitting JXL or still using lossless JPEG / uncompressed (Cloudinary blog says Samsung S24+ does emit JXL DNGs).
4. **OpcodeList3 lateral CA correction (WarpRectilinear with per-plane radial polynomials)** — does Adobe's Camera Raw inferred behaviour match Capture One's? Some smartphone DNGs produce visible CA when both apps' implementations diverge slightly. Investigate via `dng_validate`.
5. **dt-dng / RawTherapee opcode test corpus** — both are GPL-only, so we cannot include their test images. Build our own DNG corpus from sample phones (Pixel 8, Galaxy S24 Ultra, iPhone 16 Pro, Sony Xperia 1 V) for cpipe regression.
6. **ProfileEmbedPolicy = 1 / 2** — should cpipe respect "embed only" and refuse to extract / re-embed the profile in derivative DNGs? v1: yes (be conservative).
