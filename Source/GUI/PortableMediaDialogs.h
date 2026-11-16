#pragma once

#include "../PluginProcessor.h"

#include <functional>
#include <memory>

namespace texture
{
    /** File-menu flows for sound bundles and missing samples (F05).

        Owned by the editor. Every change goes through the processor on the message
        thread. Asynchronous callbacks check a SafePointer to the editor before touching
        this object, so a dialog that outlives its window does nothing.
    */
    class PortableMediaDialogs final
    {
    public:
        PortableMediaDialogs (juce::Component& owner, TextureAudioProcessor& processor);

        /** Caller has already checked the Pro tier. */
        void exportBundle();

        void importBundle();

        /** Offers Locate file, Search folder or Keep missing for one layer. */
        void offerRelink (int layerIndex, const juce::String& expectedSha256, const juce::String& fileName);

    private:
        using Anchor = juce::Component::SafePointer<juce::Component>;

        void launchChooser (std::unique_ptr<juce::FileChooser> newChooser, int flags,
                            std::function<void (const juce::File&)> onPicked);

        void relinkWithConfirmation (int layerIndex, const juce::File& file, const juce::String& expectedSha256);
        void searchFolder (int layerIndex, const juce::String& expectedSha256, const juce::String& fileName,
                           const juce::File& folder);

        Anchor anchor;
        TextureAudioProcessor& processor;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
