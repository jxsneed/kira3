#pragma once
// DSP puro (sin dependencias de JUCE) para poder probarlo de forma aislada.
#include <algorithm>
#include <cmath>
#include <vector>

namespace kira
{
static constexpr float kPi = 3.14159265358979323846f;

inline float smoothstep01(float x)
{
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return x * x * (3.0f - 2.0f * x);
}

// ---------------------------------------------------------------------------
// Cuantizador de escala. Devuelve la nota MIDI (entera) permitida mas cercana.
// ---------------------------------------------------------------------------
inline float quantiseMidi(float midi, int root, int scale)
{
    // bit i = grado cromatico i permitido (relativo a la tonica)
    static const unsigned masks[] = { 0xFFFu,  // Chromatic
                                      0xAB5u,  // Major
                                      0x5ADu,  // Minor
                                      0x295u,  // Major pentatonic
                                      0x4A9u,  // Minor pentatonic
                                      0x9ADu };// Harmonic minor
    if (scale < 0 || scale > 5) return midi; // Chromatic (0) = semitono mas cercano
    const unsigned mask = masks[scale];
    const int nearest = (int) std::lround(midi);
    float best = (float) nearest, bestDist = 1.0e9f;
    for (int cand = nearest - 3; cand <= nearest + 3; ++cand)
    {
        const int pc = (((cand - root) % 12) + 12) % 12;
        if (((mask >> pc) & 1u) == 0u) continue;
        const float d = std::abs(midi - (float) cand);
        if (d < bestDist) { bestDist = d; best = (float) cand; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Filtro biquad (formulas RBJ, forma TDF-II). Por defecto es transparente.
// ---------------------------------------------------------------------------
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f, z1 = 0.0f, z2 = 0.0f;

    float process(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void copyCoeffsFrom(const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    void setHighpass(double sr, double f, double q = 0.7071)
    {
        double c, a; trig(sr, f, q, c, a);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void setLowpass(double sr, double f, double q = 0.7071)
    {
        double c, a; trig(sr, f, q, c, a);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void setPeak(double sr, double f, double q, double gainDb)
    {
        double c, a; trig(sr, f, q, c, a);
        const double A = std::pow(10.0, gainDb / 40.0);
        set(1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
    }
    void setHighShelf(double sr, double f, double q, double gainDb)
    {
        double c, a; trig(sr, f, q, c, a);
        const double A = std::pow(10.0, gainDb / 40.0), t = 2.0 * std::sqrt(A) * a;
        set(A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
            (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }

private:
    static void trig(double sr, double f, double q, double& c, double& a)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * (std::min)(f, sr * 0.45) / sr;
        c = std::cos(w0);
        a = std::sin(w0) / (2.0 * q);
    }
    void set(double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
        a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
    }
};

// ---------------------------------------------------------------------------
// Detector de tono YIN (monofonico), trabaja a ~16 kHz para ser barato.
// ---------------------------------------------------------------------------
class PitchDetector
{
public:
    void prepare(double sr)
    {
        decim = (std::max)(1, (int) std::lround(sr / 16000.0));
        fs = sr / (double) decim;
        ring.assign((size_t) kRing, 0.0f);
        frame.assign((size_t) kFrame, 0.0f);
        cmnd.assign((size_t) kFrame / 2, 1.0f);
        minTau = (std::max)(2, (int) (fs / 1000.0));
        maxTau = (std::min)(kFrame / 2 - 1, (int) (fs / 70.0));
        wr = 0; acc = 0.0f; accCount = 0; hopCount = 0;
        voiced = false; unvoicedRun = 99; midi = 60.0f; pendingCount = 0;
        hist[0] = hist[1] = hist[2] = 60.0f; histPos = 0;
    }

    // true cuando hay una estimacion nueva (cada ~16 ms)
    bool push(float x)
    {
        acc += x;
        if (++accCount < decim) return false;
        ring[(size_t) wr] = acc / (float) decim;
        acc = 0.0f; accCount = 0;
        wr = (wr + 1) & (kRing - 1);
        if (++hopCount < kHop) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    bool isVoiced() const { return voiced; }
    float midiNote() const { return midi; }

private:
    static constexpr int kRing = 2048, kFrame = 768, kHop = 128;

    void analyse()
    {
        const int half = kFrame / 2;
        float energy = 0.0f;
        for (int i = 0; i < kFrame; ++i)
        {
            const float v = ring[(size_t) ((wr - kFrame + i + kRing) & (kRing - 1))];
            frame[(size_t) i] = v;
            energy += v * v;
        }
        const float rms = std::sqrt(energy / (float) kFrame);
        if (rms < 0.004f) { markUnvoiced(); return; }

        cmnd[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < half; ++j)
            {
                const float diff = frame[(size_t) j] - frame[(size_t) (j + tau)];
                d += diff * diff;
            }
            running += d;
            cmnd[(size_t) tau] = running > 1.0e-12f ? d * (float) tau / running : 1.0f;
        }

        int est = -1;
        for (int tau = minTau; tau <= maxTau; ++tau)
        {
            if (cmnd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 <= maxTau && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau]) ++tau;
                est = tau;
                break;
            }
        }
        if (est < 0)
        {
            int m = minTau;
            for (int tau = minTau; tau <= maxTau; ++tau)
                if (cmnd[(size_t) tau] < cmnd[(size_t) m]) m = tau;
            if (cmnd[(size_t) m] < 0.30f) est = m;
        }
        if (est < 0) { markUnvoiced(); return; }

        float shift = 0.0f;
        if (est > minTau && est < maxTau)
        {
            const float s0 = cmnd[(size_t) (est - 1)], s1 = cmnd[(size_t) est], s2 = cmnd[(size_t) (est + 1)];
            const float den = s0 + s2 - 2.0f * s1;
            if (std::abs(den) > 1.0e-9f) shift = (std::max)(-1.0f, (std::min)(1.0f, 0.5f * (s0 - s2) / den));
        }
        const float freq = (float) (fs / ((double) est + (double) shift));
        const float m = 69.0f + 12.0f * std::log2(freq / 440.0f);

        // Un salto grande y aislado casi siempre es un error de octava: se exige
        // que el nuevo valor se repita en dos analisis seguidos antes de aceptarlo.
        if (voiced && std::abs(m - midi) > 5.0f)
        {
            if (pendingCount > 0 && std::abs(m - pending) < 1.5f)
            {
                pendingCount = 0;
                hist[0] = hist[1] = hist[2] = m;
            }
            else { pending = m; pendingCount = 1; return; }
        }
        else pendingCount = 0;

        if (!voiced) hist[0] = hist[1] = hist[2] = m;
        else { hist[(size_t) histPos] = m; }
        histPos = (histPos + 1) % 3;
        const float a = hist[0], b = hist[1], c = hist[2];
        midi = (std::max)((std::min)(a, b), (std::min)((std::max)(a, b), c)); // mediana de 3
        voiced = true;
        unvoicedRun = 0;
    }

    void markUnvoiced()
    {
        if (++unvoicedRun >= 2) voiced = false;
    }

    std::vector<float> ring, frame, cmnd;
    double fs = 16000.0;
    float acc = 0.0f, midi = 60.0f, hist[3] = { 60.0f, 60.0f, 60.0f };
    int decim = 1, wr = 0, accCount = 0, hopCount = 0, minTau = 16, maxTau = 200;
    int histPos = 0, unvoicedRun = 99, pendingCount = 0;
    float pending = 0.0f;
    bool voiced = false;
};

// ---------------------------------------------------------------------------
// Pitch shifter de dos cabezales con ventana Hann (retardo variable) y saltos
// sincronizados con el periodo detectado (la ventana mide 2*M*periodo), de modo
// que ambos cabezales estan en fase y no hay sesgo de afinacion.
// Con ratio ~ 1 se cruza a un retardo fijo para evitar el filtro peine.
// La latencia del camino fijo es constante: latencySamples().
// ---------------------------------------------------------------------------
class PitchShifter
{
public:
    void prepare(double sr)
    {
        win = (float) (sr * 0.030);
        maxWin = (float) (sr * 0.048);
        minDelay = (float) (sr * 0.022); // alinea el audio con el retardo del detector
        latency = (int) std::lround(minDelay + win * 0.5f);
        size = (int) std::ceil(minDelay + maxWin) + 128;
        buf.assign((size_t) size, 0.0f);
        w = 0; period = 0.0f;
        len = win;
        d0 = minDelay;
    }

    int latencySamples() const { return latency; }

    // periodo de la senal de entrada en muestras (0 = sin tono / desconocido)
    void setPeriod(float samples) { period = samples; }

    float process(float in, float ratio)
    {
        buf[(size_t) w] = in;
        const float delta = 1.0f - ratio; // cambio de retardo por muestra

        d0 += delta;
        if (delta > 0.0f && d0 >= minDelay + len)
        {
            len = newWindow();
            d0 -= len;
        }
        else if (delta < 0.0f && d0 < minDelay)
        {
            len = newWindow();
            d0 += len;
        }
        d0 = (std::max)(2.0f, (std::min)(d0, minDelay + maxWin));

        // El segundo cabezal va siempre medio ciclo de ventana (= M periodos) detras.
        float d1 = d0 + 0.5f * len;
        if (d1 >= minDelay + len) d1 -= len;

        float p0 = (d0 - minDelay) / len;
        float p1 = (d1 - minDelay) / len;
        p0 = p0 < 0.0f ? 0.0f : (p0 > 1.0f ? 1.0f : p0);
        p1 = p1 < 0.0f ? 0.0f : (p1 > 1.0f ? 1.0f : p1);
        const float w0 = 0.5f - 0.5f * std::cos(2.0f * kPi * p0);
        const float w1 = 0.5f - 0.5f * std::cos(2.0f * kPi * p1);
        const float wsum = w0 + w1;

        const float fixedTap = read((float) latency);
        const float shifted = wsum > 1.0e-3f ? (read(d0) * w0 + read(d1) * w1) / wsum : fixedTap;
        const float g = smoothstep01((std::abs(ratio - 1.0f) - 0.0006f) / 0.004f);
        const float out = fixedTap + (shifted - fixedTap) * g;
        if (++w >= size) w = 0;
        return out;
    }

private:
    float newWindow() const
    {
        if (period > 20.0f)
        {
            const float m = (std::max)(1.0f, std::floor(win / (2.0f * period)));
            return (std::min)(2.0f * m * period, maxWin);
        }
        return win;
    }

    float read(float delay) const
    {
        float pos = (float) w - delay;
        if (pos < 0.0f) pos += (float) size;
        int a = (int) pos;
        const float f = pos - (float) a;
        if (a >= size) a -= size;
        int b = a + 1; if (b >= size) b = 0;
        return buf[(size_t) a] * (1.0f - f) + buf[(size_t) b] * f;
    }

    std::vector<float> buf;
    int size = 1, w = 0, latency = 0;
    float win = 1440.0f, maxWin = 2304.0f, minDelay = 64.0f, period = 0.0f;
    float d0 = 64.0f, len = 1440.0f;
};
} // namespace kira
