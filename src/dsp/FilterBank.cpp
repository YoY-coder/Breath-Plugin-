#include "FilterBank.h"
#include <complex>

namespace breath
{
    namespace
    {
        // Largeur de chaque cloche : 2 intervalles de bande (≈ 0,31 octave).
        float bellQ (int nb)
        {
            const float bwOct = 2.0f * std::log2 (kMaxFreq / kMinFreq) / (float) nb;
            const float r = std::pow (2.0f, bwOct);
            return std::sqrt (r) / (r - 1.0f);
        }

        // Réponse (dB) d'une cloche « dénominateur fixe » : H(s) = (s² + s·k·A² + 1) / (s² + s·k + 1),
        // avec prédistorsion bilinéaire autour de la fréquence centrale (identique au filtre réel).
        float bellResponseDb (float gTanCentre, float k, float reductionDb, float w)
        {
            const float A2 = std::pow (10.0f, -reductionDb / 20.0f);
            const float omega = std::tan (0.5f * w) / gTanCentre;
            const std::complex<float> sj (0.0f, omega);
            const auto h = (sj * sj + sj * (k * A2) + 1.0f) / (sj * sj + sj * k + 1.0f);
            return 20.0f * std::log10 (std::abs (h));
        }
    }

    void FilterBank::prepare (double sampleRate, int numBandsToUse)
    {
        nb = juce::jlimit (8, kNumBands, numBandsToUse);
        q = bellQ (nb);
        // Double lissage ≈ 1 ms par étage.
        smoothK = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
        const float sr = (float) sampleRate;

        for (int b = 0; b < nb; ++b)
        {
            const float fc = bandCentreN (b, nb);
            usable[(size_t) b] = fc < 0.45f * sr;
            const float w0 = juce::MathConstants<float>::twoPi * juce::jmin (fc, 0.45f * sr) / sr;
            cosw[(size_t) b] = std::cos (w0);
            const float g = std::tan (0.5f * w0);
            gTan[(size_t) b] = g;
            const float k = 1.0f / q;
            kDamp[(size_t) b] = k;
            auto& c = fixed[(size_t) b];
            c.a1 = 1.0f / (1.0f + g * (g + k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
            alpha[(size_t) b] = std::sin (w0) / (2.0f * q);
        }

        // Matrice de recouvrement : réponse (dB) de la cloche j (1 dB) au centre de la bande b.
        for (int b = 0; b < nb; ++b)
        {
            const float wb = juce::MathConstants<float>::twoPi * juce::jmin (bandCentreN (b, nb), 0.49f * sr) / sr;
            for (int d = -kSpread; d <= kSpread; ++d)
            {
                const int j = b + d;
                float v = 0.0f;
                if (j >= 0 && j < nb && usable[(size_t) j])
                    v = -bellResponseDb (gTan[(size_t) j], kDamp[(size_t) j], 1.0f, wb);
                overlap[(size_t) b][(size_t) (d + kSpread)] = v;
            }
        }

        overlapSum = 0.0f;
        for (auto v : overlap[(size_t) (nb / 2)])
            overlapSum += v;
        overlapSum = juce::jmax (overlapSum, 0.1f);

        reset();
    }

    void FilterBank::reset() noexcept
    {
        for (auto& ch : state)
            for (auto& st : ch)
                st = {};
        m1.fill (0.0f);
        m1Mid.fill (0.0f);
        m1Goal.fill (0.0f);
        isActive.fill (false);
        numActive = 0;
    }

    float FilterBank::m1For (int b, float red) const noexcept
    {
        // Gain au centre = A² = 10^(−réduction/20).
        const float A2 = std::pow (10.0f, -red / 20.0f);
        return kDamp[(size_t) b] * (A2 - 1.0f);
    }

    void FilterBank::setReductions (const float* target) noexcept
    {
        // Gains individuels tels que la cascade reproduise la courbe voulue
        // (quelques itérations de correction, gains toujours ≥ 0 dB de réduction).
        std::array<float, kNumBands> g {};
        for (int b = 0; b < nb; ++b)
            g[(size_t) b] = target[b] / overlapSum;

        for (int it = 0; it < 3; ++it)
        {
            std::array<float, kNumBands> next {};
            for (int b = 0; b < nb; ++b)
            {
                float approx = 0.0f;
                for (int d = -kSpread; d <= kSpread; ++d)
                {
                    const int j = b + d;
                    if (j >= 0 && j < nb)
                        approx += overlap[(size_t) b][(size_t) (d + kSpread)] * g[(size_t) j];
                }
                next[(size_t) b] = juce::jmax (0.0f, g[(size_t) b] + (target[b] - approx) / overlapSum);
            }
            g = next;
        }

        // Les coefficients rejoignent leur cible en douceur (voir advance()).
        numActive = 0;
        for (int b = 0; b < nb; ++b)
        {
            const bool want = usable[(size_t) b] && g[(size_t) b] > 0.005f;   // < 0,005 dB : inaudible
            const bool wasActive = isActive[(size_t) b];

            if (! want && ! wasActive)
                continue;

            if (! wasActive)
            {
                // Activation : on part de la cloche neutre (m1 = 0), avec des états à zéro.
                m1[(size_t) b] = m1Mid[(size_t) b] = 0.0f;
                for (auto& ch : state) ch[(size_t) b] = {};
            }
            else if (! want && std::abs (m1[(size_t) b]) < 1.0e-7f && std::abs (m1Mid[(size_t) b]) < 1.0e-7f)
            {
                // Désactivation une fois revenue (quasiment exactement) à la cloche neutre.
                isActive[(size_t) b] = false;
                for (auto& ch : state) ch[(size_t) b] = {};
                continue;
            }

            m1Goal[(size_t) b] = want ? m1For (b, g[(size_t) b]) : 0.0f;
            isActive[(size_t) b] = true;
            activeList[(size_t) numActive++] = b;
        }
    }
}
