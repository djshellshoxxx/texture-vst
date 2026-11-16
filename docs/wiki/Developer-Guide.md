# Developer Guide

## Build

```bash
cmake --preset release
cmake --build --preset release
ctest --test-dir build/release -C Release --output-on-failure
```

Requirements: CMake 3.22+, C++20, JUCE 8.0.8 (fetched, or `-DTEXTURE_JUCE_PATH=/path/to/JUCE`).
Options (`TEXTURE_BUILD_TESTS`, `TEXTURE_BUILD_CLAP`, `TEXTURE_ENABLE_ASAN`, `TEXTURE_WARNINGS_AS_ERRORS`,
`TEXTURE_LICENSE_SECRET`, `TEXTURE_CODESIGN_IDENTITY`, `TEXTURE_AAX_SDK_PATH`) are in the README.

CI (`.github/workflows/build.yml`) builds and tests on Windows (MSVC), macOS (clang) and Linux (gcc),
with a separate CLAP job on Linux. Cross-compiling Windows from Linux is described in the README and
needs Wine for helper steps.

## Test

`tests/` holds 16 files (C++ tests registered with CTest as `Texture_Tests`). The README "Running the
tests" section lists how to run a single suite.

## Rules that the code relies on

- The audio thread never writes parameters; learned CC values go through an atomic and `AsyncUpdater`.
- Sample swaps happen by pointer store; buffers are built on a background thread.
- Layout is fixed at 720x480 and scaled uniformly; do not add a second layout path.
- Release builds must set a private `TEXTURE_LICENSE_SECRET`; the development fallback is not for release.
- Keep the product scope: no filter, arpeggiator, wavetables, modulation matrix or delay (README).

## Before release

Read `docs/RELEASE_VALIDATION.md` and `docs/MANUAL_TEST_PLAN.md`.
