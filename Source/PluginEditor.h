#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class RoomprintLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RoomprintLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff15171c));
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffe3a23b));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff343944));
        setColour (juce::Slider::thumbColourId, juce::Colour (0xfff2efe8));
        setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffd8d4ca));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, juce::Colour (0xff9aa0ad));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff222630));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff343944));
        setColour (juce::ComboBox::textColourId, juce::Colour (0xfff2efe8));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffe3a23b));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff222630));
        setColour (juce::PopupMenu::textColourId, juce::Colour (0xfff2efe8));
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffe3a23b));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff15171c));
    }
};

class RoomprintEditor : public juce::AudioProcessorEditor
{
public:
    explicit RoomprintEditor (RoomprintProcessor&);
    ~RoomprintEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;   // declared last: destroyed first
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    struct Group
    {
        juce::String name;
        juce::Rectangle<int> area;
    };

    void addKnob (Knob&, const char* paramId, const juce::String& name, bool big = false);
    void addChoice (Choice&, const char* paramId, const juce::String& name);

    static void placeChoices (juce::Rectangle<int>& area, std::initializer_list<Choice*> list);
    static void placeKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> list, int columns);

    RoomprintProcessor& audioProcessor;
    RoomprintLookAndFeel lnf;

    // Source
    Choice voicing;
    Knob lowCut, highCut, resonance, proximity;
    // Saturation
    Choice satType, satPos;
    Knob drive, bias, tone, satMix;
    // Distance
    Knob distance, depth;
    // Room
    Choice material;
    Knob size, decay, diffusion, width, roomLow, roomHigh, duck, squash;
    // Output
    Choice oversampling;
    Knob mix, output;

    std::vector<Group> groups;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomprintEditor)
};
