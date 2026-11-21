#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"

namespace ids
{
    // source
    inline constexpr const char* voicing     = "voicing";
    inline constexpr const char* lowCut      = "lowCut";
    inline constexpr const char* highCut     = "highCut";
    inline constexpr const char* resonance   = "resonance";
    inline constexpr const char* proximity   = "proximity";
    // saturation
    inline constexpr const char* satType     = "satType";
    inline constexpr const char* satPos      = "satPos";
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* bias        = "bias";
    inline constexpr const char* tone        = "tone";
    inline constexpr const char* satMix      = "satMix";
    // distance
    inline constexpr const char* distance    = "distance";
    inline constexpr const char* depth       = "distDepth";
    // room
    inline constexpr const char* material    = "material";
    inline constexpr const char* size        = "size";
    inline constexpr const char* decay       = "decay";
    inline constexpr const char* diffusion   = "diffusion";
    inline constexpr const char* width       = "width";
    inline constexpr const char* roomLow     = "roomLow";
    inline constexpr const char* roomHigh    = "roomHigh";
    inline constexpr const char* duck        = "duck";
    inline constexpr const char* squash      = "squash";
    // output
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* output      = "output";
}

class RoomprintProcessor : public juce::AudioProcessor
{
public:
    RoomprintProcessor();
    ~RoomprintProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Roomprint"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    rp::Params readParams() const;
    float raw (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    rp::Engine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomprintProcessor)
};
