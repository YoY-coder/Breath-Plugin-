#pragma once

#include "../Common.h"

namespace breath
{
    // Banque de 64 ou 128 filtres « en cloche » (IIR à variable d'état, phase minimale, zéro latence)
    // en cascade. Chaque bande reçoit une réduction en dB ; les coefficients sont interpolés
    // échantillon par échantillon entre deux calculs (aucune marche, aucun clic).
    class FilterBank
    {
    public:
        static constexpr int kMaxChannels = 2;

        // numBands : 64 ou 128 cloches réparties de 20 Hz à 20 kHz.
        void prepare (double sampleRate, int numBands);
        int  getNumBands() const noexcept { return nb; }
        void reset() noexcept;

        // reductionDb : réduction souhaitée pour chacune des getNumBands() bandes (dB positifs).
        void setReductions (const float* reductionDb) noexcept;
        // Réductions distinctes pour les deux voies (gauche/droite ou mid/side).
        void setReductions (const float* reductionA, const float* reductionB) noexcept;

        // Une seule passe par échantillon : lissage du gain de chaque cloche (double lissage,
        // trajectoire sans cassure) puis filtrage. Cloche à variable d'état (« trapézoïdale ») :
        // seul le gain (m1) varie, la partie récursive reste fixe → aucun clic quand le gain bouge.
        inline void processStereo (float& l, float& r) noexcept
        {
            auto& sl = state[0];
            auto& sr = state[1];
            float xl = l, xr = r;
            for (int i = 0; i < numActive; ++i)
            {
                const auto b = (size_t) activeList[(size_t) i];
                const auto& c = fixed[b];
                m1Mid[0][b] += (m1Goal[0][b] - m1Mid[0][b]) * smoothK;
                m1[0][b]    += (m1Mid[0][b] - m1[0][b]) * smoothK;
                m1Mid[1][b] += (m1Goal[1][b] - m1Mid[1][b]) * smoothK;
                m1[1][b]    += (m1Mid[1][b] - m1[1][b]) * smoothK;

                auto& s0 = sl[b];
                const float v3l = xl - s0.ic2;
                const float v1l = c.a1 * s0.ic1 + c.a2 * v3l;
                const float v2l = s0.ic2 + c.a2 * s0.ic1 + c.a3 * v3l;
                s0.ic1 = 2.0f * v1l - s0.ic1;
                s0.ic2 = 2.0f * v2l - s0.ic2;
                xl += m1[0][b] * v1l;

                auto& s1 = sr[b];
                const float v3r = xr - s1.ic2;
                const float v1r = c.a1 * s1.ic1 + c.a2 * v3r;
                const float v2r = s1.ic2 + c.a2 * s1.ic1 + c.a3 * v3r;
                s1.ic1 = 2.0f * v1r - s1.ic1;
                s1.ic2 = 2.0f * v2r - s1.ic2;
                xr += m1[1][b] * v1r;
            }
            l = xl;
            r = xr;
        }

        inline float processMono (float x) noexcept
        {
            auto& st = state[0];
            for (int i = 0; i < numActive; ++i)
            {
                const auto b = (size_t) activeList[(size_t) i];
                const auto& c = fixed[b];
                m1Mid[0][b] += (m1Goal[0][b] - m1Mid[0][b]) * smoothK;
                m1[0][b]    += (m1Mid[0][b] - m1[0][b]) * smoothK;
                auto& s0 = st[b];
                const float v3 = x - s0.ic2;
                const float v1 = c.a1 * s0.ic1 + c.a2 * v3;
                const float v2 = s0.ic2 + c.a2 * s0.ic1 + c.a3 * v3;
                s0.ic1 = 2.0f * v1 - s0.ic1;
                s0.ic2 = 2.0f * v2 - s0.ic2;
                x += m1[0][b] * v1;
            }
            return x;
        }

    private:
        struct Coeffs { float a1 = 0, a2 = 0, a3 = 0; };
        struct State  { float ic1 = 0, ic2 = 0; };

        float m1For (int band, float reductionDb) const noexcept;

        std::array<Coeffs, kNumBands> fixed {};
        std::array<std::array<float, kNumBands>, kMaxChannels> m1 {}, m1Mid {}, m1Goal {};
        std::array<float, kNumBands> kDamp {};
        void solveGains (const float* target, float* g) const noexcept;
        float smoothK = 0.02f;
        std::array<std::array<State, kNumBands>, kMaxChannels> state {};
        std::array<float, kNumBands> gTan {}, cosw {}, alpha {};
        std::array<bool, kNumBands> usable {};
        std::array<bool, kNumBands> isActive {};
        std::array<int, kNumBands> activeList {};
        float q = 4.6f;
        int numActive = 0;
        int nb = 64;

        // Recouvrement des cloches (réponse en dB d'une cloche de 1 dB aux bandes voisines),
        // utilisé pour que la cascade suive précisément la courbe voulue.
        static constexpr int kSpread = 6;
        std::array<std::array<float, 2 * kSpread + 1>, kNumBands> overlap {};
        float overlapSum = 1.0f;
    };
}
