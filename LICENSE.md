# Licences

TEXTURE is made of three things with three different licences: the plugin code,
the factory content, and the third-party libraries it is built on.

---

## 1. Plugin code — MIT

Copyright (c) 2026 TextureAudio

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## 2. Factory content — proprietary

*Placeholder terms. Replace before release.*

The factory samples (`Pad_01.wav` .. `Pad_20.wav`, the Pro expansion library, and
the bundled `.texture` presets) are **not** covered by the MIT licence above.

You may:

- use them in your own musical works, commercial or otherwise, without royalties
  or attribution;
- render, process, and distribute those works.

You may not:

- redistribute the samples as samples, whether on their own, repackaged, or as
  part of another sample library or instrument;
- train a machine learning model on them;
- resell or sublicense them.

The v1 factory samples are generated procedurally by the plugin itself, so they
are original by construction and contain no third-party material. The Pro library
is either MSR-cleared or procedurally generated. No copyrighted material of any
kind ships with TEXTURE.

---

## 3. Third-party components

### JUCE

TEXTURE is built with the [JUCE](https://juce.com) framework, used under its own
licence terms. JUCE is not redistributed as source in this repository; the build
fetches a pinned tag. Shipping a binary built with JUCE requires a JUCE licence
appropriate to your situation — see <https://juce.com/get-juce>.

### Inter

The interface typeface is [Inter](https://rsms.me/inter/) by Rasmus Andersson,
licensed under the SIL Open Font License 1.1. The full licence text ships beside
the font files in `Resources/Fonts/OFL.txt`.

If the font files are absent at build time the plugin falls back to the platform
sans-serif and logs a warning; nothing breaks.

### clap-juce-extensions (optional)

The CLAP build, enabled with `-DTEXTURE_BUILD_CLAP=ON`, fetches
[clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions), which
is MIT licensed. It is not required for the VST3, AU or Standalone builds.

### VST, AU, AAX

VST is a trademark of Steinberg Media Technologies GmbH. Audio Units is a
trademark of Apple Inc. AAX is a trademark of Avid Technology, Inc. Building the
AAX target additionally requires Avid's AAX SDK, which is not included here and
carries its own terms.

---

## 4. Privacy

TEXTURE contains no telemetry, no analytics, no crash reporting and no update
checks. Licence validation is performed entirely offline. The plugin makes no
network connections of any kind at runtime. The only files it writes are inside
your own user data folder: presets, settings, and logs you asked for.
