#include "TestHelpers.h"

#include "../Source/Utility/StateSchema.h"

namespace texture::tests
{
    class StateSchemaTests : public juce::UnitTest
    {
    public:
        StateSchemaTests()
            : juce::UnitTest ("Host state schema", "texture") {}

        void runTest() override
        {
            beginTest ("valid version 1 state with parameters is accepted");
            auto valid = makeRoot ("1.0.0", true);
            expectEquals (StateSchema::validate (valid).status, StateSchema::Status::ok);

            beginTest ("legacy state without a version remains readable");
            auto legacy = makeRoot ({}, true);
            expectEquals (StateSchema::validate (legacy).status, StateSchema::Status::ok);

            beginTest ("future major versions are rejected before application");
            auto future = makeRoot ("2.0.0", true);
            const auto futureResult = StateSchema::validate (future);
            expectEquals (futureResult.status, StateSchema::Status::futureVersion);
            expect (futureResult.message.containsIgnoreCase ("newer"));

            beginTest ("malformed version text is rejected");
            auto malformedVersion = makeRoot ("banana", true);
            expectEquals (StateSchema::validate (malformedVersion).status,
                          StateSchema::Status::invalidVersion);

            beginTest ("wrong root is rejected");
            juce::ValueTree wrongRoot ("OtherPluginState");
            expectEquals (StateSchema::validate (wrongRoot).status,
                          StateSchema::Status::wrongRoot);

            beginTest ("missing parameter tree is rejected atomically");
            auto partial = makeRoot ("1.0.0", false);
            expectEquals (StateSchema::validate (partial).status,
                          StateSchema::Status::missingParameters);

            beginTest ("sample layer indices must be in range");
            auto badIndex = makeRoot ("1.0.0", true);
            juce::ValueTree invalidSample (StateSchema::sampleTag);
            invalidSample.setProperty ("index", 7, nullptr);
            badIndex.appendChild (invalidSample, nullptr);
            expectEquals (StateSchema::validate (badIndex).status,
                          StateSchema::Status::invalidSampleIndex);

            beginTest ("duplicate sample layer records are rejected");
            auto duplicate = makeRoot ("1.0.0", true);
            juce::ValueTree sampleA (StateSchema::sampleTag);
            sampleA.setProperty ("index", 0, nullptr);
            juce::ValueTree sampleB (StateSchema::sampleTag);
            sampleB.setProperty ("index", 0, nullptr);
            duplicate.appendChild (sampleA, nullptr);
            duplicate.appendChild (sampleB, nullptr);
            expectEquals (StateSchema::validate (duplicate).status,
                          StateSchema::Status::duplicateSampleIndex);

            beginTest ("sample start positions must be finite and normalized");
            auto badStart = makeRoot ("1.0.0", true);
            juce::ValueTree sampleWithBadStart (StateSchema::sampleTag);
            sampleWithBadStart.setProperty ("index", 0, nullptr);
            sampleWithBadStart.setProperty ("start", 1.5, nullptr);
            badStart.appendChild (sampleWithBadStart, nullptr);
            expectEquals (StateSchema::validate (badStart).status,
                          StateSchema::Status::invalidSampleData);

            beginTest ("duplicate singleton metadata records are rejected");
            auto duplicateGrit = makeRoot ("1.0.0", true);
            duplicateGrit.appendChild (juce::ValueTree (StateSchema::secretTag), nullptr);
            duplicateGrit.appendChild (juce::ValueTree (StateSchema::secretTag), nullptr);
            expectEquals (StateSchema::validate (duplicateGrit).status,
                          StateSchema::Status::duplicateSingleton);

            beginTest ("GRIT numeric state must remain finite and in range");
            auto badGrit = makeRoot ("1.0.0", true);
            juce::ValueTree grit (StateSchema::secretTag);
            grit.setProperty ("bits", 99.0, nullptr);
            grit.setProperty ("downsample", 4.0, nullptr);
            grit.setProperty ("mix", 0.5, nullptr);
            badGrit.appendChild (grit, nullptr);
            expectEquals (StateSchema::validate (badGrit).status,
                          StateSchema::Status::invalidSecretData);

            beginTest ("A/B compare state validates slot identity and active index");
            auto compareValid = makeRoot ("1.0.0", true);
            juce::ValueTree compare (StateSchema::compareTag);
            compare.setProperty ("active", 1, nullptr);

            for (int index = 0; index < 2; ++index)
            {
                juce::ValueTree slot (StateSchema::compareSlotTag);
                slot.setProperty ("index", index, nullptr);
                slot.setProperty ("morph", index == 0 ? 0.25 : 0.75, nullptr);
                compare.appendChild (slot, nullptr);
            }

            compareValid.appendChild (compare, nullptr);
            expectEquals (StateSchema::validate (compareValid).status, StateSchema::Status::ok);

            beginTest ("invalid compare active slot is rejected");
            auto badCompare = makeRoot ("1.0.0", true);
            juce::ValueTree invalidCompare (StateSchema::compareTag);
            invalidCompare.setProperty ("active", 7, nullptr);
            badCompare.appendChild (invalidCompare, nullptr);
            expectEquals (StateSchema::validate (badCompare).status,
                          StateSchema::Status::invalidCompareData);

            beginTest ("empty parameter wrapper is rejected atomically");
            juce::ValueTree emptyWrapper (StateSchema::rootTag);
            emptyWrapper.setProperty (StateSchema::versionAttribute, "1.0.0", nullptr);
            emptyWrapper.appendChild (juce::ValueTree (StateSchema::parametersTag), nullptr);
            expectEquals (StateSchema::validate (emptyWrapper).status,
                          StateSchema::Status::missingParameters);
        }

    private:
        static juce::ValueTree makeRoot (const juce::String& version, bool includeParameterState)
        {
            juce::ValueTree root (StateSchema::rootTag);

            if (version.isNotEmpty())
                root.setProperty (StateSchema::versionAttribute, version, nullptr);

            if (includeParameterState)
            {
                juce::ValueTree wrapper (StateSchema::parametersTag);
                wrapper.appendChild (juce::ValueTree ("APVTS"), nullptr);
                root.appendChild (wrapper, nullptr);
            }

            return root;
        }
    };

    static StateSchemaTests stateSchemaTests;
}
