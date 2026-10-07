#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace texture
{
    /** Offline Pro activation.

        A key is the base32 encoding of the first `keyBytes` bytes of
        HMAC-SHA256(normalised email, secret), formatted in dash-separated groups.
        There is no online check and no obfuscation: the check exists to keep honest
        users honest (21_WHAT_NOT_TO_DO.md, items 19 and 20).
    */
    class LicenseManager
    {
    public:
        enum class Result
        {
            activated,
            emptyFields,
            malformedKey,
            signatureMismatch
        };

        /** Bytes of the HMAC kept in a key; 10 bytes gives a 16-character key. */
        static constexpr int keyBytes = 10;

        /** Characters per dash-separated group in the printed key. */
        static constexpr int groupSize = 4;

        /** Longest input we will even look at, per 21_WHAT_NOT_TO_DO.md item 34. */
        static constexpr int maxFieldLength = 256;

        /** @param properties  persistent store; may be nullptr in tests, in which
                               case activation lives only for this object's lifetime.
        */
        explicit LicenseManager (juce::ApplicationProperties* properties);

        /** Re-reads persisted activation state. Called on construction. */
        void refreshFromStorage();

        /** Validates and, on success, persists the pair synchronously. */
        Result activate (const juce::String& email, const juce::String& key);

        /** Re-runs the check against the stored pair ("Restore" button). */
        Result restore();

        /** Clears activation and the stored pair. */
        void deactivate();

        bool isPro() const noexcept { return proActivated; }

        juce::String getStoredEmail() const { return storedEmail; }

        /** Stored key with everything but the final four characters masked. */
        juce::String getMaskedKey() const;

        /** Raw stored key. Debug panel only. */
        juce::String getStoredKey() const { return storedKey; }

        /** Human-readable message for a result, matching 16_ERROR_HANDLING.md. */
        static juce::String describe (Result result);

       #if defined (TEXTURE_ENABLE_LICENSE_ISSUER) && TEXTURE_ENABLE_LICENSE_ISSUER
        /** Development/test-only key issuer. Never compiled into production plugin targets. */
        static juce::String generateKey (const juce::String& email);
       #endif

        /** Trim, lower-case, cap length. */
        static juce::String normaliseEmail (const juce::String& email);

        /** Strip dashes and whitespace, upper-case, cap length. */
        static juce::String normaliseKey (const juce::String& key);

        /** True if the string is the right length and alphabet to be a key at all. */
        static bool isWellFormedKey (const juce::String& normalisedKey);

    private:
        juce::PropertiesFile* getStore() const;
        void persist();
        static juce::String generateKeyInternal (const juce::String& email);

        juce::ApplicationProperties* applicationProperties { nullptr };
        juce::String storedEmail;
        juce::String storedKey;
        bool proActivated { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
    };
}
