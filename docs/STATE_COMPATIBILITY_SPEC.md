# TEXTURE — Host State Compatibility Specification

**Status:** Required product specification  
**Current state major version:** 1

## Purpose

Host project recall must either apply a structurally valid TEXTURE state or leave the
current instance untouched. A malformed or unsupported state must never be partially
applied.

## Serialized root

The binary host state contains XML converted from a JUCE ValueTree rooted at
`TexturePluginState`.

Required root data:

- `pluginVersion`: semantic-style version text. Missing version is treated as a
  legacy version-1 state for backward compatibility.
- exactly one `Parameters` wrapper containing exactly one APVTS state tree.

Optional product state:

- zero or one `Sample` record for each layer index 0 and 1;
- one `Preset` metadata record;
- one `Grit` hidden-effect record.

## Validation before mutation

The complete root structure is validated before APVTS parameters or any other
product state are changed.

The loader MUST reject the entire state when:

1. the binary blob cannot be decoded as XML;
2. the root tree is invalid or has the wrong type;
3. the version identifier is malformed;
4. the state was written by a future major version;
5. the required parameter wrapper or its contained state is missing;
6. a sample record uses a layer index outside 0..1;
7. more than one sample record targets the same layer.

Rejection leaves the current parameters, loaded samples, preset metadata, GRIT
settings and sample start positions unchanged.

## Version policy

Version 1.x states are compatible with the version-1 reader. A missing version is
accepted as legacy version 1. A state from major version 2 or newer is rejected until
an explicit migration path exists.

Minor/patch version differences within major version 1 may add optional state fields.
Unknown optional fields are ignored.

## Sample restore

Sample references may be absent. When present, the loader restores the saved start
position and queues file loading outside the audio callback. If an absolute path is
missing, TEXTURE attempts its normal preset/sample path resolution fallback. A missing
audio file does not invalidate otherwise valid state; parameters still restore and the
user receives a replacement prompt.

## Non-parameter state

Preset metadata and GRIT state are optional for version-1 compatibility. GRIT numeric
values are clamped by the existing effect setters. Optional records must not be
required merely to open older version-1 projects.

## Tests

`tests/test_state_schema.cpp` covers accepted version-1/legacy state, wrong roots,
malformed versions, future major versions, missing parameter state, invalid sample
indices and duplicate sample-layer records.
