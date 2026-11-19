#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>

namespace breath
{
    // --- Dimensions générales ------------------------------------------------
    constexpr int kMaxInstances   = 16;     // instances max dans un projet
    constexpr int kNumBands       = 128;    // grille d'analyse fine (le filtrage utilise 64 ou 128 bandes)
    constexpr int kHop            = 128;    // pas d'analyse (échantillons), aligné sur la timeline
    constexpr int kTick           = 32;     // pas de contrôle (calcul des gains), aligné sur la timeline
    constexpr int kRingFrames     = 2048;   // trames mémorisées par instance (≈ 5 s à 48 kHz)
    constexpr float kMinFreq      = 20.0f;
    constexpr float kMaxFreq      = 20000.0f;
    constexpr float kSilenceDb    = -120.0f;
    constexpr int kDefaultSyncDelay = 512;  // délai de synchro par défaut (échantillons)

    static_assert (kHop % kTick == 0, "le pas d'analyse doit être un multiple du pas de contrôle");

    // Fréquence centrale de la bande b (répartition logarithmique 20 Hz → 20 kHz).
    inline float bandCentre (int b) noexcept
    {
        return kMinFreq * std::pow (kMaxFreq / kMinFreq, ((float) b + 0.5f) / (float) kNumBands);
    }

    // Position (0..1) d'une fréquence sur l'échelle logarithmique.
    inline float freqToNorm (float f) noexcept
    {
        return std::log (juce::jlimit (kMinFreq, kMaxFreq, f) / kMinFreq) / std::log (kMaxFreq / kMinFreq);
    }

    inline float normToFreq (float n) noexcept
    {
        return kMinFreq * std::pow (kMaxFreq / kMinFreq, juce::jlimit (0.0f, 1.0f, n));
    }

    // Grille de N bandes (N = 64 ou 128) : centre de la bande b.
    inline float bandCentreN (int b, int n) noexcept
    {
        return kMinFreq * std::pow (kMaxFreq / kMinFreq, ((float) b + 0.5f) / (float) n);
    }

    // Bande (fractionnaire) correspondant à une fréquence.
    inline float freqToBand (float f) noexcept
    {
        return freqToNorm (f) * (float) kNumBands - 0.5f;
    }

    inline int64_t floorDiv (int64_t a, int64_t b) noexcept
    {
        return (a >= 0) ? a / b : -((-a + b - 1) / b);
    }

    inline int roundUpTo (int v, int multiple) noexcept
    {
        return ((v + multiple - 1) / multiple) * multiple;
    }

    // Point de façonnage (« bell ») de la courbe de réduction max d'une relation.
    struct ShapeNode
    {
        float freq = 1000.0f;   // Hz
        float gain = 0.0f;      // dB ajoutés à la réduction max (négatif = protège cette zone)
        float width = 1.0f;     // largeur en octaves

        bool operator== (const ShapeNode&) const = default;
    };

    constexpr int kMaxNodes = 8;

    // Réglages d'une relation « piste source → piste creusée ».
    struct RelationSettings
    {
        bool  enabled     = true;
        bool  followOrder = true;    // active seulement si la source est au-dessus dans la hiérarchie
        float amount      = 0.5f;    // 0..1 (dB de réduction par dB au-dessus du seuil)
        float maxReduction = 6.0f;   // dB (positif)
        float threshold   = -30.0f;  // dBFS
        float attackMs    = 5.0f;
        float releaseMs   = 120.0f;
        float lowHz       = 20.0f;
        float highHz      = 20000.0f;
        float lowSlope    = 0.3f;    // largeur du fondu du bord bas (octaves) : petit = pente raide
        float highSlope   = 0.3f;    // largeur du fondu du bord haut (octaves)
        float resolution  = 0.6f;    // 0 = large bande, 1 = spectral fin
        int   numNodes    = 0;
        std::array<ShapeNode, kMaxNodes> nodes {};

        bool operator== (const RelationSettings&) const = default;

        // Relation active compte tenu de la position des deux pistes dans la hiérarchie.
        bool isActive (bool sourceIsAbove) const noexcept { return enabled && (! followOrder || sourceIsAbove); }

        // Pondération de la zone traitée à la fréquence f (0..1), bords en fondu selon la pente.
        float rangeWeight (float f) const noexcept
        {
            auto edge = [] (float octFromEdge, float width)
            {
                const float t = juce::jlimit (0.0f, 1.0f, octFromEdge / juce::jmax (0.02f, width) + 0.5f);
                return t * t * (3.0f - 2.0f * t);    // transition douce (smoothstep)
            };
            const float lo = juce::jmin (lowHz, highHz), hi = juce::jmax (lowHz, highHz);
            return edge (std::log2 (f / lo), lowSlope) * edge (std::log2 (hi / f), highSlope);
        }

        // Pente exprimée en dB/octave (équivalent visuel, comme sur un séparateur de bandes).
        static float slopeToDbPerOct (float widthOct) noexcept { return 24.0f / juce::jmax (0.02f, widthOct); }

        // Contribution d'un point à la fréquence f (dB).
        static float nodeShape (const ShapeNode& n, float f) noexcept
        {
            const float d = std::log2 (f / n.freq) / juce::jmax (0.05f, n.width * 0.5f);
            return n.gain * std::exp (-1.386f * d * d);   // demi-hauteur à ± largeur/2
        }

        // Réduction max effective à la fréquence f (base + points de façonnage).
        float maxAt (float f) const noexcept
        {
            float v = maxReduction;
            for (int i = 0; i < numNodes; ++i)
                v += nodeShape (nodes[(size_t) i], f);
            return juce::jlimit (0.0f, 30.0f, v);
        }
    };

    // Version « atomique » lue par le thread audio (écrite par le thread de l'interface).
    struct AtomicRelation
    {
        std::atomic<bool>  enabled { true }, followOrder { true };
        std::atomic<float> amount { 0.5f }, maxReduction { 6.0f }, threshold { -30.0f },
                           attackMs { 5.0f }, releaseMs { 120.0f },
                           lowHz { 20.0f }, highHz { 20000.0f }, lowSlope { 0.3f }, highSlope { 0.3f }, resolution { 0.6f };
        // Réduction max par bande (courbe façonnée), et facteur d'intensité associé.
        std::array<std::atomic<float>, kNumBands> maxCurve {}, intensity {};

        AtomicRelation() { store (RelationSettings {}); }

        void store (const RelationSettings& s) noexcept
        {
            amount.store (s.amount, std::memory_order_relaxed);
            maxReduction.store (s.maxReduction, std::memory_order_relaxed);
            threshold.store (s.threshold, std::memory_order_relaxed);
            attackMs.store (s.attackMs, std::memory_order_relaxed);
            releaseMs.store (s.releaseMs, std::memory_order_relaxed);
            lowHz.store (s.lowHz, std::memory_order_relaxed);
            highHz.store (s.highHz, std::memory_order_relaxed);
            lowSlope.store (s.lowSlope, std::memory_order_relaxed);
            highSlope.store (s.highSlope, std::memory_order_relaxed);
            resolution.store (s.resolution, std::memory_order_relaxed);
            for (int b = 0; b < kNumBands; ++b)
            {
                const float m = s.maxAt (bandCentre (b));
                maxCurve[(size_t) b].store (m, std::memory_order_relaxed);
                // Un point qui creuse plus (ou protège) agit aussi sur l'intensité, pas seulement sur le plafond.
                intensity[(size_t) b].store (s.maxReduction > 0.01f ? m / s.maxReduction : (m > 0.0f ? 1.0f : 0.0f),
                                             std::memory_order_relaxed);
            }
            followOrder.store (s.followOrder, std::memory_order_relaxed);
            enabled.store (s.enabled, std::memory_order_release);
        }

        RelationSettings load() const noexcept
        {
            RelationSettings s;
            s.enabled      = enabled.load (std::memory_order_acquire);
            s.followOrder  = followOrder.load (std::memory_order_relaxed);
            s.amount       = amount.load (std::memory_order_relaxed);
            s.maxReduction = maxReduction.load (std::memory_order_relaxed);
            s.threshold    = threshold.load (std::memory_order_relaxed);
            s.attackMs     = attackMs.load (std::memory_order_relaxed);
            s.releaseMs    = releaseMs.load (std::memory_order_relaxed);
            s.lowHz        = lowHz.load (std::memory_order_relaxed);
            s.highHz       = highHz.load (std::memory_order_relaxed);
            s.lowSlope     = lowSlope.load (std::memory_order_relaxed);
            s.highSlope    = highSlope.load (std::memory_order_relaxed);
            s.resolution   = resolution.load (std::memory_order_relaxed);
            return s;
        }
    };
}
