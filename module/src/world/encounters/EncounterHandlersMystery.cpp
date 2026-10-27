#include "EncounterHandlers.h"
#include "BotMovement.h"
#include "EncounterConfig.h"
#include "EncounterNarrator.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <cmath>

namespace
{
    // Safe built-in WotLK 3.3.5a spell kit visual ID (arcane rune/scrying ground glow)
    constexpr uint32 DEFAULT_MYSTERY_SPELL_VISUAL_ID = 5106;

    float Dist2D(WorldObject const* a, WorldObject const* b)
    {
        return std::hypot(a->GetPositionX() - b->GetPositionX(), a->GetPositionY() - b->GetPositionY());
    }
}

namespace EncounterHandlers
{
    // =========================================================================
    // 7.13 Mystery Encounter
    // =========================================================================
    void UpdateMysteryEncounter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        Player* target = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive())
        {
            inst.state.Terminate(EncounterStopReason::PlayerLogout, nowMs);
            return;
        }

        // Non-combat by default: if pulled into combat, depart immediately
        if (bot->IsInCombat())
        {
            inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
            return;
        }

        EncounterConfig const& cfg = EncounterSettings::Get();

        // Apply optional configured mysterious visual override once
        if (!part.morphApplied && cfg.mysteryMorphDisplayId != 0)
        {
            part.originalDisplayId = bot->GetDisplayId();
            bot->SetDisplayId(cfg.mysteryMorphDisplayId);
            part.morphApplied = true;
        }

        float dist = Dist2D(bot, target);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (dist <= 38.0f && dist >= 16.0f && bot->IsWithinLOSInMap(target))
            {
                if (bot->IsMounted())
                    BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Manual);
                BotMovement::Stop(bot, part.moveToken);
                bot->SetFacingToObject(target);
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                return;
            }

            float angle = target->GetOrientation() + 0.6f;
            float vx = target->GetPositionX() + std::cos(angle) * 28.0f;
            float vy = target->GetPositionY() + std::sin(angle) * 28.0f;
            float vz = target->GetPositionZ();
            if (target->GetMap())
            {
                float gz = target->GetMap()->GetHeight(target->GetPhaseMask(), vx, vy, vz + 10.0f);
                if (gz > INVALID_HEIGHT)
                    vz = gz;
            }

            uint64 goalId = (inst.id << 8) | 1u;
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, vx, vy, vz, 5.0f, &part.moveToken, false);
            if (nav == NavStatus::Stuck)
                inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            BotMovement::Stop(bot, part.moveToken);
            bot->SetFacingToObject(target);

            uint32 elapsed = inst.state.PhaseElapsedMs(nowMs);

            // Stage 0 (at ~4s): kneel / channel and leave rune visual or temporary safe GameObject
            if (inst.subStage == 0 && elapsed >= 4000)
            {
                inst.subStage = 1;
                bot->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);

                uint32 visualId = cfg.mysterySpellVisualId != 0 ? cfg.mysterySpellVisualId : DEFAULT_MYSTERY_SPELL_VISUAL_ID;
                bot->SendPlaySpellVisual(visualId);

                if (cfg.mysteryRuneGameObjectEntry != 0 && sObjectMgr->GetGameObjectTemplate(cfg.mysteryRuneGameObjectEntry))
                {
                    if (bot->GetMap())
                    {
                        if (GameObject* gob = bot->SummonGameObject(
                                cfg.mysteryRuneGameObjectEntry,
                                bot->GetPositionX(),
                                bot->GetPositionY(),
                                bot->GetPositionZ(),
                                bot->GetOrientation(),
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                12))
                        {
                            part.temporaryGobGuid = gob->GetGUID();
                        }
                    }
                }
            }

            // Stage 1 (at ~6.5s): send contextual atmospheric narrator line
            if (inst.subStage == 1 && elapsed >= 6500)
            {
                inst.subStage = 2;
                if (!inst.introNarrated)
                {
                    if (EncounterNarrator::TrySendPlayerAtmosphere(target, bot, EncounterType::MysteryEncounter, elapsed, uint32(inst.id)))
                        inst.introNarrated = true;
                }
            }

            // Stage 2 (at ~11s): depart silently
            if (elapsed >= 11000 || dist < 12.0f)
            {
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            uint64 goalId = (inst.id << 8) | 99u;
            BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, part.originX, part.originY, part.originZ, 8.0f, &part.moveToken, false);
            if (inst.state.PhaseElapsedMs(nowMs) >= 6000)
                inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }
}
