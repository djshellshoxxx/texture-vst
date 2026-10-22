#include "PortableMediaDialogs.h"

#include "../IO/BundleArchive.h"
#include "../IO/MediaRelinker.h"
#include "../IO/MediaStore.h"

#include <juce_cryptography/juce_cryptography.h>

namespace texture
{
    namespace
    {
        // AlertWindow reports a press as (index + 1) % buttons: the first of two buttons
        // returns 1, the last returns 0; of three, the first returns 1, the second 2.
        constexpr int firstOfTwoButtons = 1;
        constexpr int firstOfThreeButtons = 1;
        constexpr int secondOfThreeButtons = 2;

        void confirmThen (const juce::String& title, const juce::String& message,
                          const juce::String& acceptText, std::function<void()> onAccept)
        {
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle (title)
                    .withMessage (message)
                    .withButton (acceptText)
                    .withButton ("Cancel"),
                [accepted = std::move (onAccept)] (int choice)
                {
                    if (choice == firstOfTwoButtons && accepted)
                        accepted();
                });
        }
    }

    PortableMediaDialogs::PortableMediaDialogs (juce::Component& owner, TextureAudioProcessor& audioProcessor)
        : anchor (&owner),
          processor (audioProcessor)
    {
    }

    void PortableMediaDialogs::launchChooser (std::unique_ptr<juce::FileChooser> newChooser, int flags,
                                              std::function<void (const juce::File&)> onPicked)
    {
        // The previous chooser is replaced here, never from its own callback.
        chooser = std::move (newChooser);

        chooser->launchAsync (flags, [alive = anchor, callback = std::move (onPicked)] (const juce::FileChooser& picked)
        {
            if (alive == nullptr)
                return;

            const auto result = picked.getResult();

            if (result != juce::File())
                callback (result);
        });
    }

    void PortableMediaDialogs::exportBundle()
    {
        if (processor.getPortableMediaWorker().isBusy())
        {
            processor.postToast ("Another bundle job is still running.", SampleLoader::Severity::warning);
            return;
        }

        if (processor.getSampleForLayer (0) == nullptr && processor.getSampleForLayer (1) == nullptr)
        {
            processor.postToast ("Load a sample before exporting a bundle.", SampleLoader::Severity::warning);
            return;
        }

        const auto suggested = juce::File::createLegalFileName (processor.getCurrentPresetName()) + BundleFormat::extension;

        launchChooser (std::make_unique<juce::FileChooser> (
                           "Export sound bundle",
                           juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (suggested),
                           juce::String ("*") + BundleFormat::extension),
                       juce::FileBrowserComponent::saveMode
                           | juce::FileBrowserComponent::canSelectFiles
                           | juce::FileBrowserComponent::warnAboutOverwriting,
                       [this] (const juce::File& picked)
        {
            auto destination = picked;

            if (! destination.hasFileExtension (BundleFormat::extension))
                destination = destination.withFileExtension (BundleFormat::extension);

            // Captured on the message thread: later edits cannot change the bundle.
            const auto source = processor.captureBundleSource (processor.getCurrentPresetName(),
                                                               processor.getCurrentPresetCategory(), {});

            const auto started = processor.getPortableMediaWorker().startExport (
                destination, processor.getPortableStagingDirectory(), source.preset, source.files,
                [alive = anchor, &audioProcessor = processor, destination] (const BundleArchive::WriteResult& result)
                {
                    if (alive == nullptr)
                        return;

                    if (result.status != BundleArchive::Status::ok)
                    {
                        audioProcessor.postToast (result.message.isNotEmpty() ? result.message
                                                                              : BundleArchive::describe (result.status),
                                                  SampleLoader::Severity::error);
                        return;
                    }

                    audioProcessor.postToast ("Exported " + destination.getFileName() + " with "
                                                  + juce::String (static_cast<int> (result.manifest.media.size()))
                                                  + " sample(s).",
                                              SampleLoader::Severity::info);
                });

            if (! started)
                processor.postToast ("Another bundle job is still running.", SampleLoader::Severity::warning);
        });
    }

    void PortableMediaDialogs::importBundle()
    {
        if (processor.getPortableMediaWorker().isBusy())
        {
            processor.postToast ("Another bundle job is still running.", SampleLoader::Severity::warning);
            return;
        }

        launchChooser (std::make_unique<juce::FileChooser> (
                           "Import sound bundle",
                           juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                           juce::String ("*") + BundleFormat::extension),
                       juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                       [this] (const juce::File& archive)
        {
            const auto started = processor.getPortableMediaWorker().startImport (
                archive, processor.getPortableStagingDirectory(), processor.getPresetManager(),
                processor.getMediaStore(), PresetManager::getUserPresetDirectory(),
                [alive = anchor, &audioProcessor = processor] (const BundleImportPlanner::CommitResult& result)
                {
                    // Nothing is applied unless the whole bundle validated and committed.
                    if (alive == nullptr)
                        return;

                    if (! result.succeeded)
                    {
                        audioProcessor.postToast (result.message.isNotEmpty() ? result.message
                                                                              : "The bundle could not be imported.",
                                                  SampleLoader::Severity::error);
                        return;
                    }

                    audioProcessor.getPresetManager().refresh();

                    if (audioProcessor.loadPreset (result.presetFile))
                        audioProcessor.postToast ("Imported " + result.presetFile.getFileNameWithoutExtension(),
                                                  SampleLoader::Severity::info);
                });

            if (! started)
                processor.postToast ("Another bundle job is still running.", SampleLoader::Severity::warning);
        });
    }

    void PortableMediaDialogs::offerRelink (int layerIndex, const juce::String& expectedSha256, const juce::String& fileName)
    {
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Sample missing")
                .withMessage ("Layer " + juce::String (layerIndex + 1) + " could not find \""
                              + fileName + "\". Locate a replacement, search a folder you choose, "
                              "or keep the layer empty.")
                .withButton ("Locate file...")
                .withButton ("Search folder...")
                .withButton ("Keep missing"),
            [this, alive = anchor, layerIndex, expectedSha256, fileName] (int choice)
            {
                if (alive == nullptr)
                    return;

                if (choice == firstOfThreeButtons)
                {
                    launchChooser (std::make_unique<juce::FileChooser> (
                                       "Locate missing sample",
                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                       processor.getSampleLoader().getSupportedWildcard()),
                                   juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                   [this, layerIndex, expectedSha256] (const juce::File& file)
                                   {
                                       relinkWithConfirmation (layerIndex, file, expectedSha256);
                                   });
                }
                else if (choice == secondOfThreeButtons)
                {
                    launchChooser (std::make_unique<juce::FileChooser> (
                                       "Search this folder for the sample",
                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory)),
                                   juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                   [this, layerIndex, expectedSha256, fileName] (const juce::File& folder)
                                   {
                                       searchFolder (layerIndex, expectedSha256, fileName, folder);
                                   });
                }
            });
    }

    void PortableMediaDialogs::relinkWithConfirmation (int layerIndex, const juce::File& file,
                                                       const juce::String& expectedSha256)
    {
        if (expectedSha256.isNotEmpty() && juce::SHA256 (file).toHexString() != expectedSha256)
        {
            confirmThen ("Different audio",
                         file.getFileName() + " is not the file this preset was saved with: its audio "
                         "differs from the original. Use it anyway?",
                         "Use it",
                         [&audioProcessor = processor, layerIndex, file] { audioProcessor.relinkLayer (layerIndex, file); });
            return;
        }

        processor.relinkLayer (layerIndex, file);
    }

    void PortableMediaDialogs::searchFolder (int layerIndex, const juce::String& expectedSha256,
                                             const juce::String& fileName, const juce::File& folder)
    {
        MediaRelinker::Expectation expectation;
        expectation.sha256 = expectedSha256;
        expectation.fileName = fileName;

        const auto started = processor.getPortableMediaWorker().startSearch (
            folder, expectation,
            [alive = anchor, &audioProcessor = processor, layerIndex] (const MediaRelinker::Result& result)
            {
                if (alive == nullptr)
                    return;

                if (result.candidates.empty())
                {
                    audioProcessor.postToast ("No matching sample was found in that folder.",
                                              SampleLoader::Severity::warning);
                    return;
                }

                const auto& best = result.candidates.front();

                if (best.kind == MediaRelinker::MatchKind::content)
                {
                    audioProcessor.relinkLayer (layerIndex, best.file);
                    return;
                }

                // A name-only match is not proof of identity, so the user must confirm it.
                confirmThen ("Unverified file",
                             best.file.getFullPathName() + " has the same name, but its audio is not "
                             "verified against the original. Use it?",
                             "Use it",
                             [&audioProcessor, layerIndex, file = best.file] { audioProcessor.relinkLayer (layerIndex, file); });
            });

        if (! started)
            processor.postToast ("Another bundle job is still running.", SampleLoader::Severity::warning);
    }
}
