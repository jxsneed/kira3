#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
// ---- Paleta "Wired": casi negro, blanco sucio, rojo de alarma, cian de datos ----
const juce::Colour kBg      (0xff07080a);
const juce::Colour kPanel   (0xff0e1012);
const juce::Colour kLine    (0xff3a3e42);
const juce::Colour kText    (0xffd8d4c8);
const juce::Colour kDim     (0xff7b7d79);
const juce::Colour kRed     (0xffd7302b);
const juce::Colour kCyan    (0xff4fb3b0);

juce::Font mono(float size, float kerning = 0.0f, bool bold = false)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size,
                                        bold ? juce::Font::bold : juce::Font::plain)
                          .withKerningFactor(kerning));
}

// Textos japoneses en UTF-8 explicito (no dependen de la codificacion del archivo).
juce::String jpWired()  { return juce::String::fromUTF8("\xe3\x83\xaf" "\xe3\x82\xa4" "\xe3\x83\xa4" "\xe3\x83\xbc" "\xe3\x83\x89"); }
juce::String jpLink()   { return juce::String::fromUTF8("\xe6\x8e\xa5" "\xe7\xb6\x9a"); }
juce::String jpKira()   { return juce::String::fromUTF8("\xe3\x82\xad" "\xe3\x83\xa9"); }
juce::String jpSignal() { return juce::String::fromUTF8("\xe4\xbf\xa1" "\xe5\x8f\xb7"); }

juce::String noteName(int midi)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = ((midi % 12) + 12) % 12;
    return juce::String(names[n]) + juce::String(midi / 12 - 1);
}

juce::String noteLabel(float midi)
{
    if (midi < 0.0f) return "--";
    const int n = (int) std::lround(midi);
    const int cents = (int) std::lround((midi - (float) n) * 100.0f);
    return noteName(n) + " " + juce::String(cents >= 0 ? "+" : "-")
           + juce::String(std::abs(cents)).paddedLeft('0', 2) + "c";
}

void bracket(juce::Graphics& g, juce::Rectangle<float> r, float len, juce::Colour c)
{
    g.setColour(c);
    const float l = r.getX(), t = r.getY(), rr = r.getRight(), b = r.getBottom();
    g.drawLine(l, t, l + len, t, 1.0f);  g.drawLine(l, t, l, t + len, 1.0f);
    g.drawLine(rr, t, rr - len, t, 1.0f); g.drawLine(rr, t, rr, t + len, 1.0f);
    g.drawLine(l, b, l + len, b, 1.0f);  g.drawLine(l, b, l, b - len, 1.0f);
    g.drawLine(rr, b, rr - len, b, 1.0f); g.drawLine(rr, b, rr, b - len, 1.0f);
}

// ---------------------------------------------------------------------------
class WiredLook : public juce::LookAndFeel_V4
{
public:
    WiredLook()
    {
        setColour(juce::PopupMenu::backgroundColourId, kPanel);
        setColour(juce::PopupMenu::textColourId, kText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kRed);
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::Label::textColourId, kText);
    }

    juce::Font getLabelFont(juce::Label&) override { return mono(12.0f); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return mono(12.0f); }
    juce::Font getPopupMenuFont() override { return mono(12.0f); }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float pos, float start, float end, juce::Slider&) override
    {
        auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(6.0f);
        const auto c = b.getCentre();
        const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;
        const float angle = start + pos * (end - start);

        // base oscura con brillo de fosforo tenue
        juce::ColourGradient base(juce::Colour(0xff15181a), c.x, c.y,
                                  juce::Colour(0xff050606), c.x, c.y - r, true);
        g.setGradientFill(base);
        g.fillEllipse(c.x - r * 0.78f, c.y - r * 0.78f, r * 1.56f, r * 1.56f);
        g.setColour(kLine);
        g.drawEllipse(c.x - r * 0.78f, c.y - r * 0.78f, r * 1.56f, r * 1.56f, 1.0f);

        // marcas: las activas en rojo
        const int ticks = 29;
        for (int i = 0; i < ticks; ++i)
        {
            const float f = (float) i / (float) (ticks - 1);
            const float a = start + f * (end - start);
            const bool major = (i % 7 == 0);
            const float r1 = r * (major ? 0.90f : 0.94f), r2 = r;
            const float ang = a - juce::MathConstants<float>::halfPi; // 0 = arriba
            g.setColour(f <= pos + 0.001f ? kRed : kLine);
            g.drawLine(c.x + std::cos(ang) * r1, c.y + std::sin(ang) * r1,
                       c.x + std::cos(ang) * r2, c.y + std::sin(ang) * r2, major ? 1.6f : 1.0f);
        }

        // arco de valor con halo
        const float ra = r * 0.82f;
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, ra, ra, 0.0f, start, angle, true);
        g.setColour(kRed.withAlpha(0.18f));
        g.strokePath(arc, juce::PathStrokeType(6.0f));
        g.setColour(kRed.withAlpha(0.9f));
        g.strokePath(arc, juce::PathStrokeType(1.6f));

        // aguja + mira central
        const float ang = angle - juce::MathConstants<float>::halfPi;
        g.setColour(kText);
        g.drawLine(c.x + std::cos(ang) * r * 0.30f, c.y + std::sin(ang) * r * 0.30f,
                   c.x + std::cos(ang) * r * 0.70f, c.y + std::sin(ang) * r * 0.70f, 1.6f);
        g.setColour(kDim);
        g.drawLine(c.x - 5.0f, c.y, c.x + 5.0f, c.y, 1.0f);
        g.drawLine(c.x, c.y - 5.0f, c.x, c.y + 5.0f, 1.0f);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                          float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour(kLine);
        g.drawLine((float) x, cy, (float) (x + w), cy, 1.0f);
        for (int i = 0; i <= 10; ++i)
        {
            const float tx = (float) x + (float) w * (float) i / 10.0f;
            g.drawLine(tx, cy - 3.0f, tx, cy + 3.0f, 1.0f);
        }
        g.setColour(kRed.withAlpha(0.8f));
        g.drawLine((float) x, cy, sliderPos, cy, 1.6f);
        juce::Path tri;
        tri.addTriangle(sliderPos - 4.0f, cy - 8.0f, sliderPos + 4.0f, cy - 8.0f, sliderPos, cy - 1.0f);
        g.setColour(kText);
        g.fillPath(tri);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        const auto r = juce::Rectangle<float>(0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
        g.setColour(kPanel);
        g.fillRect(r);
        g.setColour(kLine);
        g.drawRect(r, 1.0f);
        juce::Path tri;
        const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
        tri.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(kRed);
        g.fillPath(tri);
    }
};

constexpr int kW = 760, kH = 480;
constexpr int kCellW = 112, kCellStep = 119, kCellX = 24, kKnobY = 246;
const char* const kLabels[] = { "BEND", "TUNE", "SPEED", "VIBE", "SMILE", "MIX" };
} // namespace

// ===========================================================================
KIRAEditor::KIRAEditor(KIRAProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    traceIn.fill(-1.0f);
    traceOut.fill(-1.0f);

    look = std::make_unique<WiredLook>();
    setLookAndFeel(look.get());
    setSize(kW, kH);

    // ruido de grano y scanlines pre-renderizados (se repiten en mosaico)
    noiseImg = juce::Image(juce::Image::ARGB, 128, 128, true);
    for (int yy = 0; yy < 128; ++yy)
        for (int xx = 0; xx < 128; ++xx)
            noiseImg.setPixelAt(xx, yy, juce::Colour::fromFloatRGBA(1.0f, 1.0f, 1.0f, rng.nextFloat() * 0.07f));
    scanImg = juce::Image(juce::Image::ARGB, 4, 3, true);
    scanImg.setPixelAt(0, 0, juce::Colour::fromFloatRGBA(0.0f, 0.0f, 0.0f, 0.34f));
    scanImg.setPixelAt(1, 0, juce::Colour::fromFloatRGBA(0.0f, 0.0f, 0.0f, 0.34f));
    scanImg.setPixelAt(2, 0, juce::Colour::fromFloatRGBA(0.0f, 0.0f, 0.0f, 0.34f));
    scanImg.setPixelAt(3, 0, juce::Colour::fromFloatRGBA(0.0f, 0.0f, 0.0f, 0.34f));

    auto setup = [this](juce::Slider& s, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                              juce::MathConstants<float>::pi * 2.75f, true);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 20);
        s.setColour(juce::Slider::textBoxTextColourId, kText);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxHighlightColourId, kRed);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);
    };
    setup(bend, " st"); setup(tune, " %"); setup(speed, " %");
    setup(vibe, " %");  setup(smile, " %"); setup(mix, " %");

    bendAtt.reset(new SliderAttachment(proc.params, "bend", bend));
    tuneAtt.reset(new SliderAttachment(proc.params, "tune", tune));
    speedAtt.reset(new SliderAttachment(proc.params, "speed", speed));
    vibeAtt.reset(new SliderAttachment(proc.params, "vibe", vibe));
    smileAtt.reset(new SliderAttachment(proc.params, "smile", smile));
    mixAtt.reset(new SliderAttachment(proc.params, "mix", mix));

    refA4.setSliderStyle(juce::Slider::LinearHorizontal);
    refA4.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
    refA4.setTextValueSuffix(" Hz");
    refA4.setColour(juce::Slider::textBoxTextColourId, kText);
    refA4.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    refA4.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(refA4);
    refAtt.reset(new SliderAttachment(proc.params, "ref", refA4));

    // Los items salen de los propios parametros: nunca se desincronizan.
    if (auto* k = dynamic_cast<juce::AudioParameterChoice*>(proc.params.getParameter("root")))
        key.addItemList(k->choices, 1);
    if (auto* s = dynamic_cast<juce::AudioParameterChoice*>(proc.params.getParameter("scale")))
        scale.addItemList(s->choices, 1);
    addAndMakeVisible(key);
    addAndMakeVisible(scale);
    for (auto* c : { &key, &scale })
    {
        c->setColour(juce::ComboBox::textColourId, kText);
        c->setColour(juce::ComboBox::backgroundColourId, kPanel);
        c->setColour(juce::ComboBox::outlineColourId, kLine);
        c->setJustificationType(juce::Justification::centredLeft);
    }
    keyAtt.reset(new ComboAttachment(proc.params, "root", key));
    scaleAtt.reset(new ComboAttachment(proc.params, "scale", scale));

    startTimerHz(30);
}

KIRAEditor::~KIRAEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void KIRAEditor::timerCallback()
{
    t += 1.0f / 30.0f;
    lastIn = proc.uiIn.load();
    lastOut = proc.uiOut.load();
    traceIn[(size_t) tracePos] = lastIn;
    traceOut[(size_t) tracePos] = lastOut;
    tracePos = (tracePos + 1) % kTrace;

    if (lastIn > 0.0f) centreMidi += (lastIn - centreMidi) * 0.06f;
    eyeOpen += ((lastIn > 0.0f ? 1.0f : 0.22f) - eyeOpen) * 0.15f;
    pupil += ((lastIn > 0.0f ? 6.5f : 3.0f) - pupil) * 0.2f;
    tickerX -= 0.9f;
    if (tickerW > 1.0f && tickerX < -tickerW) tickerX += tickerW;

    if (glitchFrames > 0) --glitchFrames;
    else if (--nextGlitch <= 0) { glitchFrames = 2 + rng.nextInt(4); nextGlitch = 70 + rng.nextInt(220); }

    repaint();
}

// ---------------------------------------------------------------------------
void KIRAEditor::drawWires(juce::Graphics& g)
{
    // Cables de tension colgando de poste a poste, con balanceo lento.
    const float w = (float) kW;
    for (int i = 0; i < 6; ++i)
    {
        const float y0 = 8.0f + (float) i * 4.5f;
        const float sag = 16.0f + (float) i * 3.0f + 3.0f * std::sin(t * 0.6f + (float) i);
        juce::Path p;
        p.startNewSubPath(0.0f, y0);
        p.quadraticTo(w * 0.5f, y0 + sag, w, y0 + 2.0f * (float) (i % 3));
        g.setColour((i == 2 ? kRed : kText).withAlpha(i == 2 ? 0.55f : 0.14f + 0.03f * (float) i));
        g.strokePath(p, juce::PathStrokeType(i == 2 ? 1.2f : 0.8f));
    }
    g.setColour(kLine);
    for (float px : { 0.0f, w * 0.5f, w })
        g.drawLine(px, 0.0f, px, 6.0f, 1.0f);
}

void KIRAEditor::drawEye(juce::Graphics& g, juce::Point<float> c)
{
    const float rx = 34.0f, ry = 28.0f * eyeOpen + 2.0f;
    juce::Path eye;
    eye.startNewSubPath(c.x - rx, c.y);
    eye.quadraticTo(c.x, c.y - ry * 1.5f, c.x + rx, c.y);
    // con SMILE el parpado inferior se curva hacia arriba: ojo sonriente
    const float smileAmt = juce::jlimit(0.0f, 1.0f, (float) smile.getValue() * 0.01f);
    eye.quadraticTo(c.x, c.y + ry * 1.5f * (1.0f - 1.7f * smileAmt), c.x - rx, c.y);
    eye.closeSubPath();

    g.setColour(juce::Colour(0xff0b0c0d));
    g.fillPath(eye);

    g.saveState();
    g.reduceClipRegion(eye);
    const float ox = std::sin(t * 0.7f) * 5.0f, oy = std::cos(t * 0.5f) * 2.0f;
    const juce::Point<float> ic(c.x + ox, c.y + oy);
    for (int i = 0; i < 4; ++i) // anillos concentricos del iris
    {
        const float rr = 17.0f - (float) i * 3.5f;
        g.setColour((i % 2 == 0 ? kRed : kCyan).withAlpha(0.5f));
        g.drawEllipse(ic.x - rr, ic.y - rr, rr * 2.0f, rr * 2.0f, 1.0f);
    }
    g.setColour(kBg);
    g.fillEllipse(ic.x - pupil, ic.y - pupil, pupil * 2.0f, pupil * 2.0f);
    g.setColour(kText.withAlpha(0.85f));
    g.fillEllipse(ic.x - pupil * 0.35f - 2.5f, ic.y - pupil * 0.35f - 3.0f, 3.0f, 3.0f); // destello
    g.restoreState();

    g.setColour(kText.withAlpha(0.8f));
    g.strokePath(eye, juce::PathStrokeType(1.2f));
    g.setColour(kDim);
    g.drawLine(c.x - rx - 10.0f, c.y, c.x - rx - 3.0f, c.y, 1.0f); // mira lateral
    g.drawLine(c.x + rx + 3.0f, c.y, c.x + rx + 10.0f, c.y, 1.0f);
}

void KIRAEditor::drawTrace(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(kPanel);
    g.fillRect(area);
    g.setColour(kLine);
    g.drawRect(area, 1.0f);

    g.saveState();
    g.reduceClipRegion(area.toNearestInt());

    const float lo = centreMidi - 8.0f;
    auto yOf = [&](float m) { return area.getBottom() - (m - lo) / 16.0f * area.getHeight(); };

    // rejilla de semitonos con nombre de nota
    g.setFont(mono(9.0f));
    for (int m = (int) std::ceil(lo); m <= (int) std::floor(lo + 16.0f); ++m)
    {
        const float y = yOf((float) m);
        const bool natural = noteName(m).containsChar('#') == false;
        g.setColour(natural ? kLine.withAlpha(0.65f) : kLine.withAlpha(0.22f));
        g.drawLine(area.getX(), y, area.getRight(), y, 1.0f);
        g.setColour(natural ? kDim : kDim.withAlpha(0.45f));
        g.drawText(noteName(m), juce::Rectangle<int>((int) area.getX() + 4, (int) y - 6, 34, 12),
                   juce::Justification::centredLeft, false);
    }

    auto drawLine = [&](const std::array<float, kTrace>& arr, juce::Colour col, float thick)
    {
        juce::Path path;
        bool pen = false;
        float prev = 0.0f;
        for (int i = 0; i < kTrace; ++i)
        {
            const float v = arr[(size_t) ((tracePos + i) % kTrace)];
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kTrace - 1);
            if (v < 0.0f) { pen = false; continue; }
            if (pen && std::abs(v - prev) > 4.0f) pen = false;
            const float y = juce::jlimit(area.getY(), area.getBottom(), yOf(v));
            if (! pen) { path.startNewSubPath(x, y); pen = true; }
            else path.lineTo(x, y);
            prev = v;
        }
        g.setColour(col.withAlpha(0.22f));
        g.strokePath(path, juce::PathStrokeType(thick + 3.0f));
        g.setColour(col);
        g.strokePath(path, juce::PathStrokeType(thick));
    };
    drawLine(traceIn, kCyan.withAlpha(0.8f), 1.0f);
    drawLine(traceOut, kRed, 1.7f);

    // cabezal de lectura
    g.setColour(kText.withAlpha(0.5f));
    g.drawLine(area.getRight() - 1.0f, area.getY(), area.getRight() - 1.0f, area.getBottom(), 1.0f);
    g.restoreState();
}

void KIRAEditor::drawTicker(juce::Graphics& g, juce::Rectangle<float> area)
{
    const juce::String msg = "KIRA.NAVI  //  PITCH LAYER ACTIVE  //  " + jpSignal()
                           + "  //  ARE YOU CONNECTED?  //  " + jpLink() + "  //  ";
    const auto font = mono(10.0f, 0.08f);
    if (tickerW < 1.0f)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, msg, 0.0f, 0.0f);
        tickerW = ga.getBoundingBox(0, -1, true).getWidth();
    }
    g.saveState();
    g.reduceClipRegion(area.toNearestInt());
    g.setColour(kDim);
    g.setFont(font);
    const int h = (int) area.getHeight();
    for (float x = area.getX() + tickerX; x < area.getRight(); x += tickerW)
        g.drawText(msg, juce::Rectangle<int>((int) x, (int) area.getY(), (int) tickerW + 4, h),
                   juce::Justification::centredLeft, false);
    g.restoreState();
}

// ---------------------------------------------------------------------------
void KIRAEditor::paint(juce::Graphics& g)
{
    const float w = (float) kW, h = (float) kH;

    juce::ColourGradient bg(juce::Colour(0xff0d1012), w * 0.5f, h * 0.35f,
                            kBg, 0.0f, 0.0f, true);
    g.setGradientFill(bg);
    g.fillAll();

    drawWires(g);

    // marco principal con esquinas
    bracket(g, juce::Rectangle<float>(12.0f, 40.0f, w - 24.0f, h - 52.0f), 16.0f, kText.withAlpha(0.55f));

    // --- cabecera ---
    drawEye(g, juce::Point<float>(62.0f, 84.0f));

    const bool glitch = glitchFrames > 0;
    g.setFont(mono(46.0f, 0.35f, true));
    if (glitch)
    {
        g.setColour(kRed.withAlpha(0.8f));
        g.drawText("KIRA", juce::Rectangle<int>(118 + 4, 52, 260, 54), juce::Justification::centredLeft, false);
        g.setColour(kCyan.withAlpha(0.7f));
        g.drawText("KIRA", juce::Rectangle<int>(118 - 4, 52, 260, 54), juce::Justification::centredLeft, false);
    }
    g.setColour(kText);
    g.drawText("KIRA", juce::Rectangle<int>(118, 52, 260, 54), juce::Justification::centredLeft, false);

    g.setColour(kRed);
    g.setFont(mono(11.0f, 0.2f));
    g.drawText("PITCH LAYER // " + jpWired() + " // " + jpKira(),
               juce::Rectangle<int>(120, 106, 360, 16), juce::Justification::centredLeft, false);

    // estado de enlace (parpadea)
    const bool on = std::fmod(t, 1.2f) < 0.8f;
    g.setFont(mono(11.0f, 0.15f));
    g.setColour(kText.withAlpha(0.9f));
    g.drawText("NODE 07", juce::Rectangle<int>((int) w - 250, 54, 222, 16), juce::Justification::centredRight, false);
    g.setColour(on ? kRed : kRed.withAlpha(0.25f));
    g.fillEllipse(w - 232.0f, 77.0f, 7.0f, 7.0f);
    g.setColour(kDim);
    g.drawText("LINK ESTABLISHED " + jpLink(), juce::Rectangle<int>((int) w - 250, 73, 222, 16),
               juce::Justification::centredRight, false);

    // --- monitor de afinacion ---
    const juce::Rectangle<float> trace(24.0f, 138.0f, w - 48.0f, 84.0f);
    drawTrace(g, trace);
    g.setFont(mono(10.0f, 0.15f));
    g.setColour(kDim);
    g.drawText("01 // PITCH TRACE  " + jpSignal(), juce::Rectangle<int>(30, 124, 260, 13),
               juce::Justification::centredLeft, false);
    g.setColour(kCyan);
    g.drawText("IN  " + noteLabel(lastIn), juce::Rectangle<int>((int) w - 270, 124, 120, 13),
               juce::Justification::centredRight, false);
    g.setColour(kRed);
    g.drawText("OUT " + noteLabel(lastOut), juce::Rectangle<int>((int) w - 150, 124, 126, 13),
               juce::Justification::centredRight, false);

    // --- perillas: etiquetas ---
    g.setFont(mono(10.0f, 0.15f));
    for (int i = 0; i < 6; ++i)
    {
        const int x = kCellX + i * kCellStep;
        g.setColour(kDim);
        g.drawText("P-0" + juce::String(i + 1), juce::Rectangle<int>(x, 234, kCellW, 12),
                   juce::Justification::centred, false);
        g.setColour(kText);
        g.setFont(mono(12.0f, 0.3f, true));
        g.drawText(kLabels[i], juce::Rectangle<int>(x, 376, kCellW, 16), juce::Justification::centred, false);
        g.setFont(mono(10.0f, 0.15f));
        bracket(g, juce::Rectangle<float>((float) x, 232.0f, (float) kCellW, 164.0f), 6.0f, kLine);
    }

    // --- fila inferior ---
    g.setFont(mono(10.0f, 0.15f));
    g.setColour(kDim);
    g.drawText("A4", juce::Rectangle<int>(24, 410, 24, 20), juce::Justification::centredLeft, false);
    g.drawText("KEY", juce::Rectangle<int>(352, 410, 34, 20), juce::Justification::centredLeft, false);
    g.drawText("SCALE", juce::Rectangle<int>(472, 410, 44, 20), juce::Justification::centredLeft, false);

    // línea y ticker
    g.setColour(kLine);
    g.drawLine(24.0f, 444.0f, w - 24.0f, 444.0f, 1.0f);
    drawTicker(g, juce::Rectangle<float>(24.0f, 448.0f, w - 150.0f, 16.0f));
    g.setColour(kDim);
    g.setFont(mono(9.0f, 0.1f));
    g.drawText("KIRA v1.4", juce::Rectangle<int>((int) w - 124, 448, 100, 16), juce::Justification::centredRight, false);
}

void KIRAEditor::paintOverChildren(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    // scanlines y grano (el grano se desplaza al azar en cada fotograma)
    g.setTiledImageFill(scanImg, 0, 0, 1.0f);
    g.fillRect(bounds);
    g.setTiledImageFill(noiseImg, rng.nextInt(128), rng.nextInt(128), 1.0f);
    g.fillRect(bounds);

    // viñeta de tubo CRT
    juce::ColourGradient vig(juce::Colours::transparentBlack, (float) kW * 0.5f, (float) kH * 0.5f,
                             juce::Colour(0xd0000000), 0.0f, 0.0f, true);
    g.setGradientFill(vig);
    g.fillRect(bounds);

    // destello/ruptura de imagen ocasional
    if (glitchFrames > 0)
    {
        for (int i = 0; i < 3; ++i)
        {
            const float y = (float) rng.nextInt(kH - 4);
            g.setColour((i == 0 ? kRed : kText).withAlpha(0.16f + 0.18f * rng.nextFloat()));
            g.fillRect(0.0f, y, (float) kW, 1.0f + (float) rng.nextInt(3));
        }
    }
}

void KIRAEditor::resized()
{
    juce::Slider* sliders[] = { &bend, &tune, &speed, &vibe, &smile, &mix };
    for (int i = 0; i < 6; ++i)
        sliders[i]->setBounds(kCellX + i * kCellStep, kKnobY, kCellW, 128);
    refA4.setBounds(50, 410, 270, 22);
    key.setBounds(386, 410, 74, 22);
    scale.setBounds(518, 410, 130, 22);
}
