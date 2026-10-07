#!/usr/bin/env bash
# Packages built TEXTURE binaries into release assets.
#
#   packaging/package_release.sh <version> <out-dir> [windows-build-dir] [linux-build-dir]
#
# Each build dir is a CMake build with TEXTURE_BUILD_CLAP=ON (its Texture_artefacts/Release
# holds Standalone/, VST3/ and CLAP/). Either platform may be omitted. Needs zip, tar and,
# for the Windows installer, makensis (NSIS 3).
set -euo pipefail

version="$1"; out="$(realpath -m "$2")"
win="${3:+$(realpath "$3")}"; lin="${4:+$(realpath "$4")}"
root="$(cd "$(dirname "$0")/.." && pwd)"
name="Texture-v${version}"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
mkdir -p "$out"

readme() {   # $1 = platform label, $2 = install notes
    cat <<TXT
TEXTURE ${version} (${1})
Drop a sample. Play a chord. It sounds finished.

${2}

Press F1 inside TEXTURE for the full manual.
Licence: see LICENSE.txt. Copyright (c) 2026 Sheldon Davidson.
TXT
}

if [[ -n "$win" ]]; then
    art="$win/Texture_artefacts/Release"
    s="$work/win"; mkdir -p "$s"
    cp "$art/Standalone/Texture.exe" "$s/"
    cp -r "$art/VST3/Texture.vst3" "$s/"
    cp "$art/CLAP/Texture.clap" "$s/"
    rm -f "$s/Texture.vst3/desktop.ini"
    readme "Windows x64" "Installer: run the Setup .exe and pick the formats you want.
Portable: unzip anywhere and run Texture.exe - nothing is installed.
VST3: copy Texture.vst3 into C:\\Program Files\\Common Files\\VST3
CLAP: copy Texture.clap into C:\\Program Files\\Common Files\\CLAP
Requires 64-bit Windows 10 or later." | sed 's/$/\r/' > "$s/README.txt"
    sed 's/$/\r/' "$root/LICENSE" > "$s/LICENSE.txt"

    (cd "$s" && zip -qr9 "$out/${name}-Windows-x64-Portable.zip" Texture.exe README.txt LICENSE.txt)
    (cd "$s" && zip -qr9 "$out/${name}-Windows-x64-VST3.zip" Texture.vst3 README.txt LICENSE.txt)
    (cd "$s" && zip -qr9 "$out/${name}-Windows-x64-CLAP.zip" Texture.clap README.txt LICENSE.txt)
    makensis -V2 -DVERSION="$version" -DSTAGE="$s" \
             -DICON="$win/Texture_artefacts/JuceLibraryCode/icon.ico" \
             -DOUTFILE="$out/${name}-Windows-x64-Setup.exe" \
             "$root/packaging/windows/TextureInstaller.nsi"
fi

if [[ -n "$lin" ]]; then
    art="$lin/Texture_artefacts/Release"
    s="$work/lin/${name}-Linux-x64"; mkdir -p "$s"
    cp "$art/Standalone/Texture" "$s/"
    cp -r "$art/VST3/Texture.vst3" "$s/"
    cp "$art/CLAP/Texture.clap" "$s/"
    cp "$root/LICENSE" "$s/LICENSE.txt"
    cp "$root/Resources/Images/logo.png" "$s/texture.png"
    readme "Linux x64" "Installer: run  sh ${name}-Linux-x64-Installer.sh  (per-user, no root needed).
It installs the app to ~/.local/bin, VST3 to ~/.vst3, CLAP to ~/.clap and a menu entry.
Portable: untar anywhere and run ./Texture.
VST3 / CLAP: copy Texture.vst3 to ~/.vst3 and Texture.clap to ~/.clap.
Needs ALSA or JACK, X11 and freetype (present on any desktop distribution)." > "$s/README.txt"
    cat > "$s/install.sh" <<'SH'
#!/bin/sh
# Per-user install of TEXTURE. Re-run to upgrade; remove the listed paths to uninstall.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/.local/bin" "$HOME/.vst3" "$HOME/.clap" "$HOME/.local/share/applications" "$HOME/.local/share/icons"
install -m 755 "$here/Texture" "$HOME/.local/bin/Texture"
rm -rf "$HOME/.vst3/Texture.vst3" && cp -r "$here/Texture.vst3" "$HOME/.vst3/"
install -m 755 "$here/Texture.clap" "$HOME/.clap/Texture.clap"
install -m 644 "$here/texture.png" "$HOME/.local/share/icons/texture.png"
cat > "$HOME/.local/share/applications/texture.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=TEXTURE
Comment=Granular pad synthesizer
Exec=$HOME/.local/bin/Texture
Icon=$HOME/.local/share/icons/texture.png
Categories=AudioVideo;Audio;Music;
DESKTOP
echo "TEXTURE installed:"
echo "  app   $HOME/.local/bin/Texture"
echo "  VST3  $HOME/.vst3/Texture.vst3"
echo "  CLAP  $HOME/.clap/Texture.clap"
SH
    chmod +x "$s/install.sh"

    (cd "$work/lin" && tar -czf "$out/${name}-Linux-x64-Portable.tar.gz" \
        "${name}-Linux-x64/Texture" "${name}-Linux-x64/README.txt" "${name}-Linux-x64/LICENSE.txt")
    (cd "$work/lin" && tar -czf "$out/${name}-Linux-x64-VST3.tar.gz" \
        "${name}-Linux-x64/Texture.vst3" "${name}-Linux-x64/README.txt" "${name}-Linux-x64/LICENSE.txt")
    (cd "$work/lin" && tar -czf "$out/${name}-Linux-x64-CLAP.tar.gz" \
        "${name}-Linux-x64/Texture.clap" "${name}-Linux-x64/README.txt" "${name}-Linux-x64/LICENSE.txt")

    # Self-extracting installer: a shell header followed by the whole tarball.
    (cd "$work/lin" && tar -czf "$work/payload.tar.gz" "${name}-Linux-x64")
    {
        cat <<SH
#!/bin/sh
# TEXTURE ${version} installer for Linux x64. Usage: sh \$0
set -e
tmp="\$(mktemp -d)"; trap 'rm -rf "\$tmp"' EXIT
line=\$(awk '/^__PAYLOAD__\$/ { print NR + 1; exit }' "\$0")
tail -n +"\$line" "\$0" | tar -xz -C "\$tmp"
sh "\$tmp/${name}-Linux-x64/install.sh"
exit 0
__PAYLOAD__
SH
        cat "$work/payload.tar.gz"
    } > "$out/${name}-Linux-x64-Installer.sh"
    chmod +x "$out/${name}-Linux-x64-Installer.sh"
fi

(cd "$out" && sha256sum Texture-* > SHA256SUMS.txt)
ls -la "$out"
