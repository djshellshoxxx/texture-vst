# Architecture and Design Rationale

Source: the "Design notes" in `README.md`, `docs/PRODUCT_SPEC.md` and the code. Items marked *(inferred)*
are read from the code, not written down.

- **Tukey grain window, not Hann.** At MORPH 0 the grains do not overlap, and a pure Hann on
  non-overlapped grains is a 50 Hz tremolo. The taper follows overlap: a 2% fade at MORPH 0, widening to
  Hann as grains stack. See `Grain::windowValue` (`Source/Engine/Grain.cpp`).
- **No parameter writes from the audio thread.** Learned CC values are stored atomically in
  `processBlock` and applied on the message thread through `AsyncUpdater`.
- **Sample swaps are a pointer store.** `SampleLoader` builds an immutable `SampleBuffer` off the
  audio thread; the message thread publishes it and holds its own `shared_ptr`, so the audio thread
  never runs the destructor.
- **Fixed layout, uniform scale.** `MainPanel` is laid out once at 720x480 and the editor scales it;
  the window is locked to 3:2, so there is one layout path.
- **Everything is generated.** Factory samples, presets and the icon come from code in the repo, so
  there is no binary content to licence separately (README).
- **Fixed effects order.** Saturator, chorus, reverb, high shelf, DC blocker, output gain (`EffectsChain`).
- **Offline licensing.** Key is an HMAC-SHA256 of the email; no server (README). *(inferred: the design
  accepts that this is not tamper-proof, which the README states.)*
- **Pinned JUCE with an override.** `cmake/JuceFetch.cmake` pins a tag; `TEXTURE_JUCE_PATH` allows a local
  checkout.
