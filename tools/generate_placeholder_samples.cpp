/*
    Writes the twenty placeholder factory samples into a directory.

    Real curated content does not exist yet, so the build generates it: twenty
    thirty-second detuned major-triad drones spanning A2 to G4, which is enough
    for a fresh clone to produce an audible, musical result with no manual steps
    (18_BUILD_AND_PACKAGING.md).

    The plugin runs the same code on first launch to populate the user's factory
    samples folder, so this tool only exists to give developers the same files in
    the repository. Existing files are never overwritten.

    Usage: generate_placeholder_samples [output directory]
*/

#include "../Source/IO/FactoryContent.h"

#include <cstdio>

int main (int argc, char* argv[])
{
    const juce::File outputDirectory = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::CharPointer_UTF8 (argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("factory_samples");

    const auto written = texture::FactoryContent::generateInto (outputDirectory);

    if (written < 0)
    {
        std::fprintf (stderr, "TEXTURE: could not create %s\n",
                      outputDirectory.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("TEXTURE: %d placeholder sample(s) written to %s\n",
                 written, outputDirectory.getFullPathName().toRawUTF8());

    return 0;
}
