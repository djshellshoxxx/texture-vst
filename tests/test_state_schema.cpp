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
