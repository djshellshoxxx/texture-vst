# TEXTURE — Release Validation Record

**Status:** v0.1.0 pre-release published (Windows, macOS, Linux); asset contents inspected, binaries not yet run on Windows or macOS hosts.
**Release automation:** A `v*` tag runs the Windows, macOS and Linux build/test matrix, then packages Windows and macOS assets.
**Updated:** 2026-10-20

This file records evidence for beta/release claims. A target is not marked verified
unless its stated check was actually run and the result inspected.

| Area | Current evidence | Status |
|---|---|---|
| Windows build + tests | GitHub Actions run `39399502499`, commit `9b890d3` on `codex/shared-guidance-and-feature-work`; MSVC build and CTest succeeded | Verified for that commit |
| macOS build + tests | Same run; universal macOS/clang build and CTest succeeded | Verified for that commit |
| Linux build + tests | Same run; GCC build and CTest succeeded | Verified for that commit |
| Development candidate Linux build + tests | Commit `2378c74`; Linux Release VST3 and Standalone built, all three CTest entries passed (109,332 unit assertions plus fuzz and waveform math) | Verified for this commit |
| Linux CLAP build | Same run; CLAP configure and build succeeded | Verified for that commit |
| Candidate Windows/macOS release build | No tag-triggered run for commit `2378c74` yet | Pending |
| Current `master` release build | No tag-triggered release run recorded yet | Pending |
| Windows standalone GUI | CI build only; no native interactive GUI sweep recorded | Outstanding |
| macOS standalone and AU host validation | CI build only; Logic/AU host sweep not recorded | Outstanding |
| AAX | SDK unavailable | Conditional / unverified |
| Licence secret | Release candidate uses the development fallback unless a private secret is configured | Beta only |
| DAW manual sweep | `docs/MANUAL_TEST_PLAN.md` still needs a human at a DAW | Outstanding |
| 24-hour soak / CPU targets | Not run | Outstanding |

## Beta gate

Before marking TEXTURE beta-ready:

- obtain a clean configure/build/test result for the current head;
- run the deterministic unit and fuzz suites including the new state/motion tests;
- complete the representative VST3 + standalone manual sweep;
- run pluginval and record version/settings/results;
- record at least one representative CPU/performance measurement;
- list every host/format combination that remains untested.
