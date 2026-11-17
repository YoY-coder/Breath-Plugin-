#include "Analyzer.h"

namespace breath
{
    void Analyzer::prepare (double sampleRate)
    {
        // ≈ 21 ms de fenêtre quelle que soit la fréquence d'échantillonnage.
        int mult = 1;
        while (sampleRate / mult > 60000.0)
            mult *= 2;
        fftOrder = 10 + (mult == 1 ? 0 : (mult == 2 ? 1 : 2));
        fftSize = 1 << fftOrder;
        mask = fftSize - 1;

        fft = std::make_unique<juce::dsp::FFT> (fftOrder);
        history.assign ((size_t) fftSize, 0.0f);
        work.assign ((size_t) fftSize * 2, 0.0f);
        window.resize ((size_t) fftSize);

        double sum = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
            sum += window[(size_t) i];
        }
        // Une sinusoïde d'amplitude 1 donne 0 dB.
        scale = (float) (2.0 / sum);

        // Poids de chaque bande : moyenne de la puissance échantillonnée en 8 points
        // logarithmiques dans la bande (interpolation linéaire entre les bins).
        bandWeights.assign ((size_t) kNumBands, {});
        const float ratio = std::pow (kMaxFreq / kMinFreq, 1.0f / (float) kNumBands);
        const int numBins = fftSize / 2;

        for (int b = 0; b < kNumBands; ++b)
        {
            const float lo = bandCentre (b) / std::sqrt (ratio);
            std::vector<float> acc ((size_t) numBins + 1, 0.0f);
            constexpr int points = 8;

            for (int p = 0; p < points; ++p)
            {
                const float f = lo * std::pow (ratio, ((float) p + 0.5f) / (float) points);
                const float bin = juce::jlimit (0.0f, (float) numBins - 1.001f, f / (float) sampleRate * (float) fftSize);
                const int i0 = (int) bin;
                const float frac = bin - (float) i0;
                acc[(size_t) i0]     += (1.0f - frac) / points;
                acc[(size_t) i0 + 1] += frac / points;
            }

            for (int i = 0; i <= numBins; ++i)
                if (acc[(size_t) i] > 0.0f)
                    bandWeights[(size_t) b].push_back ({ i, acc[(size_t) i] });
        }

        reset();
    }

    void Analyzer::reset()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        writePos = 0;
    }

    void Analyzer::compute (float* bandsDb) noexcept
    {
        for (int i = 0; i < fftSize; ++i)
            work[(size_t) i] = history[(size_t) ((writePos + i) & mask)] * window[(size_t) i];
        std::fill (work.begin() + fftSize, work.end(), 0.0f);

        fft->performFrequencyOnlyForwardTransform (work.data(), true);

        for (int b = 0; b < kNumBands; ++b)
        {
            float power = 0.0f;
            for (const auto& w : bandWeights[(size_t) b])
            {
                const float mag = work[(size_t) w.bin] * scale;
                power += w.w * mag * mag;
            }
            bandsDb[b] = juce::jmax (kSilenceDb, 10.0f * std::log10 (power + 1.0e-14f));
        }
    }
}
