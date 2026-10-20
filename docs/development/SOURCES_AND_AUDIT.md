# Sources, audit and design provenance

Reviewed 2026-10-20 against master 38d9aec4556fc3b6594fe53888ea15d36698e43e. Recursive tree was complete (not truncated); no root or nested AGENTS.md/CLAUDE.md was present. Existing docs were reviewed for duplicate scope. No detailed per-feature spec for F01–F26 existed. Partial foundations are recorded in each feature's Existing coverage and gap section.

## Repository evidence
- [Product contract](../PRODUCT_SPEC.md): current instrument, MIDI, state and offline export behaviour.
- [State contract](../STATE_COMPATIBILITY_SPEC.md): XML host root and migration policy.
- [Processor](../../Source/PluginProcessor.cpp): Randomize, parameter/state coordination, render settings, MIDI routing and sample lifetime.
- [MainPanel](../../Source/GUI/MainPanel.cpp): existing controls, keyboard actions and synchronous export UI.
- [Parameter registry](../../Source/Utility/ParameterIDs.h): existing IDs/ranges and MORPH mapping.
- [Voice](../../Source/Engine/Voice.cpp) and [engine](../../Source/Engine/GranularEngine.cpp): fixed source note, PRNG initialization, bounded grains and note handling.
- [MORPH LFO](../../Source/Modulation/MorphLFO.cpp): sine and transport behaviour.
- [Effects chain](../../Source/Effects/EffectsChain.cpp): legacy routing and effect targets.
- [Preset model](../../Source/Presets/PresetManager.h) and [browser](../../Source/Presets/PresetBrowser.cpp): schema 2, 8 KiB preset limit, media paths, search/tags/favourites.
- [Factory content](../../Source/IO/FactoryContent.cpp): generated starter media.
- [WAV exporter](../../Source/IO/WavExporter.cpp): fixed event schedule and file writing.
- [GUI gap specification](../GUI_GAP_SPEC.md) and [progress](../../PROGRESS.md): prior GUI pass is recorded complete.
- [Prior fix PR #4](https://github.com/djshellshoxxx/texture-vst/pull/4): waveform/tuning fixes; these specs do not repeat them.

## Primary technical references
Consulted 2026-10-20. Documentation at master is reference material; check API signatures against pinned JUCE 8.0.8 before implementation.
- [JUCE AbstractFifo](https://docs.juce.com/master/classjuce_1_1AbstractFifo.html): the primitive manages positions for a single-reader/single-writer queue; storage and ownership are application responsibilities. It is not evidence that arbitrary producer sharing is safe.
- [JUCE MPEInstrument](https://docs.juce.com/master/classjuce_1_1MPEInstrument.html) and [MPE tutorial](https://juce.com/tutorials/tutorial_mpe_introduction/): candidates for adapting MPE events; TEXTURE-specific note ownership, capability and state rules are proposed here.
- [Julius O. Smith III, Introduction to Digital Filters, Complex Resonator](https://www.dsprelated.com/freebooks/filters/Complex_Resonator.html): background for F26’s complex first-order recurrence. The material ratios, control mapping, weights, routing and acceptance budgets in F26 are design proposals.
- [First-order complex resonator banks](https://www.dsprelated.com/freebooks/filters/First_Order_Complex_Resonators.html): background for parallel mode structure.

The CCRMA Modal_Synthesis page could not be retrieved; the Stanford Resonarium PDF exceeded retrieval limits. Neither is used as evidence for a performance or novelty claim. No research source validates the proposed CPU budgets or musical quality; those are implementation gates.

## Competitive context from the preceding review
- [Granulator III](https://roberthenke.com/technology/granulator3.html): existing capture, modulation and MPE demonstrate these are established capabilities.
- [Novum](https://www.tracktion.com/products/novum): decomposition, cross-synthesis and component locking mean these broad concepts should not be marketed as first-ever inventions.
- [Efx Fragments](https://www.arturia.com/products/software-effects/efx-fragments/overview): freeze and rhythmic/pitch manipulation are established granular workflows.

F25 and F26 are product-specific designs, not patentability assessments. Feature usefulness and popularity are hypotheses; user testing and listening evidence are still required.
