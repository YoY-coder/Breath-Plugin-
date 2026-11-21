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
        // phase : position de la trame dans le cycle de l'analyse longue (0 .. kLowsEvery-1),
        // alignée sur la timeline. À la phase 0 l'analyse des graves est recalculée ; entre deux
        // calculs, elle est interpolée (pas de marches dans la détection des graves).
        void compute (float* bandsDb, int phase) noexcept;

        // Analyse courte seule (utilisée pour comparer les deux voies stéréo).
        void computeShort (float* bandsDb) noexcept { shortFft.run (history, writePos, mask, bandsDb); }

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
        std::array<float, kNumBands> lows {}, lowsPrev {}, blend {};
        int mask = 4095, writePos = 0;
    };
}
