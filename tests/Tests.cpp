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
        s.lowSlope = 0.1f; s.highSlope = 1.5f;    // pentes : raide en bas, douce en haut
        s.knee = 6.0f;                            // seuil « soft »
        s.attackTiltLow = 3.0f; s.releaseTiltHigh = 0.3f;   // tilts d'attaque / release
        s.numThrNodes = 1;
        s.thrNodes[0] = { 60.0f, -4.0f, 1.0f };   // seuil abaissé autour de 60 Hz
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), s);
    }
    // Stéréo M/S non liée sur la basse (doit aussi être identique à l'export).
    {
        auto setP = [] (BreathAudioProcessor& pr, const char* id, float v) { auto* q = pr.getParams().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        setP (rig[1], "stereoType", 1.0f);
        setP (rig[1], "link", 50.0f);
        setP (rig[1], "focusB", 70.0f);
    }
    // La voix travaille en 128 bandes (doit aussi être identique à l'export).
    rig[2].getParams().getParameter ("bands")->setValueNotifyingHost (1.0f);
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


    // --- 5c. Nouvelles fonctions ---------------------------------------------
    {
        rig.stopAll();
        auto lowLevel = [&] (const std::vector<Signal>& fr, int track, int lat)
        {
            Signal o ((size_t) kLen);
            for (int i = 0; i < kLen; ++i)
                o[(size_t) i] = i + lat < kLen ? fr[(size_t) track][(size_t) (i + lat)] : std::array<float, 2> { 0.0f, 0.0f };
            return lowEnergyDuringKicks (o);
        };
        const int lat = rig[1].getLatencyInSamples();
        const float ref = lowEnergyDuringKicks (rig.inputs[1]);

        // Bypass de relation : la relation reste en place mais ne creuse plus.
        auto rel = hub.getRelation (rig[0].getSlot(), rig[1].getSlot());
        rel.enabled = false;
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), rel);
        check (hub.isRelationVisible (rig[0].getSlot(), rig[1].getSlot()) && ! hub.isRelationActive (rig[0].getSlot(), rig[1].getSlot()),
               "Bypass de relation : la relation reste affichee mais ne travaille plus");
        float byp = lowLevel (rig.render ({ 256 }, 0, 21), 1, lat);
        check (std::abs (byp - ref) < 1.5f, "Bypass de relation : la basse n'est plus creusee");
        rel.enabled = true;

        // Mode Hard : réduction franche, bien au-delà de 24 dB.
        rel.hard = true; rel.maxReduction = 60.0f; rel.amount = 1.0f; rel.threshold = -60.0f;
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), rel);
        const float hard = lowLevel (rig.render ({ 256 }, 0, 22), 1, lat);
        std::printf ("        mode Hard : graves de la basse %.1f dB -> %.1f dB\n", ref, hard);
        check (ref - hard > 15.0f, "Mode Hard : la basse est creusee de plus de 15 dB");
        rel.hard = false; rel.maxReduction = 9.0f; rel.amount = 0.8f; rel.threshold = -45.0f;

        // Masking : la voix n'a presque pas de graves → le kick ne doit quasiment pas la creuser dans les graves.
        {
            auto kv = hub.getRelation (rig[0].getSlot(), rig[2].getSlot());
            kv.assigned = true; kv.enabled = true; kv.lowHz = 20.0f; kv.highHz = 120.0f; kv.threshold = -40.0f; kv.amount = 1.0f;
            kv.masking = false;
            hub.setRelation (rig[0].getSlot(), rig[2].getSlot(), kv);
            rig.render ({ 256 }, 0, 23);
            const float without = rig[0].getHub().slot (rig[2].getSlot()).activityFrom[(size_t) rig[0].getSlot()].load();
            kv.masking = true;
            hub.setRelation (rig[0].getSlot(), rig[2].getSlot(), kv);
            float peakWith = 0.0f;
            rig.render ({ 256 }, 0, 24);
            peakWith = rig[0].getHub().slot (rig[2].getSlot()).activityFrom[(size_t) rig[0].getSlot()].load();
            std::printf ("        masking : reduction kick -> voix (graves) %.1f dB sans, %.1f dB avec\n", without, peakWith);
            check (without > 1.0f && peakWith < 0.5f, "Masking : pas de creusage la ou la piste creusee n'a rien");
            kv.masking = false;
            hub.setRelation (rig[0].getSlot(), rig[2].getSlot(), kv);
        }

        // Self : la nappe de bruit se compresse elle-même.
        {
            auto self = hub.getRelation (rig[3].getSlot(), rig[3].getSlot());
            check (! self.assigned, "Self desactive par defaut");
            self.assigned = true; self.enabled = true; self.threshold = -60.0f; self.amount = 1.0f; self.maxReduction = 12.0f;
            hub.setRelation (rig[3].getSlot(), rig[3].getSlot(), self);
            const auto fr = rig.render ({ 256 }, 0, 25);
            double ein = 0.0, eout = 0.0;
            for (int i = (int) kSr; i < kLen; ++i) { ein += std::pow (rig.inputs[3][(size_t) i][0], 2.0f); eout += std::pow (fr[3][(size_t) i][0], 2.0f); }
            const float drop = (float) (10.0 * std::log10 (ein / juce::jmax (eout, 1e-20)));
            std::printf ("        Self : la nappe perd %.1f dB\n", drop);
            check (drop > 3.0f, "Self : la piste se creuse elle-meme (compresseur)");
            self.assigned = false;
            hub.setRelation (rig[3].getSlot(), rig[3].getSlot(), self);
        }

        // Familles : une piste d'une autre famille ne creuse plus.
        {
            auto* fam = rig[0].getParams().getParameter ("family");
            fam->setValueNotifyingHost (fam->convertTo0to1 (2.0f));
            hub.setFamily (rig[0].getSlot(), 2);
            check (! hub.isRelationActive (rig[0].getSlot(), rig[1].getSlot()), "Familles : pas de relation entre familles differentes");
            const float sep = lowLevel (rig.render ({ 256 }, 0, 26), 1, lat);
            check (std::abs (sep - ref) < 1.5f, "Familles : le kick (autre famille) ne creuse plus la basse");
            fam->setValueNotifyingHost (fam->convertTo0to1 (0.0f));
            hub.setFamily (rig[0].getSlot(), 0);
        }

        // Résolution basse : la réduction s'étale autour du conflit, mais aucun « plateau » là où rien ne dépasse.
        {
            auto ks = hub.getRelation (rig[0].getSlot(), rig[3].getSlot());
            const auto saved = ks;
            ks.assigned = true; ks.enabled = true; ks.lowHz = 20.0f; ks.highHz = 20000.0f;
            ks.threshold = -60.0f; ks.amount = 1.0f; ks.maxReduction = 12.0f; ks.resolution = 0.875f;   // ≈ 38 bandes
            hub.setRelation (rig[0].getSlot(), rig[3].getSlot(), ks);
            rig.render ({ 256 }, 0, 34);
            float lowRed = 0.0f, highRed = 0.0f;
            for (int b = 0; b < breath::kNumBands; ++b)
            {
                const float v = hub.slot (rig[3].getSlot()).activityFrom[(size_t) rig[0].getSlot()].load();
                juce::ignoreUnused (v);
                const float r = hub.slot (rig[3].getSlot()).reductionCurve[(size_t) b].load();
                if (breath::bandCentre (b) < 150.0f) lowRed = juce::jmax (lowRed, r);
                if (breath::bandCentre (b) > 3000.0f) highRed = juce::jmax (highRed, r);
            }
            std::printf ("        resolution 38 bandes : reduction graves %.1f dB, aigus %.2f dB\n", lowRed, highRed);
            check (lowRed > 1.0f && highRed < 0.2f, "Resolution : pas de plateau la ou rien ne depasse le seuil");
            hub.setRelation (rig[0].getSlot(), rig[3].getSlot(), saved);
        }

        // Point sur la courbe de seuil : abaisser le seuil creuse davantage.
        {
            auto t = hub.getRelation (rig[0].getSlot(), rig[1].getSlot());
            const auto saved = t;
            t.numThrNodes = 0; t.knee = 0.0f; t.threshold = -20.0f; t.amount = 1.0f; t.maxReduction = 24.0f;
            hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), t);
            const float noNode = lowLevel (rig.render ({ 256 }, 0, 31), 1, lat);
            t.numThrNodes = 1; t.thrNodes[0] = { 70.0f, -15.0f, 1.5f };
            hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), t);
            const float withNode = lowLevel (rig.render ({ 256 }, 0, 32), 1, lat);
            std::printf ("        point de seuil -15 dB a 70 Hz : graves de la basse %.1f dB -> %.1f dB\n", noNode, withNode);
            check (withNode < noNode - 2.0f, "Point sur la courbe de seuil : abaisse le seuil localement");
            t.numThrNodes = 0; t.knee = 12.0f;
            hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), t);
            const float soft = lowLevel (rig.render ({ 256 }, 0, 33), 1, lat);
            std::printf ("        knee 12 dB : graves de la basse %.1f dB (knee 0 : %.1f dB)\n", soft, noNode);
            check (std::abs (soft - noNode) > 0.2f, "Knee : le seuil soft change bien la reduction");
            hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), saved);
        }

        // Automation : la Quantité et le seuil de la relation viennent des paramètres de la piste creusée.
        {
            const int slot = hub.getRelation (rig[0].getSlot(), rig[1].getSlot()).slot;
            check (slot >= 1 && slot < breath::kNumSlots, "Automation : la relation Kick -> Basse a un emplacement (" + juce::String (slot) + ")");
            auto* amt = rig[1].getParams().getParameter ("slot" + juce::String (slot) + "_amount");
            const float savedAmt = amt->getValue();
            check (std::abs (hub.relationAmount (rig[0].getSlot(), rig[1].getSlot()) - hub.getRelation (rig[0].getSlot(), rig[1].getSlot()).amount) < 1e-3f,
                   "Automation : le parametre Quantite reprend la valeur reglee");
            amt->setValueNotifyingHost (0.0f);
            const float noAmt = lowLevel (rig.render ({ 256 }, 0, 41), 1, lat);
            check (std::abs (noAmt - ref) < 1.5f, "Automation : Quantite a 0 % -> plus de creusage");
            amt->setValueNotifyingHost (savedAmt);
            auto* thr = rig[1].getParams().getParameter ("slot" + juce::String (slot) + "_thr");
            auto high = hub.getRelation (rig[0].getSlot(), rig[1].getSlot());
            const auto savedRel = high;
            high.threshold = -5.0f;                 // seuil haut : presque rien n'est creusé…
            hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), high);
            const float before = lowLevel (rig.render ({ 256 }, 0, 42), 1, lat);
            thr->setValueNotifyingHost (thr->convertTo0to1 (-20.0f));
            const float lowered = lowLevel (rig.render ({ 256 }, 0, 43), 1, lat);
            std::printf ("        automation du seuil -20 dB : %.1f dB -> %.1f dB\n", before, lowered);
            check (lowered < before - 1.0f, "Automation : le seuil global abaisse le seuil");
            thr->setValueNotifyingHost (thr->convertTo0to1 (0.0f));
            hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), savedRel);
            check (rig[1].getSlotName (slot).isNotEmpty(), "Automation : l'emplacement porte un nom");
        }

        // Focus 0 / 0 : aucune voie traitée.
        {
            auto setP = [] (BreathAudioProcessor& pr, const char* id, float v) { auto* q = pr.getParams().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            setP (rig[1], "focusA", 0.0f); setP (rig[1], "focusB", 0.0f);
            const float nofocus = lowLevel (rig.render ({ 256 }, 0, 44), 1, lat);
            check (std::abs (nofocus - ref) < 1.0f, "Stereo : Focus 0 | 0 -> aucune voie creusee");
            setP (rig[1], "focusA", 100.0f); setP (rig[1], "focusB", 70.0f);
        }

        // Noms de piste : un message qui n'apporte que la couleur ne doit pas effacer le nom.
        {
            hub.setDawTrackInfo (rig[2].getSlot(), juce::String ("Lead Vox"), std::nullopt);
            hub.setDawTrackInfo (rig[2].getSlot(), std::nullopt, juce::Colours::red);
            check (hub.displayName (rig[2].getSlot()) == "Lead Vox", "Noms de piste : le nom reste apres une mise a jour de couleur seule");
        }

        // Ancien projet (Stéréo / Mid / Side) : conversion en M/S + Focus.
        {
            std::unique_ptr<BreathAudioProcessor> old (static_cast<BreathAudioProcessor*> (createPluginFilter()));
            auto* sm = old->getParams().getParameter ("stereoMode");
            sm->setValueNotifyingHost (sm->convertTo0to1 (2.0f));    // ancien « Side »
            juce::MemoryBlock mb;
            old->getStateInformation (mb);
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            auto st = juce::ValueTree::fromXml (*xml);
            auto params = st.getChildWithName (old->getParams().state.getType());
            for (auto* id : { "stereoType", "link", "focusA", "focusB" })
                params.removeChild (params.getChildWithProperty ("id", id), nullptr);
            juce::MemoryBlock mb2;
            juce::AudioProcessor::copyXmlToBinary (*st.createXml(), mb2);
            std::unique_ptr<BreathAudioProcessor> reloaded (static_cast<BreathAudioProcessor*> (createPluginFilter()));
            reloaded->setStateInformation (mb2.getData(), (int) mb2.getSize());
            const auto raw = [&] (const char* id) { return reloaded->getParams().getRawParameterValue (id)->load(); };
            std::printf ("        conversion : stereoType %.1f focusA %.1f focusB %.1f stereoMode %.1f\n", raw ("stereoType"), raw ("focusA"), raw ("focusB"), raw ("stereoMode"));
            check (raw ("stereoType") > 0.5f && raw ("focusA") < 0.5f && raw ("focusB") > 99.0f,
                   "Ancien projet : « Side » converti en M/S, Focus M 0 | S 100");
        }

        // Snapshots : copier A dans B.
        hub.selectSnapshot (0);
        auto a = hub.getRelation (rig[0].getSlot(), rig[1].getSlot());
        a.amount = 0.33f;
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), a);
        hub.copySnapshot (0, 1);
        hub.selectSnapshot (1);
        check (std::abs (hub.getRelation (rig[0].getSlot(), rig[1].getSlot()).amount - 0.33f) < 1e-4f, "Snapshots : A copie dans B");
        hub.selectSnapshot (0);
        a.amount = 0.8f;
        hub.setRelation (rig[0].getSlot(), rig[1].getSlot(), a);

        // Lookahead auto : latence = délai de synchro.
        rig[1].getParams().getParameter ("autoLookahead")->setValueNotifyingHost (1.0f);
        rig.render ({ 256 }, 0, 27);
        check (rig[1].getLatencyInSamples() == hub.getSyncDelay(), "Lookahead auto = delai de synchro (" + juce::String (hub.getSyncDelay()) + " echantillons)");
        rig[1].getParams().getParameter ("autoLookahead")->setValueNotifyingHost (0.0f);
        rig.render ({ 256 }, 0, 28);
    }

    // --- 5b. Pas de clic : kick + sub pur ----------------------------------
    {
        rig.stopAll();
        std::unique_ptr<BreathAudioProcessor> kick (static_cast<BreathAudioProcessor*> (createPluginFilter()));
        std::unique_ptr<BreathAudioProcessor> sub (static_cast<BreathAudioProcessor*> (createPluginFilter()));
        FakePlayHead hk, hs;
        for (auto* pr : { kick.get(), sub.get() })
        {
            pr->setPlayConfigDetails (2, 2, kSr, 256);
            pr->prepareToPlay (kSr, 256);
        }
        kick->setPlayHead (&hk);
        sub->setPlayHead (&hs);
        auto setRank = [] (BreathAudioProcessor& p, int r) { auto* q = p.getParams().getParameter ("priority"); q->setValueNotifyingHost (q->convertTo0to1 ((float) r)); };
        setRank (*kick, 1);
        setRank (*sub, 2);
        // On isole ce couple dans sa propre famille pour ne pas être influencé par les autres instances.
        if (auto* fam = kick->getParams().getParameter ("family")) { fam->setValueNotifyingHost (fam->convertTo0to1 (1.0f)); }
        if (auto* fam = sub->getParams().getParameter ("family"))  { fam->setValueNotifyingHost (fam->convertTo0to1 (1.0f)); }
        // Les autres instances ne doivent pas creuser ce couple : relations depuis rig désactivées.
        for (int i = 0; i < kNum; ++i)
            for (auto* pr : { kick.get(), sub.get() })
            {
                auto r = hub.getRelation (rig[i].getSlot(), pr->getSlot()); r.enabled = false; hub.setRelation (rig[i].getSlot(), pr->getSlot(), r);
            }
        auto rel = hub.getRelation (kick->getSlot(), sub->getSlot());
        rel.enabled = true; rel.amount = 1.0f; rel.maxReduction = 12.0f; rel.threshold = -40.0f;
        rel.attackMs = 2.0f; rel.releaseMs = 60.0f; rel.lowHz = 20.0f; rel.highHz = 200.0f; rel.resolution = 0.8f;
        hub.setRelation (kick->getSlot(), sub->getSlot(), rel);

        const int n = 256, len = ((int) kSr * 3 / n) * n;
        std::vector<float> out ((size_t) len);
        juce::AudioBuffer<float> bk (2, n), bs (2, n);
        juce::MidiBuffer m;
        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int pos = 0; pos < len; pos += n)
        {
            for (int k = 0; k < n; ++k)
            {
                const float t = (float) (pos + k) / (float) kSr;
                const float tb = std::fmod (t, 0.375f);
                const float kv = 0.9f * std::exp (-tb * 18.0f) * std::sin (twoPi * 55.0f * tb);
                const float sv = 0.8f * std::sin (twoPi * 45.0f * t);
                bk.setSample (0, k, kv); bk.setSample (1, k, kv);
                bs.setSample (0, k, sv); bs.setSample (1, k, sv);
            }
            hk.time = hs.time = pos;
            kick->processBlock (bk, m);
            sub->processBlock (bs, m);
            for (int k = 0; k < n; ++k)
                out[(size_t) (pos + k)] = bs.getSample (0, k);
        }
        // Dérivée seconde : une sinusoïde à 45 Hz en a une très petite ; un clic / zipper la fait exploser.
        const float ref = 0.8f * std::pow (twoPi * 45.0f / (float) kSr, 2.0f);
        float worst = 0.0f, minLevel = 1.0f;
        for (int i = (int) kSr / 2; i < len; ++i)
            worst = juce::jmax (worst, std::abs (out[(size_t) i] - 2.0f * out[(size_t) i - 1] + out[(size_t) i - 2]));
        for (int i = (int) kSr / 2; i < len; i += 64)
            minLevel = juce::jmin (minLevel, std::abs (out[(size_t) i]) + 0.0f);
        float peakDuck = 0.0f;
        {
            // réduction max observée (enveloppe crête par fenêtre de 1/45 s)
            const int w = (int) (kSr / 45.0);
            for (int i = (int) kSr / 2; i + w < len; i += w)
            {
                float pk = 0.0f;
                for (int k = 0; k < w; ++k) pk = juce::jmax (pk, std::abs (out[(size_t) (i + k)]));
                peakDuck = juce::jmax (peakDuck, 20.0f * std::log10 (0.8f / juce::jmax (pk, 1e-6f)));
            }
        }
        if (std::getenv ("BREATH_DEBUG_CLICKS"))
        {
            int shown = 0;
            for (int i = (int) kSr / 2; i < len && shown < 25; ++i)
            {
                const float d2 = std::abs (out[(size_t) i] - 2.0f * out[(size_t) i - 1] + out[(size_t) i - 2]);
                if (d2 > 10.0f * ref)
                {
                    std::printf ("          pic %6.1fx a %d (mod 32 = %d, mod 128 = %d, mod 512 = %d, dans le temps = %.3f s)\n",
                                 d2 / ref, i, i % 32, i % 128, i % 512, std::fmod ((double) i / kSr, 0.375));
                    ++shown;
                    i += 200;
                }
            }
        }
        std::printf ("        kick + sub : reduction max %.1f dB, discontinuite max = %.1f x la normale\n", peakDuck, worst / ref);
        check (peakDuck > 6.0f, "Kick + sub : le sub est bien creuse");
        check (worst / ref < 6.0f, "Kick + sub : aucun clic (avant correction : 127x la normale)");
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

        auto bench = [&] (int bandsChoice)
        {
            for (auto& pr : many)
                pr->getParams().getParameter ("bands")->setValueNotifyingHost ((float) bandsChoice);
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
            return ms / (seconds * 1000.0) * 100.0;
        };
        const double load = bench (0);
        std::printf ("        12 instances, 64 bandes  : %.1f %% d'un coeur (%.2f %% par instance)\n", load, load / 12.0);
        const double load128 = bench (1);
        std::printf ("        12 instances, 128 bandes : %.1f %% d'un coeur (%.2f %% par instance)\n", load128, load128 / 12.0);
        // Indicatif : dépend beaucoup de la machine. On ne signale qu'une dérive importante.
        check (load < 120.0, "Charge processeur (pire cas, 64 bandes) sans derive importante");
        check (load128 < 200.0, "Charge processeur (pire cas, 128 bandes) sans derive importante");
    }

    std::printf ("\n%s (%d echec(s))\n", failures == 0 ? "TOUS LES TESTS PASSENT" : "DES TESTS ECHOUENT", failures);
    return failures == 0 ? 0 : 1;
}
