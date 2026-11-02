#include "PluginProcessor.h"
#include "PluginEditor.h"

KIRAProcessor::KIRAProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "PARAMS", createParams())
{
    pBend  = params.getRawParameterValue("bend");
    pTune  = params.getRawParameterValue("tune");
    pSpeed = params.getRawParameterValue("speed");
    pVibe  = params.getRawParameterValue("vibe");
    pSmile = params.getRawParameterValue("smile");
    pMix   = params.getRawParameterValue("mix");
    pRef   = params.getRawParameterValue("ref");
    pRoot  = params.getRawParameterValue("root");
    pScale = params.getRawParameterValue("scale");
}

juce::AudioProcessorValueTreeState::ParameterLayout KIRAProcessor::createParams()
{
    using P = juce::AudioParameterFloat;
    using ID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<P>(ID { "bend", 1 },  "BEND",  juce::NormalisableRange<float>(-12.0f, 12.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<P>(ID { "tune", 1 },  "TUNE",  juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f));
    layout.add(std::make_unique<P>(ID { "speed", 1 }, "SPEED", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 70.0f));
    layout.add(std::make_unique<P>(ID { "vibe", 1 },  "VIBE",  juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 15.0f));
    layout.add(std::make_unique<P>(ID { "smile", 1 }, "SMILE", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<P>(ID { "mix", 1 },   "MIX",   juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    layout.add(std::make_unique<P>(ID { "ref", 1 },   "A4",    juce::NormalisableRange<float>(430.0f, 450.0f, 0.1f), 440.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { "root", 1 }, "KEY",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { "scale", 1 }, "SCALE",
        juce::StringArray { "Chromatic", "Major", "Minor", "Major Pent", "Minor Pent", "Harm Minor" }, 2));
    return layout;
}

void KIRAProcessor::prepareToPlay(double sr, int samplesPerBlock)
{
    sampleRate = sr;
    detector.prepare(sr);
    for (auto& s : mainShift) s.prepare(sr);
    for (auto& s : riseShift) s.prepare(sr);

    latencySamples = mainShift[0].latencySamples();
    setLatencySamples(latencySamples);

    drySize = latencySamples + 1;
    riseSize = (int) (sr * 0.8) + 8;
    haloDelay = juce::jlimit(1, riseSize / 4, (int) (sr * 0.16));
    for (int ch = 0; ch < 2; ++ch)
        dryBuf[(size_t) ch].assign((size_t) drySize, 0.0f);
    for (auto& d : riseDelay) d.assign((size_t) riseSize, 0.0f);
    dryPos = 0; risePos = 0;

    for (int ch = 0; ch < 2; ++ch)
    {
        shelf[(size_t) ch] = kira::Biquad(); peak[(size_t) ch] = kira::Biquad();
        exHp1[(size_t) ch].setHighpass(sr, 4000.0); exHp1[(size_t) ch].z1 = exHp1[(size_t) ch].z2 = 0.0f;
        exHp2[(size_t) ch].setHighpass(sr, 4000.0); exHp2[(size_t) ch].z1 = exHp2[(size_t) ch].z2 = 0.0f;
        haloHp[(size_t) ch].setHighpass(sr, 350.0); haloHp[(size_t) ch].z1 = haloHp[(size_t) ch].z2 = 0.0f;
        haloLp[(size_t) ch].setLowpass(sr, 9000.0); haloLp[(size_t) ch].z1 = haloLp[(size_t) ch].z2 = 0.0f;
    }

    wetBuf.setSize(2, juce::jmax(samplesPerBlock, 1), false, false, true);
    wetBuf.clear();
    reverb.setSampleRate(sr);
    reverb.reset();

    bendSm = 0.0f; corrSm = 0.0f; targetCorr = 0.0f; noteValid = false; unvoicedSamples = 0; uiIn.store(-1.0f); uiOut.store(-1.0f);
    smileSm = pSmile->load() * 0.01f;
    mixSm = pMix->load() * 0.01f;
    vibPhase = 0.0f; drift = 0.0f; driftTarget = 0.0f; driftCounter = 0;
}

void KIRAProcessor::releaseResources() {}

bool KIRAProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
            || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
}

void KIRAProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int chans = juce::jmin(buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    if (chans < 1 || n < 1) return;
    if (wetBuf.getNumSamples() < n) wetBuf.setSize(2, n, false, false, true);

    const float sr = (float) sampleRate;
    const float bendTarget = pBend->load();
    const float tune  = pTune->load() * 0.01f;
    const float speed = pSpeed->load() * 0.01f;
    const float vibe  = pVibe->load() * 0.01f;
    const float smileTarget = pSmile->load() * 0.01f;
    const float mixTarget = pMix->load() * 0.01f;
    const float refOffset = 12.0f * std::log2(juce::jmax(pRef->load(), 400.0f) / 440.0f); // desvio de A4
    const int root  = (int) pRoot->load();
    const int scale = (int) pScale->load();

    // SPEED: tiempo de retune de ~300 ms (suave) a ~4 ms (efecto duro)
    const float tau = 0.3f * std::pow(0.004f / 0.3f, speed);
    const float corrCoef  = 1.0f - std::exp(-1.0f / (tau * sr));
    const float releaseCoef = 1.0f - std::exp(-1.0f / (0.08f * sr)); // al acabar la nota
    const int holdSamples = (int) (0.05f * sr);                      // mantiene la correccion en consonantes
    const float bendCoef  = 1.0f - std::exp(-1.0f / (0.012f * sr));
    const float smileCoef = 1.0f - std::exp(-1.0f / (0.030f * sr));
    const float driftCoef = 1.0f - std::exp(-1.0f / (0.12f * sr));
    const float vibInc = 5.3f / sr;

    for (int i = 0; i < n; ++i)
    {
        const float l = buffer.getSample(0, i);
        const float r = chans > 1 ? buffer.getSample(1, i) : l;
        const float monoIn = chans > 1 ? 0.5f * (l + r) : l;

        // --- deteccion de tono + objetivo de correccion ---
        if (detector.push(monoIn))
        {
            if (detector.isVoiced())
            {
                const float det = detector.midiNote() - refOffset;
                lastDet = det;
                // Histeresis de nota: el vibrato natural no debe saltar entre notas vecinas.
                float target = kira::quantiseMidi(det, root, scale);
                if (noteValid && target != currentNote
                    && kira::quantiseMidi(currentNote, root, scale) == currentNote
                    && std::abs(det - target) + 0.30f >= std::abs(det - currentNote))
                    target = currentNote;
                currentNote = target;
                noteValid = true;
                targetCorr = (target - det) * tune;
                const float freq = 440.0f * std::exp2((detector.midiNote() - 69.0f) / 12.0f);
                const float period = sr / juce::jmax(freq, 1.0f);
                for (auto& s : mainShift) s.setPeriod(period);
                for (auto& s : riseShift) s.setPeriod(period);
            }
            else
            {
                for (auto& s : mainShift) s.setPeriod(0.0f);
                for (auto& s : riseShift) s.setPeriod(0.0f);
            }
        }

        // Sin tono (consonantes, respiraciones, fin de nota): se mantiene la
        // correccion un instante y despues se libera suavemente.
        if (detector.isVoiced()) unvoicedSamples = 0; else ++unvoicedSamples;
        const bool released = unvoicedSamples > holdSamples;
        if (released) { targetCorr = 0.0f; noteValid = false; }

        // --- suavizados ---
        bendSm  += (bendTarget - bendSm) * bendCoef;
        corrSm  += (targetCorr - corrSm) * (released ? releaseCoef : corrCoef);
        smileSm += (smileTarget - smileSm) * smileCoef;

        // --- VIBE: vibrato real (modulacion de afinacion) + deriva lenta ---
        vibPhase += vibInc;
        if (vibPhase >= 1.0f) vibPhase -= 1.0f;
        if (--driftCounter <= 0) { driftCounter = (int) (0.05f * sr); driftTarget = rng.nextFloat() * 2.0f - 1.0f; }
        drift += (driftTarget - drift) * driftCoef;
        const float vibSemis = vibe * (0.30f * std::sin(2.0f * kira::kPi * vibPhase) + 0.20f * drift);

        const float semis = juce::jlimit(-24.0f, 24.0f, bendSm + corrSm + vibSemis);
        lastSemis = semis;
        const float ratio = std::exp2(semis / 12.0f);

        // --- SMILE por etapas ---
        //   0-50 %  : voz mas abierta y brillante (shelf de aire + presencia + excitador armonico)
        //   12-100 %: halo de voces ascendentes (quinta, octava, octava+quinta) con eco
        //   35-100 %: floracion de reverb
        const float smile = smileSm;
        const float bright = juce::jlimit(0.0f, 1.0f, smile / 0.5f);
        if ((i & 31) == 0)
        {
            kira::Biquad tmp;
            tmp.setHighShelf(sampleRate, 7000.0, 0.7071, 5.0 * bright);
            for (auto& b : shelf) b.copyCoeffsFrom(tmp);
            tmp.setPeak(sampleRate, 3200.0, 0.9, 2.5 * bright);
            for (auto& b : peak) b.copyCoeffsFrom(tmp);
        }
        const float exGain = 0.55f * bright;
        const float drive = 1.0f + smile * 1.5f;
        const float satGain = 1.0f / std::pow(drive, 0.5f);
        const float satAmt = juce::jmin(1.0f, smile * 2.0f);
        const float makeup = 1.0f / (1.0f + 0.30f * smile);

        // halo: 3 voces fijas por encima de la voz corregida, cada una con su eco
        static const float riseRatio[3] = { 1.4983071f, 2.0f, 2.9966142f }; // +7, +12, +19 semitonos
        static const float riseBase[3]  = { 0.55f, 0.42f, 0.32f };
        float hv[3];
        for (int k = 0; k < 3; ++k)
        {
            const float v = riseShift[(size_t) k].process(monoIn, juce::jmin(4.0f, ratio * riseRatio[k]));
            riseDelay[(size_t) k][(size_t) risePos] = v;
            int idx = risePos - (k + 1) * haloDelay;
            if (idx < 0) idx += riseSize;
            const float gk = riseBase[k] * kira::smoothstep01((smile - 0.12f - 0.22f * (float) k) / 0.30f);
            hv[k] = riseDelay[(size_t) k][(size_t) idx] * gk;
        }
        if (++risePos >= riseSize) risePos = 0;
        float haloL = hv[0] + 0.35f * hv[1] + 0.70f * hv[2];
        float haloR = 0.35f * hv[0] + hv[1] + 0.70f * hv[2];
        haloL = haloLp[0].process(haloHp[0].process(haloL));
        haloR = haloLp[1].process(haloHp[1].process(haloR));
        if (chans == 1) { haloL = 0.5f * (haloL + haloR); haloR = haloL; }

        dryPos = (dryPos + 1) % drySize;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float in = ch == 0 ? l : r;
            dryBuf[(size_t) ch][(size_t) dryPos] = in;

            const float body = mainShift[(size_t) ch].process(in, ratio);

            // brillo: aire + presencia, mas armonicos generados en las altas frecuencias
            float v = peak[(size_t) ch].process(shelf[(size_t) ch].process(body));
            const float hf = exHp1[(size_t) ch].process(body);
            v += exHp2[(size_t) ch].process(std::tanh(4.0f * hf) * 0.25f) * exGain;

            // saturacion suave del cuerpo
            v += (std::tanh(v * drive) * satGain - v) * satAmt;

            v = (v + (ch == 0 ? haloL : haloR)) * makeup;
            wetBuf.setSample(ch, i, v);

            // la senal seca se retrasa para alinearla con la latencia del efecto
            buffer.setSample(ch, i, dryBuf[(size_t) ch][(size_t) ((dryPos + 1) % drySize)]);
        }
    }

    // --- datos para el monitor de la interfaz ---
    const bool tracking = detector.isVoiced() && unvoicedSamples <= holdSamples;
    uiIn.store(tracking ? lastDet : -1.0f);
    uiOut.store(tracking ? lastDet + lastSemis : -1.0f);

    // --- reverb de SMILE ---
    const float bloom = juce::jlimit(0.0f, 1.0f, (smileTarget - 0.35f) / 0.65f);
    juce::Reverb::Parameters rp;
    rp.roomSize = 0.60f + 0.30f * bloom;
    rp.damping = 0.50f;
    rp.wetLevel = bloom * 0.24f;
    rp.dryLevel = 0.5f;
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters(rp);
    if (chans > 1) reverb.processStereo(wetBuf.getWritePointer(0), wetBuf.getWritePointer(1), n);
    else           reverb.processMono(wetBuf.getWritePointer(0), n);

    // --- mezcla seca/humeda con rampa para evitar clics ---
    const float m0 = mixSm;
    mixSm += (mixTarget - mixSm) * (1.0f - std::exp(-(float) n / (0.03f * sr)));
    for (int ch = 0; ch < chans; ++ch)
    {
        float* out = buffer.getWritePointer(ch);
        const float* wet = wetBuf.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
        {
            const float m = m0 + (mixSm - m0) * (float) (i + 1) / (float) n;
            out[i] = out[i] * (1.0f - m) + wet[i] * m;
        }
    }
}

juce::AudioProcessorEditor* KIRAProcessor::createEditor() { return new KIRAEditor(*this); }
bool KIRAProcessor::hasEditor() const { return true; }
const juce::String KIRAProcessor::getName() const { return JucePlugin_Name; }
bool KIRAProcessor::acceptsMidi() const { return false; }
bool KIRAProcessor::producesMidi() const { return false; }
bool KIRAProcessor::isMidiEffect() const { return false; }
double KIRAProcessor::getTailLengthSeconds() const { return 4.0; }
int KIRAProcessor::getNumPrograms() { return 1; }
int KIRAProcessor::getCurrentProgram() { return 0; }
void KIRAProcessor::setCurrentProgram(int) {}
const juce::String KIRAProcessor::getProgramName(int) { return {}; }
void KIRAProcessor::changeProgramName(int, const juce::String&) {}

void KIRAProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void KIRAProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml != nullptr && xml->hasTagName(params.state.getType()))
        params.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KIRAProcessor(); }
