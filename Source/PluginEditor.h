#pragma once
#include <JuceHeader.h>
#include <array>

class KIRAProcessor;

// Interfaz estilo "Wired": cables, CRT, scanlines, ruido, glitch y un monitor de afinacion.
class KIRAEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KIRAEditor(KIRAProcessor&);
    ~KIRAEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawWires(juce::Graphics&);
    void drawEye(juce::Graphics&, juce::Point<float> centre);
    void drawTrace(juce::Graphics&, juce::Rectangle<float> area);
    void drawTicker(juce::Graphics&, juce::Rectangle<float> area);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    KIRAProcessor& proc;
    juce::Slider bend, tune, speed, vibe, smile, mix, refA4;
    juce::ComboBox key, scale;
    std::unique_ptr<juce::LookAndFeel_V4> look;
    std::unique_ptr<SliderAttachment> bendAtt, tuneAtt, speedAtt, vibeAtt, smileAtt, mixAtt, refAtt;
    std::unique_ptr<ComboAttachment> keyAtt, scaleAtt;

    static constexpr int kTrace = 150; // ~5 s a 30 fps
    std::array<float, kTrace> traceIn, traceOut;
    int tracePos = 0;
    float centreMidi = 60.0f, eyeOpen = 0.25f, pupil = 3.5f, t = 0.0f;
    float tickerX = 0.0f, tickerW = 0.0f;
    float lastIn = -1.0f, lastOut = -1.0f;
    int glitchFrames = 0, nextGlitch = 70;

    juce::Image noiseImg, scanImg;
    juce::Random rng { 1234 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KIRAEditor)
};
