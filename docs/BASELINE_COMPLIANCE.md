# TEXTURE — CDL Baseline Compliance

**Baseline:** `docs/standards/CDL_PLUGIN_BASELINE.md` v1.0  
**Product spec:** [PRODUCT_SPEC.md](PRODUCT_SPEC.md)  
**Status date:** 2026-10-06

| Baseline area | Status | Evidence / remaining work |
|---|---|---|
| Product profile and compliance record | Implemented | `docs/PRODUCT_SPEC.md` and this file. |
| Predictable controls / parameter contract | Implemented | Stable IDs in `ParameterIDs.*`; APVTS attachments; documented defaults/ranges; contextual reset/value/MIDI actions. |
| Accessibility / keyboard / tooltips | Implemented | Focus order, accessible titles/descriptions, tooltip preference, keyboard shortcuts and help. |
| Reduced motion | Implemented on development branch | Preferences toggle disables nonessential control easing, pulsing, panel sliding, data-stream glide, icon flash and toast fades while leaving meters/state feedback active. |
| Host/device responsibility | Implemented | Plug-in does not open host devices; standalone-only audio/MIDI setup is gated by wrapper type. |
| Real-time safety | Implemented + tested | Cached parameter pointers, preallocated DSP paths, async file loading, lock-free sample publication strategy, allocation/fuzz tests. |
| Sample rate / block size / finite output | Implemented + tested | Unit tests cover variable block sizes, NaN handling, effects finiteness and engine behavior. |
| Buses / MIDI / latency / tail | Implemented | Processor declares MIDI instrument behavior, supported layouts, zero latency and tail length. |
| State recall / malformed state | Implemented, further hardening planned | Version-tagged state and safe top-level parse checks exist. Add explicit future-version/partial-tree regression tests before beta. |
| Presets / A-B | Implemented + tested | Schema validation, round-trip/error tests, A/B slots and factory/user preset paths. |
| File loading / export | Implemented + tested | Async multi-format loader, limits/error paths and stereo WAV export. |
| MIDI Learn / modulation | Implemented | Learn/unlink UI, lock-free controller capture, MORPH LFO, sustain and panic MIDI behavior. |
| CDL visual identity | Implemented with product exception | TEXTURE keeps its MORPH identity accent while using CDL dark technical layout and shared typography principles. |
| Help / About | Implemented | In-product Help, About/version, diagnostics and repository documentation. |
| Privacy / network disclosure | Implemented | No automatic telemetry/analytics/update checks; network-related links are explicit user actions. |
| Build and CI | Implemented | Windows/macOS/Linux CI plus optional CLAP job. |
| Manual host validation | Planned | `docs/MANUAL_TEST_PLAN.md` remains the beta blocker for representative DAW/standalone checks. |
| AAX validation | Planned / conditional | Requires Avid SDK and developer access. |
| Long soak / CPU target recording | Planned | Manual 24-hour soak and performance measurements remain outstanding. |

## Next implementation priorities

1. Add regression tests around malformed/partial/future-version host state before changing state migration behavior.
2. Add a release-validation document that records actual host/pluginval versions and results instead of relying on generic claims.
3. Complete the manual test plan and record results per host/format.
4. Run and record performance and soak targets.
