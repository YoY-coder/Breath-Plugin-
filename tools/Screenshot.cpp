// Outil de développement : simule 4 pistes, ouvre l'interface hors DAW et l'enregistre en PNG.
// Usage : BreathScreenshot <dossier de sortie>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

namespace
{
    struct Head final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying (false); return p; }
    };

    float sample (int track, int64_t i, uint32_t& seed)
    {
        const float t = (float) i / 48000.0f, twoPi = juce::MathConstants<float>::twoPi;
        switch (track)
        {
            case 0: { const float tb = std::fmod (t, 0.5f); return 0.9f * std::exp (-tb * 6.0f) * std::sin (twoPi * (55.0f + 80.0f * std::exp (-tb * 30.0f)) * tb); }
            case 1: return 0.4f * std::sin (twoPi * 72.0f * t) + 0.25f * std::sin (twoPi * 144.0f * t) + 0.1f * std::sin (twoPi * 430.0f * t);
            case 2: { const float ph = std::fmod (220.0f * t + 0.8f * std::sin (twoPi * 5.0f * t), 1.0f); return 0.25f * (2.0f * ph - 1.0f); }
            default: seed = seed * 1664525u + 1013904223u; return 0.12f * ((float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f);
        }
    }

    void save (juce::Component& c, const juce::File& f)
    {
        auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        f.deleteFile();
        juce::FileOutputStream stream (f);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : ".");
    outDir.createDirectory();

    const char* names[] = { "Kick", "Basse", "Voix lead", "Synth" };
    const juce::uint32 cols[] = { 0xffff7a45, 0xffffd23f, 0xff64d2ff, 0xffbf8bff };

    std::vector<std::unique_ptr<BreathAudioProcessor>> procs;
    Head head;
    for (int i = 0; i < 4; ++i)
    {
        procs.emplace_back (static_cast<BreathAudioProcessor*> (createPluginFilter()));
        auto& p = *procs.back();
        p.setPlayConfigDetails (2, 2, 48000.0, 256);
        p.prepareToPlay (48000.0, 256);
        p.setPlayHead (&head);
        juce::AudioProcessor::TrackProperties tp;
        tp.name = juce::String (names[i]);
        tp.colourARGB = cols[i];
        p.updateTrackProperties (tp);
    }

    // Laisse passer le placement automatique des nouvelles instances, puis fixe les rangs.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
    for (int i = 0; i < 4; ++i)
    {
        auto* prio = procs[(size_t) i]->getParams().getParameter ("priority");
        prio->setValueNotifyingHost (prio->convertTo0to1 ((float) (i + 1)));
    }

    auto& hub = procs[0]->getHub();
    {
        auto s = hub.getRelation (procs[0]->getSlot(), procs[1]->getSlot());
        s.lowHz = 30.0f; s.highHz = 180.0f; s.threshold = -38.0f; s.amount = 0.9f;
        hub.setRelation (procs[0]->getSlot(), procs[1]->getSlot(), s);
        s.numNodes = 1;
        s.nodes[0] = { 110.0f, -3.5f, 0.8f };
        s.lowSlope = 0.15f; s.highSlope = 1.2f;
        hub.setRelation (procs[0]->getSlot(), procs[1]->getSlot(), s);
        auto v = hub.getRelation (procs[2]->getSlot(), procs[1]->getSlot());
        v.lowHz = 300.0f; v.highHz = 3000.0f; v.followOrder = false; v.threshold = -45.0f; v.enabled = false;
        hub.setRelation (procs[2]->getSlot(), procs[1]->getSlot(), v);
        auto d = hub.getRelation (procs[1]->getSlot(), procs[2]->getSlot());
        d.enabled = false;
        hub.setRelation (procs[1]->getSlot(), procs[2]->getSlot(), d);
    }
    procs[1]->getParams().getParameter ("autoMakeup")->setValueNotifyingHost (1.0f);
    {
        // Self sur la basse (anti-résonances) pour l'aperçu.
        auto self = hub.getRelation (procs[1]->getSlot(), procs[1]->getSlot());
        self.assigned = true; self.autoThreshold = true; self.lowHz = 200.0f; self.highHz = 8000.0f;
        hub.setRelation (procs[1]->getSlot(), procs[1]->getSlot(), self);
    }
    auto* la = procs[1]->getParams().getParameter ("lookahead");
    la->setValueNotifyingHost (la->convertTo0to1 (5.0f));

    // Audio en continu dans un thread, à peu près en temps réel.
    std::atomic<bool> run { true };
    std::thread audio ([&]
    {
        uint32_t seed = 1;
        int64_t pos = 0;
        juce::AudioBuffer<float> b (2, 256);
        juce::MidiBuffer m;
        while (run)
        {
            for (int i = 0; i < 4; ++i)
            {
                for (int k = 0; k < 256; ++k)
                {
                    const float x = sample (i, pos + k, seed);
                    b.setSample (0, k, x);
                    b.setSample (1, k, x);
                }
                procs[(size_t) i]->processBlock (b, m);
            }
            pos += 256;
            std::this_thread::sleep_for (std::chrono::microseconds (5333));
        }
    });

    auto* proc = procs[1].get();
    proc->currentView = 0;
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditorAndMakeActive());
    editor->addToDesktop (0);
    editor->setVisible (true);

    // Capture au moment d'un coup de kick (≈ 60 ms après le début d'un temps).
    auto waitForKick = [&]
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        for (int tries = 0; tries < 200; ++tries)
        {
            float m = 0.0f;
            for (auto& v : hub.slot (proc->getSlot()).reductionCurve) m = juce::jmax (m, v.load());
            if (m > 4.0f) break;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    };

    waitForKick();
    save (*editor, outDir.getChildFile ("ui-instance.png"));

    if (auto* ed = dynamic_cast<BreathAudioProcessorEditor*> (editor.get()))
        ed->showView (1);
    proc->selectedUpper = procs[0]->getSlot();
    proc->selectedLower = procs[1]->getSlot();
    hub.sendChangeMessage();
    waitForKick();
    save (*editor, outDir.getChildFile ("ui-matrix.png"));

    // Petite taille (vérifie le redimensionnement)
    editor->setSize (600, 373);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    save (*editor, outDir.getChildFile ("ui-matrix-small.png"));

    run = false;
    audio.join();
    editor.reset();
    procs.clear();
    return 0;
}
