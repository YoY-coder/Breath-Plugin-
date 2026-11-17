#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>

namespace breath
{
    // --- Dimensions générales ------------------------------------------------
    constexpr int kMaxInstances   = 16;     // instances max dans un projet
    constexpr int kNumBands       = 64;     // bandes d'analyse / de filtrage
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

    // Réglages d'une relation « piste supérieure → piste inférieure ».
    struct RelationSettings
    {
        bool  enabled     = true;
        float amount      = 0.6f;    // 0..1 (dB de réduction par dB au-dessus du seuil)
        float maxReduction = 6.0f;   // dB (positif)
        float threshold   = -40.0f;  // dBFS
        float attackMs    = 5.0f;
        float releaseMs   = 120.0f;
        float lowHz       = 20.0f;
        float highHz      = 20000.0f;
        float resolution  = 0.6f;    // 0 = large bande, 1 = spectral fin

        bool operator== (const RelationSettings&) const = default;
    };

    // Version « atomique » lue par le thread audio (écrite par le thread de l'interface).
    struct AtomicRelation
    {
        std::atomic<bool>  enabled { true };
        std::atomic<float> amount { 0.6f }, maxReduction { 6.0f }, threshold { -40.0f },
                           attackMs { 5.0f }, releaseMs { 120.0f },
                           lowHz { 20.0f }, highHz { 20000.0f }, resolution { 0.6f };

        void store (const RelationSettings& s) noexcept
        {
            amount.store (s.amount, std::memory_order_relaxed);
            maxReduction.store (s.maxReduction, std::memory_order_relaxed);
            threshold.store (s.threshold, std::memory_order_relaxed);
            attackMs.store (s.attackMs, std::memory_order_relaxed);
            releaseMs.store (s.releaseMs, std::memory_order_relaxed);
            lowHz.store (s.lowHz, std::memory_order_relaxed);
            highHz.store (s.highHz, std::memory_order_relaxed);
            resolution.store (s.resolution, std::memory_order_relaxed);
            enabled.store (s.enabled, std::memory_order_release);
        }

        RelationSettings load() const noexcept
        {
            RelationSettings s;
            s.enabled      = enabled.load (std::memory_order_acquire);
            s.amount       = amount.load (std::memory_order_relaxed);
            s.maxReduction = maxReduction.load (std::memory_order_relaxed);
            s.threshold    = threshold.load (std::memory_order_relaxed);
            s.attackMs     = attackMs.load (std::memory_order_relaxed);
            s.releaseMs    = releaseMs.load (std::memory_order_relaxed);
            s.lowHz        = lowHz.load (std::memory_order_relaxed);
            s.highHz       = highHz.load (std::memory_order_relaxed);
            s.resolution   = resolution.load (std::memory_order_relaxed);
            return s;
        }
    };
}
