#include "PluginEditor.h"

namespace
{
    const juce::Colour kPanel  { 0xff1d2027 };
    const juce::Colour kEdge   { 0xff2c313b };
    const juce::Colour kAccent { 0xffe3a23b };
    const juce::Colour kText   { 0xfff2efe8 };
    const juce::Colour kMuted  { 0xff7d8493 };

    juce::Font makeFont (float height, int styleFlags = juce::Font::plain)
    {
        return juce::Font (juce::FontOptions (height, styleFlags));
    }
}

RoomprintEditor::RoomprintEditor (RoomprintProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lnf);

    addChoice (voicing, ids::voicing, "Voicing");
    addKnob (lowCut, ids::lowCut, "Low Cut");
    addKnob (highCut, ids::highCut, "High Cut");
    addKnob (resonance, ids::resonance, "Resonance");
    addKnob (proximity, ids::proximity, "Proximity");

    addChoice (satType, ids::satType, "Type");
    addChoice (satPos, ids::satPos, "Position");
    addKnob (drive, ids::drive, "Drive");
    addKnob (bias, ids::bias, "Bias");
    addKnob (tone, ids::tone, "Tone");
    addKnob (satMix, ids::satMix, "Blend");

    addKnob (distance, ids::distance, "DISTANCE", true);
    addKnob (depth, ids::depth, "Depth");

    addChoice (material, ids::material, "Material");
    addKnob (size, ids::size, "Size");
    addKnob (decay, ids::decay, "Decay");
    addKnob (diffusion, ids::diffusion, "Diffusion");
    addKnob (width, ids::width, "Width");
    addKnob (roomLow, ids::roomLow, "Low Cut");
    addKnob (roomHigh, ids::roomHigh, "High Cut");
    addKnob (duck, ids::duck, "Duck");
    addKnob (squash, ids::squash, "Squash");

    addChoice (oversampling, ids::oversampling, "Oversampling");
    addKnob (mix, ids::mix, "Mix");
    addKnob (output, ids::output, "Output");

    setSize (900, 600);
}

RoomprintEditor::~RoomprintEditor()
{
    setLookAndFeel (nullptr);
}

void RoomprintEditor::addKnob (Knob& k, const char* paramId, const juce::String& name, bool big)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 90 : 64, 16);
    k.slider.setDoubleClickReturnValue (true, audioProcessor.apvts.getParameter (paramId)->convertFrom0to1 (
                                                  audioProcessor.apvts.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (makeFont (big ? 15.0f : 12.5f, big ? juce::Font::bold : juce::Font::plain));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramId, k.slider);
}

void RoomprintEditor::addChoice (Choice& c, const char* paramId, const juce::String& name)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter (paramId)))
        c.box.addItemList (param->choices, 1);
    addAndMakeVisible (c.box);

    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setFont (makeFont (12.5f));
    addAndMakeVisible (c.label);

    c.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, paramId, c.box);
}

void RoomprintEditor::placeChoices (juce::Rectangle<int>& area, std::initializer_list<Choice*> list)
{
    auto row = area.removeFromTop (46);
    const int w = row.getWidth() / (int) list.size();
    for (auto* c : list)
    {
        auto cell = row.removeFromLeft (w).reduced (6, 0);
        c->label.setBounds (cell.removeFromTop (16));
        c->box.setBounds (cell.removeFromTop (26));
    }
}

void RoomprintEditor::placeKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> list, int columns)
{
    const int rows = ((int) list.size() + columns - 1) / columns;
    const int cw = area.getWidth() / columns;
    const int ch = area.getHeight() / rows;
    int i = 0;
    for (auto* k : list)
    {
        juce::Rectangle<int> cell (area.getX() + (i % columns) * cw, area.getY() + (i / columns) * ch, cw, ch);
        cell = cell.reduced (3);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell);
        ++i;
    }
}

void RoomprintEditor::resized()
{
    groups.clear();
    const int pad = 12, header = 28;

    juce::Rectangle<int> sourceArea (pad, 52, 430, 236);
    juce::Rectangle<int> satArea (sourceArea.getRight() + 10, 52, 900 - pad - (sourceArea.getRight() + 10), 236);
    juce::Rectangle<int> distArea (pad, 298, 224, 290);
    juce::Rectangle<int> roomArea (distArea.getRight() + 10, 298, 470, 290);
    juce::Rectangle<int> outArea (roomArea.getRight() + 10, 298, 900 - pad - (roomArea.getRight() + 10), 290);

    groups.push_back ({ "SOURCE", sourceArea });
    groups.push_back ({ "SATURATION", satArea });
    groups.push_back ({ "DISTANCE", distArea });
    groups.push_back ({ "ROOM", roomArea });
    groups.push_back ({ "OUTPUT", outArea });

    {   // source
        auto a = sourceArea.reduced (8); a.removeFromTop (header);
        placeChoices (a, { &voicing });
        placeKnobs (a, { &lowCut, &highCut, &resonance, &proximity }, 4);
    }
    {   // saturation
        auto a = satArea.reduced (8); a.removeFromTop (header);
        placeChoices (a, { &satType, &satPos });
        placeKnobs (a, { &drive, &bias, &tone, &satMix }, 4);
    }
    {   // distance
        auto a = distArea.reduced (8); a.removeFromTop (header);
        placeKnobs (a.removeFromTop (a.getHeight() * 2 / 3), { &distance }, 1);
        placeKnobs (a, { &depth }, 1);
    }
    {   // room
        auto a = roomArea.reduced (8); a.removeFromTop (header);
        placeChoices (a, { &material });
        placeKnobs (a, { &size, &decay, &diffusion, &width, &roomLow, &roomHigh, &duck, &squash }, 4);
    }
    {   // output
        auto a = outArea.reduced (8); a.removeFromTop (header);
        placeChoices (a, { &oversampling });
        placeKnobs (a, { &mix, &output }, 1);
    }
}

void RoomprintEditor::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (kText);
    g.setFont (makeFont (24.0f, juce::Font::bold));
    g.drawText ("ROOMPRINT", 16, 10, 300, 32, juce::Justification::centredLeft);

    g.setColour (kMuted);
    g.setFont (makeFont (13.0f));
    g.drawText ("source  >  saturation  >  distance  >  room", 190, 14, 420, 24, juce::Justification::centredLeft);

    for (const auto& grp : groups)
    {
        auto r = grp.area.toFloat();
        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (kEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (kAccent);
        g.setFont (makeFont (12.0f, juce::Font::bold));
        g.drawText (grp.name, grp.area.getX() + 14, grp.area.getY() + 8, 200, 18, juce::Justification::centredLeft);
    }
}
