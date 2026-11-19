// Tests automatiques de Breath (sans DAW).
//
// 1. Export fiable : 4 instances rendues « en temps réel » (blocs de 256, ordre de traitement
//    aléatoire) puis « en export » (blocs de 1024 traités en parallèle sur 4 threads, puis blocs
//    de 64 dans l'ordre inverse). Les sorties doivent être identiques à l'échantillon près.
// 2. Fonctionnel : la basse est bien atténuée quand le kick joue, et pas sans relation.
// 3. Bypass : sortie = entrée.

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <random>
#include <thread>
#include <cstdio>

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kLen = (int) kSr * 4;
    constexpr int kNum = 4;

    struct FakePlayHead final : juce::AudioPlayHead
    {
        bool playing = true;
        int64_t time = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (playing);
            p.setTimeInSamples (time);
            return p;
        }
    };

    using Signal = std::vector<std::array<float, 2>>;

    Signal makeSignal (int track)
    {
        Signal s ((size_t) kLen);
        uint32_t seed = 12345u + (uint32_t) track * 777u;
        auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f; };
        const float twoPi = juce::MathConstants<float>::twoPi;
        double ph = 0.0;

        for (int i = 0; i < kLen; ++i)
        {
            const float t = (float) i / (float) kSr;
            float l = 0.0f, r = 0.0f;
            switch (track)
            {
                case 0: // kick : 2 coups par seconde
                {
                    const float tb = std::fmod (t, 0.5f);
                    const float f = 50.0f + 120.0f * std::exp (-tb * 40.0f);
                    ph += f / kSr;
                    l = r = 0.9f * std::exp (-tb * 9.0f) * std::sin (twoPi * (float) ph);
                    break;
                }
                case 1: // basse continue
                    l = r = 0.5f * std::sin (twoPi * 70.0f * t) + 0.25f * std::sin (twoPi * 140.0f * t + 0.3f);
                    break;
                case 2: // « voix » : dent de scie avec vibrato
                {
                    ph += (220.0 + 4.0 * std::sin (twoPi * 5.0f * t)) / kSr;
                    const float saw = (float) (2.0 * (ph - std::floor (ph)) - 1.0);
                    l = 0.3f * saw;
                    r = 0.28f * saw;
                    break;
                }
                default: // nappe de bruit
                    l = 0.15f * noise();
                    r = 0.15f * noise();
                    break;
            }
            s[(size_t) i] = { l, r };
        }
        return s;
    }

    struct Rig
    {
        std::vector<std::unique_ptr<BreathAudioProcessor>> procs;
        std::vector<FakePlayHead> heads;
        std::vector<Signal> inputs;

        Rig() : heads (kNum)
        {
            for (int i = 0; i < kNum; ++i)
            {
                procs.emplace_back (static_cast<BreathAudioProcessor*> (createPluginFilter()));
                auto& p = *procs.back();
                p.setPlayConfigDetails (2, 2, kSr, 1024);
                p.setPlayHead (&heads[(size_t) i]);
                auto* prio = p.getParams().getParameter ("priority");
                prio->setValueNotifyingHost (prio->convertTo0to1 ((float) (i + 1)));
                inputs.push_back (makeSignal (i));
            }
        }

        BreathAudioProcessor& operator[] (int i) { return *procs[(size_t) i]; }

        void stopAll()
        {
            juce::AudioBuffer<float> b (2, 64);
            juce::MidiBuffer m;
            for (int i = 0; i < kNum; ++i)
            {
                heads[(size_t) i].playing = false;
                b.clear();
                procs[(size_t) i]->processBlock (b, m);
                heads[(size_t) i].playing = true;
            }
        }

        // Rend toute la durée. mode : 0 = temps réel séquentiel aléatoire, 1 = export parallèle,
        // 2 = export séquentiel en ordre inverse.
        std::vector<Signal> render (const std::vector<int>& blockPattern, int mode, unsigned seed)
        {
            stopAll();
            for (auto& p : procs)
            {
                p->setNonRealtime (mode != 0);
                p->prepareToPlay (kSr, 1024);
            }

            std::vector<Signal> out ((size_t) kNum, Signal ((size_t) kLen));
            std::mt19937 rng (seed);
            std::vector<int> order (kNum);
            for (int i = 0; i < kNum; ++i) order[(size_t) i] = i;

            auto processOne = [&] (int i, int pos, int n)
            {
                juce::AudioBuffer<float> b (2, n);
                for (int k = 0; k < n; ++k)
                {
                    b.setSample (0, k, inputs[(size_t) i][(size_t) (pos + k)][0]);
                    b.setSample (1, k, inputs[(size_t) i][(size_t) (pos + k)][1]);
                }
                heads[(size_t) i].time = pos;
                juce::MidiBuffer m;
                procs[(size_t) i]->processBlock (b, m);
                for (int k = 0; k < n; ++k)
                    out[(size_t) i][(size_t) (pos + k)] = { b.getSample (0, k), b.getSample (1, k) };
            };

            int pos = 0, bi = 0;
            while (pos < kLen)
            {
                const int n = juce::jmin (blockPattern[(size_t) (bi++ % (int) blockPattern.size())], kLen - pos);

                if (mode == 1)
                {
                    std::vector<std::thread> th;
                    for (int i = 0; i < kNum; ++i)
                        th.emplace_back (processOne, i, pos, n);
                    for (auto& t : th) t.join();
                }
                else
                {
                    if (mode == 0) std::shuffle (order.begin(), order.end(), rng);
                    else           std::reverse (order.begin(), order.end());
                    for (int i : order)
                        processOne (i, pos, n);
                }
                pos += n;
            }
            return out;
        }
    };

    float maxDiff (const std::vector<Signal>& a, const std::vector<Signal>& b)
    {
        float d = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t k = 0; k < a[i].size(); ++k)
                for (int c = 0; c < 2; ++c)
                    d = juce::jmax (d, std::abs (a[i][k][(size_t) c] - b[i][k][(size_t) c]));
        return d;
    }

    // Énergie (dB) d'un signal filtré passe-bas simple autour des graves, pendant les coups de kick.
    float lowEnergyDuringKicks (const Signal& s)
    {
        double e = 0.0, lp = 0.0;
        const double a = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 200.0 / kSr);
        for (int i = (int) kSr; i < kLen; ++i)
        {
            lp += a * (s[(size_t) i][0] - lp);
            if (std::fmod ((double) i / kSr, 0.5) < 0.12)
                e += lp * lp;
        }
        return (float) (10.0 * std::log10 (e + 1e-20));
    }

    int failures = 0;
    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "[OK]   " : "[ECHEC]", what.toRawUTF8());
        if (! ok) ++failures;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    Rig rig;

    auto& hub = rig[0].getHub();
    check (hub.numActive() == kNum, "4 instances inscrites dans la memoire partagee");

    // Relation Kick → Basse ciblée sur les graves, un peu plus forte.
    {
        auto s = hub.getRelation (rig[0].getSlot(), rig[1].getSlot());
        s.amount = 0.8f; s.maxReduction = 9.0f; s.threshold = -45.0f;
        s.lowHz = 30.0f; s.highHz = 250.0f; s.resolution = 0.8f;
        s.numNodes = 1;
        s.nodes[0] = { 120.0f, -3.0f, 1.0f };     // point de façonnage : protège un peu 120 Hz
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), s);
    }
    // Make-up automatique sur la basse (doit lui aussi être identique à l'export).
    rig[1].getParams().getParameter ("autoMakeup")->setValueNotifyingHost (1.0f);
    // Lookahead de 6 ms sur la basse (latence + compensation).
    {
        auto* la = rig[1].getParams().getParameter ("lookahead");
        la->setValueNotifyingHost (la->convertTo0to1 (6.0f));
    }

    // --- 1. Export = temps réel ----------------------------------------------
    const auto rt = rig.render ({ 256, 256, 200, 256, 77, 256 }, 0, 42);
    std::printf ("        delai de synchro mesure : %d echantillons\n", hub.getSyncDelay());
    const auto off1 = rig.render ({ 1024 }, 1, 0);
    const auto off2 = rig.render ({ 64, 100, 31 }, 2, 0);
    const auto rt2 = rig.render ({ 256, 128, 256, 64 }, 0, 7);

    const float d1 = maxDiff (rt, off1), d2 = maxDiff (rt, off2), d3 = maxDiff (rt, rt2);
    std::printf ("        ecart max temps reel / export parallele (blocs 1024) : %g\n", d1);
    std::printf ("        ecart max temps reel / export sequentiel (blocs 64)  : %g\n", d2);
    std::printf ("        ecart max entre deux lectures temps reel             : %g\n", d3);
    check (d1 == 0.0f, "Export (parallele, gros blocs) identique au temps reel");
    check (d2 == 0.0f, "Export (sequentiel, petits blocs, ordre inverse) identique au temps reel");
    check (d3 == 0.0f, "Deux lectures temps reel avec des blocs differents sont identiques");
    check (! rig[1].hasOfflineDesync() && ! rig[2].hasOfflineDesync(), "Aucune desynchronisation signalee pendant l'export");

    const float autoMk = rig[1].getAutoMakeupDb();
    std::printf ("        make-up automatique de la basse en fin de lecture : %.2f dB\n", autoMk);
    check (autoMk > 0.3f, "Make-up automatique : compense le volume retire");
    rig[1].getParams().getParameter ("autoMakeup")->setValueNotifyingHost (0.0f);

    // --- 2. Fonctionnel ------------------------------------------------------
    const auto fn = rig.render ({ 256 }, 0, 11);
    const int lat = rig[1].getLatencyInSamples();
    check (lat == juce::roundToInt (0.006 * kSr), "Lookahead : latence declaree = " + juce::String (lat) + " echantillons");

    Signal bassIn = rig.inputs[1];
    Signal bassOutAligned ((size_t) kLen);
    for (int i = 0; i < kLen; ++i)
        bassOutAligned[(size_t) i] = i + lat < kLen ? fn[1][(size_t) (i + lat)] : std::array<float, 2> { 0.0f, 0.0f };
    const float before = lowEnergyDuringKicks (bassIn), after = lowEnergyDuringKicks (bassOutAligned);
    std::printf ("        graves de la basse pendant les kicks : %.1f dB -> %.1f dB\n", before, after);
    check (after < before - 3.0f, "La basse est creusee par le kick (> 3 dB)");

    // Le kick (en tete) n'est pas modifie.
    float kickDiff = 0.0f;
    for (int i = 0; i < kLen; ++i)
        kickDiff = juce::jmax (kickDiff, std::abs (fn[0][(size_t) i][0] - rig.inputs[0][(size_t) i][0]));
    check (kickDiff < 1.0e-6f, "Le kick (rang 1) n'est pas modifie");

    // Relation désactivée : plus d'atténuation.
    {
        auto s = hub.getRelation (rig[0].getSlot(), rig[1].getSlot());
        s.enabled = false;
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), s);
        const auto noRel = rig.render ({ 256 }, 0, 1);
        Signal o ((size_t) kLen);
        for (int i = 0; i < kLen; ++i)
            o[(size_t) i] = i + lat < kLen ? noRel[1][(size_t) (i + lat)] : std::array<float, 2> { 0.0f, 0.0f };
        const float e = lowEnergyDuringKicks (o);
        std::printf ("        relation desactivee : %.1f dB\n", e);
        check (std::abs (e - before) < 1.5f, "Relation desactivee : la basse n'est plus creusee par le kick");
        s.enabled = true;
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), s);
    }


    // Relation libre : le synthé (rang 4) creuse le kick (rang 1) malgré la hiérarchie.
    {
        auto s = hub.getRelation (rig[3].getSlot(), rig[0].getSlot());
        s.enabled = true; s.followOrder = false; s.threshold = -60.0f; s.amount = 1.0f;
        hub.setRelation (rig[3].getSlot(), rig[0].getSlot(), s);
        const auto fr = rig.render ({ 256 }, 0, 5);
        float d = 0.0f;
        for (int i = 0; i < kLen; ++i)
            d = juce::jmax (d, std::abs (fr[0][(size_t) i][0] - rig.inputs[0][(size_t) i][0]));
        check (d > 1.0e-3f, "Relation libre : une piste du dessous peut creuser une piste du dessus");
        s.enabled = false;
        hub.setRelation (rig[3].getSlot(), rig[0].getSlot(), s);
    }

    // Make-up manuel : +6 dB sur le kick (aucune relation active) = gain x2.
    {
        auto* mk = rig[0].getParams().getParameter ("makeup");
        mk->setValueNotifyingHost (mk->convertTo0to1 (6.0f));
        const float gain = juce::Decibels::decibelsToGain (6.0f);
        const auto fr = rig.render ({ 256 }, 0, 6);
        float maxErr = 0.0f;
        for (int i = (int) kSr; i < kLen; ++i)
            maxErr = juce::jmax (maxErr, std::abs (fr[0][(size_t) i][0] - gain * rig.inputs[0][(size_t) i][0]));
        check (maxErr < 1.0e-4f, "Make-up manuel +6 dB = volume x2");
        mk->setValueNotifyingHost (mk->convertTo0to1 (0.0f));
    }

    // --- 3. Bypass All ---------------------------------------------------------
    {
        auto* b = rig[0].getParams().getParameter ("bypassAll");   // instance maîtresse = la première
        b->setValueNotifyingHost (1.0f);
        const auto byp = rig.render ({ 256 }, 0, 3);
        float d = 0.0f; int at = 0;
        for (int i = (int) kSr; i < kLen - lat; ++i)
        {
            const float e = std::abs (byp[1][(size_t) (i + lat)][0] - rig.inputs[1][(size_t) i][0]);
            if (e > d) { d = e; at = i; }
        }
        std::printf ("        bypass all : ecart max %g a l'echantillon %d\n", d, at);
        check (d == 0.0f, "Bypass All (porte par l'instance maitresse) : sortie = entree");
        b->setValueNotifyingHost (0.0f);
    }

    // --- 4. Sauvegarde / restauration ------------------------------------------
    {
        juce::MemoryBlock mb;
        rig[1].getStateInformation (mb);
        std::unique_ptr<BreathAudioProcessor> dup (static_cast<BreathAudioProcessor*> (createPluginFilter()));
        dup->setStateInformation (mb.getData(), (int) mb.getSize());
        check (dup->getSlot() >= 0 && hub.info (dup->getSlot()).id != hub.info (rig[1].getSlot()).id,
               "Duplication : la copie recoit un nouvel identifiant");
        const auto r = hub.getRelation (rig[0].getSlot(), dup->getSlot());
        check (std::abs (r.highHz - 250.0f) < 0.01f, "Duplication : la copie herite des relations de l'original");
    }

    // --- 5. Undo / redo --------------------------------------------------------
    {
        const auto before = hub.getRelation (rig[0].getSlot(), rig[2].getSlot());
        hub.beginEdit();
        auto s = before; s.amount = 0.1f;
        hub.setRelation (rig[0].getSlot(), rig[2].getSlot(), s);
        hub.endEdit();
        hub.undo();
        check (hub.getRelation (rig[0].getSlot(), rig[2].getSlot()).amount == before.amount, "Undo restaure la relation");
        hub.redo();
        check (std::abs (hub.getRelation (rig[0].getSlot(), rig[2].getSlot()).amount - 0.1f) < 1e-6f, "Redo reapplique la modification");
    }


    // --- 6. Performance : 12 instances, toutes les relations actives ---------
    {
        rig.stopAll();
        std::vector<std::unique_ptr<BreathAudioProcessor>> many;
        std::vector<FakePlayHead> heads (12);
        for (int i = 0; i < 12; ++i)
        {
            many.emplace_back (static_cast<BreathAudioProcessor*> (createPluginFilter()));
            many.back()->setPlayConfigDetails (2, 2, kSr, 256);
            many.back()->prepareToPlay (kSr, 256);
            many.back()->setPlayHead (&heads[(size_t) i]);
            auto* prio = many.back()->getParams().getParameter ("priority");
            prio->setValueNotifyingHost (prio->convertTo0to1 ((float) (i + 1)));
        }
        for (int a = 0; a < 12; ++a)
            for (int b = 0; b < 12; ++b)
                if (a != b)
                {
                    auto r = hub.getRelation (many[(size_t) a]->getSlot(), many[(size_t) b]->getSlot());
                    r.resolution = 1.0f; r.threshold = -60.0f;
                    hub.setRelation (many[(size_t) a]->getSlot(), many[(size_t) b]->getSlot(), r);
                }

        const int seconds = 10, n = 256;
        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer m;
        uint32_t seed = 1;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int64_t pos = 0; pos < (int64_t) kSr * seconds; pos += n)
            for (int i = 0; i < 12; ++i)
            {
                for (int k = 0; k < n; ++k)
                    for (int c = 0; c < 2; ++c)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        buf.setSample (c, k, 0.3f * ((float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f));
                    }
                heads[(size_t) i].time = pos;
                many[(size_t) i]->processBlock (buf, m);
            }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double load = ms / (seconds * 1000.0) * 100.0;
        std::printf ("        12 instances, 10 s d'audio : %.0f ms de calcul -> %.1f %% d'un coeur (%.2f %% par instance)\n", ms, load, load / 12.0);
        check (load < 50.0, "Charge processeur raisonnable avec 12 instances (pire cas)");
    }

    std::printf ("\n%s (%d echec(s))\n", failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT", failures);
    return failures == 0 ? 0 : 1;
}
