#pragma once

#include "../Common.h"

namespace breath
{
    // Banque de 64 filtres « en cloche » (biquads IIR, phase minimale, zéro latence)
    // en cascade. Chaque bande reçoit une réduction en dB.
    class FilterBank
    {
    public:
        static constexpr int kMaxChannels = 2;

        void prepare (double sampleRate);
        void reset() noexcept;

        // reductionDb : réduction souhaitée par bande (dB positifs). Calcule les coefficients.
        void setReductions (const float* reductionDb) noexcept;

        inline float process (int ch, float x) noexcept
        {
            auto& st = state[(size_t) ch];
            for (int i = 0; i < numActive; ++i)
            {
                const auto b = (size_t) activeList[(size_t) i];
                const auto& c = coeffs[b];
                auto& s = st[b];
                const float y = c.b0 * x + s.z1;
                s.z1 = c.b1 * x - c.a1 * y + s.z2;
                s.z2 = c.b2 * x - c.a2 * y;
                x = y;
            }
            return x;
        }

    private:
        struct Coeffs { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
        struct State  { float z1 = 0, z2 = 0; };

        std::array<Coeffs, kNumBands> coeffs {};
        std::array<std::array<State, kNumBands>, kMaxChannels> state {};
        std::array<float, kNumBands> cosw {}, alpha {};
        std::array<bool, kNumBands> usable {};
        std::array<bool, kNumBands> isActive {};
        std::array<int, kNumBands> activeList {};
        int numActive = 0;

        // Recouvrement des cloches (réponse en dB d'une cloche de 1 dB aux bandes voisines),
        // utilisé pour que la cascade suive précisément la courbe voulue.
        static constexpr int kSpread = 6;
        std::array<std::array<float, 2 * kSpread + 1>, kNumBands> overlap {};
        float overlapSum = 1.0f;
    };
}
