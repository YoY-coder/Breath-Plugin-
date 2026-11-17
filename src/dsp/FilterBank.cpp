#include "FilterBank.h"
#include <complex>

namespace breath
{
    namespace
    {
        // Largeur de chaque cloche : 2 intervalles de bande (≈ 0,31 octave).
        float bellQ()
        {
            const float bwOct = 2.0f * std::log2 (kMaxFreq / kMinFreq) / (float) kNumBands;
            const float r = std::pow (2.0f, bwOct);
            return std::sqrt (r) / (r - 1.0f);
        }

        float bellResponseDb (float cosw0, float alpha, float gainDb, float w)
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float b0 = 1 + alpha * A, b1 = -2 * cosw0, b2 = 1 - alpha * A;
            const float a0 = 1 + alpha / A, a1 = -2 * cosw0, a2 = 1 - alpha / A;
            const std::complex<float> z1 = std::polar (1.0f, -w), z2 = std::polar (1.0f, -2.0f * w);
            const auto h = (b0 + b1 * z1 + b2 * z2) / (a0 + a1 * z1 + a2 * z2);
            return 20.0f * std::log10 (std::abs (h));
        }
    }

    void FilterBank::prepare (double sampleRate)
    {
        const float q = bellQ();
        const float sr = (float) sampleRate;

        for (int b = 0; b < kNumBands; ++b)
        {
            const float fc = bandCentre (b);
            usable[(size_t) b] = fc < 0.45f * sr;
            const float w0 = juce::MathConstants<float>::twoPi * juce::jmin (fc, 0.45f * sr) / sr;
            cosw[(size_t) b] = std::cos (w0);
            alpha[(size_t) b] = std::sin (w0) / (2.0f * q);
        }

        // Matrice de recouvrement : réponse (dB) de la cloche j (1 dB) au centre de la bande b.
        for (int b = 0; b < kNumBands; ++b)
        {
            const float wb = juce::MathConstants<float>::twoPi * juce::jmin (bandCentre (b), 0.49f * sr) / sr;
            for (int d = -kSpread; d <= kSpread; ++d)
            {
                const int j = b + d;
                float v = 0.0f;
                if (j >= 0 && j < kNumBands && usable[(size_t) j])
                    v = -bellResponseDb (cosw[(size_t) j], alpha[(size_t) j], -1.0f, wb);
                overlap[(size_t) b][(size_t) (d + kSpread)] = v;
            }
        }

        overlapSum = 0.0f;
        for (auto v : overlap[kNumBands / 2])
            overlapSum += v;
        overlapSum = juce::jmax (overlapSum, 0.1f);

        reset();
    }

    void FilterBank::reset() noexcept
    {
        for (auto& ch : state)
            for (auto& s : ch)
                s = {};
        for (auto& c : coeffs)
            c = {};
        isActive.fill (false);
        numActive = 0;
    }

    void FilterBank::setReductions (const float* target) noexcept
    {
        // Gains individuels tels que la cascade reproduise la courbe voulue
        // (quelques itérations de correction, gains toujours ≥ 0 dB de réduction).
        std::array<float, kNumBands> g {};
        for (int b = 0; b < kNumBands; ++b)
            g[(size_t) b] = target[b] / overlapSum;

        for (int it = 0; it < 3; ++it)
        {
            std::array<float, kNumBands> next {};
            for (int b = 0; b < kNumBands; ++b)
            {
                float approx = 0.0f;
                for (int d = -kSpread; d <= kSpread; ++d)
                {
                    const int j = b + d;
                    if (j >= 0 && j < kNumBands)
                        approx += overlap[(size_t) b][(size_t) (d + kSpread)] * g[(size_t) j];
                }
                next[(size_t) b] = juce::jmax (0.0f, g[(size_t) b] + (target[b] - approx) / overlapSum);
            }
            g = next;
        }

        numActive = 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const bool on = usable[(size_t) b] && g[(size_t) b] > 1.0e-4f;

            if (! on)
            {
                if (isActive[(size_t) b])
                    for (auto& ch : state)
                        ch[(size_t) b] = {};
                isActive[(size_t) b] = false;
                continue;
            }

            const float A = std::pow (10.0f, -g[(size_t) b] / 40.0f);
            const float al = alpha[(size_t) b], cw = cosw[(size_t) b];
            const float a0 = 1.0f + al / A;
            auto& c = coeffs[(size_t) b];
            c.b0 = (1.0f + al * A) / a0;
            c.b1 = (-2.0f * cw) / a0;
            c.b2 = (1.0f - al * A) / a0;
            c.a1 = (-2.0f * cw) / a0;
            c.a2 = (1.0f - al / A) / a0;

            isActive[(size_t) b] = true;
            activeList[(size_t) numActive++] = b;
        }
    }
}
