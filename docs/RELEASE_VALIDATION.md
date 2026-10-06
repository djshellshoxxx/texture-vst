# TEXTURE — Release Validation Record

**Status:** In progress  
**Branch:** `codex/spec-compliance-motion`  
**Updated:** 2026-10-06

This file records evidence for beta/release claims. A target is not marked verified
unless its stated check was actually run and the result inspected.

| Area | Current evidence | Status |
|---|---|---|
| Windows VST3 build | Previously built successfully on MSVC; current Actions run does not start job steps | Re-validation required |
| macOS AU build | Previously green in CI according to project progress log | Re-validation required |
| Linux build | Previously green in CI according to project progress log | Re-validation required |
| CLAP | Previously built in CI and locally on MSVC | Re-validation required |
| Unit tests | Existing suite previously recorded green; new reduced-motion/state tests added | Re-run required |
| Deterministic DSP fuzzer | Existing target previously recorded green | Re-run required |
| VST3 host load | Prior standalone/VST3 launch evidence exists in progress log | Manual sweep incomplete |
| Standalone | Prior window/render verification exists | Manual sweep incomplete |
| AU in Logic | Not yet manually loaded | Planned |
| AAX | SDK unavailable | Conditional / unverified |
| pluginval | No current recorded run | Planned |
| 24-hour soak | Not completed | Planned |
| CPU target measurements | Not recorded | Planned |

## Current CI infrastructure issue

GitHub Actions runs for both the current development branch and its unchanged master
base report failure with no workflow job steps returned. Because the same condition is
present on the base revision, it is tracked as an unresolved CI/runner execution issue
rather than attributed to the code changes in this branch.

## Beta gate

Before marking TEXTURE beta-ready:

- obtain a clean configure/build/test result for the current head;
- run the deterministic unit and fuzz suites including the new state/motion tests;
- complete the representative VST3 + standalone manual sweep;
- run pluginval and record version/settings/results;
- record at least one representative CPU/performance measurement;
- list every host/format combination that remains untested.
