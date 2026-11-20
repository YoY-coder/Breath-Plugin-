#include "BreathHub.h"

namespace breath
{
    namespace
    {
        const juce::Colour fallbackPalette[] = {
            juce::Colour (0xffff7a45), juce::Colour (0xffffd23f), juce::Colour (0xff64d2ff),
            juce::Colour (0xffbf8bff), juce::Colour (0xff5ee08a), juce::Colour (0xffff6b9a),
            juce::Colour (0xff4fd1c5), juce::Colour (0xffff9f0a)
        };

        juce::ValueTree relationToTree (const juce::String& src, const juce::String& dst, const RelationSettings& r)
        {
            juce::ValueTree t ("Relation");
            t.setProperty ("src", src, nullptr);
            t.setProperty ("dst", dst, nullptr);
            t.setProperty ("enabled", r.enabled, nullptr);
            t.setProperty ("amount", r.amount, nullptr);
            t.setProperty ("maxRed", r.maxReduction, nullptr);
            t.setProperty ("threshold", r.threshold, nullptr);
            t.setProperty ("attack", r.attackMs, nullptr);
            t.setProperty ("release", r.releaseMs, nullptr);
            t.setProperty ("low", r.lowHz, nullptr);
            t.setProperty ("high", r.highHz, nullptr);
            t.setProperty ("resolution", r.resolution, nullptr);
            t.setProperty ("follow", r.followOrder, nullptr);
            t.setProperty ("assigned", r.assigned, nullptr);
            t.setProperty ("hard", r.hard, nullptr);
            t.setProperty ("masking", r.masking, nullptr);
            t.setProperty ("autoThr", r.autoThreshold, nullptr);
            t.setProperty ("autoOffset", r.autoOffset, nullptr);
            t.setProperty ("detailLow", r.detailLow, nullptr);
            t.setProperty ("detailHigh", r.detailHigh, nullptr);
            t.setProperty ("lowSlope", r.lowSlope, nullptr);
            t.setProperty ("highSlope", r.highSlope, nullptr);
            for (int i = 0; i < r.numNodes; ++i)
            {
                juce::ValueTree n ("Node");
                n.setProperty ("f", r.nodes[(size_t) i].freq, nullptr);
                n.setProperty ("g", r.nodes[(size_t) i].gain, nullptr);
                n.setProperty ("w", r.nodes[(size_t) i].width, nullptr);
                t.appendChild (n, nullptr);
            }
            return t;
        }

        RelationSettings treeToRelation (const juce::ValueTree& t)
        {
            RelationSettings r;
            r.enabled      = t.getProperty ("enabled", r.enabled);
            r.amount       = t.getProperty ("amount", r.amount);
            r.maxReduction = t.getProperty ("maxRed", r.maxReduction);
            r.threshold    = t.getProperty ("threshold", r.threshold);
            r.attackMs     = t.getProperty ("attack", r.attackMs);
            r.releaseMs    = t.getProperty ("release", r.releaseMs);
            r.lowHz        = t.getProperty ("low", r.lowHz);
            r.highHz       = t.getProperty ("high", r.highHz);
            r.resolution   = t.getProperty ("resolution", r.resolution);
            r.followOrder  = t.getProperty ("follow", r.followOrder);
            // Anciens projets : « désactivée » voulait dire « retirée ».
            if (t.hasProperty ("assigned")) r.assigned = t["assigned"];
            else { r.assigned = r.enabled; r.enabled = true; }
            r.hard         = t.getProperty ("hard", r.hard);
            r.masking      = t.getProperty ("masking", r.masking);
            r.autoThreshold = t.getProperty ("autoThr", r.autoThreshold);
            r.autoOffset   = t.getProperty ("autoOffset", r.autoOffset);
            r.detailLow    = t.getProperty ("detailLow", r.detailLow);
            r.detailHigh   = t.getProperty ("detailHigh", r.detailHigh);
            r.lowSlope     = t.getProperty ("lowSlope", r.lowSlope);
            r.highSlope    = t.getProperty ("highSlope", r.highSlope);
            for (const auto& n : t)
                if (n.hasType ("Node") && r.numNodes < kMaxNodes)
                    r.nodes[(size_t) r.numNodes++] = { (float) n["f"], (float) n["g"], (float) n.getProperty ("w", 1.0f) };
            return r;
        }

        // Action d'undo : remplace l'état complet par « avant » ou « après ».
        struct StateAction final : public juce::UndoableAction
        {
            StateAction (BreathHub& h, juce::ValueTree b, juce::ValueTree a) : hub (h), before (std::move (b)), after (std::move (a)) {}
            bool perform() override { hub.applyState (after, true); return true; }
            bool undo() override    { hub.applyState (before, true); return true; }
            BreathHub& hub;
            juce::ValueTree before, after;
        };
    }

    BreathHub::BreathHub() = default;
    BreathHub::~BreathHub() = default;

    //==========================================================================
    void BreathHub::reportRealtimeBlockSize (int numSamples) noexcept
    {
        const int d = roundUpTo (juce::jmax (numSamples, 1), 64);

        if (! syncDelayMeasured.exchange (true))
        {
            syncDelay.store (d);
            return;
        }

        int cur = syncDelay.load();
        while (d > cur && ! syncDelay.compare_exchange_weak (cur, d)) {}
    }

    void BreathHub::restoreSyncDelay (int samples)
    {
        if (samples <= 0)
            return;
        const bool wasMeasured = syncDelayMeasured.exchange (true);
        if (! wasMeasured || samples > syncDelay.load())
            syncDelay.store (samples);
    }

    //==========================================================================
    int BreathHub::registerInstance (HubClient* client, juce::Uuid& id, int& seq)
    {
        for (int i = 0; i < kMaxInstances; ++i)
        {
            if (slots[(size_t) i].active.load())
                continue;

            auto& in = infos[(size_t) i];
            in = {};
            in.id = id;
            in.seq = seq = nextSeq++;
            in.client = client;

            slots[(size_t) i].resetRing();
            slots[(size_t) i].creationSeq.store (in.seq);
            slots[(size_t) i].active.store (true);

            refresh();
            return i;
        }
        return -1;
    }

    void BreathHub::unregisterInstance (int s)
    {
        if (! isActive (s))
            return;

        slots[(size_t) s].active.store (false);
        infos[(size_t) s].client = nullptr;
        refresh();
    }

    void BreathHub::restoreIdentity (int s, juce::Uuid& id, int& seq, const juce::String& manualName)
    {
        if (! isActive (s))
            return;

        const auto oldId = infos[(size_t) s].id.toString();
        const auto wanted = id.toString();
        const int existing = slotForId (wanted);

        if (existing >= 0 && existing != s)
        {
            // Duplication : l'identifiant appartient déjà à une instance vivante.
            // La copie garde un nouvel identifiant et hérite des relations de l'original.
            const auto newId = infos[(size_t) s].id.toString();
            std::vector<std::pair<Key, RelationSettings>> copies;
            for (auto& [k, v] : relationModel)
            {
                if (k.first == wanted && k.second == wanted) { copies.push_back ({ { newId, newId }, v }); continue; }
                if (k.first == wanted)  copies.push_back ({ { newId, k.second }, v });
                if (k.second == wanted) copies.push_back ({ { k.first, newId }, v });
            }
            for (auto& c : copies)
                relationModel[c.first] = c.second;

            id = infos[(size_t) s].id;
            infos[(size_t) s].manualName = manualName;
            seq = infos[(size_t) s].seq;
        }
        else
        {
            infos[(size_t) s].id = id;
            infos[(size_t) s].manualName = manualName;

            if (seq > 0)
            {
                infos[(size_t) s].seq = seq;
                slots[(size_t) s].creationSeq.store (seq);
                nextSeq = juce::jmax (nextSeq, seq + 1);
            }
            else
            {
                seq = infos[(size_t) s].seq;
            }

            juce::ignoreUnused (oldId);
        }

        refresh();
    }

    void BreathHub::setDawTrackInfo (int s, const juce::String& name, std::optional<juce::Colour> colour)
    {
        if (! isActive (s))
            return;
        infos[(size_t) s].dawName = name;
        infos[(size_t) s].dawColour = colour;
        sendChangeMessage();
    }

    void BreathHub::setManualName (int s, const juce::String& name)
    {
        if (! isActive (s))
            return;
        infos[(size_t) s].manualName = name.trim();
        sendChangeMessage();
    }

    int BreathHub::numActive (int family) const noexcept
    {
        int n = 0;
        for (auto& sl : slots)
            n += (sl.active.load() && (family < 0 || sl.family.load() == family)) ? 1 : 0;
        return n;
    }

    void BreathHub::setFamily (int s, int f)
    {
        if (! isActive (s))
            return;
        f = juce::jlimit (0, kNumFamilies - 1, f);
        if (slots[(size_t) s].family.load() == f)
            return;
        slots[(size_t) s].family.store (f);
        refresh();
    }

    juce::String BreathHub::familyName (int f)
    {
        const char* names[] = { "Alpha", "B\xc3\xaata", "Gamma", "Sigma", "Om\xc3\xa9ga" };
        return juce::String::fromUTF8 (names[juce::jlimit (0, kNumFamilies - 1, f)]);
    }

    juce::String BreathHub::familyGlyph (int f)
    {
        const char* glyphs[] = { "\xce\xb1", "\xce\xb2", "\xce\xb3", "\xcf\x83", "\xcf\x89" };   // α β γ σ ω
        return juce::String::fromUTF8 (glyphs[juce::jlimit (0, kNumFamilies - 1, f)]);
    }

    juce::Colour BreathHub::familyColour (int f)
    {
        const juce::uint32 c[] = { 0xff0a84ff, 0xff2ec4b6, 0xffa27bff, 0xffffb340, 0xffff6b8b };
        return juce::Colour (c[juce::jlimit (0, kNumFamilies - 1, f)]);
    }

    juce::String BreathHub::displayName (int s) const
    {
        const auto& in = infos[(size_t) s];
        if (in.manualName.isNotEmpty()) return in.manualName;
        if (in.dawName.isNotEmpty())    return in.dawName;
        return "Piste " + juce::String (in.seq);
    }

    juce::Colour BreathHub::displayColour (int s) const
    {
        const auto& in = infos[(size_t) s];
        if (in.dawColour.has_value())
            return in.dawColour->withAlpha (1.0f);
        return fallbackPalette[(size_t) ((in.seq - 1 + 8) % 8)];
    }

    int BreathHub::slotForId (const juce::String& id) const
    {
        for (int i = 0; i < kMaxInstances; ++i)
            if (slots[(size_t) i].active.load() && infos[(size_t) i].id.toString() == id)
                return i;
        return -1;
    }

    //==========================================================================
    bool BreathHub::isAbove (int a, int b) const
    {
        const auto& ia = infos[(size_t) a];
        const auto& ib = infos[(size_t) b];
        const int ra = ia.client != nullptr ? ia.client->getRank() : 99;
        const int rb = ib.client != nullptr ? ib.client->getRank() : 99;
        // Règle stable : rang d'abord, puis ordre de création (la plus ancienne passe devant).
        return ra != rb ? ra < rb : ia.seq < ib.seq;
    }

    std::vector<int> BreathHub::hierarchy (int family) const
    {
        std::vector<int> order;
        for (int i = 0; i < kMaxInstances; ++i)
            if (slots[(size_t) i].active.load() && (family < 0 || slots[(size_t) i].family.load() == family))
                order.push_back (i);
        std::sort (order.begin(), order.end(), [this] (int a, int b) { return isAbove (a, b); });
        return order;
    }

    void BreathHub::reorder (const std::vector<int>& slotsInOrder)
    {
        beginEdit();
        int rank = 1;
        for (int s : slotsInOrder)
            if (isActive (s) && infos[(size_t) s].client != nullptr)
                infos[(size_t) s].client->setRankFromHub (juce::jmin (rank++, kMaxInstances));
        endEdit();
    }

    //==========================================================================
    RelationSettings BreathHub::getRelation (int upper, int lower) const
    {
        auto it = relationModel.find (keyFor (upper, lower));
        if (it != relationModel.end())
            return it->second;
        RelationSettings d;
        if (upper == lower)
        {
            // « Self » : non assignée par défaut, indépendante de la hiérarchie.
            d.assigned = false;
            d.followOrder = false;
        }
        return d;
    }

    bool BreathHub::isRelationVisible (int source, int target) const
    {
        if (! isActive (source) || ! isActive (target) || familyOf (source) != familyOf (target))
            return false;
        const auto r = getRelation (source, target);
        return source == target ? r.assigned : r.isVisible (isAbove (source, target));
    }

    bool BreathHub::isRelationActive (int source, int target) const
    {
        return isRelationVisible (source, target) && getRelation (source, target).enabled;
    }

    void BreathHub::setRelation (int upper, int lower, const RelationSettings& s)
    {
        if (! isActive (upper) || ! isActive (lower))
            return;
        relationModel[keyFor (upper, lower)] = s;
        relations[(size_t) upper][(size_t) lower].store (s);
        modelChanged (editDepth == 0);
    }

    void BreathHub::toggleRelation (int source, int target)
    {
        if (! isActive (source) || ! isActive (target))
            return;
        auto s = getRelation (source, target);
        if (source == target)
        {
            s.assigned = ! s.assigned;          // Self
            s.enabled = true;
        }
        else
        {
            const bool above = isAbove (source, target);
            if (s.isVisible (above))
            {
                if (above) s.assigned = false;   // relation « normale » : on la retire
                else       s.followOrder = true; // relation vers le haut : retour en veille
            }
            else
            {
                s.assigned = true;
                s.enabled = true;
                s.followOrder = above;           // vers le haut : active quelle que soit la hiérarchie
            }
        }
        beginEdit();
        setRelation (source, target, s);
        endEdit();
    }

    void BreathHub::pushRelationsToAudio()
    {
        for (int u = 0; u < kMaxInstances; ++u)
            for (int l = 0; l < kMaxInstances; ++l)
                if (slots[(size_t) u].active.load() && slots[(size_t) l].active.load())
                    relations[(size_t) u][(size_t) l].store (getRelation (u, l));
    }

    int BreathHub::getMaster (int family) const
    {
        family = juce::jlimit (0, kNumFamilies - 1, family);
        const int m = slotForId (masterIds[(size_t) family]);
        if (m >= 0 && slots[(size_t) m].family.load() == family)
            return m;

        int best = -1;
        for (int i = 0; i < kMaxInstances; ++i)
            if (slots[(size_t) i].active.load() && slots[(size_t) i].family.load() == family
                && (best < 0 || infos[(size_t) i].seq < infos[(size_t) best].seq))
                best = i;
        return best;
    }

    void BreathHub::setMaster (int s)
    {
        if (! isActive (s))
            return;
        beginEdit();
        masterIds[(size_t) familyOf (s)] = infos[(size_t) s].id.toString();
        modelChanged (false);
        endEdit();
    }

    void BreathHub::updateMasterSlot()
    {
        for (int f = 0; f < kNumFamilies; ++f)
            masterSlots[(size_t) f].store (getMaster (f));
    }

    float BreathHub::getGlobalMacro (int family) const
    {
        const int m = getMaster (family);
        return (m >= 0 && infos[(size_t) m].client != nullptr) ? infos[(size_t) m].client->getMacro() : 1.0f;
    }

    void BreathHub::setGlobalMacro (int family, float v)
    {
        const int m = getMaster (family);
        if (m >= 0 && infos[(size_t) m].client != nullptr)
            infos[(size_t) m].client->setMacroFromHub (v);
        sendChangeMessage();
    }

    bool BreathHub::getGlobalBypassAll (int family) const
    {
        const int m = getMaster (family);
        return m >= 0 && infos[(size_t) m].client != nullptr && infos[(size_t) m].client->getBypassAll();
    }

    void BreathHub::setGlobalBypassAll (int family, bool b)
    {
        const int m = getMaster (family);
        if (m >= 0 && infos[(size_t) m].client != nullptr)
            infos[(size_t) m].client->setBypassAllFromHub (b);
        sendChangeMessage();
    }

    //==========================================================================
    void BreathHub::refresh()
    {
        pushRelationsToAudio();
        updateMasterSlot();
        sendChangeMessage();
    }

    void BreathHub::modelChanged (bool storeSnapshot)
    {
        modelTime = juce::Time::currentTimeMillis();
        pushRelationsToAudio();
        updateMasterSlot();
        if (storeSnapshot)
            snapshots[(size_t) activeSnapshot] = captureState();
        sendChangeMessage();
    }

    void BreathHub::copySnapshot (int from, int to)
    {
        from = juce::jlimit (0, 1, from);
        to = juce::jlimit (0, 1, to);
        if (from == to)
            return;
        beginEdit();
        snapshots[(size_t) activeSnapshot] = captureState();
        snapshots[(size_t) to] = snapshots[(size_t) from].createCopy();
        if (activeSnapshot == to)
            applyState (snapshots[(size_t) to], true);
        endEdit();
    }

    void BreathHub::selectSnapshot (int index)
    {
        index = juce::jlimit (0, 1, index);
        if (index == activeSnapshot)
            return;

        beginEdit();
        snapshots[(size_t) activeSnapshot] = captureState();
        activeSnapshot = index;

        if (snapshots[(size_t) index].isValid())
            applyState (snapshots[(size_t) index], true);
        else
            snapshots[(size_t) index] = captureState();
        endEdit();
    }

    //==========================================================================
    void BreathHub::beginEdit()
    {
        if (editDepth++ == 0)
            editBefore = captureState();
    }

    void BreathHub::endEdit()
    {
        if (editDepth == 0 || --editDepth > 0)
            return;

        auto after = captureState();
        if (! after.isEquivalentTo (editBefore))
        {
            undoManager.beginNewTransaction();
            undoManager.perform (new StateAction (*this, editBefore, after));
        }
        snapshots[(size_t) activeSnapshot] = after;
        editBefore = {};
        sendChangeMessage();
    }

    void BreathHub::undo() { if (editDepth == 0) undoManager.undo(); }
    void BreathHub::redo() { if (editDepth == 0) undoManager.redo(); }

    juce::ValueTree BreathHub::captureState() const
    {
        juce::ValueTree st ("State");
        for (int f = 0; f < kNumFamilies; ++f)
            st.setProperty ("master" + juce::String (f), masterIds[(size_t) f], nullptr);
        st.setProperty ("snapshot", activeSnapshot, nullptr);
        for (int f = 0; f < kNumFamilies; ++f)
            st.setProperty ("macro" + juce::String (f), getGlobalMacro (f), nullptr);

        for (auto& [k, v] : relationModel)
            st.appendChild (relationToTree (k.first, k.second, v), nullptr);

        juce::ValueTree ranks ("Ranks");
        for (int i = 0; i < kMaxInstances; ++i)
        {
            if (! slots[(size_t) i].active.load() || infos[(size_t) i].client == nullptr)
                continue;
            juce::ValueTree r ("Rank");
            r.setProperty ("id", infos[(size_t) i].id.toString(), nullptr);
            r.setProperty ("rank", infos[(size_t) i].client->getRank(), nullptr);
            ranks.appendChild (r, nullptr);
        }
        st.appendChild (ranks, nullptr);
        return st;
    }

    void BreathHub::applyState (const juce::ValueTree& st, bool includeRanksAndMacro)
    {
        relationModel.clear();
        for (const auto& c : st)
            if (c.hasType ("Relation"))
                relationModel[{ c["src"].toString(), c["dst"].toString() }] = treeToRelation (c);

        for (int f = 0; f < kNumFamilies; ++f)
            masterIds[(size_t) f] = st.getProperty ("master" + juce::String (f), f == 0 ? st["master"].toString() : juce::String()).toString();

        if (includeRanksAndMacro)
        {
            const auto ranks = st.getChildWithName ("Ranks");
            for (const auto& r : ranks)
            {
                const int s = slotForId (r["id"].toString());
                if (s >= 0 && infos[(size_t) s].client != nullptr)
                    infos[(size_t) s].client->setRankFromHub ((int) r["rank"]);
            }

            for (int f = 0; f < kNumFamilies; ++f)
                if (st.hasProperty ("macro" + juce::String (f)))
                    setGlobalMacro (f, (float) st["macro" + juce::String (f)]);
        }

        modelChanged (false);
    }

    //==========================================================================
    juce::ValueTree BreathHub::exportModel() const
    {
        juce::ValueTree m ("Matrix");
        m.setProperty ("time", modelTime, nullptr);
        m.setProperty ("activeSnapshot", activeSnapshot, nullptr);
        m.setProperty ("syncDelay", syncDelay.load(), nullptr);
        m.appendChild (captureState(), nullptr);

        juce::ValueTree snaps ("Snapshots");
        for (int i = 0; i < 3; ++i)
        {
            juce::ValueTree s ("Snapshot");
            s.setProperty ("index", i, nullptr);
            if (snapshots[(size_t) i].isValid())
                s.appendChild (snapshots[(size_t) i].createCopy(), nullptr);
            snaps.appendChild (s, nullptr);
        }
        m.appendChild (snaps, nullptr);

        juce::ValueTree peers ("Peers");
        for (int i = 0; i < kMaxInstances; ++i)
            if (slots[(size_t) i].active.load())
            {
                juce::ValueTree p ("Peer");
                p.setProperty ("id", infos[(size_t) i].id.toString(), nullptr);
                peers.appendChild (p, nullptr);
            }
        m.appendChild (peers, nullptr);
        return m;
    }

    void BreathHub::mergeModel (const juce::ValueTree& m)
    {
        if (! m.hasType ("Matrix"))
            return;

        restoreSyncDelay ((int) m.getProperty ("syncDelay", 0));

        const juce::int64 t = m.getProperty ("time", 0);
        const auto st = m.getChildWithName ("State");

        if (t > modelTime || relationModel.empty())
        {
            // Modèle sauvegardé plus récent : on l'adopte (sans toucher aux rangs,
            // qui sont restaurés par chaque instance via son propre paramètre).
            for (const auto& c : st)
                if (c.hasType ("Relation"))
                    relationModel[{ c["src"].toString(), c["dst"].toString() }] = treeToRelation (c);

            for (int f = 0; f < kNumFamilies; ++f)
            {
                const auto id = st.getProperty ("master" + juce::String (f), f == 0 ? st["master"].toString() : juce::String()).toString();
                if (id.isNotEmpty())
                    masterIds[(size_t) f] = id;
            }

            activeSnapshot = juce::jlimit (0, 1, (int) m.getProperty ("activeSnapshot", 0));
            const auto snaps = m.getChildWithName ("Snapshots");
            for (const auto& s : snaps)
            {
                const int i = juce::jlimit (0, 2, (int) s["index"]);
                snapshots[(size_t) i] = s.getNumChildren() > 0 ? s.getChild (0).createCopy() : juce::ValueTree();
            }
        }
        else
        {
            // Modèle plus ancien : on ne complète que les relations inconnues.
            for (const auto& c : st)
                if (c.hasType ("Relation"))
                {
                    Key k { c["src"].toString(), c["dst"].toString() };
                    if (relationModel.find (k) == relationModel.end())
                        relationModel[k] = treeToRelation (c);
                }
        }

        modelTime = juce::jmax (modelTime, t);
        pushRelationsToAudio();
        updateMasterSlot();
        sendChangeMessage();
    }
}
