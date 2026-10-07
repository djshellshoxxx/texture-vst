# TEXTURE — Release Validation Record

**Status:** v0.0.1 beta released (pre-release)  
**Branch:** `claude/laughing-darwin-8l76e3` (merged to `master`)  
**Updated:** 2026-10-07

This file records evidence for beta/release claims. A target is not marked verified
unless its stated check was actually run and the result inspected.

| Area | Current evidence (v0.0.1 beta) | Status |
|---|---|---|
| Linux build (gcc 13, `-Werror`) | VST3, CLAP, Standalone built clean on Ubuntu 24.04 | Verified |
| Unit tests (Linux) | `ctest`: 11,403+ checks, 0 failures | Verified |
| Deterministic DSP fuzzer (Linux) | 60 s run passes (found and fixed the saturator Inf spike) | Verified |
| pluginval, Linux VST3 | pluginval 1.0.4 (built from source), strictness 5, in-process, under Xvfb: SUCCESS - includes editor, automation, state, bus and audio-processing tests | Verified |
| Standalone (Linux) | Launched under Xvfb; layout inspected; Undo/Redo exercised live | Verified |
| Windows build | Cross-built with clang-cl + lld-link + Windows SDK 10.0.22621 + VC 14.11 runtime + libc++ 18 (`cmake/WindowsCrossClangCl.cmake`), static runtime | Verified (non-MSVC toolchain) |
| Unit tests + fuzzer (Windows binaries) | `Texture_Tests.exe texture` 11,403 checks / `fuzz` pass under Wine 9.0 | Verified under Wine |
| Windows VST3 instantiation | `juce_vst3_helper` loaded the plug-in under Wine and wrote `moduleinfo.json` | Verified under Wine |
| Windows GUI | Not verifiable here: JUCE 8's `VBlankThread` blocks on Wine's stubbed `IDXGIOutput::WaitForVBlank`, so the window never maps under Wine | **Needs a real Windows machine** |
| Windows installer (NSIS) | Silent install under Wine places the app, VST3 bundle, CLAP and Start-menu shortcuts and registers an uninstaller; silent uninstall removes them | Verified under Wine |
| Linux installer | Installs to `~/.local/bin`, `~/.vst3`, `~/.clap` and a menu entry in a clean home | Verified |
| Linux binary compatibility | Requires glibc 2.38 or newer | Documented |
| macOS / AU | Not built: no macOS machine and GitHub Actions has no runners | Not in this release |
| AU in Logic | Not loaded | Not in this release |
| AAX | SDK unavailable | Conditional / unverified |
| Licence secret | Built with the development secret (`TEXTURE_LICENSE_SECRET` unset) | Set a private secret before a paid release |
| DAW manual sweep | `docs/MANUAL_TEST_PLAN.md` still needs a human at a DAW | Outstanding |
| 24-hour soak / CPU targets | Not run | Outstanding |

## Current CI infrastructure issue

GitHub Actions jobs on every branch, including `master`, fail within seconds with no
steps and runner id 0: no runner is ever assigned. This is an account-level Actions
block (minutes/billing for this private repository), not a code failure. Until it is
lifted, release builds are produced and validated locally as recorded above.

## Beta gate

Before marking TEXTURE beta-ready:

- obtain a clean configure/build/test result for the current head;
- run the deterministic unit and fuzz suites including the new state/motion tests;
- complete the representative VST3 + standalone manual sweep;
- run pluginval and record version/settings/results;
- record at least one representative CPU/performance measurement;
- list every host/format combination that remains untested.
