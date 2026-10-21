#!/usr/bin/env bash
# Package the Windows and macOS products collected from the tested build matrix.
# Usage: package_platform_release.sh VERSION ARTIFACT_ROOT OUTPUT_DIR
set -euo pipefail

version="${1:?version required}"
artifact_root="$(realpath "$2")"
out="$(realpath -m "$3")"
mkdir -p "$out"

fail() { echo "release packaging: $*" >&2; exit 1; }
find_one() {
    local root="$1" name="$2" result
    result="$(find "$root" -type f -name "$name" -print -quit)"
    [[ -n "$result" ]] || fail "could not find $name under $root"
    printf '%s\n' "$result"
}
find_bundle() {
    local root="$1" name="$2" result
    result="$(find "$root" -type d -name "$name" -print -quit)"
    [[ -n "$result" ]] || fail "could not find $name under $root"
    printf '%s\n' "$result"
}

versioned="Texture-v${version}"
windows_root="$artifact_root/texture-Windows"
macos_root="$artifact_root/texture-macOS"
linux_root="$artifact_root/texture-Linux"
[[ -d "$windows_root" ]] || fail "Windows build artefact is missing"
[[ -d "$macos_root" ]] || fail "macOS build artefact is missing"
[[ -d "$linux_root" ]] || fail "Linux build artefact is missing"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT

# Standalone bundles are self-contained JUCE targets. Keep the .app parent folder
# when archiving so macOS expands it as a normal application bundle.
windows_app="$(find_one "$windows_root" Texture.exe)"
macos_app="$(find_bundle "$macos_root" Texture.app)"
mkdir -p "$stage/windows" "$stage/macos"
cp "$windows_app" "$stage/windows/Texture.exe"
cp LICENSE "$stage/windows/LICENSE.txt"
cat > "$stage/windows/README.txt" <<'TXT'
TEXTURE standalone for 64-bit Windows.
Unzip the archive and run Texture.exe. This beta build is unsigned.
See LICENSE.txt for licence terms.
TXT
(cd "$stage/windows" && zip -q -9 "$out/${versioned}-Windows-x64-Standalone.zip" Texture.exe README.txt LICENSE.txt)

cp -a "$macos_app" "$stage/macos/Texture.app"
cp LICENSE "$stage/macos/LICENSE.txt"
cat > "$stage/macos/README.txt" <<'TXT'
TEXTURE standalone for universal macOS (Apple silicon and Intel).
Move Texture.app to Applications. This beta build is unsigned and not notarized.
See LICENSE.txt for licence terms.
TXT
(cd "$stage/macos" && zip -q -9 -y -r "$out/${versioned}-macOS-universal-Standalone.zip" Texture.app README.txt LICENSE.txt)

# Include host formats from the same tested builds. The standalone deliverables
# remain available even if an optional plug-in bundle is not emitted by a runner.
windows_vst3="$(find_bundle "$windows_root" Texture.vst3)"
macos_vst3="$(find_bundle "$macos_root" Texture.vst3)"
macos_au="$(find_bundle "$macos_root" Texture.component)"
cp -a "$windows_vst3" "$stage/windows/Texture.vst3"
cp -a "$macos_vst3" "$stage/macos/Texture.vst3"
cp -a "$macos_au" "$stage/macos/Texture.component"
(cd "$stage/windows" && zip -q -9 -y -r "$out/${versioned}-Windows-x64-VST3.zip" Texture.vst3 README.txt LICENSE.txt)
(cd "$stage/macos" && zip -q -9 -y -r "$out/${versioned}-macOS-universal-VST3.zip" Texture.vst3 README.txt LICENSE.txt)
(cd "$stage/macos" && zip -q -9 -y -r "$out/${versioned}-macOS-universal-AU.zip" Texture.component README.txt LICENSE.txt)

# Linux: standalone ELF binary (inside Standalone/) and the VST3 bundle.
linux_app="$(find "$linux_root" -type f -name Texture -path '*Standalone*' -print -quit)"
[[ -n "$linux_app" ]] || fail "could not find Linux standalone under $linux_root"
linux_vst3="$(find_bundle "$linux_root" Texture.vst3)"
mkdir -p "$stage/linux"
cp "$linux_app" "$stage/linux/Texture"
chmod +x "$stage/linux/Texture"
cp -a "$linux_vst3" "$stage/linux/Texture.vst3"
cp LICENSE "$stage/linux/LICENSE.txt"
cat > "$stage/linux/README.txt" <<'TXT'
TEXTURE for 64-bit Linux (built on Ubuntu, x86_64).
Standalone: chmod +x Texture && ./Texture
VST3: copy Texture.vst3 to ~/.vst3/
Needs ALSA/JACK, X11, freetype and fontconfig. This beta build is unsigned.
TXT
(cd "$stage/linux" && tar -czf "$out/${versioned}-Linux-x64-Standalone.tar.gz" Texture README.txt LICENSE.txt)
(cd "$stage/linux" && tar -czf "$out/${versioned}-Linux-x64-VST3.tar.gz" Texture.vst3 README.txt LICENSE.txt)

(cd "$out" && sha256sum "${versioned}"-* > SHA256SUMS.txt)
find "$out" -maxdepth 1 -type f -printf '%f %s bytes\n' | sort
