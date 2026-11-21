#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout RoomprintProcessor::createLayout()
{
    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] (const char* id, const char* name, float def, float lo = 0.0f, float hi = 100.0f)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name,
                                      juce::NormalisableRange<float> (lo, hi, 0.1f), def,
                                      Attr().withLabel ("%"));
    };

    auto hertz = [] (const char* id, const char* name, float lo, float hi, float centre, float def)
    {
        juce::NormalisableRange<float> range (lo, hi, 1.0f);
        range.setSkewForCentre (centre);
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def, Attr().withLabel ("Hz"));
    };

    auto choice = [] (const char* id, const char* name, juce::StringArray items, int def)
    {
        return std::make_unique<APC> (juce::ParameterID { id, 1 }, name, std::move (items), def);
    };

    // Source
    layout.add (choice (ids::voicing, "Voicing", { "Close", "Ribbon", "Room Mic", "Radio", "Cab" }, 0));
    layout.add (hertz (ids::lowCut, "Low Cut", 20.0f, 500.0f, 100.0f, 20.0f));
    layout.add (hertz (ids::highCut, "High Cut", 2000.0f, 20000.0f, 8000.0f, 20000.0f));
    layout.add (percent (ids::resonance, "Resonance", 30.0f));
    layout.add (percent (ids::proximity, "Proximity", 30.0f));

    // Saturation
    layout.add (choice (ids::satType, "Saturation Type", { "Tape", "Tube", "Transformer" }, 0));
    layout.add (choice (ids::satPos, "Saturation Position", { "Pre-Room", "Post-Room" }, 0));
    layout.add (percent (ids::drive, "Drive", 30.0f));
    layout.add (percent (ids::bias, "Bias", 0.0f));
    layout.add (percent (ids::tone, "Tone", 0.0f, -100.0f, 100.0f));
    layout.add (percent (ids::satMix, "Saturation Mix", 100.0f));

    // Distance
    layout.add (percent (ids::distance, "Distance", 40.0f));
    layout.add (percent (ids::depth, "Distance Depth", 100.0f));

    // Room
    layout.add (choice (ids::material, "Material", { "Wood", "Concrete", "Padded", "Tiled" }, 0));
    layout.add (percent (ids::size, "Size", 45.0f));
    {
        juce::NormalisableRange<float> range (0.1f, 3.0f, 0.01f);
        range.setSkewForCentre (0.6f);
        layout.add (std::make_unique<APF> (juce::ParameterID { ids::decay, 1 }, "Decay", range, 0.5f, Attr().withLabel ("s")));
    }
    layout.add (percent (ids::diffusion, "Diffusion", 70.0f));
    layout.add (percent (ids::width, "Width", 100.0f, 0.0f, 150.0f));
    layout.add (hertz (ids::roomLow, "Room Low Cut", 20.0f, 1000.0f, 150.0f, 80.0f));
    layout.add (hertz (ids::roomHigh, "Room High Cut", 1000.0f, 20000.0f, 8000.0f, 12000.0f));
    layout.add (percent (ids::duck, "Duck", 0.0f));
    layout.add (percent (ids::squash, "Squash", 0.0f));

    // Output
    layout.add (choice (ids::oversampling, "Oversampling", { "Off", "2x", "4x" }, 1));
    layout.add (percent (ids::mix, "Mix", 100.0f));
    layout.add (std::make_unique<APF> (juce::ParameterID { ids::output, 1 }, "Output",
                                       juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, Attr().withLabel ("dB")));
    return layout;
}

//==============================================================================
RoomprintProcessor::RoomprintProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

bool RoomprintProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

rp::Params RoomprintProcessor::readParams() const
{
    rp::Params p;
    p.voicing       = (int) raw (ids::voicing);
    p.lowCutHz      = raw (ids::lowCut);
    p.highCutHz     = raw (ids::highCut);
    p.resonance     = raw (ids::resonance) * 0.01f;
    p.proximity     = raw (ids::proximity) * 0.01f;

    p.satType       = (int) raw (ids::satType);
    p.satPost       = raw (ids::satPos) > 0.5f;
    p.drive         = raw (ids::drive) * 0.01f;
    p.bias          = raw (ids::bias) * 0.01f;
    p.tone          = raw (ids::tone) * 0.01f;
    p.satMix        = raw (ids::satMix) * 0.01f;

    p.distance      = raw (ids::distance) * 0.01f;
    p.distDepth     = raw (ids::depth) * 0.01f;

    p.material      = (int) raw (ids::material);
    p.size          = raw (ids::size) * 0.01f;
    p.decaySec      = raw (ids::decay);
    p.diffusion     = raw (ids::diffusion) * 0.01f;
    p.width         = raw (ids::width) * 0.01f;
    p.roomLowCutHz  = raw (ids::roomLow);
    p.roomHighCutHz = raw (ids::roomHigh);
    p.duck          = raw (ids::duck) * 0.01f;
    p.squash        = raw (ids::squash) * 0.01f;

    p.oversampling  = (int) raw (ids::oversampling);
    p.mix           = raw (ids::mix) * 0.01f;
    p.outputDb      = raw (ids::output);
    return p;
}

void RoomprintProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    engine.prepare (sampleRate);
    engine.setParams (readParams());   // after prepare(): jumps straight to the current values
    setLatencySamples (engine.getLatencySamples());
}

void RoomprintProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // Mono input on a stereo output bus: feed both sides.
    if (getTotalNumInputChannels() < 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    engine.setParams (readParams());

    const int latency = engine.getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);   // only changes when the Oversampling choice changes

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
}

juce::AudioProcessorEditor* RoomprintProcessor::createEditor()
{
    return new RoomprintEditor (*this);
}

void RoomprintProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RoomprintProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RoomprintProcessor();
}
