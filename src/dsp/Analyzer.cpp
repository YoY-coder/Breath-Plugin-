#include "Analyzer.h"

namespace breath
{
    namespace
    {
        constexpr float kLowsFullBelow = 160.0f;   // en dessous : analyse longue uniquement
        constexpr float kLowsNoneAbove = 320.0f;   // au-dessus : analyse courte uniquement
    }

    void Analyzer::Fft::prepare (int o, double sampleRate)
    {
        order = o;
        size = 1 << order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        work.assign ((size_t) size * 2, 0.0f);
        window.resize ((size_t) size);

        double sum = 0.0;
        for (int i = 0; i < size; ++i)
        {
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);
            sum += window[(size_t) i];
        }
        scale = (float) (2.0 / sum);   // une sinusoïde d'amplitude 1 donne 0 dB

        // Poids de chaque bande : moyenne de la puissance échantillonnée en 8 points
        // logarithmiques dans la bande (interpolation linéaire entre les bins).
        weights.assign ((size_t) kNumBands, {});
        const float ratio = std::pow (kMaxFreq / kMinFreq, 1.0f / (float) kNumBands);
        const int numBins = size / 2;

        for (int b = 0; b < kNumBands; ++b)
        {
            const float lo = bandCentre (b) / std::sqrt (ratio);
            std::vector<float> acc ((size_t) numBins + 1, 0.0f);
            constexpr int points = 8;

            for (int p = 0; p < points; ++p)
            {
                const float f = lo * std::pow (ratio, ((float) p + 0.5f) / (float) points);
                const float bin = juce::jlimit (0.0f, (float) numBins - 1.001f, f / (float) sampleRate * (float) size);
                const int i0 = (int) bin;
                const float frac = bin - (float) i0;
                acc[(size_t) i0]     += (1.0f - frac) / points;
                acc[(size_t) i0 + 1] += frac / points;
            }

            for (int i = 0; i <= numBins; ++i)
                if (acc[(size_t) i] > 0.0f)
                    weights[(size_t) b].push_back ({ i, acc[(size_t) i] });
        }
    }

    void Analyzer::Fft::run (const std::vector<float>& hist, int wp, int m, float* bandsDb) noexcept
    {
        // Les « size » derniers échantillons de l'historique circulaire.
        const int start = wp - size;
        for (int i = 0; i < size; ++i)
            work[(size_t) i] = hist[(size_t) ((start + i) & m)] * window[(size_t) i];
        std::fill (work.begin() + size, work.end(), 0.0f);

        fft->performFrequencyOnlyForwardTransform (work.data(), true);

        for (int b = 0; b < kNumBands; ++b)
        {
            float power = 0.0f;
            for (const auto& w : weights[(size_t) b])
            {
                const float mag = work[(size_t) w.bin] * scale;
                power += w.w * mag * mag;
            }
            bandsDb[b] = juce::jmax (kSilenceDb, 10.0f * std::log10 (power + 1.0e-14f));
        }
    }

    void Analyzer::prepare (double sampleRate)
    {
        // FFT courte ≈ 21 ms, FFT longue ≈ 85 ms, quelle que soit la fréquence d'échantillonnage.
        int extra = 0;
        while (sampleRate / (double) (1 << extra) > 60000.0)
            ++extra;
        shortFft.prepare (10 + extra, sampleRate);
        longFft.prepare (12 + extra, sampleRate);

        history.assign ((size_t) longFft.size, 0.0f);
        mask = longFft.size - 1;

        for (int b = 0; b < kNumBands; ++b)
        {
            const float f = bandCentre (b);
            blend[(size_t) b] = juce::jlimit (0.0f, 1.0f, std::log (kLowsNoneAbove / f) / std::log (kLowsNoneAbove / kLowsFullBelow));
        }
        reset();
    }

    void Analyzer::reset()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        lows.fill (kSilenceDb);
        writePos = 0;
    }

    void Analyzer::compute (float* bandsDb, bool updateLows) noexcept
    {
        shortFft.run (history, writePos, mask, bandsDb);
        if (updateLows)
            longFft.run (history, writePos, mask, lows.data());

        for (int b = 0; b < kNumBands; ++b)
        {
            const float w = blend[(size_t) b];
            if (w > 0.0f)
                bandsDb[b] = w * lows[(size_t) b] + (1.0f - w) * bandsDb[b];
        }
    }
}
