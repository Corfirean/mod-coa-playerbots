/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- Personality affinity & early retreat rules (§5 Personality integration).
 *
 * Uses the existing WorldPersona profile (grinding, patience, sociability, questing, gathering,
 * fishing, lean) without creating a duplicate personality framework:
 *   - aggressive -> Assassin, Bounty Hunter, Faction Skirmish
 *   - cautious   -> The Watcher, Patrol Ambush, Hunter
 *   - sociable   -> Caravan, Travelling Party, Faction Skirmish, Road Duel
 *   - loner      -> Assassin, Mystery Encounter, Deserter
 *   - patient    -> Stalking (Assassin, Watcher, Ambush)
 *   - adventurous-> Rare Hunter, Rival Adventuring Party
 *   - cowardly/cautious -> May retreat early from PvP encounters when low HP
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_PERSONALITY_H
#define COA_PLAYERBOTS_ENCOUNTER_PERSONALITY_H

#include "EncounterTypes.h"
#include "WorldBrain.h"
#include <algorithm>

struct DerivedEncounterTraits
{
    float aggressive = 0.5f;
    float cautious = 0.5f;
    float sociable = 0.5f;
    float loner = 0.5f;
    float patient = 0.5f;
    float adventurous = 0.5f;
};

class EncounterPersonality
{
public:
    static DerivedEncounterTraits Derive(WorldPersona const& p)
    {
        DerivedEncounterTraits t;
        // Normalise existing WorldPersona weights (0..100)
        float normGrind = std::clamp(float(p.grinding) / 100.0f, 0.0f, 1.0f);
        float normPatience = std::clamp(float(p.patience) / 100.0f, 0.0f, 1.0f);
        float normQuest = std::clamp(float(p.questing) / 100.0f, 0.0f, 1.0f);
        float normGather = std::clamp(float(p.gathering) / 100.0f, 0.0f, 1.0f);
        float soc = std::clamp(float(p.sociability) / 100.0f, 0.0f, 1.0f);

        t.aggressive = std::clamp(normGrind * 0.75f + (1.0f - normPatience) * 0.25f, 0.0f, 1.0f);
        t.cautious = std::clamp(normPatience * 0.65f + (1.0f - normGrind) * 0.35f, 0.0f, 1.0f);
        t.sociable = soc;
        t.loner = 1.0f - soc;
        t.patient = normPatience;
        t.adventurous = std::clamp((normQuest + normGather) * 0.5f + (p.lean == 5 ? 0.25f : 0.0f), 0.0f, 1.0f);
        return t;
    }

    static constexpr uint8 ENC_CLASS_ROGUE = 4;
    static constexpr uint8 ENC_CLASS_DRUID = 11;

    // Assassin Contract class priority tier (§1):
    //   3 = CLASS_ROGUE (highest priority)
    //   2 = stealth-capable CLASS_DRUID (Prowl available)
    //   1 = fallback non-stealth class (only used when no Rogue or stealth Druid is eligible)
    static uint8 GetAssassinClassTier(uint8 classId, bool hasStealthSpell)
    {
        if (classId == ENC_CLASS_ROGUE)
            return 3;
        if (classId == ENC_CLASS_DRUID && hasStealthSpell)
            return 2;
        return 1;
    }

    static char const* GetAssassinClassPreferenceLabel(uint8 classId, bool hasStealthSpell)
    {
        uint8 tier = GetAssassinClassTier(classId, hasStealthSpell);
        if (tier == 3)
            return "rogue_primary";
        if (tier == 2)
            return "druid_stealth";
        return "fallback_non_stealth";
    }

    // Scores an Assassin Contract candidate combining strict class tier preference
    // (Rogue > stealth Druid > fallback) with existing personality traits (aggressive/loner/patient).
    static float ScoreAssassinCandidate(
        WorldPersona const& persona,
        uint8 classId,
        bool hasStealthSpell)
    {
        DerivedEncounterTraits const t = Derive(persona);
        float personalityScore = std::clamp(
            t.aggressive * 0.40f + t.loner * 0.35f + t.patient * 0.25f,
            0.05f, 1.0f);

        uint8 tier = GetAssassinClassTier(classId, hasStealthSpell);
        float classBoost = (tier == 3) ? 2.0f : ((tier == 2) ? 1.0f : 0.0f);
        return classBoost + personalityScore;
    }

    // Returns an affinity score in [0.0, 2.0] for selecting a bot for a given encounter & role.
    static float ScoreAffinity(
        EncounterType type,
        EncounterRole role,
        WorldPersona const& persona,
        bool isStealthCapable = false)
    {
        DerivedEncounterTraits const t = Derive(persona);
        float score = 0.5f;

        switch (type)
        {
            case EncounterType::AssassinContract:
                score = t.aggressive * 0.40f + t.loner * 0.35f + t.patient * 0.25f;
                if (isStealthCapable)
                    score += 0.45f; // Strong preference for rogue / stealth-capable bots
                break;

            case EncounterType::TheWatcher:
                score = t.cautious * 0.45f + t.patient * 0.35f + t.loner * 0.20f;
                if (isStealthCapable)
                    score += 0.30f;
                break;

            case EncounterType::FactionSkirmish:
                score = t.aggressive * 0.55f + t.sociable * 0.45f;
                break;

            case EncounterType::PatrolAmbush:
                if (role == EncounterRole::Ambusher)
                {
                    score = t.cautious * 0.40f + t.patient * 0.35f + t.aggressive * 0.25f;
                    if (isStealthCapable)
                        score += 0.25f;
                }
                else
                {
                    score = t.sociable * 0.60f + t.adventurous * 0.40f;
                }
                break;

            case EncounterType::BountyHunter:
                score = t.aggressive * 0.65f + t.adventurous * 0.35f;
                break;

            case EncounterType::Nemesis:
                score = t.aggressive * 0.60f + t.loner * 0.40f;
                break;

            case EncounterType::RoadDuel:
                score = t.sociable * 0.50f + t.aggressive * 0.50f;
                break;

            case EncounterType::Caravan:
                score = t.sociable * 0.70f + t.cautious * 0.30f;
                break;

            case EncounterType::Deserter:
                score = t.loner * 0.55f + t.cautious * 0.45f;
                break;

            case EncounterType::HunterAndPrey:
                if (role == EncounterRole::Hunter)
                    score = t.cautious * 0.35f + t.aggressive * 0.40f + t.patient * 0.25f;
                else
                    score = t.cautious * 0.65f + t.loner * 0.35f;
                break;

            case EncounterType::RivalAdventuringParty:
                score = t.sociable * 0.55f + t.adventurous * 0.45f;
                break;

            case EncounterType::RareHunter:
                score = t.adventurous * 0.65f + t.aggressive * 0.35f;
                break;

            case EncounterType::MysteryEncounter:
                score = t.loner * 0.55f + t.patient * 0.30f + t.adventurous * 0.15f;
                break;

            default:
                break;
        }

        return std::clamp(score, 0.05f, 2.0f);
    }

    // Cowardly / cautious bots may disengage and retreat early from PvP encounters when health drops low.
    static bool ShouldRetreatFromPvP(float cautiousness, float healthPct, uint32 nearbyHostiles = 1)
    {
        float threshold = 18.0f + std::clamp(cautiousness, 0.0f, 1.0f) * 20.0f; // 18%..38% HP
        if (nearbyHostiles > 2)
            threshold += 8.0f;
        return healthPct > 0.0f && healthPct <= threshold && cautiousness >= 0.55f;
    }
};

#endif // COA_PLAYERBOTS_ENCOUNTER_PERSONALITY_H
