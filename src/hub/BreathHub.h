#pragma once

#include "../Common.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <map>
#include <optional>

namespace breath
{
    //==========================================================================
    // Ce que le Hub doit pouvoir demander à une instance (thread de l'interface).
    struct HubClient
    {
        virtual ~HubClient() = default;
        virtual int   getRank() const = 0;           // paramètre « Priorité » (1..16)
        virtual void  setRankFromHub (int rank) = 0;
        virtual float getMacro() const = 0;          // paramètre « Breath » (0..1)
        virtual void  setMacroFromHub (float v) = 0;
        virtual bool  getBypassAll() const = 0;
        virtual void  setBypassAllFromHub (bool b) = 0;
    };

    //==========================================================================
    // Une trame d'analyse publiée par une instance (lisible sans lock).
    struct FrameData
    {
        float bands[kNumBands];
        float rank = 1.0f;
        float macro = 1.0f;
        float bypassAll = 0.0f;
    };

    struct Frame
    {
        std::atomic<int64_t> seq { -1 };                // numéro de trame (‑2 pendant l'écriture)
        std::atomic<int64_t> segment { INT64_MIN };     // début du segment de lecture
        std::array<std::atomic<float>, kNumBands> bands {};
        std::atomic<float> rank { 1.0f }, macro { 1.0f }, bypassAll { 0.0f };

        void write (int64_t index, int64_t seg, const FrameData& d) noexcept
        {
            seq.store (-2, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);
            segment.store (seg, std::memory_order_relaxed);
            for (int b = 0; b < kNumBands; ++b)
                bands[(size_t) b].store (d.bands[b], std::memory_order_relaxed);
            rank.store (d.rank, std::memory_order_relaxed);
            macro.store (d.macro, std::memory_order_relaxed);
            bypassAll.store (d.bypassAll, std::memory_order_relaxed);
            seq.store (index, std::memory_order_release);
        }

        // Lecture « seqlock » : vrai si la trame demandée a été lue de façon cohérente.
        bool read (int64_t index, int64_t seg, FrameData& out) const noexcept
        {
            const auto s1 = seq.load (std::memory_order_acquire);
            if (s1 != index || (seg != INT64_MIN && segment.load (std::memory_order_relaxed) != seg))
                return false;
            for (int b = 0; b < kNumBands; ++b)
                out.bands[b] = bands[(size_t) b].load (std::memory_order_relaxed);
            out.rank = rank.load (std::memory_order_relaxed);
            out.macro = macro.load (std::memory_order_relaxed);
            out.bypassAll = bypassAll.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            return seq.load (std::memory_order_relaxed) == s1;
        }
    };

    // Emplacement fixe d'une instance dans le Hub.
    struct Slot
    {
        std::atomic<bool> active { false };
        std::atomic<int>  creationSeq { 0 };
        std::array<Frame, kRingFrames> ring;
        Frame live;                                     // dernière trame (affichage, transport arrêté)
        std::atomic<int64_t> liveCount { 0 };
        std::atomic<int64_t> segmentStart { INT64_MIN };
        std::atomic<int64_t> progress { 0 };            // échantillons traités (détection de blocage)
        std::array<std::atomic<float>, kNumBands> reductionCurve {};  // réduction appliquée (dB)
        std::array<std::atomic<float>, kMaxInstances> activityFrom {}; // réduction causée par chaque slot
        std::atomic<bool> offlineDesync { false };

        // Invalide les trames publiées (nouveau départ de lecture).
        void clearFrames() noexcept
        {
            for (auto& f : ring)
                f.seq.store (-1, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);
        }

        void resetRing() noexcept
        {
            for (auto& f : ring)
                f.seq.store (-1, std::memory_order_relaxed);
            live.seq.store (-1, std::memory_order_relaxed);
            liveCount.store (0);
            segmentStart.store (INT64_MIN);
            for (auto& r : reductionCurve) r.store (0.0f);
            for (auto& a : activityFrom) a.store (0.0f);
            offlineDesync.store (false);
        }
    };

    //==========================================================================
    // Mémoire partagée entre toutes les instances du processus.
    class BreathHub : public juce::ChangeBroadcaster
    {
    public:
        BreathHub();
        ~BreathHub() override;

        // --- Côté audio (sans lock) ------------------------------------------
        Slot& slot (int i) noexcept                             { return slots[(size_t) i]; }
        const Slot& slot (int i) const noexcept                 { return slots[(size_t) i]; }
        AtomicRelation& relation (int upper, int lower) noexcept { return relations[(size_t) upper][(size_t) lower]; }
        int  getMasterSlot() const noexcept                     { return masterSlot.load (std::memory_order_relaxed); }
        int  getSyncDelay() const noexcept                      { return syncDelay.load (std::memory_order_relaxed); }
        void reportRealtimeBlockSize (int numSamples) noexcept;

        // --- Côté interface (thread de l'interface uniquement) ---------------
        struct InstanceInfo
        {
            juce::Uuid id;
            int seq = 0;
            juce::String dawName, manualName;
            std::optional<juce::Colour> dawColour;
            HubClient* client = nullptr;
        };

        // Inscription d'une nouvelle instance : retourne son slot (‑1 si limite atteinte).
        int  registerInstance (HubClient* client, juce::Uuid& id, int& seq);
        void unregisterInstance (int slotIndex);
        // Restauration depuis l'état sauvegardé : gère la duplication (identifiant déjà pris).
        void restoreIdentity (int slotIndex, juce::Uuid& id, int& seq, const juce::String& manualName);

        void setDawTrackInfo (int slotIndex, const juce::String& name, std::optional<juce::Colour> colour);
        void setManualName (int slotIndex, const juce::String& name);

        bool isActive (int s) const noexcept { return s >= 0 && s < kMaxInstances && slots[(size_t) s].active.load(); }
        int  numActive() const noexcept;
        const InstanceInfo& info (int s) const { return infos[(size_t) s]; }
        juce::String displayName (int s) const;
        juce::Colour displayColour (int s) const;

        // Hiérarchie (du plus prioritaire au moins prioritaire), selon les paramètres Priorité.
        std::vector<int> hierarchy() const;
        bool isAbove (int a, int b) const;   // a est-il au-dessus de b ?
        void reorder (const std::vector<int>& slotsInOrder);  // glisser-déposer

        RelationSettings getRelation (int upper, int lower) const;
        // Relation active (en tenant compte de « suivre la hiérarchie »).
        bool isRelationActive (int source, int target) const { return isActive (source) && isActive (target) && source != target
                                                                       && getRelation (source, target).isActive (isAbove (source, target)); }
        // Active / désactive une relation depuis la grille (gère « suivre la hiérarchie »).
        void toggleRelation (int source, int target);
        void setRelation (int upper, int lower, const RelationSettings& s);

        int  getMaster() const;              // slot de l'instance maîtresse
        void setMaster (int slotIndex);

        // Macro et Bypass All globaux (portés par l'instance maîtresse).
        float getGlobalMacro() const;
        void  setGlobalMacro (float v);
        bool  getGlobalBypassAll() const;
        void  setGlobalBypassAll (bool b);

        // Snapshots A / B / C.
        int  getActiveSnapshot() const noexcept { return activeSnapshot; }
        void selectSnapshot (int index);
        bool snapshotIsEmpty (int index) const { return ! snapshots[(size_t) index].isValid(); }

        // Undo / Redo : regrouper une modification (geste) en une étape.
        void beginEdit();
        void endEdit();
        bool canUndo() const { return undoManager.canUndo(); }
        bool canRedo() const { return undoManager.canRedo(); }
        void undo();
        void redo();

        // Sauvegarde / restauration du modèle partagé (dans l'état de chaque instance).
        juce::ValueTree exportModel() const;
        void mergeModel (const juce::ValueTree& model);
        void restoreSyncDelay (int samples);

        // Interne : appliqué par les actions d'undo.
        juce::ValueTree captureState() const;
        void applyState (const juce::ValueTree& state, bool includeRanksAndMacro);

    private:
        using Key = std::pair<juce::String, juce::String>;   // (id supérieur, id inférieur)
        Key keyFor (int upper, int lower) const { return { infos[(size_t) upper].id.toString(), infos[(size_t) lower].id.toString() }; }

        void pushRelationsToAudio();
        void updateMasterSlot();
        void modelChanged (bool storeSnapshot = true);
        void refresh();
        int  slotForId (const juce::String& id) const;

        std::array<Slot, kMaxInstances> slots;
        std::array<std::array<AtomicRelation, kMaxInstances>, kMaxInstances> relations;
        std::atomic<int> masterSlot { -1 };
        std::atomic<int> syncDelay { kDefaultSyncDelay };
        std::atomic<bool> syncDelayMeasured { false };

        std::array<InstanceInfo, kMaxInstances> infos;
        std::map<Key, RelationSettings> relationModel;
        juce::String masterId;
        std::array<juce::ValueTree, 3> snapshots;
        int activeSnapshot = 0;
        int nextSeq = 1;
        juce::int64 modelTime = 0;

        juce::UndoManager undoManager { 0, 200 };
        juce::ValueTree editBefore;
        int editDepth = 0;

        JUCE_DECLARE_NON_COPYABLE (BreathHub)
    };
}
