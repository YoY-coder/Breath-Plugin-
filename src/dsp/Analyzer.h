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
        // updateLows : recalcule aussi l'analyse longue (graves) — à appeler à intervalles
        // réguliers alignés sur la timeline pour rester déterministe.
        void compute (float* bandsDb, bool updateLows) noexcept;

        static constexpr int kLowsEvery = 4;   // analyse des graves toutes les 4 trames

    private:
        struct Weight { int bin; float w; };

        // Deux résolutions : FFT courte (réactive) pour tout le spectre,
        // FFT 4× plus longue (précise) pour les graves.
        struct Fft
        {
            int order = 10, size = 1024;
            std::unique_ptr<juce::dsp::FFT> fft;
            std::vector<float> window, work;
            std::vector<std::vector<Weight>> weights;
            float scale = 1.0f;
            void prepare (int order, double sampleRate);
            void run (const std::vector<float>& history, int writePos, int mask, float* bandsDb) noexcept;
        };

        Fft shortFft, longFft;
        std::vector<float> history;
        std::array<float, kNumBands> lows {}, blend {};
        int mask = 4095, writePos = 0;
    };
}
