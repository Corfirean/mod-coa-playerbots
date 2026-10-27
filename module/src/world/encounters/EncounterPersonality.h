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

    // Assassin Contract capability priority tier:
    //   3 = Stealth / Invisibility + Usable Opener + DPS role (e.g. Stalker/Ambush spec)
    //   2 = Stealth / Invisibility capable (has active stealth/invisibility spell)
    //   1 = DPS fallback without stealth (melee stalker / direct DPS)
    //   0 = Non-DPS fallback (Tank / Healer / Support without stealth)
    // Completely independent of vanilla class identity (designed for Ascension custom classes 12-32).
    static uint8 GetAssassinCapabilityTier(AssassinCapability const& cap)
    {
        if (cap.hasStealthOrInvis && cap.hasStealthOpener && cap.combatRole == EncounterCombatRole::Dps)
            return 3;
        if (cap.hasStealthOrInvis)
            return 2;
        if (cap.combatRole == EncounterCombatRole::Dps)
            return 1;
        return 0;
    }

    static char const* GetAssassinCapabilityPreferenceLabel(AssassinCapability const& cap)
    {
        uint8 tier = GetAssassinCapabilityTier(cap);
        if (tier == 3)
            return cap.isMeleeStalker ? "stealth_opener_melee" : "stealth_opener_stalker";
        if (tier == 2)
            return cap.combatRole == EncounterCombatRole::Dps ? "stealth_dps" : "stealth_support";
        if (tier == 1)
            return cap.isMeleeStalker ? "fallback_melee_dps" : "fallback_dps";
        return "fallback_non_dps";
    }

    // Scores an Assassin Contract candidate combining capability tier
    // (stealth+opener > stealth > dps fallback > non-dps fallback) with
    // combat role, melee affinity, and personality traits (aggressive/loner/patient).
    static float ScoreAssassinCandidate(
        WorldPersona const& persona,
        AssassinCapability const& cap)
    {
        DerivedEncounterTraits const t = Derive(persona);
        float personalityScore = std::clamp(
            t.aggressive * 0.40f + t.loner * 0.35f + t.patient * 0.25f,
            0.05f, 1.0f);

        uint8 tier = GetAssassinCapabilityTier(cap);
        float capabilityScore = 0.0f;
        switch (tier)
        {
            case 3:
                capabilityScore = 4.0f + (cap.isMeleeStalker ? 0.40f : 0.0f);
                break;
            case 2:
                capabilityScore = 2.5f + (cap.combatRole == EncounterCombatRole::Dps ? 0.35f : 0.0f)
                                       + (cap.isMeleeStalker ? 0.20f : 0.0f);
                break;
            case 1:
                capabilityScore = 1.0f + (cap.isMeleeStalker ? 0.30f : 0.0f);
                break;
            case 0:
            default:
                capabilityScore = 0.20f;
                break;
        }

        return capabilityScore + personalityScore;
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
