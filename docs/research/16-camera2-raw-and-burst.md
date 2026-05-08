# Report 16 — Camera2 RAW Capture & Burst Pipeline (Android)

> Cluster F · cpipe research package · drafted **2026-05-08**.
> Owns the capture-side architecture: how Bayer photons land in cpipe's
> `Buffer` abstraction with DNG-grade metadata attached, in time for the
> scheduler (cluster A) to begin the pipeline within the user's perception
> of "instant."

## 1. TL;DR

For cpipe Android, **use Camera2 (not CameraX) as the capture layer**, written
in **C++/NDK (`AImageReader`, `ACameraDevice`)** and surfaced to the Compose UI
through a thin Kotlin/JNI bridge. Camera2 is the only API that gives us the
deterministic per-frame metadata, manual sensor controls, and `RAW_SENSOR`
buffer addressing the soft ISP needs. CameraX 1.5 (Nov 2025) finally ships RAW
DNG capture, but it abstracts away exactly the controls (per-request manual
sensor, multi-physical-camera, custom `OutputConfiguration` usage flags) that
cpipe needs for D2/D3/D12. Recommend Camera2 for capture, optionally use
CameraX *only* for preview in v2.

For burst (D3, 5–10 frames), use **`CameraCaptureSession.captureBurst`** on a
dedicated session whose RAW output `ImageReader` is sized to **`burst_count +
4`** images and allocated with `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
GPU_DATA_BUFFER` so the buffers flow directly into Vulkan compute via
`VK_ANDROID_external_memory_android_hardware_buffer` (cluster A, report 02).
Per-frame metadata (`SENSOR_TIMESTAMP`, exposure, ISO, tint, lens position) is
captured from `CaptureResult` and stamped onto cpipe's `BurstFrame` struct
alongside the AHardwareBuffer handle. **Avoid `DngCreator`** for the canonical
DNG path — it omits OpcodeList tags cpipe wants — but keep it as a fallback
"export DNG to user storage" feature.

## 2. Decision matrix

| Decision | Recommendation | Rationale | Cross-link |
|----------|----------------|-----------|-----------|
| API surface | Camera2 (NDK `AImageReader`, Kotlin `CameraManager`) | Per-request manual control, custom `OutputConfiguration`, future-proofs heterogeneous capture | D1, D19 |
| RAW format | `RAW_SENSOR` (16-bit) primary, `RAW10`/`RAW12` opportunistic | `RAW_SENSOR` is universal; RAW10/12 saves bandwidth on devices that expose it | D2, D12 |
| Burst | `captureBurst` on STILL_CAPTURE-only session, 5–10 requests | Lowest latency between frames; matches D3 burst-on-shutter | D3 |
| Buffer hand-off | `AImageReader_newWithUsage` + `AImage_getHardwareBuffer` | Zero-copy into Vulkan / NPU via `VK_ANDROID_external_memory_AHB` | A/02 |
| ZSL | Architecture reserves `TEMPLATE_ZERO_SHUTTER_LAG`; not implemented v1 | D3 defers ZSL; ring buffer must not break us into a corner | D3, D5 |
| DNG output | **Custom DNG writer** (libtiff + cpipe metadata) for canonical path | `DngCreator` is correct but limited — no OpcodeList, no DNG 1.6/1.7 tags | D10, E/12 |
| Multi-camera | Logical camera with physical-stream replacement | Matches main+tele+ultrawide use; future bracketed-lens captures | D12 |
| Quad Bayer | Capture remosaiced (standard Bayer) by default; 4-cell native via vendor extension | All current devices remosaic in firmware; native Quad Bayer is research-grade only | D12 |
| Permissions | `CAMERA` + `FOREGROUND_SERVICE_CAMERA` + camera FGS during long burst processing | Required by Android 14+ to keep capture+pipeline alive | — |
| Preview (v2) | Architecture reserves a `PREVIEW`-tagged `ImageReader` lane; not implemented v1 | D5 defers streaming; capture path must not preclude it | D5, A/03 |

## 3. Detailed findings

### 3.1 Why Camera2, not CameraX, for cpipe (2026)

CameraX 1.5 (released **Nov 2025**) finally added **DNG (RAW) capture** through
`OUTPUT_FORMAT_RAW` / `OUTPUT_FORMAT_RAW_JPEG`, queried via
`ImageCaptureCapabilities(CameraInfo).getSupportedOutputFormats()`, with
simultaneous JPEG+DNG via two `OutputFileOptions` to a single `takePicture`
call.[¹](https://android-developers.googleblog.com/2025/11/introducing-camerax-15-powerful-video.html)
This is a real and welcome simplification — but it remains *opinionated.*

CameraX still owns the capture-session lifecycle, picks its own
`CaptureRequest` template, and synthesizes per-frame metadata only on
selected fields. For a soft ISP consumer like cpipe, the gaps are:

- **No `setRepeatingBurst` / programmatic `captureBurst` for stills.** The
  CameraX team has explicitly stated they "do not plan to expose all Camera2
  APIs."[²](https://groups.google.com/a/android.com/g/camerax-developers/c/Z1Y7fjqjo6o)
  Burst is the single feature locked in D3.
- **No custom `OutputConfiguration` usage flags.** cpipe wants
  `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | GPU_DATA_BUFFER` so RAW buffers
  enter Vulkan compute directly. CameraX manages the surface internally.
- **No control over physical-camera streams in a logical-camera session.**
  cpipe's roadmap (post-v1) envisions per-physical-camera RAW capture from a
  multi-camera (main+telephoto+ultrawide).
- **No per-request `CaptureResult` callback access**, only the file output
  callback. cpipe needs `SENSOR_TIMESTAMP`, `SENSOR_NEUTRAL_COLOR_POINT`,
  `BLACK_LEVEL_LOCK`, etc. on a per-frame basis with frame-level partial
  results.

CameraX, by Google's own description, is a high-level wrapper over Camera2.[³](https://developer.android.com/media/camera/choose-camera-library)
For a pro-grade soft ISP it's the wrong abstraction layer. **Decision: Camera2 is
mandatory for cpipe's capture node**, even at the cost of more glue code. The
architecture should *expose* the cpipe `CameraSource` plugin's interface in a
way that *can* be reimplemented as a CameraX node later if v2 introduces a
"casual mode" preview.

#### Camera2 API surface

The four classes that frame the lifecycle, in order:[⁴](https://developer.android.com/reference/android/hardware/camera2/package-summary)

1. `CameraManager` — system service. `getCameraIdList()`, `getCameraCharacteristics(id)`. Returns static device capability.
2. `CameraDevice` — per-camera handle. Opened asynchronously via `openCamera(id, callback, handler)` (or `ACameraManager_openCamera` on NDK).
3. `CameraCaptureSession` — session bound to a fixed list of output `Surface`s. Created via `createCaptureSession` or, post-API 28, `createCaptureSessionByOutputConfigurations` to attach usage hints. **Recreating a session is expensive (~150 ms);** keep the session alive across many bursts when possible.
4. `CaptureRequest` (built via `CaptureRequest.Builder`) → submitted via `capture(...)`, `captureBurst(list, ...)`, or `setRepeatingRequest(...)`. The capture targets must be a subset of the surfaces the session was created with.
5. `CaptureResult` — delivered via `CameraCaptureSession.CaptureCallback` along with `onCaptureCompleted()` / `onCaptureProgressed()` (partial result events). One per request.

NDK has near-1:1 equivalents — `ACameraManager`, `ACameraDevice`,
`ACameraCaptureSession`, `ACaptureRequest`, `ACameraMetadata` — that bind to
`AImageReader` rather than `ImageReader`.[⁵](https://developer.android.com/ndk/reference/group/media)
For cpipe (C++ first, JNI thin), the NDK path is preferred so capture lives
in the same compilation unit as the soft ISP.

#### Required permissions (Android 14, 15, 16 as of 2026-05-08)

- `<uses-permission android:name="android.permission.CAMERA"/>` — runtime permission, required.
- `<uses-permission android:name="android.permission.FOREGROUND_SERVICE"/>` plus, on Android 14+ (`targetSdk >= 34`), `<uses-permission android:name="android.permission.FOREGROUND_SERVICE_CAMERA"/>` — required if camera access continues during a Service.[⁶](https://developer.android.com/develop/background-work/services/fgs/service-types)
- `<service ... android:foregroundServiceType="camera"/>` declaration. **You cannot start a camera FGS while the app is in the background**, with narrow exceptions.[⁶](https://developer.android.com/about/versions/14/changes/fgs-types-required)
- For saving captured DNG/HEIF to the device gallery, `READ_MEDIA_IMAGES` (Android 13+). On Android 14+ the user can grant **partial access** via `READ_MEDIA_VISUAL_USER_SELECTED` — the gallery picker shows only the user's chosen photos.[⁷](https://developer.android.com/about/versions/14/changes/partial-photo-video-access)
  cpipe should treat MediaStore as a one-shot writer and avoid keeping a long-lived index — partial access is the modern norm.

The *partial camera permission* topic the brief mentioned is, as of 2026-05,
**not** an Android 15 feature. Android 14 introduced *partial photo/video
access* (read), not partial camera (capture) access. The CAMERA permission
remains binary at runtime.

### 3.2 RAW formats and what hardware exposes

The Camera2 spec defines these RAW pixel
formats:[⁸](https://developer.android.com/reference/android/graphics/ImageFormat)

| Format | `ImageFormat.*` | Bit depth | Bytes / pixel | Memory layout |
|---|---|---|---|---|
| `RAW_SENSOR` | `0x20` | 16 | 2 | Single plane, 16-bit per pixel, 0-padded above active depth (10–14 bits typical). Universal for any device with `BACKWARD_COMPATIBLE` + `RAW` capability. |
| `RAW10` | `0x25` | 10 | 1.25 | Densely packed: every 4 pixels in 5 bytes. `getPlanes()` returns one plane; `pixelStride` reports `0`; row stride includes any vendor padding.[⁹](https://developer.android.com/reference/android/graphics/ImageFormat#RAW10) |
| `RAW12` | `0x26` | 12 | 1.5 | Densely packed: every 2 pixels in 3 bytes. Same single-plane semantics as RAW10. |
| `RAW_PRIVATE` | `0x24` | opaque | n/a | Implementation-private. CPU cannot decode; passes via `AHardwareBuffer` only. Useful if cpipe stays entirely on GPU + NPU. |
| `RAW_DEPTH` | `0x1002` | 16 | 2 | Depth metadata; not relevant for cpipe v1. |

#### Bit packing — RAW10 specifically

For each row of length `W` pixels, the byte stream is `ceil(W/4)*5` bytes
long. Each 5-byte group `B0..B4` decodes to four 10-bit pixels:

```
P0 = (B0 << 2) | ((B4 >> 0) & 0x3)
P1 = (B1 << 2) | ((B4 >> 2) & 0x3)
P2 = (B2 << 2) | ((B4 >> 4) & 0x3)
P3 = (B3 << 2) | ((B4 >> 6) & 0x3)
```

The "low bits" byte is at position **4** in the group, and the high-order
bits of each pixel are stored MSB-first in `B0..B3`. cpipe should unpack this
on the GPU once on entry and convert to FP16 (D9) — a single compute shader.

#### RAW12

Two pixels per 3 bytes. `P0 = (B0 << 4) | (B2 & 0x0F)`,
`P1 = (B1 << 4) | (B2 >> 4)`. Same single-plane access pattern.[¹⁰](https://developer.android.com/reference/android/graphics/ImageFormat#RAW12)

#### Quad Bayer (Sony / Samsung)

Sensors like Sony **IMX989** (1-inch, used in Xiaomi 12S Ultra etc.) and
Samsung **ISOCELL HM3** (108 MP, S22 Ultra) and **HP2** (200 MP, S23/S24/S25
Ultra) physically use a Quad Bayer (a.k.a. Tetracell, Quad Pixel) CFA: a 4×4
block has 4 contiguous R, 8 contiguous G, 4 contiguous
B pixels.[¹¹](https://www.sony-semicon.com/en/technology/mobile/quad-bayer-coding.html)[¹²](https://semiconductor.samsung.com/image-sensor/mobile-image-sensor/isocell-hp2/)
The HP2 reads at 200 MP/15 fps and HM3 at 108 MP/10 fps; for full-frame RAW
bursts that's the cap.[¹³](https://semiconductor.samsung.com/image-sensor/mobile-image-sensor/isocell-hp2/)
**What Android exposes** is the open question. As of API 35 the metadata key
`SENSOR_INFO_COLOR_FILTER_ARRANGEMENT` only enumerates the four standard
Bayer orderings (`RGGB`, `GRBG`, `GBRG`, `BGGR`) plus `RGB` and `MONO` — no
official Quad Bayer pattern code. In practice the device firmware
**remosaics** the Quad Bayer to standard Bayer before exposing the RAW
buffer, so cpipe sees an RGGB Bayer image at the high-resolution mode (50/108/200 MP). At pixel-binned modes (12/27/50 MP) the firmware bins 2×2 to a single super-pixel before remosaic, again exposing RGGB.

For D12 ("Sony / Samsung Quad Bayer" support), this means cpipe's demosaic
node must be aware that the highest-resolution mode of these sensors uses
**a remosaiced Bayer that loses some sharpness compared to a native Bayer
sensor of equal resolution**. Custom Quad Bayer demosaic (per Jia 2022,
"Learning Rich Information for Quad Bayer Remosaicing and
Denoising")[¹⁴](https://jhc.sjtu.edu.cn/~xiaohongliu/papers/2022learning.pdf)
is only relevant if a vendor exposes a 4-cell-native RAW stream via vendor
extension — none does as of 2026-05. **Recommend**: store
`raw_is_remosaiced_quad_bayer = true` flag in cpipe metadata when the sensor
characteristics match a known Quad Bayer sensor list (IMX800, IMX989,
HM3, HP2, etc.); use it later as a hint to the demosaic node (apply
slight extra sharpening / different chroma reconstruction parameters). Do
not attempt 4-cell-native demosaic in v1.

#### Format negotiation

```kotlin
val streamConfig = characteristics
  .get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)!!

val rawFormats = listOf(
  ImageFormat.RAW_SENSOR,
  ImageFormat.RAW10,
  ImageFormat.RAW12,
  ImageFormat.RAW_PRIVATE
).filter { streamConfig.isOutputSupportedFor(it) }

val primaryFmt = when {
  ImageFormat.RAW_SENSOR in rawFormats -> ImageFormat.RAW_SENSOR
  ImageFormat.RAW10 in rawFormats -> ImageFormat.RAW10
  else -> error("Device has no RAW capability")
}

val rawSizes = streamConfig.getOutputSizes(primaryFmt)
val largest = rawSizes.maxByOrNull { it.width.toLong() * it.height.toLong() }!!
```

The largest output size must always exist for the sensor's own native
resolution if `REQUEST_AVAILABLE_CAPABILITIES_RAW` (CameraCharacteristics
bit) is set. cpipe enforces this at startup and rejects devices missing
RAW capability.

#### `SCALER_AVAILABLE_STREAM_USE_CASES` (API 33+, Android 13+)

Apps can declare *intent* per stream so the HAL can tune the pipeline.
Documented use cases (constants on `CameraMetadata`):
`SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT`,
`PREVIEW`, `STILL_CAPTURE`, `VIDEO_RECORD`, `PREVIEW_VIDEO_STILL`,
`VIDEO_CALL`.[¹⁵](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#SCALER_AVAILABLE_STREAM_USE_CASES)

For cpipe v1 (capture-only, no preview), every output should be tagged
`STILL_CAPTURE` via `OutputConfiguration.setStreamUseCase(...)`. This signals
to the HAL that latency-per-frame matters more than throughput, and lets
the firmware choose its still-capture sensor mode (often higher bit depth
than the preview mode).

### 3.3 ImageReader sizing for burst

Per the AOSP `ImageReader` source, **maxImages caps the number of images
the consumer can hold simultaneously**.[¹⁶](https://github.com/aosp-mirror/platform_frameworks_base/blob/master/media/java/android/media/ImageReader.java)
The producer (camera) silently drops frames if it tries to push when no
slot is free. The standard burst trap is: app captures 5 frames, ImageReader
is sized to 4, and the 5th frame is dropped or the producer stalls.

For cpipe burst (5–10 frames, D3) the rule is:

```
maxImages = burstCount + headroom
headroom  = 4   // = (1 in producer write slot)
                +(1 in JNI hand-off)
                +(1 currently in pipeline)
                +(1 spare for jitter)
```

For a 10-frame burst, `maxImages = 14`. RAW16 at 100 MP is 200 MB per image;
14 buffers is **2.8 GB** of address space. Modern flagships have ≥12 GB RAM
and this is fine, but on a 6 GB mid-range it would OOM. cpipe should query
`ActivityManager.getMemoryInfo().totalMem` and clamp burst count downward
on small devices.

For NDK, `AImageReader_newWithUsage` supports the same `maxImages`
parameter with explicit usage flags
(`AHARDWAREBUFFER_USAGE_*`).[¹⁷](https://developer.android.com/ndk/reference/group/media)
Use:

```c
AImageReader_newWithUsage(
    width, height,
    AIMAGE_FORMAT_RAW16,
    /* usage = */
      AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE
    | AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER,
    /* maxImages = */ burstCount + 4,
    &reader);
```

The `GPU_DATA_BUFFER` flag is the modern path; on older firmware the
`GPU_FRAMEBUFFER | GPU_SAMPLED_IMAGE` combo also works but bloats the buffer
size. For RAW16 we want it as a *storage buffer* (typed RGBA16-like), so
`GPU_DATA_BUFFER` is correct.

**`AImage_getHardwareBuffer`** is the hand-off point. Each acquired
`AImage` exposes its underlying `AHardwareBuffer` without copy. cpipe wraps
this in its `Buffer` abstraction (cluster A, report 02), which in turn binds
to `VkImage` via `VK_ANDROID_external_memory_android_hardware_buffer`.[¹⁸](https://hackage.haskell.org/package/vulkan-3.6.7/candidate/docs/Vulkan-Extensions-VK_ANDROID_external_memory_android_hardware_buffer.html)

### 3.4 Burst capture flow

`captureBurst(List<CaptureRequest>, callback, handler)` submits requests
"in the minimum amount of time possible without interleaving."[¹⁹](https://developer.android.com/reference/android/hardware/camera2/CameraCaptureSession#captureBurst%28java.util.List%3Candroid.hardware.camera2.CaptureRequest%3E,%20android.hardware.camera2.CameraCaptureSession.CaptureCallback,%20android.os.Handler%29)
The cap is the sensor's native still-frame rate at the chosen resolution:

| Sensor | Resolution | Burst rate |
|---|---|---|
| Sony IMX800 / IMX989 | 50 MP | ~30 fps theoretical, ~15 fps RAW typical |
| Samsung HM3 | 108 MP | 10 fps (full-res), 30+ fps at 12 MP binned |
| Samsung HP2 | 200 MP | 15 fps (full-res), 60 fps at 12 MP |
| Sony IMX989 | 50 MP | ~30 fps |

So a 10-frame full-resolution burst on a Galaxy S25 Ultra (HP2) takes
~700 ms wall-clock from first to last `SENSOR_TIMESTAMP`. From the app's
perspective:

```
shutter press
  ├── ~30–80 ms : AE/AF lock + flush pending preview frames
  ├── ~700 ms   : 10× sensor read at 15 fps
  └── ~10 ms    : last ImageReader callback fires, all 10 buffers acquired
```

**`setRepeatingBurst`** and **`captureBurst` proper** differ only in
repetition: `setRepeatingBurst(list)` keeps cycling through the list (used
for high-speed video, slow-mo), while `captureBurst(list)` fires the list
exactly once. For cpipe stills, `captureBurst` is the only choice. The
high-speed `CameraConstrainedHighSpeedCaptureSession` is only for ≥120 fps
recording at non-RAW formats and is irrelevant.[²⁰](https://developer.android.com/reference/android/hardware/camera2/CameraConstrainedHighSpeedCaptureSession)

#### ZSL — architectural reservation only

D3 defers ZSL. Camera2 supports it via `TEMPLATE_ZERO_SHUTTER_LAG`
combined with a `RAW_PRIVATE` reprocessing input stream. The session must
include both an output `ImageReader` and a *reprocessing* input
configured via `InputConfiguration` (private format, sensor's native
resolution). The capture flow is:

1. Continuous `setRepeatingRequest` on `TEMPLATE_ZERO_SHUTTER_LAG` produces a circular buffer of "raw private" frames.
2. On shutter press, the app picks the best frame (highest sharpness, lowest motion) and `captureBurst`s a *reprocess* request that takes the chosen frame as input and emits a regular `RAW_SENSOR` or `JPEG` buffer.
3. The output buffer is then handed to cpipe.

cpipe's architecture should keep the `CameraSource` plugin pluggable so
that v2 can swap in the ZSL flow without rewriting the soft ISP nodes.
This means the plugin's output interface (`BurstFrames`) must be the
*same shape* whether produced by `captureBurst` (v1) or
`captureSingleRequest` reprocess (v2 ZSL).

### 3.5 Per-frame metadata (DNG-equivalent)

`CaptureResult` carries the per-frame state. The ones that flow into the
DNG-equivalent struct (cluster E, report 12) are listed in the table below
along with the calling code (Kotlin):

| Field | Source | Purpose | DNG tag |
|---|---|---|---|
| `SENSOR_TIMESTAMP` | `CaptureResult.SENSOR_TIMESTAMP` | nanos, monotonic; primary frame ID | DateTimeOriginal + sub-sec |
| `SENSOR_EXPOSURE_TIME` | `SENSOR_EXPOSURE_TIME` | nanos | ExposureTime |
| `SENSOR_SENSITIVITY` | `SENSOR_SENSITIVITY` | ISO 100..max | ISOSpeedRatings |
| `SENSOR_FRAME_DURATION` | `SENSOR_FRAME_DURATION` | nanos between frame starts | (rate; not a DNG tag) |
| `LENS_FOCAL_LENGTH` | `LENS_FOCAL_LENGTH` | mm | FocalLength |
| `LENS_APERTURE` | `LENS_APERTURE` | f-number (often fixed on phones) | FNumber |
| `LENS_FOCUS_DISTANCE` | `LENS_FOCUS_DISTANCE` | diopters | (Halide-style metadata) |
| `COLOR_CORRECTION_GAINS` | `COLOR_CORRECTION_GAINS` | RGGB white balance gains | AsShotNeutral (recip.) |
| `SENSOR_NEUTRAL_COLOR_POINT` | `SENSOR_NEUTRAL_COLOR_POINT` | (R,G,B) at neutral | AsShotNeutral |
| `BLACK_LEVEL_LOCK` | `BLACK_LEVEL_LOCK` | bool: black point stable | (cpipe internal flag) |
| `SENSOR_DYNAMIC_BLACK_LEVEL` | per-frame black levels | per-channel pedestals | LinearizationTable (computed) |
| `SENSOR_TEST_PATTERN_MODE` | enum | for calibration capture | (cpipe internal) |
| `LENS_DISTORTION` | `LENS_DISTORTION` (API 28+) | k1..k5 radial | OpcodeList3 (WarpRectilinear) |
| `LENS_INTRINSIC_CALIBRATION` | (API 28+) | fx,fy,cx,cy,s | (cpipe internal) |
| `STATISTICS_HOT_PIXEL_MAP` | per-frame hot-pixel locations | for repair node | OpcodeList1 (FixBadPixelsList) |
| `CONTROL_AE_*` | exposure target / state | metadata only | — |
| `CONTROL_AWB_*` | white-balance state | metadata only | — |
| `DYNAMIC_RANGE_PROFILE` (API 33+) | 10-bit HDR profile | for HDR HEIF | (cluster E, report 14) |

`CameraCharacteristics` carries the *static* per-device state used to populate
DNG's calibration-side tags:

| Field | DNG tag |
|---|---|
| `SENSOR_INFO_COLOR_FILTER_ARRANGEMENT` | CFAPattern (RGGB / GRBG / GBRG / BGGR) |
| `SENSOR_INFO_ACTIVE_ARRAY_SIZE` | ActiveArea |
| `SENSOR_INFO_PIXEL_ARRAY_SIZE` | DefaultCropOrigin/Size |
| `SENSOR_INFO_WHITE_LEVEL` | WhiteLevel |
| `SENSOR_INFO_PHYSICAL_SIZE` | (in JSON sidecar; not a DNG tag) |
| `SENSOR_BLACK_LEVEL_PATTERN` | static fallback black per-channel |
| `SENSOR_REFERENCE_ILLUMINANT1/2` | CalibrationIlluminant1/2 |
| `SENSOR_COLOR_TRANSFORM1/2` | ColorMatrix1/2 |
| `SENSOR_FORWARD_MATRIX1/2` | ForwardMatrix1/2 |
| `SENSOR_CALIBRATION_TRANSFORM1/2` | CameraCalibration1/2 |
| `SENSOR_AVAILABLE_TEST_PATTERN_MODES` | (used during calibration) |
| `STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES` | enables hot-pixel stream |
| `NOISE_PROFILE` (per-result) or `SENSOR_INFO_NOISE_PROFILE` | NoiseProfile (per-channel a, b coefficients) |
| `LENS_INFO_AVAILABLE_*` | (lens database; cpipe internal) |

**`SENSOR_FORWARD_MATRIX1/2`** are a 3×3 matrix from CIE XYZ to the device's
native sensor color space. They are the inverse direction of `ColorMatrix1/2`
(camera native → CIE XYZ); the DNG spec wants both. Camera2 exposes both.[²¹](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics)

**`NOISE_PROFILE`** is the per-channel `(a, b)` model where the per-pixel
variance equals `a * level + b`. This goes directly into DNG's NoiseProfile
tag and feeds cpipe's denoise node (cluster C, report 07).

#### Partial results

Camera2 supports `CaptureCallback.onCaptureProgressed(...)` *before*
`onCaptureCompleted(...)` — the HAL emits "partial" results for fields
that are available early (e.g. `SENSOR_TIMESTAMP` is known at exposure
end, but white balance gains may take longer to converge). cpipe should
hold a per-frame metadata accumulator keyed by frame number, merge
partials, and only release the buffer to the pipeline once the final
`onCaptureCompleted` fires (or a 100 ms timeout, whichever first).

### 3.6 DNG output: `DngCreator` vs custom writer

`DngCreator` (in `android.hardware.camera2`) wraps a TIFF/DNG writer and
takes `(CameraCharacteristics, CaptureResult, Image)` plus location data
to produce a DNG byte stream.[²²](https://developer.android.com/reference/android/hardware/camera2/DngCreator)
It populates ColorMatrix1/2, ForwardMatrix1/2, AsShotNeutral, NoiseProfile,
ActiveArea, DefaultCropOrigin/Size, BlackLevelRepeatDim,
CFAPattern, BlackLevel, WhiteLevel, and standard EXIF.

What it **does not** populate:
- **`OpcodeList1/2/3`** — DNG's executable processing instructions for hot-pixel repair, gain map (vignetting), warp rectilinear (lens distortion). cpipe's calibration model (cluster E, report 15) wants these.
- **DNG 1.6 / 1.7 tags** — `ProfileGainTableMap` (HDR look profiles), `RGBTables`, `IlluminantData`, semantic mask metadata.
- **Embedded JPEG preview at full resolution** — DngCreator embeds a small JPEG only.
- **Per-channel dynamic black level** — only static `SENSOR_BLACK_LEVEL_PATTERN` is written.
- **Custom ICC color profile** — DngCreator writes the standard DNG-internal calibration; cpipe may want to embed an ICC for ProPhotoRGB editing tags.

**Status (2026-05-08):** `DngCreator` is **not deprecated**. The class
continues to ship and AOSP source mirrors it actively
(`platform_frameworks_base/core/java/android/hardware/camera2/DngCreator.java`,
JNI in `core/jni/android_hardware_camera2_DngCreator.cpp`). It just hasn't
been updated to DNG 1.6/1.7.

**Recommended split:**

- **Internal pipeline**: bypass `DngCreator`. cpipe ingests
  `(AHardwareBuffer + CaptureResult + CameraCharacteristics)` and writes its
  own struct (a "DNG-equivalent in memory") that the pipeline consumes.
- **External "Save DNG" feature**: implement *cpipe's own* DNG writer using
  **libtiff + custom OpcodeList encoder**. This is the same writer used in
  the Linux CLI (D1) — Android shares it. We do not need a separate writer.
  Cluster E report 12 owns the format, cluster E report 15 owns the OpcodeList
  semantics; this report just locks in that we don't rely on `DngCreator` for
  the canonical path.

Adobe's `dng_sdk` is **not Apache-2.0 compatible** for redistribution as
modified source (custom MIT-style license with attribution); using it as a
build-time-only reference for tag values is fine. cpipe's writer is
greenfield, with `dng_sdk` as a comparison target during testing only.

### 3.7 Multi-camera and concurrent streams

Android 9 (API 28) introduced *logical multi-camera*.[²³](https://developer.android.com/media/camera/camera2/multi-camera)
A logical camera ID groups N physical IDs (e.g. main + telephoto +
ultrawide) and presents a single `CameraDevice`. The HAL chooses which
physical sensor backs the output stream based on zoom level, lighting,
etc. For cpipe v1 the logical camera is sufficient — capture one Bayer
stream and let the HAL switch sensors.

**Physical-stream replacement** (API 28+) lets the app *also* request raw
streams from each physical camera. The pattern is:

```kotlin
val mainPhysicalId = "0"
val teleId         = "1"

val rawConfigMain = OutputConfiguration(rawSurfaceMain).apply {
  setPhysicalCameraId(mainPhysicalId)
  setStreamUseCase(STILL_CAPTURE.toLong())
}
val rawConfigTele = OutputConfiguration(rawSurfaceTele).apply {
  setPhysicalCameraId(teleId)
  setStreamUseCase(STILL_CAPTURE.toLong())
}

val sessionConfig = SessionConfiguration(
  SessionConfiguration.SESSION_REGULAR,
  listOf(rawConfigMain, rawConfigTele),
  executor, sessionStateCallback)

logicalCameraDevice.createCaptureSession(sessionConfig)
```

Each `CaptureRequest` then targets one or both surfaces and the HAL fans
out to both physical sensors simultaneously. Time sync between physical
cameras is governed by **`LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE`**, with
two values: `APPROXIMATE` (no hardware sync, software-aligned timestamps,
typical drift ≤ 1 ms) and `CALIBRATED` (hardware shutter
sync).[²⁴](https://learn.microsoft.com/en-us/dotnet/api/android.hardware.camera2.camerametadata.logicalmulticamerasensorsynctypecalibrated)
Most modern flagships report `CALIBRATED`.

**Concurrent camera IDs** (`CameraManager.getConcurrentCameraIds()`) tell
the app which combinations of *separate logical cameras* (e.g. front +
back) can run simultaneously.[²⁵](https://learn.microsoft.com/en-us/dotnet/api/android.hardware.camera2.cameramanager.concurrentcameraids)
Not relevant for v1; reserve for hypothetical "selfie + main bracketed"
features in v2.

### 3.8 Time synchronization

`SENSOR_TIMESTAMP` is nanos since some monotonic origin (boot time on
modern Android) and refers to the **start of exposure**. Per-frame
timestamp accuracy claims:

- Within one logical camera: **sub-millisecond** between physical streams when `SENSOR_SYNC_TYPE = CALIBRATED`.
- Across multiple Android phones (no hardware sync): drift **< 1.2 ms / minute** of wall clock.[²⁶](https://arxiv.org/pdf/2107.00987)

For cpipe's burst, what matters is the relative timestamp between frames
within the same logical camera. This is sub-millisecond on modern devices.
cpipe stores `(SENSOR_TIMESTAMP_ns, frame_idx)` per buffer; the alignment /
fusion node (cluster C, report 07) uses these to weight motion estimates.

#### Latency: shutter to first RAW frame

Industry-typical numbers (citing public bug threads and HDR+
paper):[²⁷](http://www.hdrplusdata.org/)

- **Shutter button press → camera HAL `captureBurst` accepted**: ~5 ms (in-process).
- **Camera HAL → first SENSOR_TIMESTAMP**: 30–80 ms (AE/AF lock latency, often dominated by previously-pending flush).
- **First sensor read complete → `ImageReader.OnImageAvailableListener.onImageAvailable`**: 5–15 ms (MIPI DMA + driver callback).
- **`onImageAvailable` → cpipe pipeline graph executed first node**: <5 ms (depends on cluster A scheduler).

Total **shutter → cpipe pipeline starts processing first frame** is
typically 50–100 ms on a flagship. This is why D3 specified
"burst-on-shutter" rather than ZSL — the burst-on-shutter latency is human-
perception-borderline (100 ms is roughly one perceptual unit) and a more
involved ZSL ring buffer is the right answer for sub-50 ms feel, but
deferred to v2.

### 3.9 Streaming / preview deferred (v2 sketch only)

D5 defers preview to v2. The architecture-only sketch:

- A separate `OutputConfiguration` tagged `PREVIEW` to a separate
  `ImageReader` of YUV_420_888 or RGBA8 at preview-typical resolution
  (1080p or smaller) is added to the same session.
- `setRepeatingRequest` keeps preview frames flowing.
- `AImageReader_setBufferAvailableCallback` is the native event source.[²⁸](https://developer.android.com/ndk/reference/struct/a-image-reader-image-listener)
- A "lite" cpipe pipeline (downscaled, FP16, no AI) runs the same demosaic
  → WB → tone path on the preview stream. The capture path uses the full
  pipeline.

cpipe's `Buffer` abstraction must support both *sized for burst storage*
and *streaming, recyclable* lifetimes. v1 implements only the former,
but the abstraction lives in cluster A report 02. As long as that
abstraction is recyclable-aware from the start, v2 preview is a config
change, not a rewrite.

### 3.10 Battery, thermal, storage failure modes

Pro mobile camera apps surface these to the user (cluster F report 17).
Camera2 itself does not auto-protect against thermal throttling or
storage-full mid-burst. Specifically:

- **Storage fills mid-burst**: `ImageWriter.queueInputImage` returns
  successfully (image is in app memory), but cpipe's eventual HEIF write
  fails with `IOException`. Burst captures should be fully buffered
  in-RAM and only after pipeline completion attempted to be persisted —
  this aligns naturally with D2 (single 800 MB intermediate cap on a
  100 MP frame fits easily in a flagship's 12 GB RAM).
- **Battery dies during processing**: the foreground service with
  `foregroundServiceType="camera"` keeps the process alive even on the
  lock screen, but a battery-dead device just stops. cpipe should commit
  intermediate state to a temp file (atomic rename) every burst frame so
  a power-cycle resumes from the last completed frame — partial DNG
  burst is recoverable.
- **App switched mid-capture**: Android 14+ disallows starting a camera
  FGS from the background but allows it to continue. cpipe should set
  the FGS *before* invoking `captureBurst`, not lazily.
- **Thermal throttling**: cpipe must read `PowerManager.getCurrentThermalStatus()`
  and on `THERMAL_STATUS_SEVERE+` halve the burst count or drop AI nodes.
  Surface this to the UI via a yellow banner ("Phone is hot — burst limited
  to 5").

### 3.11 Concrete code: cpipe `CameraSource` plugin

Architecture diagram (text):

```
+-------------------------------------------------------------+
|  Kotlin UI (Compose)                                        |
|    └── ShutterButton ──► Native bridge: cpipe_capture_burst |
|                                            │                |
|                          ┌─────────────────┼──────────┐     |
|                          ▼                 ▼          ▼     |
|                JNI: openCamera   JNI: configure  JNI: fire  |
+-------------------------------------------------------------+
                            │             │           │
                            ▼             ▼           ▼
+-------------------------------------------------------------+
|  Native cpipe (C++20)                                       |
|                                                             |
|  CameraSource plugin (capture node)                         |
|  ├── ACameraManager_openCamera                              |
|  ├── AImageReader_newWithUsage(W, H, RAW_SENSOR,            |
|  │       USAGE_GPU_SAMPLED_IMAGE | GPU_DATA_BUFFER, N+4)    |
|  ├── ACameraDevice_createCaptureSession                     |
|  ├── ACaptureRequest_setEntry_*  (per-request manual ctrl)  |
|  ├── ACameraCaptureSession_captureV2                        |
|  └── AImageReader_ImageListener:                            |
|        for each AImage:                                     |
|          AImage_getHardwareBuffer(&ahb);                    |
|          BurstFrame{ahb, captureResultMeta} → BatchedBuffer |
|                                                             |
|  ── exits to scheduler (cluster A/03) ──                    |
|  Buffer wrap: cpipe::Buffer{ahb, fd, fence}                 |
|       │                                                     |
|       ▼                                                     |
|  Vulkan compute:                                            |
|    VkImage(ahb) via VK_ANDROID_external_memory_AHB          |
|    └─ first node: RAW10/12 unpack → RAW16 → FP16 (D9)       |
+-------------------------------------------------------------+
```

#### Skeleton (excerpt — Kotlin → JNI → C++)

```kotlin
// Kotlin UI
class ShutterController(private val nativeBridge: CpipeNative) {
  fun onShutterPressed() {
    nativeBridge.captureBurst(burstCount = 7, mode = "sdr-night")
  }
}

object CpipeNative {
  init { System.loadLibrary("cpipe") }
  external fun captureBurst(burstCount: Int, mode: String): Int
  external fun openCameraDevice(cameraId: String): Long
  external fun closeCameraDevice(handle: Long)
}
```

```cpp
// JNI bridge: native-bridge.cpp
extern "C" JNIEXPORT jint JNICALL
Java_com_cpipe_CpipeNative_captureBurst(
    JNIEnv*, jobject, jint count, jstring jmode) {
  auto mode = ScopedUtf{jmode};
  return cpipe::Engine::current()
      .capture()
      .burst({.count = static_cast<uint32_t>(count),
              .preset = mode.c_str()});
}
```

```cpp
// CameraSource.cpp (excerpt)
class CameraSource : public Plugin {
public:
  Status burst(BurstRequest req) override {
    if (!session_) {
      configureSession_();
    }
    std::vector<ACaptureRequest*> requests;
    requests.reserve(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
      ACaptureRequest* r;
      ACameraDevice_createCaptureRequest(
          device_, TEMPLATE_STILL_CAPTURE, &r);
      ACaptureRequest_addTarget(r, rawTarget_);
      // per-frame manual override (e.g. exposure bracket)
      auto exposureNs = req.exposureNs.value_or(autoExposureNs_);
      ACaptureRequest_setEntry_i64(r, ACAMERA_SENSOR_EXPOSURE_TIME,
                                   1, &exposureNs);
      requests.push_back(r);
    }
    int seqId = 0;
    return wrapStatus(
      ACameraCaptureSession_captureV2(
        session_, &captureCallbacks_, requests.size(),
        requests.data(), &seqId));
  }

private:
  void onImageAvailable_(AImageReader*) {
    AImage* img;
    if (AImageReader_acquireNextImage(reader_, &img) != AMEDIA_OK) return;
    AHardwareBuffer* ahb;
    AImage_getHardwareBuffer(img, &ahb);
    int64_t ts;
    AImage_getTimestamp(img, &ts);
    auto meta = pendingMeta_.consume(ts);  // joined with CaptureResult
    BurstFrame f{ahb, std::move(meta), img};
    sink_->push(std::move(f));
  }
};
```

`pendingMeta_` is a small `<int64_t timestamp, FrameMeta>` map that
absorbs the asynchronous arrival of `CaptureResult` (via
`onCaptureCompleted`) and `AImage` (via `OnImageAvailable`). The `AImage`
release is deferred until cpipe's pipeline `release(BurstFrame&)` —
holding the `AImage` keeps the `AHardwareBuffer` alive for the GPU.

### 3.12 Reference repositories (inspected commits)

cpipe's capture layer should consult:

1. **`android/camera-samples`** (Apache-2.0). The official Camera2Basic and Camera2Raw samples. Camera2Basic shows ImageReader wiring, repeating request, and JPEG/RAW capture. Last activity tag `2024-12` (master branch).[²⁹](https://github.com/android/camera-samples)
2. **`opensource:opencamera/code` (Mark Harman, GPLv3)**. Read for inspiration only — D11 hard-blocks GPLv3 in cpipe. Useful for understanding edge cases (Samsung quirks, Galaxy Tab quirks, RAW capture on devices that "lie" about RAW capability).[³⁰](https://opencamera.org.uk/)
3. **`KillerInk/FreeDcam`** (GPLv2). Same blocker. Useful as a survey of hard-to-find vendor extensions (Sony WIFI Remote, Samsung 100MP-only modes), but cpipe code may not derive from it.[³¹](https://github.com/KillerInk/FreeDcam)
4. **`kiryldz/android-hardware-buffer-camera`** (Apache-2.0). C++ end-to-end Camera2 → AHardwareBuffer → Vulkan/OpenGL preview. The closest reference for cpipe's intended architecture.[³²](https://github.com/kiryldz/android-hardware-buffer-camera)
5. **`ktzevani/native-camera-vulkan`** (MIT). Native Camera2 → AHardwareBuffer → Vulkan compute pipeline. Good for the pipeline binding pattern.[³³](https://github.com/ktzevani/native-camera-vulkan)
6. **`prime-slam/OpenCamera-Sensors`** (GPLv3). Multi-camera + IMU sync research fork; useful for the time-sync proof-of-correctness in §3.8.[³⁴](https://github.com/prime-slam/OpenCamera-Sensors)
7. **`PkmX/lcamera`** (BSD-2). Historic Camera2 + Lollipop reference; archive value, not modern.[³⁵](https://github.com/PkmX/lcamera)

For cpipe's writer of canonical C++ code, **`kiryldz/android-hardware-buffer-camera`**
(commit history active 2024-2025) and **`ktzevani/native-camera-vulkan`**
(MIT, last commit 2024) are the two cleanest models — both Apache/MIT
compatible and both implement the exact AImageReader → AHardwareBuffer →
Vulkan path cpipe needs.

## 4. Architecture sketches

### 4.1 Capture-path state machine

```
                        (idle)
                          │
              CameraManager.openCamera
                          │
                          ▼
          (opening) ──── error ──► (closed)
                          │
              onOpened(device)
                          │
                          ▼
                    (opened)
                          │
              configureSession_()
                          │
                          ▼
          (configuring) ─ error ──► (opened) ──► (closed)
                          │
              onConfigured(session)
                          │
                          ▼
                  (idle/ready)
                  │            ▲
        captureBurst()         │
                  │       last frame's
                  ▼       onCaptureCompleted
            (capturing)─────────┘
                  │
        onImageAvailable × N (collected)
                  │
                  ▼
            (handoff): BatchedBuffer{ ahb[N], meta[N] } pushed to scheduler
                  │
                  ▼
              (idle/ready)
```

### 4.2 BurstFrame data model

```cpp
struct FrameMeta {
  // From CaptureResult (per-frame)
  int64_t  sensor_timestamp_ns;
  int64_t  exposure_time_ns;
  int32_t  iso;
  int64_t  frame_duration_ns;
  std::array<float, 4>  cc_gains;        // R Gr Gb B
  std::array<float, 3>  neutral_color_point;
  std::array<float, 9>  color_correction_transform; // 3x3
  bool     black_level_lock;
  std::array<float, 4>  dynamic_black_level;
  float    lens_focal_length_mm;
  float    lens_aperture;
  float    lens_focus_distance_diopters;
  std::array<float, 5>  lens_distortion;  // k1..k5
  std::array<float, 5>  lens_intrinsic;   // fx fy cx cy s

  // From CameraCharacteristics (static, copied for self-containment)
  uint32_t cfa_pattern;
  uint16_t white_level;
  std::array<float, 9>  color_matrix1, color_matrix2;
  std::array<float, 9>  forward_matrix1, forward_matrix2;
  std::array<float, 9>  calibration_transform1, calibration_transform2;
  uint8_t  reference_illuminant1, reference_illuminant2;
  std::vector<std::pair<float, float>> noise_profile_per_channel; // (a, b)

  // Capture context
  std::string camera_id;            // logical id
  std::string physical_camera_id;   // empty if logical-only
  uint32_t    burst_index;
  uint32_t    burst_size;
};

struct BurstFrame {
  AHardwareBuffer*   buffer;     // refcounted, owned by cpipe::Buffer wrapper
  FrameMeta          meta;
  AImage*            backing;    // releases buffer when free()'d
};

struct BatchedBuffer {
  std::vector<BurstFrame> frames;
  // shared characteristics that are identical across burst (cfa pattern etc.)
  // could be hoisted, but per-frame self-contained data is simpler.
};
```

The `BatchedBuffer` is what cpipe's pipeline DAG receives. Per-frame
metadata travels alongside the buffer all the way through the DAG so any
node that needs (e.g.) the per-frame exposure time has direct access.

### 4.3 Hand-off to Vulkan compute (cluster A interaction)

```cpp
// cpipe::Buffer (cluster A report 02 owns this in detail)
struct VulkanContext { /* device, allocator, ... */ };

class Buffer {
public:
  static Buffer fromAHardwareBuffer(VulkanContext& ctx, AHardwareBuffer* ahb);

  VkImage      image() const   { return img_; }
  VkDeviceMemory memory() const { return mem_; }

private:
  VkImage img_;
  VkDeviceMemory mem_;
};

Buffer Buffer::fromAHardwareBuffer(VulkanContext& ctx, AHardwareBuffer* ahb) {
  AHardwareBuffer_Desc desc{};
  AHardwareBuffer_describe(ahb, &desc);
  // Use VK_ANDROID_external_memory_android_hardware_buffer
  VkAndroidHardwareBufferPropertiesANDROID props{
    .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID
  };
  vkGetAndroidHardwareBufferPropertiesANDROID(ctx.device, ahb, &props);
  // … (full code in cluster A report 02)
}
```

cpipe's first compute shader, run on the GPU image, unpacks RAW10/12 into
RAW16 (if the source format isn't already 16-bit) and converts to FP16
(per D9 default precision). The shader reads `BlackLevel`, `WhiteLevel`,
and `cc_gains` from a uniform buffer that mirrors `FrameMeta`.

## 5. Cited sources

1. Android Developers Blog. *Introducing CameraX 1.5: Powerful Video Recording and Pro-level Image Capture*. 2025-11. <https://android-developers.googleblog.com/2025/11/introducing-camerax-15-powerful-video.html>
2. Android CameraX Discussion Group. *How to use Burst mode in CameraX like Camera2 API*. <https://groups.google.com/a/android.com/g/camerax-developers/c/Z1Y7fjqjo6o>
3. Android Developers. *Choose a camera library*. 2024. <https://developer.android.com/media/camera/choose-camera-library>
4. Android Developers. *android.hardware.camera2 | API reference*. <https://developer.android.com/reference/android/hardware/camera2/package-summary>
5. Android NDK. *Media | Android NDK*. <https://developer.android.com/ndk/reference/group/media>
6. Android Developers. *Foreground service types | Background work*. <https://developer.android.com/develop/background-work/services/fgs/service-types> and <https://developer.android.com/about/versions/14/changes/fgs-types-required>
7. Android Developers. *Grant partial access to photos and videos*. 2023. <https://developer.android.com/about/versions/14/changes/partial-photo-video-access>
8. Android Developers. *ImageFormat | API reference*. <https://developer.android.com/reference/android/graphics/ImageFormat>
9. Android Developers. *ImageFormat#RAW10*. <https://developer.android.com/reference/android/graphics/ImageFormat#RAW10>
10. Android Developers. *ImageFormat#RAW12*. <https://developer.android.com/reference/android/graphics/ImageFormat#RAW12>
11. Sony Semiconductor Solutions. *Quad Bayer Coding | Image Sensor for Mobile*. <https://www.sony-semicon.com/en/technology/mobile/quad-bayer-coding.html>
12. Samsung Semiconductor. *ISOCELL HP2 | Mobile Image Sensor*. <https://semiconductor.samsung.com/image-sensor/mobile-image-sensor/isocell-hp2/>
13. Samsung Newsroom. *Samsung Introduces the 200-Megapixel Image Sensor for the Ultimate High Resolution Experience*. 2023-01. <https://news.samsung.com/global/samsung-introduces-the-200-megapixel-image-sensor-for-the-ultimate-high-resolution-experience-in-flagship-smartphones>
14. Jia, Liu et al. *Learning Rich Information for Quad Bayer Remosaicing and Denoising*. 2022. <https://jhc.sjtu.edu.cn/~xiaohongliu/papers/2022learning.pdf>
15. Android Developers. *CameraCharacteristics#SCALER_AVAILABLE_STREAM_USE_CASES*. <https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics>
16. AOSP. *platform_frameworks_base/media/java/android/media/ImageReader.java* (master). <https://github.com/aosp-mirror/platform_frameworks_base/blob/master/media/java/android/media/ImageReader.java>
17. Android Developers. *Native Hardware Buffer | Android NDK*. <https://developer.android.com/ndk/reference/group/a-hardware-buffer>
18. Hackage. *Vulkan.Extensions.VK_ANDROID_external_memory_android_hardware_buffer*. <https://hackage.haskell.org/package/vulkan-3.6.7/candidate/docs/Vulkan-Extensions-VK_ANDROID_external_memory_android_hardware_buffer.html>
19. Android Developers. *CameraCaptureSession#captureBurst*. <https://developer.android.com/reference/android/hardware/camera2/CameraCaptureSession#captureBurst>
20. Android Developers. *CameraConstrainedHighSpeedCaptureSession*. <https://developer.android.com/reference/android/hardware/camera2/CameraConstrainedHighSpeedCaptureSession>
21. Android Developers. *CameraCharacteristics | API reference*. <https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics>
22. Android Developers. *DngCreator | API reference*. <https://developer.android.com/reference/android/hardware/camera2/DngCreator>
23. Android Developers. *Multi-camera API*. <https://developer.android.com/media/camera/camera2/multi-camera>
24. Microsoft Learn. *CameraMetadata.LogicalMultiCameraSensorSyncTypeCalibrated*. <https://learn.microsoft.com/en-us/dotnet/api/android.hardware.camera2.camerametadata.logicalmulticamerasensorsynctypecalibrated>
25. Microsoft Learn. *CameraManager.ConcurrentCameraIds*. <https://learn.microsoft.com/en-us/dotnet/api/android.hardware.camera2.cameramanager.concurrentcameraids>
26. arXiv. *Sub-millisecond Video Synchronization of Multiple Android Smartphones*. 2021. <https://arxiv.org/pdf/2107.00987>
27. Hasinoff, Sharlet, Geiss, Adams, Barron, Kainz, Chen, Levoy. *Burst photography for high dynamic range and low-light imaging on mobile cameras*. SIGGRAPH Asia 2016. <https://research.google/pubs/pub45586/>
28. Android Developers. *AImageReader_ImageListener Struct Reference*. <https://developer.android.com/ndk/reference/struct/a-image-reader-image-listener>
29. Google. *android/camera-samples* GitHub repository. <https://github.com/android/camera-samples>
30. Mark Harman. *Open Camera*. <https://opencamera.org.uk/>
31. KillerInk. *FreeDcam* GitHub repository. <https://github.com/KillerInk/FreeDcam>
32. Kiryl Dzeraviashka. *android-hardware-buffer-camera*. <https://github.com/kiryldz/android-hardware-buffer-camera>
33. Konstantinos Tzevanidis. *native-camera-vulkan*. <https://github.com/ktzevani/native-camera-vulkan>
34. SLAM Research Group. *OpenCamera-Sensors*. <https://github.com/prime-slam/OpenCamera-Sensors>
35. PkmX. *lcamera*. <https://github.com/PkmX/lcamera>
36. Android Developers. *Use multiple camera streams simultaneously*. <https://developer.android.com/media/camera/camera2/multiple-camera-streams-simultaneously>
37. Android Developers. *Capture an image | CameraX*. <https://developer.android.com/media/camera/camerax/take-photo>
38. Android Developers. *Behavior changes: Apps targeting Android 16 or higher*. <https://developer.android.com/about/versions/16/behavior-changes-16>
39. Android Developers. *Camera capture sessions and requests*. <https://developer.android.com/media/camera/camera2/capture-sessions-requests>
40. AOSP. *core/java/android/hardware/camera2/CameraMetadata.java*. <https://android.googlesource.com/platform/frameworks/base/+/master/core/java/android/hardware/camera2/CameraMetadata.java>

## 6. See also

- [01 — Compute frameworks](01-compute-frameworks.md): the Vulkan / WebGPU runtime that consumes the AHardwareBuffer.
- [02 — Zero-copy buffer architecture](02-zero-copy-buffer-architecture.md): the AHardwareBuffer ↔ VkImage hand-off this report writes against.
- [03 — Heterogeneous scheduler](03-heterogeneous-scheduler.md): the queue that the burst frames feed into.
- [04 — Mobile AI inference](04-mobile-ai-inference.md): the AI demosaic / denoise that consume the FP16 RAW.
- [07 — Classic ISP algorithms](07-classic-isp-algorithms.md): demosaic / WB / NR; consumer of the metadata in §3.5.
- [12 — DNG format](12-dng-format.md): the DNG-equivalent struct cpipe writes from CaptureResult.
- [13 — Color management](13-color-management.md): consumes ColorMatrix1/2 and ForwardMatrix1/2 to compute the working-space transform.
- [15 — Mobile camera calibration](15-mobile-camera-calibration.md): consumes NoiseProfile, LensDistortion, etc.; defines OpcodeList tags cpipe writes.
- [17 — Mobile pro camera apps](17-mobile-pro-camera-apps.md): consumes the capture state machine through the UX abstraction.

## 7. Open questions

1. **Native Quad Bayer 4-cell exposure on Pixel / S25 Ultra**: As of 2026-05 no public Android API exposes raw 4-cell Quad Bayer. Is this likely to land in Android 17 / API 36? If yes, cpipe should reserve a CFA-pattern enum slot for `QUAD_BAYER_RGGB` etc.
2. **`DngCreator` vs custom writer cost**: the custom writer is ~600 lines of libtiff. We claim it's necessary for OpcodeList; is there a Java reflection-based hack to inject custom tags into `DngCreator` output? Worth a 1-day spike.
3. **AHardwareBuffer fence semantics across burst**: AImage carries a fence FD via `AImage_getHardwareBuffer` that the producer signals. We assume `vkAcquireImageANDROID` (or its successor) is the right import path. Cluster A report 02 must confirm.
4. **HEIC capture as a fallback**: Android 13+ exposes `ImageFormat.HEIC` directly. Does any device write *real* HEIF (HEVC main profile, 10-bit) with Camera2's HEIC stream, or is it always SDR JPEG-like quality? cpipe writes its own HEIF (cluster E report 14) but a fallback is worth knowing.
5. **`CONTROL_MODE_USE_SCENE_MODE` and `SCENE_MODE_HDR`**: any HAL behaviour we lose by ignoring scene modes? Modern cameras seem to respect manual modes correctly, but vendor extensions vary. Need a device matrix.
6. **Android 16 `EXTENSION_NIGHT_MODE_INDICATOR`**: feed this into a cpipe pipeline preset switch (sdr / sdr-night / hdr)? Worth one paragraph in the UX report (17).
7. **Concurrent rear+front for "selfie sticker on pro shot"**: out of scope for v1, but the architecture must not preclude it. Cluster D report 11's pipeline editor needs to know whether multiple capture sources are part of the DAG model.
8. **Vendor extensions (Sony's "PRO mode" stream, Samsung's "Expert RAW" stream)**: Sony / Samsung expose private sensor modes via vendor extension APIs. Do we want to surface these in v1 (best image quality on those phones) or wait for v2? Recommend **wait** — cpipe's value is the pipeline, not the capture trick.
