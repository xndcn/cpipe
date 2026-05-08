# Report 17 — Mobile Pro Camera Apps: UX & Architecture Lessons

> Cluster F · cpipe research package · drafted **2026-05-08**.
> Owns the user-facing precedent: which iOS / Android pro camera apps cpipe
> is positioned among, what UX patterns we should adopt, and what
> architectural choices their public material reveals.

## 1. TL;DR

cpipe's Android UI should be modelled on **Halide (Lux) for control surface**
("camera, not app", gesture-first, one-handed) and **Adobe Lightroom Mobile
for sync workflow** (capture → cloud-synced DNG → desktop-resumable session).
Specifically: **swipe-up/down for exposure, swipe-left/right for focus**,
**haptic feedback on horizon level / focus peaking lock / capture commit**,
**a single shutter button that handles long-press for burst**, and
**WYSIWYG with computational defaults off** — the cpipe pipeline is the
draw, so the viewfinder shows the pipeline output, not Android's stock
preview.

For sync, the **editor connectivity story (cluster D, report 11)** maps onto
Lightroom's "smart preview" pattern: capture writes a full DNG locally; a
small (2 MP, lossy DNG) "smart preview" syncs to the desktop / web editor for
node graph editing; commit pushes a render job back to the device that
applies the graph to the original RAW.

For multi-frame, the literature is unambiguous: **HDR+ (Hasinoff 2016)** is
the canonical reference. cpipe should not invent a new burst-fusion scheme;
it should ship the HDR+ pipeline as a default preset and let users author
alternatives via the node graph.

For failure modes, **OpenCamera surfaces thermal throttle, storage-full mid-burst, and battery state to the user**. cpipe must do the same; the
foreground service from report 16 keeps the process alive, but the UI must
surface "phone is hot, burst limited to 5" or "storage 200 MB free, can save
2 more bursts."

License-critical: **OpenCamera, FreeDcam, Snap Camera are GPLv3** and *cannot*
be used for code; D11 hard-blocks GPL. They are inspiration only.

## 2. Decision matrix

| App | License | Platform | RAW path | Multi-frame | Sync model | Lessons cpipe takes |
|---|---|---|---|---|---|---|
| Halide Mark II / III | Closed | iOS | ProRAW + RAW | Smart RAW (ML exposure), Process Zero (no AI) | Photos-rolled + Process app for desktop | Gesture controls, "camera not app" framing, two-mode capture (computational on / Process Zero) |
| Adobe Lightroom Mobile | Closed | iOS / Android | DNG + ProRAW | HDR (3-frame) | Adobe CC cloud sync, smart previews | Smart preview pattern is the cpipe sync model |
| ProCamera | Closed | iOS | RAW + LiveHDR | LowLight+, HDR | Photos-rolled | Manual + auto + semi-auto mode triad |
| Moment Pro Camera II | Closed | iOS | RAW + ProRAW | iOS-native HDR / Deep Fusion | Photos-rolled, custom storage path | Lens-aware metadata; left-handed layout option |
| ProShot | Closed (Android) | Android | DNG + RAW+JPEG | OS-default | Local | Customizable shooting modes (Auto, Program, Manual, 2 Custom) |
| Filmic Pro v7 / Firstlight | Subscription | iOS / Android | RAW (Firstlight) | Live analytics (waveform, vectorscope, false color, zebra, focus peaking) | Local | Reference set of pro analytic overlays — cpipe should ship them all |
| Hasselblad Phocus Mobile 2 | Closed | iOS | Tethered DNG/JPEG | n/a | Wi-Fi to camera | Camera-as-network-peer pattern; cpipe's editor connectivity reuses this idea |
| Reincubate Camo | Closed | iOS / Android / desktop | YUV stream | n/a | USB / Wi-Fi to desktop | Two-app architecture (capture / studio) precedent |
| Pixel Camera | Closed | Android (Pixel) | ProRAW (Tensor G5) | HDR+, Night Sight, Real Tone | Google Photos sync | The HDR+ paper is a public reference for our default multi-frame preset |
| iPhone Camera | Closed | iOS | ProRAW | Smart HDR, Deep Fusion, Photonic Engine | iCloud Photos sync | Public talks document the 12-stage Photonic Engine pipeline; cpipe's pipeline should match its expressivity but be user-configurable |
| Samsung Expert RAW | Closed | Android (Galaxy) | 16-bit DNG (multi-frame merged) | Multi-frame to DNG | Samsung Cloud | "Pro" mode = single-frame DNG, "Expert RAW" = multi-frame DNG; cpipe should give the user the same toggle |
| OpenCamera | GPLv3 | Android | DNG | Burst, OS NR | Local | Inspiration only. Failure-mode UI is the model. |
| FreeDcam | GPLv2 | Android | DNG (vendor extensions) | n/a | Local | Inspiration only. Vendor-extension mining is interesting future work. |
| Hipstamatic Classic Camera | Closed | iOS | RAW | n/a | Local | Tactile camera UI / film canister metaphor |
| VSCO Capture | Closed | iOS (2025) | RAW + ProRAW (12 / 48 MP) | iOS-native | VSCO cloud | Subscription-based camera-and-edit; latest entrant |

## 3. Detailed findings

### 3.1 Halide (Lux Optics) — the gold standard for pro-camera UX

Halide Mark II shipped in 2020 as a ground-up redesign of the original
2017 Halide.[¹](https://medium.com/halide/introducing-halide-mkii-30f9f2bceac3)
A public preview of Mark III was released in late 2025 / early 2026,
focused on **Smart RAW** (ML-driven RAW exposure for max detail with low
noise) and **Process Zero II** (HDR-capable, Night-Mode-capable, ProRAW-
capable AI-free pipeline).[²](https://petapixel.com/2026/01/28/halide-mark-iii-doubles-down-on-ai-free-iphone-photography/)

#### Design philosophy

Co-founder Ben Sandofsky and designer Sebastiaan de With have repeated, in
multiple interviews and in Apple's "Behind the Design" feature, the framing
that frames the entire UX:

> "We didn't say we made an app. We say we made a camera. That was a
> philosophical underpinning of everything we did."[³](https://developer.apple.com/news/?id=x6bv1a36)

That commitment shows up as:

- **One-handed control**. The app explicitly targets thumb-reach controls.[⁴](https://www.macstories.net/reviews/halide-mark-ii-review-the-convenience-of-computational-photography-and-flexibility-of-raw-in-an-elegant-camera-app/)
- **Gesture-first, button-secondary**. Swipe up/down on the viewfinder for exposure, swipe left/right for focus — this is the tactile dial replacement.
- **Haptic feedback as confirmation**. When the horizon-level indicator shows the phone is straight, a small haptic fires; same for AF lock, AE lock, capture commit. Halide describes this as "abundant" use of haptics.[⁵](https://halide.cam/)
- **Tactile Touch** — focus and exposure aids appear *while you press*, not as a permanent overlay; the assumption is "the photographer knows the framing."
- **Custom typography**. Halide uses fonts that mimic engraved metal on classic cameras. Small, but the cumulative effect signals "this is camera-grade software."

#### Process Zero / Smart RAW — the dual mode

Halide's most-cited *architectural* lesson is the **two-mode capture**
they shipped in 2024:

- **Smart RAW**: ML exposes the RAW for max detail and low noise (this is computational, but it's not multi-frame fusion in the HDR+ sense).
- **Process Zero**: a single exposure, **no AI, no computational stacking**, written as 12 MP DNG. The user gets one slider that mimics film development time.[⁶](https://www.lux.camera/introducing-process-zero-for-iphone/)

This is the right mental model for cpipe. **The default cpipe pipeline
does multi-frame HDR+-like fusion. A "Process Zero" preset disables every AI
node and emits a single-frame DNG-equivalent.** Both must be present in the
shipped node-graph library. The user picks at the shutter level.

Mark III generalises this further: ProRAW + Process Zero (the user gets
12-bit ProRAW with no Apple processing, and a deliberate analog film look
in `Chroma Noir` for B&W).[²](https://petapixel.com/2026/01/28/halide-mark-iii-doubles-down-on-ai-free-iphone-photography/)
The lesson: **the ability to pick the pipeline is more important than the
pipeline being best-in-class.** Pros want control over which processing
runs.

#### Halide architecture (what's public)

Halide does not open-source its capture path, but Sandofsky's blog and the
AppleInsider podcast reveal:

- The capture path uses AVCaptureSession (iOS's Camera2-equivalent), AVCapturePhotoOutput for stills, with `AVCapturePhotoSettings.rawPhotoPixelFormatType` for RAW DNG.[⁷](https://appleinsider.com/articles/21/06/14/camera-app-development-for-iphone-interview-with-the-halide-team-on-the-appleinsider-podcast)
- Process Zero II's pipeline is implemented in **Metal compute shaders**; they avoid Apple's `Photos` framework's automatic processing by reading the raw `CMSampleBuffer` directly.
- Halide ships a **macOS companion called Process** that consumes the iOS-captured DNG over iCloud Photos and runs additional processing. This is the precedent for cpipe's web editor: capture on phone, edit on desktop, with the original RAW (not a JPEG) as the canonical asset.
- Halide writes only standard DNG, not a proprietary container; a "Process settings" sidecar (XMP-like) lives alongside.

#### Halide's button placement (public material)

The Mark II layout (described in MacStories' detailed review[⁴](https://www.macstories.net/reviews/halide-mark-ii-review-the-convenience-of-computational-photography-and-flexibility-of-raw-in-an-elegant-camera-app/)):

- Top edge: format toggle (RAW / HEIC / JPEG), flash, level, grid.
- Bottom edge: shutter (right thumb), gallery preview (left thumb), camera switch (right thumb), white balance (right thumb).
- Side dial (in iPad mode): exposure compensation.
- iPhone 16's Camera Control button (a hardware key Apple added) is wired by Halide to expose / focus / format toggle.[⁸](https://www.idownloadblog.com/2024/10/01/halide-camera-control-button-exposure-gain-focus-iphone-16/)

For cpipe Android, the equivalent is **Compose with `Modifier.pointerInput`
gesture handlers, anchored to the right thumb's reachable arc**. The shutter
is at the bottom-right, with a long-press for burst expansion.

### 3.2 Adobe Lightroom Mobile — the sync model cpipe inherits

Lightroom Mobile launched April 2014 (iPad), January 2015 (iPhone /
Android).[⁹](https://en.wikipedia.org/wiki/Adobe_Lightroom)
DNG capture in mobile arrived in v2.5 (iOS) / v2.0 (Android), and v2.7 brought
in-camera HDR (3-frame, exposure-bracketed bright/normal/dark, merged
in-app).[¹⁰](https://helpx.adobe.com/lightroom-cc/using/hdr-android.html)

#### What Lightroom Mobile does well

- **Pro mode controls**: Aperture, ISO, shutter speed, white balance — manual, semi-auto, auto.[¹¹](https://helpx.adobe.com/lightroom-cc/using/capture-photos-mobile-ios.html)
- **HDR mode**: bright/normal/dark exposures *intelligently chosen by scene brightness*, not user-bracketed. Output is a single floating-point DNG (linear, large dynamic range).
- **Cloud sync that uses smart previews**. The original RAW lives on the device; a *smart preview* (lossy DNG, ~2 MP) is synced to the cloud and to the desktop. The desktop edits the smart preview; on commit, the edits are reapplied to the original RAW.[¹²](https://helpx.adobe.com/lightroom-classic/help/lightroom-smart-previews.html)
- **Same edit graph travels**: the XMP sidecar moves with the photo through cloud sync; both mobile and desktop edits are non-destructive and idempotent.

#### Why this is the cpipe sync pattern

cpipe's editor (cluster D, report 11) is web-based. The phone shoots a 100-MP
DNG; we cannot push 200 MB up the WebSocket on every shutter. Smart preview
solves this: a 2-MP downscale of each burst frame syncs to the editor; the
user edits the node graph on the small preview; commit triggers a render job
on the phone (or a relay) that applies the same node graph to the
full-resolution burst.

The **graph edits and the file are decoupled**. This is the architectural
prize: the user can keep editing while the phone is offline, and the phone
re-renders when reconnected.

#### Lightroom's failure modes (what to copy)

When the device runs out of cloud space, Lightroom shows a banner; original
RAW stays local. cpipe should do the same — never silently drop captured
frames; the device is the source of truth, the cloud is the working draft.

Lightroom Classic (desktop) and Lightroom Mobile have a non-trivial sync
state machine — the public docs reveal pain points (smart preview vs original
sync, "use smart previews instead of originals" preference for low-bandwidth
users).[¹³](https://helpx.adobe.com/mobile-apps/help/lightroom-mobile-faq.html)
cpipe should learn: be explicit in the UI about what's local-only, what's
synced, what's pending — Lightroom hides this and gets accused of
"losing photos."

### 3.3 Reincubate Camo — desktop-mobile bridge precedent

Camo splits responsibilities between two apps:[¹⁴](https://camo.com/support/camo/camo-overview)

- **Camo Camera** (mobile) — captures and forwards video to the desktop. Minimal UI, "just sit on a tripod" mode.
- **Camo Studio** (desktop) — controls the camera remotely, processes the stream (LUTs, crop, watermark), exposes the result as a virtual webcam to Zoom/Slack/etc.

Connection is over USB or Wi-Fi.[¹⁵](https://reincubate.com/camo/)
**The mobile app surface is intentionally small**; the heavy UI lives on the
desktop where the screen real estate is.

This is also the right shape for cpipe. The Android UI does not need to host
the node-graph editor; that's a luxury we can't afford on a 6-inch screen
anyway. The Android UI is *the camera*: capture, basic preview, basic
preset selection. The web editor is *the studio*: node graph, before/after,
A/B mosaicking, batch render.

### 3.4 ProCamera — sliders and the manual-semi-auto-auto triad

ProCamera ships a triad of capture modes:[¹⁶](https://www.eagleeyeadventures.com/blog/procamera-iphone-app-review-2025-best-manual-camera-app-for-ios)

- **Auto / point-and-shoot** — for casual capture.
- **Semi-automatic** — user controls one variable (e.g. shutter), app controls the rest.
- **Full manual** — shutter, ISO, WB, focus, exposure compensation, format.

The on-screen UI changes mode without changing physical layout — i.e., the
shutter button stays put, the slider rail above it changes function.

LiveHDR is an in-app purchase ($3.99 add-on)[¹⁶](https://www.eagleeyeadventures.com/blog/procamera-iphone-app-review-2025-best-manual-camera-app-for-ios)
that does in-camera HDR with three brackets. Their separating it suggests
that **HDR is a chargeable premium feature even in 2025**. cpipe can charge
for this if v3 introduces a paid tier; for v1, it's part of the default
preset library.

### 3.5 Moment Pro Camera II — accessory awareness

Moment is a hardware company (lenses) with a software arm. Their app's
unique value is **lens awareness**:[¹⁷](https://apps.apple.com/us/app/moment-pro-camera-ii/id6748837351)

- The user picks which Moment lens (Wide 18mm, Tele 58mm, Macro 10×, 1.33× Anamorphic, Fisheye 14mm, 1.55× Anamorphic) is attached.
- The app stamps that into image metadata.
- The app applies appropriate distortion / de-squeeze correction.

cpipe could expose this via a "lens database" plugin in v2 that ships
device + accessory metadata profiles. For v1, we capture the OEM metadata
(Camera2's `LENS_DISTORTION` and `LENS_INTRINSIC_CALIBRATION` from report
16 §3.5) and let the user override.

Moment also offers a **left-handed layout**[¹⁷](https://apps.apple.com/us/app/moment-pro-camera-ii/id6748837351),
a humble UX detail with outsized impact. cpipe must support it from day one.

### 3.6 ProShot — Android-specific lessons

ProShot is one of two long-running Android pro apps (with OpenCamera)[¹⁸](https://www.riseupgames.com/proshot/android).
Lessons:

- **Customizable shooting modes**: Auto, Program, two fully-configurable Custom slots. The user can save state and restore. cpipe's pipeline-preset system maps to this directly.
- **ProShot Evaluator** — a separate app that *audits* the device's Camera2 capabilities and reports back (which lens, sensor, RAW support, manual support).[¹⁸](https://www.riseupgames.com/proshot/android) cpipe should ship the equivalent — a "what does this device support?" diagnostic the user can run before reporting bugs.
- **Format toggle prominent**: JPEG / RAW / RAW+JPEG / HEIC. cpipe ships HEIF as default (D17), with DNG sidecar as a power-user toggle.
- **Remappable controls**: the user can move buttons on the canvas. cpipe should expose this as an enthusiast-only setting; default layout is Halide-like.

### 3.7 Filmic Pro / Firstlight — the analytic overlay reference set

Filmic Pro (renamed Filmic Legacy when v7 shipped under a subscription
model in 2023; v6 still updated for bug fixes
only)[¹⁹](https://filmicapps.zendesk.com/hc/en-us/articles/31095485377169-How-do-I-access-Filmic-Pro-V6-if-I-previously-purchased-it)
is video-first, but its analytic overlays are the gold standard for any
pro capture app. cpipe must ship all of them:[²⁰](https://www.filmicpro.com/products/filmic-pro-v6/)

- **Histogram** — luminance, RGB channel, RGB composite, zone (Ansel-Adams 11-zone).
- **Waveform** — luminance vs horizontal position.
- **Vectorscope** — chrominance polar plot, with skin-tone reference lines.
- **False color** — colored overlay where blue = under, red = over, green = correct exposure, with intermediate steps.
- **Zebra stripes** — diagonal stripes on under/over-exposed regions.
- **Focus peaking** — high-frequency edge highlight in chosen color.
- **Clipping** — pixels over/under threshold flashed.

Firstlight is Filmic's stills app, and it has the same analytic
toolset.[²¹](https://apps.apple.com/us/app/filmic-firstlight-photo-app/id1482338564)
For cpipe, every overlay should be implementable as a **GPU compute shader
that reads the preview frame and writes the overlay** — they all naturally
sit at the end of the cpipe pipeline as "viewport plugins."

This is the second big architectural lesson: **the analytic overlays are
themselves cpipe nodes, which run on the preview pipeline (deferred to v2
per D5 but architecturally reserved now).**

### 3.8 Hasselblad Phocus Mobile 2 — camera-as-network-peer

Phocus Mobile 2 connects an iPhone or iPad to a Hasselblad X / V-system
medium-format camera over Wi-Fi.[²²](https://www.hasselblad.com/phocus/phocus-mobile-2/)
Functions:

- Live view from the camera on the iPad screen.
- Remote camera control (aperture, shutter, ISO) and remote shutter.
- Auto-import of captured DNG to the iPad with cloud-friendly storage.
- Camera firmware updates over the link.
- Range up to 40 meters per Hasselblad's marketing.[²³](https://fstoppers.com/gear/wireless-tethering-hasselblad-x2d-and-phocus-mobile-2-711600)

Architecturally, this is **the cpipe editor connectivity story turned 90°**:
in Hasselblad's case the camera is the server and the iPad is the client; in
cpipe's case the phone is the server (it has the RAW) and the laptop / web
editor is the client. The protocol shape is the same — a small live-view
stream + control channel + bulk asset transfer. Cluster D report 11 owns
the protocol; this report just records that Hasselblad's choice (Wi-Fi
direct, no cloud) is one valid point on the design space.

### 3.9 Pixel Camera — the HDR+ benchmark

Pixel Camera is closed source but **Google has published the HDR+ paper
and dataset**:[²⁴](https://research.google/pubs/pub45586/)

- Hasinoff, Sharlet, Geiss, Adams, Barron, Kainz, Chen, Levoy. *Burst photography for high dynamic range and low-light imaging on mobile cameras*. SIGGRAPH Asia 2016.
- Dataset: 3,640 bursts (28,461 raw images) with intermediate and final HDR+ outputs. Public.[²⁵](https://hdrplusdata.org/)

Key technical claims from the paper that translate into cpipe nodes:

- Burst of N constant-exposure frames (typically 5–10), exposed low to avoid clipping highlights.
- Pairwise alignment via 4-level hierarchical optical flow on raw Bayer.
- Robust merge: weighted average where weights are 1 for "matched" pixels, 0 for "ghosted/moving" pixels.
- Tone mapping after merge to fit the wider HDR signal into 8-bit display.
- Local tone via dynamic range compression. (Not multi-resolution; the paper uses a faster simplified scheme.)

cpipe ships HDR+ as the **default multi-frame preset**. The IPOL 2021
paper "An Analysis and Implementation of the HDR+ Burst Pipeline"[²⁶](https://www.ipol.im/pub/art/2021/336/article_lr.pdf)
has open Python+C++ source; cpipe can use this as a reference for the
algorithmic choice (not for the code, since it's GPLv3 — same license trap).

Real Tone (skin-tone accurate processing across diverse skin tones, and
recent expansion to video) is a Pixel feature dating from Pixel 6 (2021)
that has been widely covered.[²⁷](https://blog.google/products/pixel/google-pixel-9-ai-camera-features/)
For cpipe, "skin-tone accurate" means the WB / color-correction nodes
must be **calibrated against multi-ethnicity color targets**, not just
the 24-patch Macbeth. Cluster E report 15 (calibration) inherits this
requirement.

Night Sight uses long-exposure blending with **AI noise reduction and
movement-aware merge**, increasingly leaning on Tensor's NPU on Pixel 6
and later.[²⁸](https://research.google/blog/night-sight-seeing-in-the-dark-on-pixel-phones/)
For cpipe: a "Night" preset = HDR+ default tuned for longer exposures
and a stronger AI denoise node (cluster C report 08).

Pixel 10 (2025) introduced the **Tensor G5** chip and a renovated
imaging pipeline.[²⁹](https://mydailypixel.com/article/google-pixel-10-camera-specs-features)
Public material does not document the new pipeline at the per-stage level,
but the Marc Levoy-era HDR+ stack remains the architectural ancestor.

### 3.10 iPhone Camera — Photonic Engine and ProRAW

Apple's iPhone Camera is closed but its talks reveal the shape:[³⁰](https://www.apple.com/newsroom/2022/09/apple-debuts-iphone-14-pro-and-iphone-14-pro-max/)

- **Smart HDR** (since iPhone X, 2017) — multi-frame HDR with deep neural network selection of best regions.
- **Deep Fusion** (iPhone 11 Pro, 2019) — 9 photos at different exposures, pixel-by-pixel deep learning selection. Particularly strong on textures, sweaters, hair.
- **Photonic Engine** (iPhone 14 Pro, 2022) — Deep Fusion run *earlier in the pipeline*, on uncompressed RAW frames, before tone mapping. Apple shows a 12-stage pipeline diagram with Deep Fusion in 4 of the stages.[³¹](https://www.howtogeek.com/834842/what-is-apples-photonic-engine/)
- **ProRAW** — 12-bit DNG that includes Apple's processing as a "linearization table" plus original RAW data; user can dial Apple processing on/off in post.
- **Cinematic Mode** (video) — depth-aware focus pulls.
- **Adaptive HDR** (iPhone 15+) — gain-map HDR that's compatible with both SDR and HDR displays. cpipe's HDR HEIF output (cluster E report 14) parallels this.

The lesson: **the pro app's job is not to replicate this stack but to give the user the ability to opt out of it.** Halide and Process Zero exist because Apple's pipeline is *too aggressive* for many photographers. cpipe's value proposition is identical: *pick your pipeline.*

### 3.11 Samsung Expert RAW — the "two RAWs" lesson

Samsung's Galaxy phones (S22 onward) ship two RAW capture modes:[³²](https://www.androidauthority.com/how-to-use-samsung-expert-raw-3170310/)[³³](https://www.samsung.com/uk/support/mobile-devices/how-to-use-expert-raw/)

- **Pro mode** (in stock Camera): single-frame DNG with manual ISO/shutter/WB.
- **Expert RAW** (separate app, downloaded from Galaxy Store): **multi-frame** DNG with computational HDR, virtual aperture (F1.4–F16), ND filter, multiple exposure, astrophotography modes.[³⁴](https://www.gizmochina.com/2025/01/24/samsung-galaxy-s25-ultra-camera-upgrades-detailed/)

The user-relevant difference: **Pro mode RAW is what you'd get from a DSLR
RAW (one sensor read, no fusion); Expert RAW is a computational composite
written as DNG**. Both are 16-bit. The pixls.us forum has a long thread
discussing the artifacts that the Expert RAW computational step can
introduce — moire patterns, edge artifacts in low-frequency
gradients.[³⁵](https://discuss.pixls.us/t/samsung-s25-ultra-expert-raw-app-issue/50723)

For cpipe this is a clear UX requirement: **the shutter UI must distinguish
single-frame RAW from multi-frame composite**. The user must know which
they are producing. cpipe's metadata sidecar should explicitly flag
`merged_from_n_frames` so downstream tools (the cpipe editor, third-party
DNG tools) can reason about it.

### 3.12 OpenCamera — failure-mode UI inspiration (license-blocked code)

OpenCamera (Mark Harman, GPLv3)[³⁶](https://opencamera.org.uk/) is the
canonical FOSS Android camera app. Code is **not usable** in cpipe (D11
hard-blocks GPLv3). What cpipe takes is the **failure-mode UI**:

- Storage-low banner (with computed remaining shots in the chosen format).
- Battery low → recording stops at 5%, with a warning at 10%.
- Camera2 capability mismatch → grey out unsupported buttons rather than allowing them to fail mid-capture.
- Histograms, focus peaking, zebras already implemented.
- Burst mode with explicit count + interval settings — an enthusiast UX even cpipe should imitate.

OpenCamera also has interesting **Camera2 quirk handling**: lots of vendor
fingerprinting (Samsung, Huawei, Xiaomi) for features that the Camera2
abstraction "should" provide but don't. cpipe will hit the same quirks; we
should not reverse-engineer OpenCamera's quirk database (license), but we
should expect to build our own.

### 3.13 FreeDcam — vendor extension precedent

FreeDcam (Troop, GPLv2)[³⁷](https://github.com/KillerInk/FreeDcam) is the
precedent for **mining vendor extensions**. It hooks Sony's PlayMemories
WiFi Remote API, mining each vendor's private camera streams to expose
RAW where the official Camera2 path doesn't.

Two takeaways:

- The work has been done; the device-by-device matrix exists in FreeDcam's source. We can read it for guidance (no copy).
- Sony and Samsung "exclude" their devices from RAW capture in some firmware revs.[³⁸](https://en.todoandroid.es/open-camera-para-android-guia-completa-de-la-camara-libre-que-exprime-tu-movil/)

For v1, cpipe relies on the official Camera2 path only and accepts that
some devices won't work — ProShot Evaluator's "support matrix" UX is the
right way to communicate this.

### 3.14 Hipstamatic, VSCO Capture, Snapseed — niche but illustrative

- **Hipstamatic Classic Camera** (iOS) — RAW supported, but the value is the **film-canister metaphor**: the user picks a "film stock" and "lens" combo before shooting. cpipe's preset library can be styled the same way: "Pick a look first, shoot, the look is the pipeline." Compelling for casual users, not core for v1.
- **VSCO Capture** (iOS, launched July 2025)[³⁹](https://9to5mac.com/2025/07/22/vsco-launches-capture-its-iphone-camera-app-with-analog-style-filters/) — same pattern: combine VSCO presets with RAW capture. Subscription monetization.
- **Snapseed** — Google-acquired editor (2012); discontinued for active development circa 2023, still available. No camera path; reference only.

### 3.15 CameraX-Sample (Google official) — the modern API patterns

`android/camera-samples`[⁴⁰](https://github.com/android/camera-samples)
is the official Google sample repo. Both Camera2Basic and
CameraXBasic (and HDRViewfinder, Camera2Video) live here. cpipe's
capture code should match this repo's API patterns *idiomatically* for
Android-developer familiarity, even if cpipe wraps everything in C++.

### 3.16 Architectural lessons for cpipe Android

#### Latency targets

The shutter-press → preview-update cycle for pro apps:[⁴¹](https://developer.android.com/media/camera/camerax/take-photo/zsl)

- iPhone Camera (effectively ZSL via continuous preview): **< 50 ms**.
- Halide (RAW + processing): **80–120 ms** for first JPEG-equivalent feedback.
- ProShot, OpenCamera (single capture): **80–200 ms**.
- Pixel Camera (HDR+, 5-frame burst): **300–500 ms** to first preview, **1–2 s** to final.

cpipe's target is **shutter → "captured!" haptic within 100 ms** (i.e. the
buffer is in cpipe but the pipeline hasn't finished). The user gets a
"processing…" indicator until the pipeline finishes (typically 1–3 seconds
for a 100 MP burst on a flagship; cluster A report 03 owns this latency).

#### Pipeline configurability

Halide/Process Zero, Samsung Pro vs Expert RAW, and Lightroom Pro/HDR all
implement the same idea: **the user picks the pipeline before pressing
shutter**. cpipe must surface this prominently — the shutter UI shows the
*active preset* (e.g. "HDR+", "Night", "Process Zero", "Raw Single") and a
swipe-up menu reveals more.

The presets must be *cpipe pipeline graphs*, exported to JSON, named, with
a thumbnail / one-line description. The cpipe web editor (cluster D, report
11) is what authors them; the device app picks from the library.

#### Failure modes (consolidated checklist)

| Failure | UI response |
|---|---|
| Storage < 1 GB | banner: "Storage low — 12 photos remaining at current settings" |
| Storage < 200 MB | shutter disabled, banner: "Storage critical, free space to capture" |
| Battery < 15 % | Banner: "Battery low — burst limited to 3" |
| Battery < 5 % | Capture disabled |
| Thermal status SEVERE+ | Banner: "Phone is hot — burst paused for cool-down" |
| Camera permission revoked mid-session | Modal: "Camera permission required" with deep-link to settings |
| Foreground service killed by OS | Modal at next foreground: "Capture interrupted; the last burst was saved" + recovery offer |
| Capture session reconfigure required | Hidden retry; if it persists, banner: "Camera resetting" |
| Sensor temperature warning (Camera2 returns thermal error) | Same as thermal status above |
| User switched apps mid-burst | Continue the burst via foreground service; emit a progress notification |

#### Battery / thermal management

OpenCamera and Halide both expose this state to the user (Halide's "Phone
is warm, video stopped" toast).[⁴²](https://opencamera.org.uk/) cpipe should
poll `PowerManager.getCurrentThermalStatus()` (Android 10+) and degrade the
pipeline before the OS does it for us:

| Thermal status | Pipeline behaviour |
|---|---|
| `THERMAL_STATUS_NONE..LIGHT` | Full pipeline, full burst |
| `THERMAL_STATUS_MODERATE` | Drop AI nodes; stay on classic |
| `THERMAL_STATUS_SEVERE` | Halve burst count, banner |
| `THERMAL_STATUS_CRITICAL` | Capture disabled; banner |
| `THERMAL_STATUS_EMERGENCY` | OS will likely kill us; flush state and exit gracefully |

#### Sync to desktop / cloud — cpipe's "smart preview" pattern

Inherited from Lightroom (§3.2). The protocol on the wire (cluster D, report
11) carries:

- **Capture event** (push, phone → editor): full-resolution DNG-equivalent metadata + 2 MP smart-preview JPEG-or-HEIC. The original lives on the phone.
- **Edit event** (push, editor → phone): a JSON node-graph diff, applied to the working set on the editor side; phone caches it.
- **Render request** (push, editor → phone): "apply graph G to capture C, write result to /tmp/render-K.heic"; phone runs the pipeline.
- **Render complete** (push, phone → editor): smart preview of the rendered result.
- **Export** (manual, user-initiated on phone): write final HEIF to gallery, optionally upload original DNG to user's preferred cloud (Google Drive, Dropbox; cpipe doesn't run its own cloud per D11).

#### Compose UI structure (high level)

```
@Composable
fun CpipeCameraScreen(
  vm: CameraViewModel = viewModel()
) {
  Box(Modifier.fillMaxSize()) {
    // Viewport (full screen) — runs the live preview pipeline (v2)
    // or shows a black surface with a spinner during capture (v1).
    CpipePreviewSurface(vm.previewState)

    // Top edge: format / level / grid / battery / thermal / storage
    StatusBar(
      Modifier.align(Alignment.TopCenter),
      formatLabel = vm.activeFormat,
      thermalStatus = vm.thermalStatus,
      storageRemaining = vm.storageRemaining,
      batteryPercent = vm.batteryPercent,
    )

    // Right edge: vertical strip — preset switcher, white balance,
    // exposure compensation, focus mode toggle. Reachable by right thumb.
    RightControlStrip(
      Modifier.align(Alignment.CenterEnd),
      activePreset = vm.activePreset,
      onPresetChange = vm::selectPreset,
    )

    // Left edge: gallery preview (last shot), camera switch.
    // Reachable by left thumb if user enables "left handed".
    LeftControlStrip(
      Modifier.align(Alignment.CenterStart),
      lastShotThumbnail = vm.lastShotThumb,
      onSwitchCamera = vm::switchCamera,
    )

    // Bottom edge: shutter (long-press for burst), mode dial, settings.
    BottomControls(
      Modifier.align(Alignment.BottomCenter),
      onShutter = vm::shutterPress,
      onShutterHold = vm::shutterHoldStart,
      onShutterRelease = vm::shutterRelease,
      onSettings = navigateToSettings,
    )

    // Analytic overlays — opt-in, individually toggled.
    if (vm.showHistogram) HistogramOverlay(vm.histogram)
    if (vm.showZebra) ZebraOverlay(vm.zebraConfig)
    if (vm.showFocusPeaking) FocusPeakingOverlay(vm.peakingConfig)
    if (vm.showFalseColor) FalseColorOverlay()
    if (vm.showLevel) HorizonLevelOverlay()
    if (vm.showGrid) GridOverlay(vm.gridConfig)
    if (vm.showWaveform) WaveformOverlay()
    if (vm.showVectorscope) VectorscopeOverlay()
  }
}
```

#### Preset library (initial v1 set)

```
RAW Single        — single-frame DNG, no merging, no AI. Process Zero analog.
Default           — 5-frame HDR+, classic demosaic + classic NR, SDR HEIF.
HDR+              — 7-frame HDR+, full classic pipeline, HDR HEIF (PQ).
Night             — 7-frame HDR+, AI denoise (cluster C/08), longer exposure, SDR HEIF.
Studio            — Single-frame, AI denoise + AI demosaic, slow render. SDR HEIF.
Burst Action      — 10-frame, no merging; user picks the keeper in editor.
```

The preset is selected by left/right swipe on the preset strip; the active
preset name is the most prominent label on the screen.

### 3.17 Open-source repositories worth inspecting

For inspiration only (license-checked):

| Repo | License | Useful for |
|---|---|---|
| `android/camera-samples` | Apache-2.0 | API patterns, ImageReader wiring, multi-camera samples |
| `kiryldz/android-hardware-buffer-camera` | Apache-2.0 | Camera2 → AHardwareBuffer → Vulkan/OpenGL preview pipeline |
| `ktzevani/native-camera-vulkan` | MIT | Camera2 → AHardwareBuffer → Vulkan compute |
| `prime-slam/OpenCamera-Sensors` | GPLv3 | Multi-camera + IMU sync — read only |
| `KillerInk/FreeDcam` | GPLv2 | Vendor extension catalogue — read only |
| `opensource opencamera/code` | GPLv3 | Failure-mode UI, quirk catalogue — read only |
| `google/pixelvisualcorecamera` | Apache-2.0 | Pixel Visual Core sample (early Pixel HDR+ accelerator) |

cpipe's **canonical Android-side reference** for code patterns we may copy:
the official `android/camera-samples` repo (Apache-2.0). Everything else is
read-only inspiration.

## 4. Architecture sketches

### 4.1 cpipe Android app component diagram

```
+----------------------------------------------------------------+
|  Compose UI (cpipe-android-ui, Kotlin)                         |
|  ├── CameraScreen     (Halide-style viewport + controls)       |
|  ├── PresetSwitcher   (left/right swipe on right strip)        |
|  ├── PresetEditor     (deep-link to web editor for editing)    |
|  ├── GalleryReviewer  (post-capture review with smart preview) |
|  ├── SettingsScreen                                            |
|  └── EditorBrowser    (loads cpipe Web Editor in WebView       |
|                       running locally embedded server)          |
+----------------------------------------------------------------+
                          │       JNI
                          ▼
+----------------------------------------------------------------+
|  Native cpipe (cpipe-core, C++20)                              |
|                                                                |
|  ┌──────────────────────┐    ┌──────────────────────────────┐  |
|  │ CameraSource plugin  │ →  │ Pipeline Scheduler (cl. A/03)│  |
|  │ (cluster F / 16)     │    └──────────────────────────────┘  |
|  └──────────────────────┘                ↓                     |
|         ↓                       Per-node compute               |
|  AHardwareBuffer       ┌──────────────────────────────┐        |
|  + FrameMeta           │  Vulkan / Hexagon NPU / CPU  │        |
|                        └──────────────────────────────┘        |
|                                          ↓                     |
|                            Encoded HEIF (cluster E/14)         |
|                                          ↓                     |
|                                   MediaStore writer            |
|                                                                |
|  EditorBridge plugin (cluster D/11)                            |
|  ├── HTTP server (preview / smart-preview push)                |
|  ├── WebSocket (control / node-graph edits)                    |
|  └── WebRTC DataChannel for peer-to-peer (NAT traversal)       |
+----------------------------------------------------------------+
                          │
                          ▼
+----------------------------------------------------------------+
|  cpipe Web Editor (browser, cluster D / 11)                    |
|  React Flow node graph + mosaic preview                        |
+----------------------------------------------------------------+
```

### 4.2 Capture interaction flow

```
user: presses shutter
  │
  ▼
ShutterButton (Compose) ── hapticFeedback(LIGHT_TICK)
  │
  ▼
CameraViewModel.shutterPress()
  │
  ├── thermal/storage/battery preflight
  │     ├── ok → continue
  │     └── fail → show banner + abort
  │
  ▼
JNI: cpipe_capture_burst(activePreset)
  │
  ▼
Native CameraSource.burst(...)
  │
  ▼ (5–10 frames acquired)
BatchedBuffer{ahb[N], meta[N]}
  │
  ▼
Pipeline Scheduler ── for each preset's graph node
  │
  ▼
Final HEIF encoded
  │
  ├── written to MediaStore
  ├── 2 MP smart-preview created (downscale + JPEG)
  └── EditorBridge.push(captureEvent { metadata, smartPreview })

GalleryReviewer pops up:
  │
  ▼
"Captured! [thumbnail]" with haptic confirmation (MEDIUM_TICK)
```

### 4.3 Sync flow (with cluster D / 11)

```
phone                              editor (browser)
  │                                        │
  │── captureEvent (smart preview, meta)──→│
  │                                        │
  │                            user edits node graph
  │                                        │
  │←── editEvent(graph diff) ──────────────│
  │                                        │
  │── echoes preview render of new graph ─→│
  │                                        │
  │←── renderRequest(graph G, capture C) ──│
  │                                        │
  │── runs cpipe pipeline at full RAW      │
  │                                        │
  │── renderComplete(result smart prev) ──→│
  │                                        │
  │   user clicks "Export"                 │
  │←── exportRequest(format, dest) ────────│
  │                                        │
  │── writes final HEIF to MediaStore      │
  │   (or uploads to user's cloud)         │
  │── exportComplete(uri) ────────────────→│
```

## 5. Cited sources

1. Halide / Medium. *Pro. Camera. Action. Introducing Halide Mark II.* 2020. <https://medium.com/halide/introducing-halide-mkii-30f9f2bceac3>
2. PetaPixel. *Halide Mark III Doubles Down on AI-Free iPhone Photography*. 2026-01-28. <https://petapixel.com/2026/01/28/halide-mark-iii-doubles-down-on-ai-free-iphone-photography/>
3. Apple Developer. *Behind the Design: Halide Mark II*. <https://developer.apple.com/news/?id=x6bv1a36>
4. MacStories. *Halide Mark II Review: The Convenience of Computational Photography and Flexibility of RAW in an Elegant Camera App*. <https://www.macstories.net/reviews/halide-mark-ii-review-the-convenience-of-computational-photography-and-flexibility-of-raw-in-an-elegant-camera-app/>
5. Halide. *Halide Mark II*. <https://halide.cam/>
6. Lux Camera. *Process Zero: The Anti-Intelligent Camera*. 2024-08. <https://www.lux.camera/introducing-process-zero-for-iphone/>
7. AppleInsider. *Camera app development for iPhone: Halide Team on the AppleInsider podcast*. 2021-06-14. <https://appleinsider.com/articles/21/06/14/camera-app-development-for-iphone-interview-with-the-halide-team-on-the-appleinsider-podcast>
8. iDownloadBlog. *Halide gains focus and exposure controls via the iPhone 16's Camera Control button*. 2024-10-01. <https://www.idownloadblog.com/2024/10/01/halide-camera-control-button-exposure-gain-focus-iphone-16/>
9. Wikipedia. *Adobe Lightroom*. <https://en.wikipedia.org/wiki/Adobe_Lightroom>
10. Adobe. *Edit HDR photos in Lightroom for mobile (Android)*. <https://helpx.adobe.com/lightroom-cc/using/hdr-android.html>
11. Adobe. *Learn how to capture stunning DNG and raw photos in Lightroom for mobile (iOS)*. <https://helpx.adobe.com/lightroom-cc/using/capture-photos-mobile-ios.html>
12. Adobe. *How to use Smart Previews to view and edit photos in Photoshop Lightroom Classic*. <https://helpx.adobe.com/lightroom-classic/help/lightroom-smart-previews.html>
13. Adobe. *Common questions about Adobe Lightroom for mobile and Apple TV*. <https://helpx.adobe.com/mobile-apps/help/lightroom-mobile-faq.html>
14. Reincubate / Camo. *Camo overview*. <https://camo.com/support/camo/camo-overview>
15. Reincubate. *Camo Studio - Stand out video with any camera*. <https://reincubate.com/camo/>
16. Eagle Eye Adventures. *ProCamera iPhone App Review 2025*. <https://www.eagleeyeadventures.com/blog/procamera-iphone-app-review-2025-best-manual-camera-app-for-ios>
17. Apple App Store. *Moment Pro Camera II*. <https://apps.apple.com/us/app/moment-pro-camera-ii/id6748837351>
18. Rise Up Games. *ProShot for Android*. <https://www.riseupgames.com/proshot/android>
19. Filmic Apps. *How do I access Filmic Pro V6 if I previously purchased it?*. <https://filmicapps.zendesk.com/hc/en-us/articles/31095485377169-How-do-I-access-Filmic-Pro-V6-if-I-previously-purchased-it>
20. Filmic Pro. *Filmic Legacy (v6)*. <https://www.filmicpro.com/products/filmic-pro-v6/>
21. Apple App Store. *Filmic Firstlight*. <https://apps.apple.com/us/app/filmic-firstlight-photo-app/id1482338564>
22. Hasselblad. *Phocus Mobile 2*. <https://www.hasselblad.com/phocus/phocus-mobile-2/>
23. Fstoppers. *Wireless Tethering With Hasselblad X2D and Phocus Mobile 2*. <https://fstoppers.com/gear/wireless-tethering-hasselblad-x2d-and-phocus-mobile-2-711600>
24. Hasinoff et al. *Burst photography for high dynamic range and low-light imaging on mobile cameras*. SIGGRAPH Asia 2016. <https://research.google/pubs/pub45586/>
25. Google Research. *HDR+ Burst Photography Dataset*. <https://hdrplusdata.org/>
26. IPOL. *An Analysis and Implementation of the HDR+ Burst Pipeline*. 2021. <https://www.ipol.im/pub/art/2021/336/article_lr.pdf>
27. Google. *Google Pixel 9: New camera specs and AI photo features*. <https://blog.google/products/pixel/google-pixel-9-ai-camera-features/>
28. Google Research. *Night Sight: Seeing in the Dark on Pixel Phones*. <https://research.google/blog/night-sight-seeing-in-the-dark-on-pixel-phones/>
29. My Daily Pixel. *Google Pixel 10 Camera In-Depth: Specs, AI & New Features*. <https://mydailypixel.com/article/google-pixel-10-camera-specs-features>
30. Apple. *Apple debuts iPhone 14 Pro and iPhone 14 Pro Max*. 2022-09. <https://www.apple.com/newsroom/2022/09/apple-debuts-iphone-14-pro-and-iphone-14-pro-max/>
31. How-To Geek. *What Is the iPhone 14's Photonic Engine?*. <https://www.howtogeek.com/834842/what-is-apples-photonic-engine/>
32. Android Authority. *Samsung Expert RAW: How to use Samsung's advanced photo app*. <https://www.androidauthority.com/how-to-use-samsung-expert-raw-3170310/>
33. Samsung. *How to use Expert RAW*. <https://www.samsung.com/uk/support/mobile-devices/how-to-use-expert-raw/>
34. Gizmochina. *Galaxy S25 Ultra Camera Upgrades: Virtual Aperture, RAW Editing, and Advanced AI Features*. 2025-01-24. <https://www.gizmochina.com/2025/01/24/samsung-galaxy-s25-ultra-camera-upgrades-detailed/>
35. pixls.us. *Samsung S25 Ultra & Expert RAW app issue*. <https://discuss.pixls.us/t/samsung-s25-ultra-expert-raw-app-issue/50723>
36. Mark Harman. *Open Camera*. <https://opencamera.org.uk/>
37. KillerInk. *FreeDcam* GitHub repository. <https://github.com/KillerInk/FreeDcam>
38. todo-android.es. *Open Camera para Android, guía completa*. <https://en.todoandroid.es/open-camera-para-android-guia-completa-de-la-camara-libre-que-exprime-tu-movil/>
39. 9to5Mac. *VSCO launches 'Capture', its iPhone camera app with analog-style filters*. 2025-07-22. <https://9to5mac.com/2025/07/22/vsco-launches-capture-its-iphone-camera-app-with-analog-style-filters/>
40. Google. *android/camera-samples* GitHub repository. <https://github.com/android/camera-samples>
41. Android Developers. *Reduce latency with Zero-Shutter Lag*. <https://developer.android.com/media/camera/camerax/take-photo/zsl>
42. Open Camera Help / SourceForge. <https://opencamera.sourceforge.io/help.html>
43. Lux Camera. *Lux Year 4: Doubling Down*. <https://www.lux.camera/lux-year-4-doubling-down/>
44. PitchBook. *Halide Group 2025 Company Profile*. <https://pitchbook.com/profiles/company/169430-77>
45. live-feeds.com. *Lux Optics Reveals Roadmap for Halide Mark III*. 2024-12-24. <https://www.live-feeds.com/2024/12/24/lux-optics-reveals-roadmap-for-halide-mark-iii-color-grades-hdr-and-ui-redesign-coming-in-2025/>
46. Apple Support. *About Apple ProRes on iPhone*. <https://support.apple.com/en-us/109041>
47. Adobe. *Optimize HDR photos in Lightroom for mobile (iOS)*. <https://helpx.adobe.com/lightroom-cc/using/hdr-ios.html>
48. Filmic Pro. *Filmic Pro v6 User Manual*. <https://www.filmicpro.com/FilmicProUserManualv6.pdf>
49. Apple Developer. *Developer Spotlight: Halide Mark II*. <https://developer.apple.com/news/?id=wezczlc6>
50. Sammy Fans. *Samsung releases new Expert RAW update for Galaxy S25 series*. 2025-03-10. <https://www.sammyfans.com/2025/03/10/samsung-releases-new-expert-raw-update-for-galaxy-s25-series/>

## 6. See also

- [11 — Pipeline editor and connectivity](11-pipeline-editor-and-connectivity.md): the protocol that carries the smart-preview sync described in §3.2 and §4.3.
- [14 — HEIF and HDR output](14-heif-and-hdr-output.md): the encoder for the SDR/HDR HEIF written by every preset.
- [16 — Camera2 RAW and burst](16-camera2-raw-and-burst.md): the capture path the UI here drives.
- [08 — AI ISP algorithms](08-ai-isp-algorithms.md): AI denoise / demosaic that the "Studio" / "Night" presets enable.
- [07 — Classic ISP algorithms](07-classic-isp-algorithms.md): demosaic / WB / NR / fusion shared by all presets.
- [12 — DNG format](12-dng-format.md): the DNG-equivalent metadata schema written by the capture path.
- [15 — Mobile camera calibration](15-mobile-camera-calibration.md): per-device calibration that Real-Tone-style requirements drive.

## 7. Open questions

1. **Live preview pipeline (D5 deferred)**: when do we actually ship preview? cpipe v1 ships with no preview, which is borderline embarrassing for a pro camera app. Is there a "lite preview" we can ship in v1.x — even at 5 fps, downscaled to 480p — that doesn't break the architecture? Worth evaluating.
2. **Halide-style horizon-haptic**: easy to spec, slightly tricky to implement (sensor fusion of accelerometer + magnetometer at 60 Hz). Is this v1 or v2?
3. **Presets as cpipe pipeline graphs**: the preset switcher is on the device but the authoring is on the web editor. How do users discover and import community-shared presets? GitHub-Pages-hosted preset gallery? In-app browser? v1 ships with our 6 presets only; v2 needs a story.
4. **Sync over cellular**: smart preview is ~500 KB; 5 frames × 500 KB = 2.5 MB per burst. Acceptable on Wi-Fi but expensive on cellular. Settings toggle? Lightroom has it.
5. **WebRTC vs raw WebSocket for editor link**: cluster D report 11's call. From the UX side, the only requirement is "edits feel real-time."
6. **Preview computation cost**: the analytic overlays (waveform, vectorscope, false color) are GPU compute shaders, but we're not running preview in v1 (D5). Do we run them on the captured frame post-capture, in the gallery viewer? If so, the UI is *still* useful for chimping, just not pre-shutter.
7. **Vendor extensions (Sony PlayMemories, Samsung Pro mode, Pixel HDR+ raw)**: do we surface these in v1? FreeDcam shows the work has been done by others. v1 recommendation: **no** — depth of cpipe's pipeline is the differentiator, not the capture trick.
8. **How prominent is the "Process Zero" preset?** Halide bet the brand on it. cpipe's brand is "node-graph configurable computational photography" — but the user can still want a "single-frame, no AI" preset for purists. Recommendation: ship it as `RAW Single`, prominent in the preset list, default for first-time users so they see what their camera does without our help.
9. **Lens accessory database**: Moment-style. How big is the market? Maybe a v3 conversation; for v1, OEM metadata is sufficient.
10. **Gallery integration**: do we live in the system gallery (Photos, Google Photos) only, or do we host our own album like Lightroom? v1 recommendation: **system gallery only** — we are a camera, not a Photos competitor.
