#include "WorldSocial.h"
#include "BotMovement.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "SocialRules.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldExecutor.h"
#include <algorithm>
#include <vector>

using namespace WorldBrainInternal;

namespace
{
    // Per-bot scan cadence; staggered by the bot's own rolls so bots never scan in step.
    constexpr uint32 CHECK_MIN_MS = 2500;
    constexpr uint32 CHECK_MAX_MS = 5000;
    // The spellbook scan for a resurrection spell is not repeated more often than this.
    constexpr uint32 REZ_SPELL_RECHECK_MS = 10 * MINUTE * IN_MILLISECONDS;
    // A resurrection cast owns the bot at most this long (cast times are well under it).
    constexpr uint32 REZ_CAST_MAX_MS = 15000;
    // An assist fight owns the bot at most this long -- a generous cap for a real fight, but still
    // a bound so an evaded/stuck/unkillable target can't pin the brain off its task forever.
    constexpr uint32 ASSIST_MAX_MS = 60000;
    // Someone the bot decided about (helped, resurrected, or chose not to) is not reconsidered
    // for this long -- no flip-flopping over the same fight every few seconds.
    constexpr uint32 DECIDED_MEMORY_MS = 60000;
    constexpr float REZ_SEARCH_RADIUS = 30.0f;

    bool SameSide(Player* bot, Player* other)
    {
        return bot->GetTeamId() == other->GetTeamId() && !bot->IsHostileTo(other);
    }

    // Help only happens while the bot is between things: walking somewhere or looking for a target,
    // or between tasks. Never in the middle of its own fight, loot, or a quest-giver conversation.
    bool FreeToHelp(Player* bot, BrainState const& state)
    {
        if (bot->IsInCombat() || bot->IsNonMeleeSpellCast(false) || state.opportunityActive || bot->IsInFlight())
            return false;
        if (!state.task.IsValid())
            return true;
        switch (state.task.type)
        {
            case WorldTaskType::QuestObjective:
                return state.task.phase == TaskPhase::TravelToArea || state.task.phase == TaskPhase::Search ||
                    state.task.phase == TaskPhase::Approach;
            case WorldTaskType::Travel:
                return state.task.phase == TaskPhase::TravelToArea;
            default:
                return false;
        }
    }

    // The best resurrection spell the bot knows (highest spell level), cached.
    uint32 ResurrectSpell(Player* bot, BrainState& state, uint32 now)
    {
        if (state.rezSpellCheckedMs && now - state.rezSpellCheckedMs < REZ_SPELL_RECHECK_MS)
            return state.rezSpellId;
        state.rezSpellCheckedMs = now;
        state.rezSpellId = 0;

        uint32 bestLevel = 0;
        for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
        {
            if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                continue;
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo || spellInfo->IsPassive())
                continue;
            if (!spellInfo->HasEffect(SPELL_EFFECT_RESURRECT) && !spellInfo->HasEffect(SPELL_EFFECT_RESURRECT_NEW))
                continue;
            if (!state.rezSpellId || spellInfo->SpellLevel > bestLevel)
            {
                state.rezSpellId = spellId;
                bestLevel = spellInfo->SpellLevel;
            }
        }
        return state.rezSpellId;
    }

    // Helping is an interruption from outside the task, like an ambient errand: the task is paused
    // (its clocks stop, its legs, target and heatmap "incoming" are let go) and the brain resumes
    // it once the help is over (WorldBrain::Update), with its clocks moved on by as much. An
    // objective that was walking up to its target looks for one again.
    void StepAwayFromTask(Player* bot, BrainState& state, char const* why)
    {
        if (!state.task.IsValid())
            return;
        WorldExecutor::PauseTask(bot, state, why);
        state.pausedBySocial = true;
    }

    bool TryResurrect(Player* bot, BrainState& state, std::vector<Player*> const& players, uint32 now)
    {
        uint32 spellId = ResurrectSpell(bot, state, now);
        if (!spellId)
            return false;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            return false;
        float range = std::max(5.0f, spellInfo->GetMaxRange(true, bot));

        for (Player* other : players)
        {
            if (other == bot || other->IsAlive() || other->HasPlayerFlag(PLAYER_FLAGS_GHOST) || other->isResurrectRequested())
                continue;
            uint64 key = other->GetGUID().GetRawValue();
            if (!SameSide(bot, other) || state.failures.Has(FailKind::Target, key, now))
                continue;
            if (!bot->IsWithinDistInMap(other, range) || !bot->IsWithinLOSInMap(other))
                continue;

            state.failures.Remember(FailKind::Target, key, now, DECIDED_MEMORY_MS, uint8(FailureReason::None));
            WorldExecutor::Dismount(bot, state);
            StepAwayFromTask(bot, state, "resurrecting someone");
            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
            bot->SetFacingToObject(other);

            SpellCastResult result = bot->CastSpell(other, spellId, false);
            LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' casts resurrection {} on '{}' (result {}).", bot->GetName(), spellId,
                other->GetName(), uint32(result));
            if (result != SPELL_CAST_OK)
                continue;

            state.socialActive = true;
            state.socialUntilMs = now + REZ_CAST_MAX_MS;
            Count(state.metrics, &WorldMetrics::resurrections);
            NoteEvent(state, Acore::StringFormat("resurrecting {}", other->GetName()));
            return true;
        }
        return false;
    }

    bool TryAssist(Player* bot, BrainState& state, std::vector<Player*> const& players, uint32 now)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        AssistRules rules;
        rules.radius = cfg.helpRadius;
        rules.healthPct = cfg.helpHealthPct;
        rules.maxLevelAbove = uint8(std::min<uint32>(cfg.maxLevelAbove, 10));

        for (Player* other : players)
        {
            if (other == bot || !other->IsAlive() || !other->IsInCombat())
                continue;
            uint64 key = other->GetGUID().GetRawValue();
            if (state.failures.Has(FailKind::Target, key, now))
                continue;

            for (Unit* attacker : other->getAttackers())
            {
                Creature* creature = attacker ? attacker->ToCreature() : nullptr;
                if (!creature || !creature->IsAlive() || !bot->IsValidAttackTarget(creature))
                    continue;

                AssistInput in;
                in.sameSide = SameSide(bot, other);
                in.victimInCombat = true;
                in.victimHealthPct = other->GetHealthPct();
                in.attackerIsCreature = !creature->IsControlledByPlayer();
                in.attackerTappedByVictim = creature->hasLootRecipient() && creature->isTappedBy(other);
                in.attackerElite = creature->isElite();
                in.attackerWorldBoss = creature->isWorldBoss();
                in.attackerLevel = creature->GetLevel();
                in.botLevel = bot->GetLevel();
                in.distance = bot->GetDistance(other);
                if (!SocialRules::ShouldAssist(in, rules))
                    continue;

                // Decided either way now; sociability says whether this bot is the helping kind
                // right now. Someone left to it is not reconsidered for a minute.
                state.failures.Remember(FailKind::Target, key, now, DECIDED_MEMORY_MS, uint8(FailureReason::None));
                if (WorldBrainInternal::Roll(state, 0x50c3) % 100 >= 30u + uint32(state.persona.sociability) / 2)
                    break;
                if (!bot->IsWithinLOSInMap(creature))
                    break;

                WorldExecutor::Dismount(bot, state);
                StepAwayFromTask(bot, state, "helping someone");
                if (!bot->Attack(creature, true))
                    break;

                state.socialActive = true;
                state.socialUntilMs = now + ASSIST_MAX_MS;
                Count(state.metrics, &WorldMetrics::assists);
                NoteEvent(state, Acore::StringFormat("helping {} ({:.0f}% health) against {}", other->GetName(),
                    other->GetHealthPct(), creature->GetName()));
                LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' helps '{}' ({:.0f}% health) against '{}' (entry {}).",
                    bot->GetName(), other->GetName(), other->GetHealthPct(), creature->GetName(), creature->GetEntry());
                return true;
            }
        }
        return false;
    }
}

namespace WorldSocial
{
    bool TryHelp(Player* bot, BrainState& state)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        if (!cfg.helpOthers && !cfg.resurrectOthers)
            return false;

        uint32 now = NowMs();
        if (now < state.nextSocialCheckMs)
            return false;
        state.nextSocialCheckMs = now + RollRange(state, 0x50c1, CHECK_MIN_MS, CHECK_MAX_MS);
        if (!FreeToHelp(bot, state))
            return false;

        float radius = std::max(cfg.helpRadius, REZ_SEARCH_RADIUS);
        std::vector<Player*> players;
        Acore::AnyPlayerInObjectRangeCheck check(bot, radius, false, true);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(bot, players, check);
        Cell::VisitObjects(bot, searcher, radius);
        if (std::none_of(players.begin(), players.end(), [bot](Player* other) { return other != bot; }))
            return false;

        if (cfg.resurrectOthers && TryResurrect(bot, state, players, now))
            return true;
        return cfg.helpOthers && TryAssist(bot, state, players, now);
    }

    bool IsHelping(Player* bot, BrainState& state)
    {
        if (!state.socialActive)
            return false;
        if (NowMs() >= state.socialUntilMs)
        {
            state.socialActive = false;
            return false;
        }
        // A resurrection is still owned by its cast; an assist has no cast of its own -- it is
        // owned by the fight TryAssist started (Attack()), which IsInCombat() tracks for as long
        // as it runs. Without checking combat here too, the very next brain tick after Attack()
        // saw IsHelping() go false immediately (no cast in flight) and resumed the paused task
        // while the bot was still mid-fight for someone else.
        if (bot->IsNonMeleeSpellCast(false) || bot->IsInCombat())
            return true;
        state.socialActive = false;
        return false;
    }
}
