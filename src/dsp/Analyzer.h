#pragma once

#include "../Common.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace breath
{
    // Analyse spectrale (FFT) d'un signal mono → niveau en dB sur 64 bandes logarithmiques.
    // Sert uniquement à mesurer : le son n'est jamais traité par FFT.
    class Analyzer
    {
    public:
        void prepare (double sampleRate);
        void reset();

        inline void push (float x) noexcept
        {
            history[(size_t) writePos] = x;
            writePos = (writePos + 1) & mask;
        }

        // Calcule les niveaux des bandes à partir des derniers échantillons poussés.
        void compute (float* bandsDb) noexcept;

        int getFftSize() const noexcept { return fftSize; }

    private:
        struct Weight { int bin; float w; };

        int fftOrder = 10, fftSize = 1024, mask = 1023, writePos = 0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> history, window, work;
        std::vector<std::vector<Weight>> bandWeights;
        float scale = 1.0f;
    };
}
