#include "LicenseManager.h"

#include <juce_cryptography/juce_cryptography.h>

#include <cstring>

namespace texture
{
    namespace
    {
        /** PLACEHOLDER SECRET.

            !!! REPLACE THIS BEFORE RELEASE !!!
            Every build that ships with this constant produces the same keys as every
            other build, so any key generated against it works everywhere. Swap it for
            a private per-release value and regenerate the customer key database.
        */
        constexpr const char* PRO_LICENSE_SECRET = "TEXTURE-PLACEHOLDER-SECRET-REPLACE-ME";

        constexpr const char* storageKeyName = "pro_license";
        constexpr const char* base32Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        constexpr int sha256BlockSize = 64;

        juce::MemoryBlock sha256Of (const void* data, size_t size)
        {
            juce::SHA256 hash (data, size);
            return hash.getRawData();
        }

        /** HMAC-SHA256 built on juce::SHA256, since JUCE has no HMAC of its own. */
        juce::MemoryBlock hmacSha256 (const juce::String& message, const juce::String& secret)
        {
            const auto secretUtf8 = secret.toRawUTF8();
            const auto secretLength = static_cast<size_t> (secret.getNumBytesAsUTF8());

            juce::HeapBlock<juce::uint8> paddedKey (sha256BlockSize, true);

            if (secretLength > static_cast<size_t> (sha256BlockSize))
            {
                const auto digest = sha256Of (secretUtf8, secretLength);
                std::memcpy (paddedKey.get(), digest.getData(),
                             juce::jmin (digest.getSize(), static_cast<size_t> (sha256BlockSize)));
            }
            else
            {
                std::memcpy (paddedKey.get(), secretUtf8, secretLength);
            }

            juce::HeapBlock<juce::uint8> innerPad (sha256BlockSize, true);
            juce::HeapBlock<juce::uint8> outerPad (sha256BlockSize, true);

            for (int i = 0; i < sha256BlockSize; ++i)
            {
                innerPad[i] = static_cast<juce::uint8> (paddedKey[i] ^ 0x36);
                outerPad[i] = static_cast<juce::uint8> (paddedKey[i] ^ 0x5c);
            }

            juce::MemoryBlock innerInput (innerPad.get(), static_cast<size_t> (sha256BlockSize));
            innerInput.append (message.toRawUTF8(), static_cast<size_t> (message.getNumBytesAsUTF8()));

            const auto innerDigest = sha256Of (innerInput.getData(), innerInput.getSize());

            juce::MemoryBlock outerInput (outerPad.get(), static_cast<size_t> (sha256BlockSize));
            outerInput.append (innerDigest.getData(), innerDigest.getSize());

            return sha256Of (outerInput.getData(), outerInput.getSize());
        }

        /** RFC 4648 base32 without padding. */
        juce::String toBase32 (const juce::uint8* data, int numBytes)
        {
            juce::String result;
            int bitBuffer = 0;
            int bitCount = 0;

            for (int i = 0; i < numBytes; ++i)
            {
                bitBuffer = (bitBuffer << 8) | data[i];
                bitCount += 8;

                while (bitCount >= 5)
                {
                    bitCount -= 5;
                    result += base32Alphabet[(bitBuffer >> bitCount) & 0x1f];
                }
            }

            if (bitCount > 0)
                result += base32Alphabet[(bitBuffer << (5 - bitCount)) & 0x1f];

            return result;
        }

        /** Constant-time-ish comparison. Not a security boundary, just good hygiene. */
        bool equalsInConstantTime (const juce::String& a, const juce::String& b)
        {
            if (a.length() != b.length())
                return false;

            int difference = 0;

            for (int i = 0; i < a.length(); ++i)
                difference |= (a[i] ^ b[i]);

            return difference == 0;
        }
    }

    LicenseManager::LicenseManager (juce::ApplicationProperties* properties)
        : applicationProperties (properties)
    {
        refreshFromStorage();
    }

    juce::PropertiesFile* LicenseManager::getStore() const
    {
        return applicationProperties != nullptr ? applicationProperties->getUserSettings() : nullptr;
    }

    juce::String LicenseManager::normaliseEmail (const juce::String& email)
    {
        return email.trim().toLowerCase().substring (0, maxFieldLength);
    }

    juce::String LicenseManager::normaliseKey (const juce::String& key)
    {
        return key.removeCharacters (" -\t\r\n").toUpperCase().substring (0, maxFieldLength);
    }

    juce::String LicenseManager::generateKey (const juce::String& email)
    {
        const auto normalised = normaliseEmail (email);

        if (normalised.isEmpty())
            return {};

        const auto digest = hmacSha256 (normalised, PRO_LICENSE_SECRET);
        const auto* bytes = static_cast<const juce::uint8*> (digest.getData());
        const auto available = juce::jmin (static_cast<int> (digest.getSize()), keyBytes);

        const auto raw = toBase32 (bytes, available);

        // Print in dash-separated groups so users can read it off an invoice.
        juce::String formatted;

        for (int i = 0; i < raw.length(); ++i)
        {
            if (i > 0 && i % groupSize == 0)
                formatted += "-";

            formatted += raw[i];
        }

        return formatted;
    }

    bool LicenseManager::isWellFormedKey (const juce::String& normalisedKey)
    {
        // 10 bytes -> ceil(80/5) = 16 base32 characters.
        constexpr int expectedLength = ((keyBytes * 8) + 4) / 5;

        if (normalisedKey.length() != expectedLength)
            return false;

        for (int i = 0; i < normalisedKey.length(); ++i)
            if (juce::String (base32Alphabet).indexOfChar (normalisedKey[i]) < 0)
                return false;

        return true;
    }

    LicenseManager::Result LicenseManager::activate (const juce::String& email, const juce::String& key)
    {
        const auto cleanEmail = normaliseEmail (email);
        const auto cleanKey = normaliseKey (key);

        if (cleanEmail.isEmpty() || cleanKey.isEmpty())
            return Result::emptyFields;

        if (! isWellFormedKey (cleanKey))
            return Result::malformedKey;

        const auto expected = normaliseKey (generateKey (cleanEmail));

        if (! equalsInConstantTime (expected, cleanKey))
            return Result::signatureMismatch;

        storedEmail = cleanEmail;
        storedKey = cleanKey;
        proActivated = true;

        // Written synchronously so the unlock survives a crash right after activation
        // (19_DEBUGGING.md, common bug 8).
        persist();

        return Result::activated;
    }

    LicenseManager::Result LicenseManager::restore()
    {
        if (storedEmail.isEmpty() || storedKey.isEmpty())
        {
            proActivated = false;
            return Result::emptyFields;
        }

        return activate (storedEmail, storedKey);
    }

    void LicenseManager::deactivate()
    {
        storedEmail.clear();
        storedKey.clear();
        proActivated = false;

        if (auto* store = getStore())
        {
            store->removeValue (storageKeyName);
            store->saveIfNeeded();
        }
    }

    void LicenseManager::persist()
    {
        auto* store = getStore();

        if (store == nullptr)
            return;

        juce::DynamicObject::Ptr object (new juce::DynamicObject());
        object->setProperty ("email", storedEmail);
        object->setProperty ("key", storedKey);

        store->setValue (storageKeyName, juce::JSON::toString (juce::var (object.get()), true));
        store->saveIfNeeded();
    }

    void LicenseManager::refreshFromStorage()
    {
        storedEmail.clear();
        storedKey.clear();
        proActivated = false;

        auto* store = getStore();

        if (store == nullptr)
            return;

        const auto raw = store->getValue (storageKeyName);

        if (raw.isEmpty())
            return;

        const auto parsed = juce::JSON::parse (raw);

        if (parsed.isVoid() || ! parsed.isObject())
        {
            juce::Logger::writeToLog ("[warn] [license] Stored activation was unreadable; ignoring.");
            return;
        }

        storedEmail = normaliseEmail (parsed.getProperty ("email", juce::var()).toString());
        storedKey = normaliseKey (parsed.getProperty ("key", juce::var()).toString());

        if (storedEmail.isEmpty() || ! isWellFormedKey (storedKey))
        {
            storedEmail.clear();
            storedKey.clear();
            return;
        }

        proActivated = equalsInConstantTime (normaliseKey (generateKey (storedEmail)), storedKey);
    }

    juce::String LicenseManager::getMaskedKey() const
    {
        if (storedKey.isEmpty())
            return "(none)";

        if (storedKey.length() <= 4)
            return storedKey;

        return juce::String::repeatedString ("*", storedKey.length() - 4)
             + storedKey.getLastCharacters (4);
    }

    juce::String LicenseManager::getExpectedKeyForStoredEmail() const
    {
        return storedEmail.isEmpty() ? juce::String ("(no email stored)") : generateKey (storedEmail);
    }

    juce::String LicenseManager::describe (Result result)
    {
        switch (result)
        {
            case Result::activated:
                return "Pro activated. Thanks for supporting TEXTURE.";
            case Result::emptyFields:
                return "Please enter both email and license key.";
            case Result::malformedKey:
                return "Invalid key format.";
            case Result::signatureMismatch:
                return "Invalid key. Check your email or contact support.";
        }

        return "Unknown licensing result.";
    }
}
