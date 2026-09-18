#include "TestHelpers.h"

#include "../Source/License/LicenseManager.h"
#include "../Source/Utility/MidiLearn.h"
#include "../Source/Utility/ParameterIDs.h"

#include <memory>

namespace texture::tests
{
    namespace
    {
        /** An ApplicationProperties pointed at a throwaway folder, so tests never
            touch the real user settings.
        */
        class ScopedProperties
        {
        public:
            explicit ScopedProperties (const juce::File& folder)
            {
                juce::PropertiesFile::Options options;
                options.applicationName = "TextureTests";
                options.filenameSuffix = ".settings";
                options.folderName = folder.getFullPathName();
                options.storageFormat = juce::PropertiesFile::storeAsXML;
                options.commonToAllUsers = false;

                properties.setStorageParameters (options);
            }

            ~ScopedProperties()
            {
                if (auto* store = properties.getUserSettings())
                    store->saveIfNeeded();
            }

            juce::ApplicationProperties& get() noexcept { return properties; }

        private:
            juce::ApplicationProperties properties;
        };
    }

    class LicenseManagerTests : public juce::UnitTest
    {
    public:
        LicenseManagerTests()
            : juce::UnitTest ("Licence manager", "texture") {}

        void runTest() override
        {
            testValidPairActivates();
            testInvalidKeyRejected();
            testEmptyFieldsRejected();
            testInputNormalisation();
            testPersistenceAcrossRecreation();
            testDeactivateClearsState();
            testKeyFormat();
            testMasking();
        }

    private:
        void testValidPairActivates()
        {
            beginTest ("a valid email and key pair activates Pro");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());
            LicenseManager license (&properties.get());

            expect (! license.isPro(), "a fresh manager must start on the free tier");

            const juce::String email = "buyer@example.com";
            const auto key = LicenseManager::generateKey (email);

            expect (key.isNotEmpty());
            expectEquals (license.activate (email, key), LicenseManager::Result::activated);
            expect (license.isPro());
            expectEquals (license.getStoredEmail(), email);
        }

        void testInvalidKeyRejected()
        {
            beginTest ("a key issued for a different email does not activate");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());
            LicenseManager license (&properties.get());

            const auto otherKey = LicenseManager::generateKey ("someone.else@example.com");

            expectEquals (license.activate ("buyer@example.com", otherKey),
                          LicenseManager::Result::signatureMismatch);
            expect (! license.isPro());

            // Right shape, wrong signature.
            const auto wrongButWellFormed = juce::String ("AAAA-BBBB-CCCC-DDDD");
            expectEquals (license.activate ("buyer@example.com", wrongButWellFormed),
                          LicenseManager::Result::signatureMismatch);
            expect (! license.isPro());

            // Wrong shape entirely.
            expectEquals (license.activate ("buyer@example.com", "not-a-key"),
                          LicenseManager::Result::malformedKey);

            // Characters outside the base32 alphabet, correct length.
            expectEquals (license.activate ("buyer@example.com", "0000-0000-0000-0001"),
                          LicenseManager::Result::malformedKey);

            expect (! license.isPro(), "no failed attempt may leave the plugin unlocked");
        }

        void testEmptyFieldsRejected()
        {
            beginTest ("empty fields are reported, not treated as a mismatch");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());
            LicenseManager license (&properties.get());

            expectEquals (license.activate ("", ""), LicenseManager::Result::emptyFields);
            expectEquals (license.activate ("buyer@example.com", ""), LicenseManager::Result::emptyFields);
            expectEquals (license.activate ("", "AAAA-BBBB-CCCC-DDDD"), LicenseManager::Result::emptyFields);
            expectEquals (license.activate ("   ", "   "), LicenseManager::Result::emptyFields);

            expect (! license.isPro());
        }

        void testInputNormalisation()
        {
            beginTest ("whitespace, case and dashes are all forgiven");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());
            LicenseManager license (&properties.get());

            const juce::String email = "Buyer@Example.COM";
            const auto key = LicenseManager::generateKey (email);

            // Same key, typed back in every way a user might realistically type it.
            const auto withoutDashes = key.removeCharacters ("-");
            const auto lowerCase = key.toLowerCase();
            const auto padded = "   " + key + "  ";

            expectEquals (license.activate ("  buyer@example.com  ", withoutDashes),
                          LicenseManager::Result::activated);

            license.deactivate();
            expectEquals (license.activate ("BUYER@EXAMPLE.COM", lowerCase),
                          LicenseManager::Result::activated);

            license.deactivate();
            expectEquals (license.activate (email, padded), LicenseManager::Result::activated);

            // An absurdly long field must be capped, not crash.
            const auto huge = juce::String::repeatedString ("x", 100000);
            expectEquals (license.activate (huge, huge), LicenseManager::Result::malformedKey);
        }

        void testPersistenceAcrossRecreation()
        {
            beginTest ("activation survives destroying and recreating the manager");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());

            const juce::String email = "persist@example.com";
            const auto key = LicenseManager::generateKey (email);

            {
                LicenseManager first (&properties.get());
                expectEquals (first.activate (email, key), LicenseManager::Result::activated);
                expect (first.isPro());
            }

            if (auto* store = properties.get().getUserSettings())
                store->saveIfNeeded();

            {
                LicenseManager second (&properties.get());
                expect (second.isPro(), "the stored activation should be recognised");
                expectEquals (second.getStoredEmail(), email);
                expectEquals (second.restore(), LicenseManager::Result::activated);
            }
        }

        void testDeactivateClearsState()
        {
            beginTest ("deactivate clears the stored pair for good");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());

            const juce::String email = "gone@example.com";
            const auto key = LicenseManager::generateKey (email);

            LicenseManager license (&properties.get());
            expectEquals (license.activate (email, key), LicenseManager::Result::activated);

            license.deactivate();

            expect (! license.isPro());
            expect (license.getStoredEmail().isEmpty());
            expectEquals (license.restore(), LicenseManager::Result::emptyFields);

            LicenseManager recreated (&properties.get());
            expect (! recreated.isPro(), "the cleared state must persist too");
        }

        void testKeyFormat()
        {
            beginTest ("keys are stable, well formed and email-specific");

            const juce::String email = "shape@example.com";
            const auto key = LicenseManager::generateKey (email);

            // 10 bytes of HMAC in base32 is 16 characters, printed in 4 groups.
            expectEquals (key.length(), 19, "expected XXXX-XXXX-XXXX-XXXX");
            expectEquals (key.removeCharacters ("-").length(), 16);
            expect (LicenseManager::isWellFormedKey (LicenseManager::normaliseKey (key)));

            // Deterministic.
            expectEquals (LicenseManager::generateKey (email), key);

            // Case-insensitive on the email side.
            expectEquals (LicenseManager::generateKey ("SHAPE@EXAMPLE.COM"), key);

            // Different emails must not collide.
            expect (LicenseManager::generateKey ("other@example.com") != key);

            // An empty email yields no key at all.
            expect (LicenseManager::generateKey ("").isEmpty());
        }

        void testMasking()
        {
            beginTest ("the debug view only ever shows the last four characters");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());
            LicenseManager license (&properties.get());

            expectEquals (license.getMaskedKey(), juce::String ("(none)"));

            const juce::String email = "mask@example.com";
            const auto key = LicenseManager::generateKey (email);
            expectEquals (license.activate (email, key), LicenseManager::Result::activated);

            const auto masked = license.getMaskedKey();
            const auto stored = license.getStoredKey();

            expectEquals (masked.length(), stored.length());
            expectEquals (masked.getLastCharacters (4), stored.getLastCharacters (4));
            expect (masked.startsWith ("*"), "everything but the tail must be hidden");
            expect (! masked.dropLastCharacters (4).containsAnyOf (stored.dropLastCharacters (4)));
        }
    };

    static LicenseManagerTests licenseManagerTests;

    // -------------------------------------------------------------------------

    class MidiLearnTests : public juce::UnitTest
    {
    public:
        MidiLearnTests()
            : juce::UnitTest ("MIDI learn", "texture") {}

        void runTest() override
        {
            beginTest ("learns, reports and clears a mapping");

            TemporaryDirectory temp;
            ScopedProperties properties (temp.get());
            MidiLearn learn (&properties.get());

            expectEquals (learn.getControllerForParameter (params::morph), -1);

            learn.startLearning (params::morph);
            expect (learn.isLearning());
            expectEquals (learn.getLearningParameterID(), juce::String (params::morph));

            learn.requestBinding (21);
            expect (learn.applyPendingBinding());
            expect (! learn.isLearning());

            expectEquals (learn.getControllerForParameter (params::morph), 21);
            expectEquals (learn.getParameterForController (21), juce::String (params::morph));
            expectGreaterThan (learn.getMappedSlotForController (21), -1);

            beginTest ("one CC per parameter");

            learn.setMapping (22, params::morph);
            expectEquals (learn.getControllerForParameter (params::morph), 22);
            expectEquals (learn.getParameterForController (21), juce::String(),
                          "the old binding must be dropped");

            beginTest ("reserved controllers are never mapped");

            for (const auto reserved : { 64, 120, 123 })
            {
                expect (MidiLearn::isReservedController (reserved));
                learn.setMapping (reserved, params::gainDb);
                expectEquals (learn.getParameterForController (reserved), juce::String());
            }

            beginTest ("mappings persist and can be cleared");

            if (auto* store = properties.get().getUserSettings())
                store->saveIfNeeded();

            {
                MidiLearn reloaded (&properties.get());
                expectEquals (reloaded.getControllerForParameter (params::morph), 22);
            }

            learn.clearAll();
            expectEquals (learn.getControllerForParameter (params::morph), -1);
            expectEquals (learn.describeMappings(), juce::String ("(none)"));

            beginTest ("cancelling a learn binds nothing");

            learn.startLearning (params::gainDb);
            learn.cancelLearning();
            learn.requestBinding (30);

            expect (! learn.applyPendingBinding());
            expectEquals (learn.getControllerForParameter (params::gainDb), -1);
        }
    };

    static MidiLearnTests midiLearnTests;
}
