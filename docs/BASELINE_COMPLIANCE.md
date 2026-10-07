# TEXTURE — CDL Baseline Compliance

**Baseline:** `docs/standards/CDL_PLUGIN_BASELINE.md` v1.0  
**Product spec:** [PRODUCT_SPEC.md](PRODUCT_SPEC.md)  
**Status date:** 2026-10-06

| Baseline area | Status | Evidence / remaining work |
|---|---|---|
| Product profile and compliance record | Implemented | `docs/PRODUCT_SPEC.md` and this file. |
| Predictable controls / parameter contract | Implemented | Stable IDs in `ParameterIDs.*`; APVTS attachments; documented defaults/ranges; contextual reset/value/MIDI actions; strict numeric parser rejects malformed/out-of-range direct entry; parameter-aware clipboard rejects cross-parameter and out-of-range paste instead of silently clamping. |
| Accessibility / keyboard / tooltips | Implemented | Focus order, accessible titles/descriptions, tooltip preference, keyboard shortcuts and help. |
| Reduced motion | Implemented | Preferences toggle disables nonessential control easing, pulsing, panel sliding, data-stream glide, icon flash and toast fades while leaving meters/state feedback active. |
| Host/device responsibility | Implemented | Plug-in does not open host devices; standalone-only audio/MIDI setup is gated by wrapper type. |
| Real-time safety | Implemented + tested | Cached parameter pointers, preallocated DSP paths, async file loading, lock-free sample publication strategy, allocation/fuzz tests. |
| Sample rate / block size / finite output | Implemented + tested | Unit tests cover variable block sizes, NaN handling, effects finiteness and engine behavior. |
| Buses / MIDI / latency / tail | Implemented | Processor declares MIDI instrument behavior, supported layouts, zero latency and tail length; synced MORPH LFO defines stopped/missing-tempo/seek/loop behavior. |
| State recall / malformed state | Implemented | Pre-application schema validation rejects wrong roots, malformed/future versions, missing parameter state, invalid sample-layer indices and duplicate layer records. Covered by `test_state_schema.cpp`; detailed policy in `STATE_COMPATIBILITY_SPEC.md`. |
| Presets / A-B | Implemented + expanded tests | Schema/version/type/range validation, atomic rejection, filtered serialization, round-trip/error tests, A/B slots and factory/user preset paths. |
| File loading / export | Implemented + expanded tests | Async multi-format loader, limits/error paths, exact-sample offline event timeline, dual-layer/LFO/GRIT-aware stereo WAV export and documented no-normalization policy. |
| MIDI Learn / modulation | Implemented | Persistent global learn status, knob indication, explicit cancel, 15-second timeout, learn/unlink UI, reserved-CC handling, lock-free controller capture and documented MIDI/automation precedence. |
| CDL visual identity | Implemented with product exception | TEXTURE keeps its MORPH identity accent while using CDL dark technical layout and shared typography principles. |
| Help / About | Implemented | In-product Help, About/version, diagnostics, current licensing text, Circuit Drift Labs link and developer-contact wording. |
| Privacy / network disclosure | Implemented | No automatic telemetry/analytics/update checks; network-related links are explicit user actions. |
| Build and CI | Implemented | Windows/macOS/Linux CI plus optional CLAP job. |
| Manual host validation | Planned | `docs/MANUAL_TEST_PLAN.md` remains the beta blocker for representative DAW/standalone checks. |
| AAX validation | Planned / conditional | Requires Avid SDK and developer access. |
| Long soak / CPU target recording | Planned | Manual 24-hour soak and performance measurements remain outstanding. |

## Next implementation priorities

1. Restore functioning CI execution, then run the new reduced-motion and host-state tests on all claimed build targets.
2. Complete the manual test plan and record results per host/format in `RELEASE_VALIDATION.md`.
3. Run pluginval and record its version/settings/results.
4. Run and record performance and soak targets.
