# CLAUDE.md

Behavioral guidelines for all LLM coding assistants on this project (Claude Code, Codex, and any subagents they spawn). Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Don't Reinvent the Wheel

**Use what's already chosen. Cite when you must build.**

- Before writing ≥50 lines of custom algorithm or utility code, check [`docs/tech.md`](docs/tech.md). If an approved dependency covers it, use it.
- If you genuinely need a dependency that isn't in `tech.md`, justify in the PR description why the current stack is insufficient. Do not silently add packages to `vcpkg.json` / `FetchContent`.
- When implementing a classic or AI ISP algorithm, cite the relevant section of [`docs/research/07-classic-isp-algorithms.md`](docs/research/07-classic-isp-algorithms.md) or [`08-ai-isp-algorithms.md`](docs/research/08-ai-isp-algorithms.md) in both the code doc-comment and the PR description. Inspiration / re-implementation from primary papers is allowed (D11) — cite the source.

## 2. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 3. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 4. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 5. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

## Agent Development Workflow

The project advances one **T-task** at a time, defined in the active phase doc (`docs/phase-XX-*.md`). The workflow below is mandatory for every code-touching task.

### Before

- **Read** `CLAUDE.md` + active `phase-XX-*.md`. Pull `architecture / buffer / plugin-sdk / tech / research` on demand.
- **Stop and ask** if the task depends on an unresolved Open Question ([`architecture.md §17`](docs/architecture.md)) or crosses v1 scope (`_toc.md §6`, `buffer.md §13`, `plugin-sdk.md §12`, v1.1+ tags in `roadmap.md`). Do not pick a default and run.

### During

- **Branch:** `<type>/p<phase>-t<task>-<slug>` where `<type>` ∈ `feat / fix / refactor / docs / test / chore / perf / ci`.
- **One PR per T-task.** Sub-task PRs are allowed only if the T DoD closes before any of its acceptance boxes is ticked.
- **Strict TDD.** Failing Catch2 test (RED) first, then implementation (GREEN). For structural tasks (repo / CMake / CI), `cmake` or `ctest` failing on a missing target is a valid RED.
- **SPDX header (PD-25)** on every new `.h / .hpp / .cpp`.
- **Phase-local decisions** append a `PD-N` row in the same PR. `RD-N / D-N / B-N / P-N` are human-only — flag, do not edit.

### Done — all must hold in the PR

- Every acceptance box for the T is ticked `[ ]` → `[x]`; every Verification command in `phase-XX-*.md` ran locally.
- Local CI-equivalent is green: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset ci && pre-commit run --all-files` (refresh the command per phase).
- Non-code tasks state explicitly *how* the change was verified.
- PR description carries evidence (command output, screenshot, or CI run URL).

### After

Same PR also updates the changelog surface:

- **Always:** tick boxes + append `PD-N` rows in `phase-XX-*.md`.
- **Phase tag:** "What Shipped / What Slipped" entry in `phase-XX-*.md`; phase-status table in [`roadmap.md`](docs/roadmap.md); "Current Status" table in [`README.md`](README.md).
- **Open Question resolved:** [`architecture.md §17`](docs/architecture.md) + the references in [`roadmap.md`](docs/roadmap.md).
- Commit message: `<type>: <description>`. Co-author attribution off.

`CHANGELOG.md` is intentionally absent (PD-2): `phase-XX-*.md` + `roadmap.md` + `git log` is the changelog.

---

## Project: cpipe

**cpipe** (Computational Photography Pipeline) is a professional camera + post-processing app. Core = DAG-based soft ISP running on CPU + GPU + NPU with zero-copy buffers and plugin nodes (classic + AI). v1 = Linux desktop CLI (DNG → HEIF batch + benchmark) + Web Editor on GitHub Pages; Android in v1.1; macOS / iOS = v2.

**D1–D19, B1–B12, P1–P16, RD-1–RD-28, and the current phase's PD-N tables are hard constraints.** Re-deriving any of them without consulting the source will produce decisions that conflict with the locked plan.

### Reading map

1. **[`docs/research/_toc.md`](docs/research/_toc.md)** — D1–D19, 6 research clusters, methodology.
2. **[`docs/research/00-summary.md`](docs/research/00-summary.md)** — recommended stack, architecture overview, cross-cluster decision matrix, license inventory, risk register, 15 open questions.
3. **[`docs/tech.md`](docs/tech.md)** — every external library, version pin, license verdict.
4. **[`docs/architecture.md`](docs/architecture.md)** — system assembly: six CMake targets, single-process threading model, Pipeline lifecycle, editor protocol, test pyramid, open-question tracker (§17).
5. **[`docs/buffer.md`](docs/buffer.md)** — B1–B12: `IBuffer`, allocators, external imports, fences, producers.
6. **[`docs/plugin-sdk.md`](docs/plugin-sdk.md)** — P1–P16: C ABI, four suites, `CPIPE_REGISTER_NODE`, JSON Schema manifest.
7. **[`docs/roadmap.md`](docs/roadmap.md)** — RD-1–RD-28, phase shape, slip strategy. Phase detail in `phase-XX-*.md` siblings.
8. Numbered research chapters `01`–`17` for evidence behind any recommendation.

### Lookup, license, and scope rules

- **Lookup.** Library / version / license → `docs/tech.md`. Module / threading / lifecycle / protocol → `docs/architecture.md`. Cite the underlying research chapter when proposing a change. Do not restate stack details in code comments or new docs — link to the source of truth.
- **License (D11).** Project is **Apache 2.0**. The following are **license traps** — do not link or copy code from them: x265, exiv2, dt-dng, darktable / RawTherapee cores, dcraw, Argyll CMS, DCamProf, OpenCamera, FreeDcam. Inspiration / re-implementation from primary papers is OK; cite the paper.
- **Scope.** Out of v1 per `_toc.md` §6 + `buffer.md` §13 + `plugin-sdk.md` §12: tile-based processing, streaming preview, ZSL, X-Trans demosaic, plugin marketplace, video output, JPEG output, hot-reload, sandboxing, `param_changed`, cross-process plugins. Architecture preserves v2 evolution; v1 implements none of these. Apply the workflow's scope check before touching adjacent code.

### Open questions

[`docs/research/00-summary.md` §9](docs/research/00-summary.md) lists 15 open questions; [`docs/architecture.md` §17](docs/architecture.md) tracks them with architecture impact. Resolved so far: **Q10** (burst = N independent `.dng`), **Q12** (no Windows v1), **Q15** (editor edits `pipeline.cpipe.json` only — no in-browser node authoring). The other 12 still need human decision; apply the workflow's open-question check before implementing in any area they touch.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, clarifying questions come before implementation rather than after mistakes, and `phase-XX-*.md` / `roadmap.md` / `README.md` always reflect the repository's true state at HEAD.
