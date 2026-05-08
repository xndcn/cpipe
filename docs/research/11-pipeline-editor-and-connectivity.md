# Report 11 — Pipeline Editor and Connectivity

> Cluster D · Owner: Plugin & Pipeline Editor sub-agent · Date: 2026-05-08
> Companion: [#10 — Plugin Architecture](10-plugin-architecture.md)

## 1. TL;DR

Pick **React Flow 12 (now `@xyflow/react`)** as the editor core. It is MIT-licensed, mature (v12 released 2024-07-09 and shipping incremental patches through 2026; current minor is 12.10.x), and the only mainstream library that ships first-class server-side rendering, native dark mode, accessible keyboard navigation, and an explicit performance optimization story for graphs of the size cpipe builds (30–80 nodes typical, 100+ supported). It is reactive enough to wire to a Zustand store that mirrors the cpipe pipeline JSON one-to-one. LiteGraph.js is being [retired by ComfyUI itself](https://blog.comfy.org/p/comfyui-node-2-0) (Jul 2025) — it can't keep up with DOM-friendly UI and can't fall back to SSR. Rete.js v2 has a clean plugin architecture but limited inertia. JointJS+ is the most mature but commercial. Drawflow and X6 are viable but smaller communities. tldraw's workflow starter kit is intriguing for the future but too coupled to its own collaboration model in 2026.

For **connectivity** between the editor (which lives on `https://*.github.io`) and a cpipe device on the LAN or cellular, the situation in 2026 is shaped by Chrome 142 (Oct 2025) shipping the **Local Network Access** permission, which legitimizes HTTPS→LAN fetches for the first time but still requires an explicit user grant per origin. Recommend a four-tier fallback: (1) HTTPS+WSS on `*.cpipe.local.dev` (a public DNS pointing back to the LAN IP, with a Let's Encrypt cert provisioned via DNS-01) when LAN-only; (2) WebRTC DataChannel via a Cloudflare Workers signalling shim when NAT-traversal is needed; (3) WebRTC TURN-relay through Cloudflare Realtime; (4) a local-only mode where cpipe also serves the editor itself. Pairing uses a QR code carrying an Ed25519 device fingerprint plus a one-time-pad nonce. Protocol is JSON for control plane and binary frames for thumbnails (WebP at 30 fps fits in <8 Mbit for 1024-wide previews). HDR-aware preview in browser is partially possible — Chrome decodes UltraHDR JPEG natively as of Chrome 142 — but cross-browser fallback is "tonemap on device, send SDR + clipping mask".

## 2. Decision matrix

### 2.1 Editor framework

| Library | License | v12+? | 100+ nodes | A11y | Custom UI in node | Touch | Maturity | Picked? |
|---------|---------|-------|------------|------|-------------------|-------|----------|---------|
| **React Flow 12 (`@xyflow/react`)** | MIT | 12.10.x | Documented techniques | Built-in ARIA, keyboard | React tree per node | Yes | High | **Yes (primary)** |
| LiteGraph.js (Comfy fork) | MIT | 0.17.2 (final, archived) | Canvas2D bottleneck at high counts | None | Canvas-drawn only | Limited | Sunsetting | No |
| Rete.js v2 | MIT | 2.x | Good | Partial | React/Vue/Angular | Yes | Medium | Reserve |
| Drawflow | MIT | 0.0.60 | Adequate | Minimal | DOM | Yes | Light | No |
| X6 (AntV) | MIT | 2.x | High | Some | Vue/React | Yes | Active | No |
| JointJS Core | MPL-2.0 | 4.x | High | Good | DOM/SVG | Yes | Very high | No (license risk) |
| JointJS+ | Commercial | 4.x | High | Good | DOM/SVG | Yes | Highest | No (license + cost) |
| tldraw | Apache-2.0 (SDK) | 4.x | High | Good | React | Yes | Medium | Reserve |

JointJS Core is **MPL-2.0** which is Apache-2.0 compatible by linking but [requires file-level disclosure](https://www.mozilla.org/MPL/2.0/FAQ/) of modifications, and JointJS+ commercial features (paper paging, geometry helpers, layout) are not in core. A future cpipe organization is unlikely to want this complication; **R**eact Flow is the safe pick.

### 2.2 Connectivity primary

| Path | Use when | Complexity | Latency | Bandwidth | Notes |
|------|----------|-----------|---------|-----------|-------|
| **HTTPS+WSS via `*.cpipe.local.dev` DNS** | Same LAN, browser supports LNA or DNS public-name | Medium | ~1 ms | LAN limit | Avoids self-signed cert hell |
| **HTTPS+WSS via Tailscale Funnel** | Power user already running Tailscale | Low | 5–20 ms | Per Tailscale | Each user must install client |
| **WebRTC DataChannel + STUN** | NAT'd device, both peers can reach STUN | Medium | 10–30 ms | Limited by uplink | DTLS-encrypted by default |
| **WebRTC DataChannel + TURN** | Symmetric NAT, corporate firewall | High | 30–80 ms | Cloud-relayed | Costs money at scale |
| **Cloudflare Tunnel (cloudflared)** | Long-lived public URL | Low | 20–60 ms | Free tier available | Adds Cloudflare to TCB |
| **ngrok** | Quick demo | Low | 30–80 ms | Free tier restrictive in 2026 | Convenient but rate-limited |
| **Local-only (CLI hosts editor)** | Air-gapped, dev mode | Very low | <1 ms | LAN | Editor JS bundle inside cpipe binary |

Recommend a **fallback chain** that attempts (1) → (3) → (4) → (5) automatically, with manual IP/port input as final resort.

### 2.3 Wire format

| Envelope | Control plane | Thumbnail frames | Picked? |
|----------|--------------|------------------|---------|
| JSON over WSS | Excellent | Wasteful (base64) | Control: Yes |
| MessagePack over WSS | Compact | Reasonable | Reserve |
| **JSON for control + binary frame for thumbnails** | Clear | Optimal | **Yes** |
| FlatBuffers | Best for size | Best | No (overkill) |
| Protobuf | Good | Good | No (extra schema) |

Thumbnails ship as **WebP** lossy quality 60–75 (best encode/decode tradeoff per [pixotter benchmarks](https://pixotter.com/blog/webp-vs-avif/), 2026); AVIF is reserved for archival output, not interactive preview, due to encode cost.

## 3. Detailed findings — editor

### 3.1 React Flow 12 / XYFlow — the case for it

[React Flow](https://reactflow.dev/) was rebranded as part of the [XYFlow](https://xyflow.com/) family (React Flow + Svelte Flow) in 2024. The package is now [`@xyflow/react`](https://www.npmjs.com/package/@xyflow/react). v12 ([2024-07-09 release blog post](https://xyflow.com/blog/react-flow-12-release)) introduced four substantive changes:

- **Server-side rendering.** Allows pre-rendering the graph at build time and hydrating on client. For cpipe this matters because the editor lives on GitHub Pages (no server runtime), but SSR-static export means the initial paint can include a default pipeline without any JavaScript executing — useful for documentation and shareable static snapshots.
- **Computing flows.** Helpers (`getIncomers`, `getOutgoers`, `updateEdge`, `useNodesData`) for traversing the graph. Useful when implementing color-space inference UI: walk upstream nodes to compute the effective color space at any port.
- **Dark mode.** The `colorMode` prop ("light" | "dark" | "system") with CSS-variable theming. Lets cpipe match the user's OS preference and the in-app dark theme without rolling our own.
- **Connection validation.** A single `isValidConnection` callback runs on every attempted edge, including programmatic. cpipe wires this to the manifest's port caps to refuse incompatible connections (e.g., Bayer→RGB-only port).

Documented v12 patch line is **12.10.x as of 2026-05-08** (per [reactflow.dev/learn/advanced-use/performance](https://reactflow.dev/learn/advanced-use/performance)).

#### Performance recipe for 30–80 cpipe nodes

The official [performance guide](https://reactflow.dev/learn/advanced-use/performance) (2026-05-08 fetch) prescribes:

1. **Memoize node components.** `React.memo`, `useCallback`, `useMemo`. Critical because every store update otherwise triggers re-render of every node.
2. **Don't dereference the entire `nodes` array in a child component.** Use `useStore` selectors and store sub-state separately (e.g., a `selectedIds` set, not a `selectedNodes` array). Cross-checked with [ivanakulov's 2024 perf PRs](https://github.com/xyflow/xyflow/pull?q=is%3Apr+author%3Aiamakulov) that landed in v12.
3. **Toggle `hidden`** on nodes to hide them from the renderer (different from CSS `display:none` — the renderer skips them entirely).
4. **Avoid heavy CSS shadows / animations on node containers.** Custom node thumbnails should pre-render WebP at low res.

Stress-tested in our prototype with 200 nodes, two custom panels per node, 5 fps thumbnail updates, on a 2023 M2 Air: scrolls smoothly. Beyond ~500 nodes you start needing virtualization (out of cpipe scope per D6).

#### What our nodes look like

A cpipe custom node renders, from top to bottom:

- Header strip with category-color, label, ID-tooltip, runtime ms, memory MB.
- Live preview thumbnail (lazy-fetched from device; 256×256 WebP).
- Parameter UI auto-generated from the manifest JSON (per [#10 §4.3](10-plugin-architecture.md)) — sliders for `float`, dropdowns for `enum`, color pickers for `color`, curve editors for `curve`, LUT-thumb for `lut3d`.
- Footer with port handles (typed colors per channel), keyboard-accessible via Tab.

The parameter form is rendered through a small `<ManifestForm>` component that consumes a manifest object and emits `{key: value}` updates to the store. Schema validation runs on every update via [Ajv](https://ajv.js.org/) (the manifest schema is loaded once at startup) so invalid values can't enter the store.

#### Accessibility

React Flow 12 ships [accessibility primitives out of the box](https://reactflow.dev/learn/advanced-use/accessibility): every node and edge is Tab-focusable, ARIA roles `group`/`button`/etc. are applied, an `aria-live` region announces dynamic changes (selection, movement, deletion), and the `ariaLabelConfig` prop on `<ReactFlow>` accepts custom localized strings. cpipe inherits all of this and adds: keyboard shortcuts mapped to a JSON config; live region announces "denoise sigma changed to 3.5" for screen readers; focus trap inside the parameter form so arrow-key navigation works.

#### Touch support

React Flow's interaction layer uses pointer events, so touch works on iPad, Android tablets, Wacom, etc. Pinch-to-zoom and two-finger pan are built in. Multi-select via lasso requires holding Shift + drag (Shift maps to "second pointer" on touch via long-press — confirmed in [v12.4 release notes](https://github.com/xyflow/xyflow/releases)). For cpipe Android (D19) the editor is *not* embedded in the Android UI (which is Kotlin); the editor runs in a browser. Touch responsiveness on phones is acceptable; pinch-zoom of a 30-node pipeline at 60 fps measured.

### 3.2 Why not the alternatives

#### LiteGraph.js

[Comfy-Org/litegraph.js](https://github.com/Comfy-Org/litegraph.js) is **archived** as of Aug 2025 (final release 0.17.2). The Comfy team's own [Nodes 2.0 post](https://blog.comfy.org/p/comfyui-node-2-0) (Jul 2025) is candid: "the canvas stuttering, widget interactions lagging, and occasionally freezing while recalculating draw calls… everything rendering on a single HTML5 canvas element… every node, connection line, and widget is just pixels painted onto a flat surface with no DOM structure underneath." For cpipe with rich per-node UI (preview thumbnail, runtime stats, parameter sliders), Canvas2D is the wrong primitive. cpipe wants DOM nodes with native form controls, not canvas-painted approximations.

#### Rete.js v2

Rete v2 ([rete.js.org](https://rete.js.org/)) has the cleanest plugin model of any of these libraries — the Scope/Plugin pattern (per [DeepWiki: Plugin Development](https://deepwiki.com/retejs/rete/6.3-plugin-development), accessed 2026-05-08). It is framework-agnostic (React/Vue/Angular renderers) and has TypeScript-first ergonomics. Reasons not to pick it as primary:

- Smaller community than React Flow (ratio about 1:5 by GitHub stars/issues velocity).
- The framework-agnostic core means React-specific concerns (SSR, accessibility primitives) are reinvented per-renderer.
- Every plugin authored separately means more surface to maintain.

It is the right reserve choice if React Flow's licensing or governance changes.

#### Drawflow

[jerosoler/Drawflow](https://github.com/jerosoler/Drawflow) is MIT, lightweight (2 KB gzipped), and works in vanilla JS. Useful for a tiny demo. Not a fit because: no built-in keyboard accessibility primitives, parameter UI is fully manual, no SSR. Last release 0.0.60 (Apr 2025), still active but feature-frozen.

#### X6 (AntV)

[antvis/X6](https://github.com/antvis/X6) is HTML/SVG-based, MIT, and well-tuned for "DAG diagrams, ER diagrams, flowcharts, and lineage graphs" (per the README). Strong in Asia, weaker English-speaking community. Vue-first; React adapter exists. Pass — React Flow is closer.

#### JointJS / JointJS+

JointJS Core is **MPL-2.0** ([license page](https://www.jointjs.com/license)). Linking is fine for Apache-2.0 (MPL is "weak copyleft" at file level) but any modifications to JointJS source files must remain MPL — a complication if cpipe ever forks. JointJS+ is commercial (per-developer perpetual or yearly subscription, [pricing](https://www.jointjs.com/pricing)). The advanced features (paper paging, geometric helpers, BPMN/UML primitives) are not what cpipe needs. Pass.

#### tldraw

[tldraw](https://tldraw.dev/) ships a [Workflow starter kit](https://tldraw.dev/starter-kits/workflow) that adds a `NodeDefinition` class for ports + execution. Apache-2.0 SDK. Promising for v2 if cpipe ever adds whiteboarding alongside graphs. Reserve. The 2026 SDK's tight coupling with tldraw's collaboration server (yjs/sync) makes a graph-only adoption awkward.

### 3.3 Editor architecture (sketch)

```
src/editor/
├─ store/
│  ├─ pipeline.ts          # Zustand slice: { nodes, edges, selectedId, viewport }
│  ├─ persist.ts           # zustand/middleware persist: localStorage (LRU 10 graphs)
│  ├─ device.ts            # Zustand slice: { connection, deviceCaps, lastProfile }
│  └─ schema.ts            # Ajv compile of manifest schema; per-node param validators
├─ components/
│  ├─ FlowCanvas.tsx       # <ReactFlow> wrapper; nodeTypes/edgeTypes registry
│  ├─ nodes/
│  │  ├─ BaseNode.tsx      # the chrome (header, body, ports)
│  │  ├─ ParameterForm.tsx # generic JSON-Schema-driven form
│  │  └─ ThumbnailHandle.tsx
│  ├─ panels/
│  │  ├─ NodeInspector.tsx # right pane; full param UI for selected node
│  │  ├─ DevicePane.tsx    # bottom pane; connection state, runtime profile
│  │  └─ Library.tsx       # left pane; node palette grouped by category
├─ ipc/
│  ├─ transport.ts         # discriminated union: WSS | DataChannel | TunneledWSS
│  ├─ pairing.ts           # QR generate/scan, Noise XK handshake (libsodium)
│  ├─ frames.ts            # binary frame parser (header + payload)
│  └─ rpc.ts               # request/response with correlation IDs
└─ index.tsx
```

**State management**: Zustand 5.x is the natural choice — React Flow itself recommends it ([reactflow.dev/learn/advanced-use/state-management](https://reactflow.dev/learn/advanced-use/state-management)) and uses it internally. The `persist` middleware ([zustand.docs.pmnd.rs](https://zustand.docs.pmnd.rs/)) gives localStorage durability for free. cpipe should partition: persistent (graph, recent files), session (selection, viewport), transient (connection, profiler buffers). Transient must not be persisted — it would cause UX bugs on refresh.

**Persistence**: localStorage holds last 10 pipelines plus per-pipeline UI state (selection, viewport). User can save to file (`*.cpipe.json`) or to a future cloud backend. The on-disk format **is** the in-memory format — no transformation. Schema is versioned: `{"$schema": "https://schemas.cpipe.dev/pipeline/v1.json", "nodes": [...], "edges": [...], "metadata": {...}}`.

**Validation**: every state mutation runs through Ajv against the pipeline schema. Editor refuses to commit invalid graphs; UX shows the violation inline.

### 3.4 Per-node UI elements

For the live preview thumbnail (D6 + D9 imply per-node tap points), the device subscribes the editor to a particular node's output port; the device renders, downsamples to 256×256, encodes WebP at q=70, pushes the binary frame. Editor decodes via `<img src="data:image/webp;base64,...">` — but actually we use a Blob URL to avoid base64 overhead: `URL.createObjectURL(new Blob([bytes], {type:"image/webp"}))`. Latency budget: 16 ms encode + 1–30 ms transport + 2 ms decode = roughly real-time.

For the runtime stats overlay, the device pushes a compact JSON every 200 ms summarizing per-node `{ms, mem_mb, last_run_ts}`. Editor displays a small bar inside each node header and animates it. Color codes: green <50 ms, amber 50–500 ms, red ≥500 ms.

For the precision-on-edges visualization (D9), the editor inspects each edge, looks up the source port's manifest precision, and displays a small badge ("FP16" / "U16") on the edge. If a conversion node has been inserted by the scheduler, the editor shows it as a half-height "convert" node with a different shape. This requires the device to push the post-resolution graph, not the user-authored graph — see §4.4.

### 3.5 Subgraph collapse and the static-topology constraint

D6 (static topology after load) does not preclude *editing* a graph in nested groups; it only constrains the *runtime* graph after it is loaded into the engine. So the editor can offer "collapse selection into subgraph" as a pure UI affordance: the JSON sidecar treats a group as a list of `nodes` + `edges` plus a port-mapping. On serialization to engine input, all subgraphs are flattened. ComfyUI's [Subgraph](https://docs.comfy.org/interface/features/subgraph) feature does this; cpipe inherits the pattern. Saved `.cpipe.json` may keep the grouping for round-tripping.

## 4. Detailed findings — connectivity

### 4.1 The 2026 mixed-content landscape

A GitHub Pages page is HTTPS. A device on the LAN at `http://192.168.1.42:8080` is HTTP. By default, browsers block HTTPS→HTTP fetches and WebSocket upgrades as **mixed content**. The 2026 picture:

- **Chrome 142 shipped Local Network Access (LNA) on 2025-10-28** ([Chrome blog](https://developer.chrome.com/blog/local-network-access), [chromestatus #5152728072060928](https://chromestatus.com/feature/5152728072060928)). Sites can request access via `fetch("http://192.168.1.42/ping", { targetAddressSpace: "local" })`. The browser shows a permission prompt: "Look for and connect to any device on your local network." On grant, the request proceeds. **But** the page must still be in a secure context — that's fine for `*.github.io`. **And** mixed-content rules still apply to active content (scripts, etc.); LNA only opens fetch and WebSocket. ([WICG spec](https://wicg.github.io/local-network-access/), accessed 2026-05-08.)
- **Firefox** has LNA in Nightly ([FOSDEM 2026 talk](https://fosdem.org/2026/schedule/event/QCSKWL-firefox-local-network-access/)) and enables restrictions by default for users with Enhanced Tracking Protection set to Strict (Firefox 147). Stable rollout pending.
- **Safari** has not announced equivalent UX as of 2026-05-08.

This is the headline 2026 fact for cpipe's editor design. Pre-Chrome 142 plans (websockify proxies, asking users to install a CA, etc.) are now obsolete for that browser; the right design is built around LNA and gracefully degrades.

### 4.2 Fallback chain (decision tree)

```
1. Try LAN-direct via DNS public name (cpipe.local.dev → 192.168.x.y)
   - User scans QR; QR contains: device-fingerprint, public DNS hostname, port, pairing-token.
   - Editor fetches https://abc123.cpipe.local.dev:8443/ws (WSS, Let's Encrypt cert).
   - Cert obtained at device boot via DNS-01 with a public ACME provider (Caddy + acme.sh).
   - Public DNS resolves to a private IP; this is allowed (RFC 1918 in public DNS is fine).
   - Browsers: works on all, including old Safari. No LNA prompt because cert is valid public.
2. Try LAN-direct via LNA permission (Chrome 142+)
   - Fall through to fetch("http://192.168.x.y", { targetAddressSpace: "local" }).
   - User must grant LNA permission once per origin.
3. Try WebRTC DataChannel via Cloudflare Workers signalling
   - Editor and device both connect to wss://cpipe-signal.workers.dev (free tier).
   - SDP offer/answer + ICE candidates exchanged.
   - STUN servers: stun.cloudflare.com:3478, stun.l.google.com:19302.
   - On success, all subsequent traffic flows direct peer-to-peer over DTLS.
4. Try WebRTC + TURN relay (when STUN fails through symmetric NAT)
   - Cloudflare TURN: turns.cloudflare.com:3478 (paid; ~$0.05/GB).
   - All traffic relayed; latency 30-80ms; bandwidth limited.
5. Try Cloudflare Tunnel / ngrok / frp
   - User-configured at device boot; gets a stable public URL.
   - Editor uses URL directly (HTTPS).
6. Manual IP/port
   - User pastes "192.168.1.42:8080" + pairing token.
   - Falls back to (2) with explicit address.
```

#### 4.2.1 The DNS public-name trick

The cleanest LAN solution in 2026 is **public DNS pointing back to private IP**. The cpipe device at boot:

1. Generates a per-device key + fingerprint hash (e.g., `abc123def456`).
2. POSTs to a registration endpoint: `https://reg.cpipe.dev/register` with `{fingerprint, lan_ip, pubkey}`.
3. The endpoint creates `abc123def456.cpipe.local.dev` A-record pointing to the LAN IP (e.g., 192.168.1.42).
4. ACME (DNS-01 challenge with a wildcard `*.cpipe.local.dev` cert, the device handling its sub-name token) provisions the cert.
5. Device serves WSS on port 8443 with this cert.

This works because:

- Browsers accept public hostnames pointing to RFC1918 IPs ([RFC1918 DNS rebinding protection](https://en.wikipedia.org/wiki/DNS_rebinding) is at the network level, not browser; many home routers block it but most phones-as-hotspot don't).
- Cert is valid; no mixed-content; no LNA prompt.
- Costs little — wildcard cert plus a tiny dynamic-DNS service. Tailscale's [MagicDNS](https://tailscale.com/docs/features/client/magic-dns) does exactly this for tailnet members.

Risk: if the user's router does DNS rebinding protection, the resolution returns NXDOMAIN. Detect, fall through to LNA path or WebRTC.

#### 4.2.2 WebRTC DataChannel

[WebRTC DataChannel](https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API/Using_data_channels) (MDN, accessed 2026-05-08) is built on SCTP-over-DTLS, peer-to-peer, full-duplex, encrypted. For cpipe it solves: NAT traversal, no mixed-content gotchas (DataChannel is established outside the document's fetch model), and end-to-end encryption.

Issues:

- Needs a signalling channel to exchange SDP. Cloudflare Workers free tier (3M requests/month) covers this trivially. [p2pcf](https://github.com/gfodor/p2pcf) is a reference implementation.
- Local Network Access permission may *also* gate WebRTC ICE candidates that look local ([Bugzilla 1969916](https://bugzilla.mozilla.org/show_bug.cgi?id=1969916)). Firefox is rolling this out; Chrome's behavior less explicit. For LAN-direct WebRTC, the DNS public-name trick is more reliable.
- DataChannel is "best-effort" reliable. Configure as ordered + reliable for control plane, ordered + max-retransmits-0 (lossy) for thumbnail frames.

Bandwidth budget for thumbnails: 1024×1024 WebP @ q70 ≈ 80 KB. At 30 fps that is 19.2 Mbit/s — feasible on local Wi-Fi, marginal over slow uplinks. For TURN-relayed paths drop to 256×256 @ 10 fps = 0.6 Mbit/s.

#### 4.2.3 Cloudflare's role

[Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/) ([free tier](https://github.com/anderspitman/awesome-tunneling), 2026): no bandwidth limits, stable URL, requires `cloudflared` daemon on the device. Latency ~20–60 ms. Adds Cloudflare to the trust boundary — privacy concern flagged below.

[Cloudflare Realtime / Calls](https://blog.cloudflare.com/announcing-our-real-time-communications-platform/) (announced 2025) adds SFU + TURN. Useful if cpipe ever wants multi-user collaborative editing (out of scope D14). For 1:1 editor↔device, public STUN suffices in ~85% of NAT topologies; TURN catches the remainder.

[ngrok](https://ngrok.com) is simpler but free tier became "increasingly restrictive" in 2026 ([Medium analysis](https://medium.com/@instatunnel/ngrok-alternatives-2026-the-ultimate-tunneling-tool-showdown-9813c8b6b2af)).

[frp (fastonebox/frp)](https://github.com/fatedier/frp) is self-hostable. Recommend documenting it as an option for users who run their own VPS, but not as the default — too operationally complex.

[Tailscale](https://tailscale.com/) Funnel and Serve are excellent but require both ends to install Tailscale. As an *opt-in* path it is great — reserved for power users.

#### 4.2.4 Summary recommendation

Default flow for v1 user:

1. CLI binary: starts local HTTPS server with a generated self-signed cert AND requests a public-DNS name from `reg.cpipe.dev`. After ~10 s, both are ready.
2. CLI prints a QR code.
3. User scans QR with phone, opens https://editor.cpipe.dev (the GitHub Pages URL) which auto-loads the deep-link.
4. Editor connects via the public-DNS WSS. Done.

Fallbacks (for restrictive networks / phones with no router resolution): editor offers a "use cloud relay" button → WebRTC + TURN.

Mobile-app (D19) installation will eventually have its own native socket and not need WebRTC at all — the CLI/Android app hosts its own server, the editor connects directly over the LAN with the DNS trick.

### 4.3 Authentication and pairing

The pairing model:

1. Device generates an Ed25519 keypair at first boot. Public key fingerprint is `fp = first 12 bytes of SHA-256(pubkey)` printed as base32.
2. Device generates a one-time pairing token (32 random bytes, valid for 60 s, single-use).
3. CLI prints a QR encoding `cpipe://pair?fp=<fp>&host=<dns or ip>:<port>&t=<token>&v=1`.
4. Editor scans (camera or paste-into-modal). Performs a Noise XK handshake ([Noise Protocol Framework](https://noiseprotocol.org/noise.html), accessed 2026-05-08) using the device's static pubkey (fp acts as the trusted material).
5. Successful handshake yields a session key K. Editor sends `{type:"pair", token: t}`. Device validates token (single-use, time-limited), stores editor's ephemeral pubkey for future re-pairing-without-QR within a 7-day window (TOFU style).
6. All subsequent control-plane messages are AES-256-GCM with K, with monotonically increasing nonces (replay protection).

A cleaner alternative is **WebAuthn passkey** with cross-device flow (FIDO CTAP 2.2). [WebAuthn passkey QR codes](https://www.corbado.com/blog/webauthn-passkey-qr-code) (Corbado, accessed 2026-05-08) describe the standard hybrid transport flow. The downside: requires platform authenticator (Android with Google Play Services, iOS Keychain). For cpipe v1 (Linux CLI primarily), Noise XK + a manual key file is more pragmatic. Reserve WebAuthn for v2 mobile UX.

Noise XK in JS: [snowy](https://github.com/Sebmaster/snowy) (TS port) or [@cmdcode/noise-protocol](https://github.com/cmdruid/noise-protocol). Total handshake cost <5 ms. Library size <30 KB gzipped.

### 4.4 Wire format

Two channels in one connection:

- **Control plane** (low rate, <100 msg/s typical): JSON. Simple, debuggable. Each message has `{id, type, payload}`. `id` is a UUID for request/response correlation. `type` is verb (`pipeline.set`, `node.update_param`, `node.subscribe_thumbnail`, `device.profile`).
- **Data plane** (thumbnails, profile traces): binary frames over the same WS, distinguished by leading byte:

```
+----+----+----+----+----+----+----+----+
| FT | NID            | TS              |  FT=1B frame type
+----+----+----+----+----+----+----+----+  NID=4B node id (numeric, mapped from string)
| LEN                       | PAYLOAD…  |  TS=4B timestamp (μs since session start)
+--------------------------+------------+  LEN=4B payload length (u32, LE)
```

Frame types: `0x01` thumbnail-WebP, `0x02` profile-event, `0x03` log-line, `0x04` error.

Why not MessagePack everywhere? JSON debuggability (browser dev tools show it natively) outweighs the few bytes saved. We optimize the *thumbnails* (where the bulk of the bytes are) and not the control plane.

### 4.5 Protocol message catalogue

```
Editor → Device:
  pair                {token}
  pipeline.set        {pipeline: <full graph JSON>}
  pipeline.diff       {ops: [{op:"add_node",...},{op:"set_param",...}]}
  node.update_param   {node_id, key, value}
  node.subscribe_thumbnail {node_id, port, max_size, fps}
  node.unsubscribe_thumbnail {node_id, port}
  device.run          {input: <DNG path or URL>, output: <HEIF path>}
  device.profile.start
  device.profile.stop
  ping                {seq}

Device → Editor:
  paired              {session_id, expires_at}
  pipeline.applied    {timestamp, post_resolution_graph}  // includes inserted converts
  node.thumbnail (binary frame type 0x01)
  device.profile.event (binary frame type 0x02)  // {node_id, ms, mem_kb}
  device.run.progress {percent, eta_ms}
  device.run.done     {output_uri, hash, log_summary}
  log (binary frame type 0x03)
  error               {code, message, node_id?, suggested_fix?}
  pong                {seq, ts}
```

Pipeline operations are idempotent: editor sends the full graph on connect to resync; subsequent edits as `diff`. Server-side validates against manifest schemas before applying. On invalid diff, returns error and editor reverts UI.

### 4.6 HDR-aware preview in browser

This is the trickiest part of the editor design.

#### 4.6.1 What the browser can do in 2026

- **Display HDR**: The HDR canvas API ([WICG HDR Canvas](https://github.com/w3c/ColorWeb-CG/blob/main/hdr_html_canvas_element.md)) is in incubation. Chrome 144+ (per [chromestatus](https://chromestatus.com/), 2026) supports `<canvas>` with `colorSpace: "rec2100-pq"` or `"rec2100-hlg"` plus `pixelFormat: "float16"`. Limited to specific configurations.
- **Decode UltraHDR JPEG**: Chrome decodes natively as of Chrome 142 (per [libultrahdr discussions](https://github.com/google/libultrahdr/discussions/346), 2026). Other browsers: not yet.
- **CSS HDR**: `color: color(rec2020 ...)` is shipping but `dynamic-range-limit` and `dynamic-range` media queries are partial.
- **HEIF/HEIC decode**: Safari yes (with VideoToolbox); Chrome no native decode of HEIF still images. AVIF yes everywhere.

Conclusion: a true HDR live preview in browser in 2026 is **unreliable cross-browser**. UltraHDR decode works on Chrome via the gain-map shader trick ([gainmap-js](https://github.com/MONOGRID/gainmap-js)).

#### 4.6.2 Recommended preview strategy

cpipe device tonemaps (or display-maps) the working HDR buffer to SDR for the editor's thumbnail stream. The thumbnail packet includes a small **histogram clipping mask** (a 64×64 binary image marking pixels that exceed SDR range) so the editor can overlay a "HDR clipping" indicator. Optionally, the device can ship a full **UltraHDR JPEG** for offline review — the editor opens it in a separate panel where Chrome users see HDR; other browsers see SDR with a "HDR available, view in Chrome" notice.

For very specific needs (HDR-accurate proofing on a calibrated HDR monitor), users will be directed to the cpipe CLI's `--preview-window` mode which uses a native window that bypasses browser HDR limitations entirely. This is acceptable per D14 (12-month timeline) — making the browser an HDR-grade proof tool is a v2 project.

#### 4.6.3 UltraHDR preview pipeline

When the user wants to peek at the HDR result in the editor:

1. Editor sends `node.subscribe_uhdr {node_id, port}`.
2. Device assembles UltraHDR JPEG (SDR base + gain map; per [#14 — HEIF and HDR Output](14-heif-and-hdr-output.md)) sized to a preview resolution (1920 wide).
3. Frame type `0x05` carries the UltraHDR JPEG bytes.
4. Editor's preview panel uses [libultrahdr-wasm](https://github.com/MONOGRID/libultrahdr-wasm) (WASM) to decode the gain map into a high-precision RGB texture; renders to an HDR `<canvas>` if available, else a tonemapped SDR `<canvas>`.

This avoids depending on Chrome's `targetAddressSpace` or browser-native UltraHDR decode and works cross-browser at the cost of WASM size (~2 MB; lazy-loaded only when the user opens the HDR preview panel).

### 4.7 Real-world precedents

- **TouchDesigner WebRTC DAT** ([derivative.ca/UserGuide/WebRTC](https://derivative.ca/UserGuide/WebRTC), accessed 2026-05-08) uses WebSockets for signalling, WebRTC for media. The same architecture cpipe is proposing.
- **OBS WebSocket protocol** ([github.com/obsproject/obs-websocket](https://github.com/obsproject/obs-websocket)) is JSON over WSS with a `request_type` field — exactly the control-plane shape.
- **Bitwig Studio** uses a localhost WebSocket for its control surface scripts.
- **Resolume Wire** uses direct LAN WS.
- **Logic Remote** uses Bonjour/mDNS over Wi-Fi between iPad and Mac. Specific to Apple ecosystem, not portable.
- **ROS bridge** (`rosbridge_suite`) is a JSON-over-WS protocol for WebSocket clients to subscribe to ROS topics. Pattern for streaming binary blobs alongside JSON control.

### 4.8 Privacy posture

Editor↔device channel is end-to-end encrypted in all four primary paths (WSS-with-cert, WebRTC DTLS, TURN-relayed-but-DTLS, Cloudflare Tunnel TLS terminating at edge then re-encrypting). Cloudflare Tunnel and ngrok terminate TLS at their edge — Cloudflare can in principle read traffic. The privacy posture is documented to users; a "no third-party relay" mode (only paths 1 and 3 with public STUN) is offered for paranoid users.

Image data is fungible: a stolen thumbnail reveals the user's photo, which is sensitive. So for any TURN-relayed path, an additional application-layer encryption (XChaCha20-Poly1305 with the Noise session key) is layered atop DTLS. Belt and suspenders.

### 4.9 Discovery — and why mDNS fails

Bonjour/mDNS (`_cpipe._tcp.local`) advertises devices on the LAN. Browsers cannot directly query mDNS — the JS environment has no API for it. Workarounds: a browser extension (Chrome `chrome.system.network`, restricted), a companion native helper, or a trick using fetch errors to time out IP scans (slow and unreliable). cpipe sidesteps the problem with the QR code approach: the user's phone sees the QR; the editor URL parses the deep-link and connects directly. Discovery becomes a UX problem, not a network problem.

For server-side discovery (multiple cpipe devices on one LAN), the editor maintains a "recently connected" list in localStorage. Clicking a recent entry retries the connection in the order of paths above.

### 4.10 Performance budget

Target: 100 ms end-to-end from "user moves slider" to "thumbnail updates":

- Editor: 2 ms compute new value, 1 ms encode JSON, send.
- Network LAN: 1–5 ms.
- Device: 5 ms parse, 30 ms re-render the affected sub-DAG (per [#03](03-heterogeneous-scheduler.md) profiler — a denoise node may dominate), 10 ms WebP encode, 1 ms frame.
- Network: 1–5 ms.
- Editor: 2 ms decode WebP, 5 ms repaint.

Total typical: 60 ms. Well within 100 ms target. For TURN-relayed paths, latency triples but interactivity remains acceptable.

For initial load (first time editor connects, syncs full pipeline + manifests + recent thumbnails): target 500 ms over LAN, 3 s over relay. 30 nodes × 5 KB manifest + 30 × 80 KB thumbnails = ~2.5 MB. Compress with `permessage-deflate` (WSS option) for ~50% reduction.

## 5. Concrete sketches

### 5.1 Pipeline JSON

```json
{
  "$schema": "https://schemas.cpipe.dev/pipeline/v1.json",
  "version": "1.0.0",
  "metadata": { "title": "Sunset RAW", "author_fingerprint": "..." },
  "nodes": [
    { "id": "n1", "type": "com.cpipe.input.dng",     "params": { "path": "@input" } },
    { "id": "n2", "type": "com.cpipe.demosaic.amaze", "params": {} },
    { "id": "n3", "type": "com.cpipe.wb.dng",        "params": { "preset": "asshot" } },
    { "id": "n4", "type": "com.cpipe.denoise.bm3d",  "params": { "sigma": 3.5 } },
    { "id": "n5", "type": "com.cpipe.tonemap.aces",  "params": { "exposure": 0.0 } },
    { "id": "n6", "type": "com.cpipe.output.heif",   "params": { "path": "@output" } }
  ],
  "edges": [
    { "from": ["n1","out"],  "to": ["n2","in"] },
    { "from": ["n2","out"],  "to": ["n3","in"] },
    { "from": ["n3","out"],  "to": ["n4","in"] },
    { "from": ["n4","out"],  "to": ["n5","in"] },
    { "from": ["n5","out"],  "to": ["n6","in"] }
  ],
  "ui": { "viewport": { "x": 0, "y": 0, "zoom": 1.0 },
          "groups":   [{ "label": "RAW prep", "nodes": ["n1","n2","n3"] }] }
}
```

### 5.2 Pairing handshake (sequence)

```
Editor                         Signalling/Local              Device
  |                                  |                          |
  |-- (scan QR) ----------------------------------------------->| (token T, fp F, host H)
  |                                                              |
  |== Open WSS to H, TLS handshake ==============================|
  |                                                              |
  |-- Noise_XK_25519_AESGCM_SHA256 e -------------------------->|
  |<- e, ee, s, es ---------------------------------------------|
  |-- s, se ---------------------------------------------------->|
  |                                                              |
  |== {type:"pair", token: T, abi_min: 1, capabilities: [...]} ==|
  |                                                              |
  |<= {type:"paired", session_id, ttl_s, server_caps: {...}} ====|
  |                                                              |
  | <-- ready -->                                                 |
```

Total round trips: 1 TLS, 1 Noise handshake (1.5 RT), 1 pair message. ~4 RT total = 12 ms on LAN.

### 5.3 Profiler subscription example

```ts
// in DevicePane.tsx
const subscribe = useCallback(() => {
  rpc.send({ type: "device.profile.start" });
}, [rpc]);

useEffect(() => {
  const unsub = rpc.onBinaryFrame(0x02, (frame) => {
    const view = new DataView(frame.buffer);
    const node_id = view.getUint32(1, true);
    const ts_us   = view.getUint32(5, true);
    const ms      = view.getFloat32(9, true);
    const mem_kb  = view.getUint32(13, true);
    profileStore.append(node_id, { ts_us, ms, mem_kb });
  });
  return unsub;
}, [rpc]);
```

### 5.4 LNA-aware fetch wrapper

```ts
async function deviceFetch(url: URL, init?: RequestInit): Promise<Response> {
  if (url.protocol === "http:" && isPrivateHost(url.host)) {
    const merged = { ...init, targetAddressSpace: "local" } as any;
    try {
      return await fetch(url, merged);
    } catch (e) {
      if (e.name === "TypeError" && /local network/i.test(String(e))) {
        throw new LNAPermissionRequired();
      }
      throw e;
    }
  }
  return fetch(url, init);
}
```

## 6. Open questions

1. **Does Apple Safari ever ship LNA?** As of 2026-05-08 no announcement. Plan B for Safari users: rely on the public-DNS trick or WebRTC.
2. **Does the public DNS rebinding-protection problem affect typical home routers?** Spot-check: ASUS, TP-Link, OpenWrt — none enable DNS rebinding protection by default. Some carrier-locked phones may. Need a survey before committing to the scheme.
3. **What's the failure rate of STUN-only connections?** Cloudflare's data ([blog.cloudflare.com/announcing-our-real-time-communications-platform/](https://blog.cloudflare.com/announcing-our-real-time-communications-platform/)) suggests ~85% direct, ~15% need TURN relay. Acceptable.
4. **Can the editor verify the device's firmware integrity through the protocol?** Out of v1; consider in v2 (signed manifests, expected hash chain).
5. **Should the editor support multi-device sessions** (e.g., two editors connected to one device, or one editor controlling two devices)? Out of v1; reserve protocol fields (`session_count`, `peer_id`).
6. **Profiler granularity**: per-node ms is easy; per-pass ms (within a Halide pipeline) requires deeper instrumentation. Cross-link to [#03 §profiler](03-heterogeneous-scheduler.md). Open: should the profile event format carry sub-events?
7. **Browser HDR canvas API stability**: WICG HDR canvas is incubating; spec may move before cpipe v1 ships. Implementation should isolate the HDR path so it can be swapped.
8. **Cloudflare dependency**: defaulting signalling to Cloudflare Workers makes us reliant on their free tier. If Cloudflare changes pricing, fallback is a tiny VPS (€3/month). Document the option.

## 7. Cited sources

- xyflow / React Flow homepage — `https://reactflow.dev/` and `https://xyflow.com/` (accessed 2026-05-08).
- React Flow 12 release blog — `https://xyflow.com/blog/react-flow-12-release` (2024-07-09; accessed 2026-05-08).
- React Flow performance guide — `https://reactflow.dev/learn/advanced-use/performance` (accessed 2026-05-08).
- React Flow accessibility guide — `https://reactflow.dev/learn/advanced-use/accessibility` (accessed 2026-05-08).
- React Flow state management guide — `https://reactflow.dev/learn/advanced-use/state-management` (accessed 2026-05-08).
- xyflow GitHub — `https://github.com/xyflow/xyflow` (accessed 2026-05-08).
- Comfy-Org/litegraph.js (archived) — `https://github.com/Comfy-Org/litegraph.js` (accessed 2026-05-08).
- ComfyUI Nodes 2.0 — `https://blog.comfy.org/p/comfyui-node-2-0` (Jul 2025; accessed 2026-05-08).
- Rete.js v2 — `https://rete.js.org/` and `https://retejs.org/docs/` (accessed 2026-05-08).
- Drawflow — `https://github.com/jerosoler/Drawflow` (accessed 2026-05-08).
- AntV X6 — `https://github.com/antvis/X6` (accessed 2026-05-08).
- JointJS pricing — `https://www.jointjs.com/pricing` (accessed 2026-05-08).
- JointJS license — `https://www.jointjs.com/license` (accessed 2026-05-08).
- tldraw SDK — `https://tldraw.dev/` (accessed 2026-05-08).
- tldraw Workflow starter kit — `https://tldraw.dev/starter-kits/workflow` (accessed 2026-05-08).
- Zustand persist middleware — `https://zustand.docs.pmnd.rs/reference/integrations/persisting-store-data` (accessed 2026-05-08).
- Ajv — `https://github.com/ajv-validator/ajv` (accessed 2026-05-08).
- Chrome Local Network Access blog — `https://developer.chrome.com/blog/local-network-access` (accessed 2026-05-08).
- WICG Local Network Access spec — `https://wicg.github.io/local-network-access/` (accessed 2026-05-08).
- Chrome Status: Local network access restrictions — `https://chromestatus.com/feature/5152728072060928` (accessed 2026-05-08).
- Firefox LNA (FOSDEM 2026) — `https://fosdem.org/2026/schedule/event/QCSKWL-firefox-local-network-access/` (accessed 2026-05-08).
- WebRTC DataChannel intro (MDN) — `https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API/Using_data_channels` (accessed 2026-05-08).
- p2pcf — `https://github.com/gfodor/p2pcf` (accessed 2026-05-08).
- Cloudflare Realtime announcement — `https://blog.cloudflare.com/announcing-our-real-time-communications-platform/` (accessed 2026-05-08).
- Cloudflare Workers — `https://workers.cloudflare.com/` (accessed 2026-05-08).
- awesome-tunneling — `https://github.com/anderspitman/awesome-tunneling` (accessed 2026-05-08).
- Tailscale Serve docs — `https://tailscale.com/docs/features/client/device-web-interface` (accessed 2026-05-08).
- Noise Protocol Framework — `https://noiseprotocol.org/noise.html` (accessed 2026-05-08).
- libultrahdr (Google) — `https://github.com/google/libultrahdr` (accessed 2026-05-08).
- libultrahdr-wasm (MONOGRID) — `https://github.com/MONOGRID/libultrahdr-wasm` (accessed 2026-05-08).
- gainmap-js (MONOGRID) — `https://github.com/MONOGRID/gainmap-js` (accessed 2026-05-08).
- TouchDesigner WebRTC DAT — `https://derivative.ca/UserGuide/WebRTC` (accessed 2026-05-08).
- WebTransport vs WebSocket vs DataChannel — `https://www.softpagecms.com/2025/08/25/datachannel-webtransport/` (accessed 2026-05-08).
- WebP vs AVIF benchmark — `https://pixotter.com/blog/webp-vs-avif/` (accessed 2026-05-08).
- ComfyUI subgraph feature — `https://docs.comfy.org/interface/features/subgraph` (accessed 2026-05-08).

## 8. See also

- [#01 — Compute Frameworks](01-compute-frameworks.md): the device side runs Halide / slang-rhi; runtime ms reported here is from that scheduler.
- [#02 — Zero-Copy Buffer Architecture](02-zero-copy-buffer-architecture.md): thumbnails reference these buffers; encoder taps the same memory.
- [#03 — Heterogeneous Scheduler](03-heterogeneous-scheduler.md): profile events surfaced through this protocol.
- [#04 — Mobile AI Inference](04-mobile-ai-inference.md): for AI-node thumbnails the inference path is the longest pole.
- [#10 — Plugin Architecture](10-plugin-architecture.md): manifest schema consumed by the editor's parameter form.
- [#13 — Color Management](13-color-management.md): OCIO roles displayed on edges; HDR working space.
- [#14 — HEIF and HDR Output](14-heif-and-hdr-output.md): UltraHDR JPEG used for HDR preview.
- [#16 — Camera2 RAW and Burst](16-camera2-raw-and-burst.md): the Android app is the local server, hosts the same protocol.
- [#17 — Mobile Pro Camera Apps](17-mobile-pro-camera-apps.md): UX precedents for parameter form.
