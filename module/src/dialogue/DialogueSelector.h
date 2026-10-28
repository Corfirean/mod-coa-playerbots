/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Personality-Weighted Line Selection & Context Arbitration.
 */

#ifndef COA_PLAYERBOTS_DIALOGUE_SELECTOR_H
#define COA_PLAYERBOTS_DIALOGUE_SELECTOR_H

#include "DialoguePool.h"
#include "DialogueMemory.h"
#include "DialogueLocalization.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>
#include <random>

class Player;

struct DialogueContext
{
    Player* speaker = nullptr;
    Player* listener = nullptr;

    ObjectGuid speakerGuid;
    std::string speakerName;
    uint8 speakerRace = 1;
    uint32 groupLow = 0;

    ObjectGuid targetGuid;
    std::string targetName;
    uint8 targetRace = 0;

    std::string spellName;
    std::string zoneName;
    std::string enemyName;
    std::string groupLeaderName;
    std::string mechanicKey;

    float currentManaPct = 100.0f;
    float currentHealthPct = 100.0f;
    bool isCaster = false;
    bool isHealer = false;
    bool isTank = false;

    // Personality weights [0.0 .. 1.0]
    float aggressive = 0.5f;
    float patient = 0.5f;
    float sociable = 0.5f;
    float loner = 0.2f;

    DialoguePriority priority = DialoguePriority::Ambient;
    DialogueChannel preferredChannel = DialogueChannel::Say;
    DialogueChannel channel = DialogueChannel::Say;
    DialogueLocale requestedLocale = DialogueLocale::EnUS;
    DialogueLocale serverDefaultLocale = DialogueLocale::EnUS;
    bool isMysteryQuery = false;
};

class DialogueSelector
{
public:
    static DialogueLine const* SelectLine(
        DialoguePool const& pool,
        DialogueMemory const& memory,
        DialogueContext const& ctx,
        DialogueBeat beat,
        uint32 nowMs,
        std::string& outSuppressionReason)
    {
        // 1. Priority-Gated Cooldown Check
        // Critical combat callouts bypass ambient chatter cooldowns
        bool isCritical = (ctx.priority >= DialoguePriority::CombatHigh);

        if (!isCritical)
        {
            if (memory.IsBotOnChatterCooldown(ctx.speakerGuid, nowMs))
            {
                outSuppressionReason = "Bot chatter cooldown active";
                return nullptr;
            }
            if (ctx.groupLow != 0 && memory.IsGroupOnChatterCooldown(ctx.groupLow, nowMs))
            {
                outSuppressionReason = "Group chatter cooldown active";
                return nullptr;
            }
            if (memory.IsBeatOnCooldownForBot(ctx.speakerGuid, beat, nowMs))
            {
                outSuppressionReason = "Beat cooldown active for bot";
                return nullptr;
            }
        }

        // Semantic Resource Threshold Validation (§28, §29)
        if (beat == DialogueBeat::NeedMana || beat == DialogueBeat::NeedDrink)
        {
            if (!ctx.isCaster || ctx.currentManaPct >= 20.0f)
            {
                outSuppressionReason = "Resource threshold not met: NeedMana requires caster with mana < 20%";
                return nullptr;
            }
        }
        else if (beat == DialogueBeat::NeedFood || beat == DialogueBeat::LowHealth)
        {
            if (ctx.currentHealthPct >= 35.0f)
            {
                outSuppressionReason = "Resource threshold not met: LowHealth requires hp < 35%";
                return nullptr;
            }
        }

        // 2. Fetch candidates from Pool
        std::vector<DialogueLine const*> allLines = pool.GetLinesForBeat(beat, ctx.isMysteryQuery);
        if (allLines.empty())
        {
            outSuppressionReason = "No lines found for beat";
            return nullptr;
        }

        // 3. Filter candidates
        struct ScoredCandidate
        {
            DialogueLine const* line = nullptr;
            float score = 1.0f;
        };

        std::vector<ScoredCandidate> eligible;
        for (DialogueLine const* line : allLines)
        {
            if (!line)
                continue;

            // Cooldown check per line
            if (!isCritical && memory.IsLineOnCooldown(line->id, nowMs))
                continue;

            // Anti-repeat check (bot & group)
            if (!isCritical && memory.WasRecentlySpokenByBot(ctx.speakerGuid, line->id))
                continue;
            if (!isCritical && ctx.groupLow != 0 && memory.WasRecentlySpokenInGroup(ctx.groupLow, line->id))
                continue;

            // Role tag check
            if (line->HasTag("tank") && !ctx.isTank)
                continue;
            if (line->HasTag("healer") && !ctx.isHealer)
                continue;
            if (line->HasTag("caster") && !ctx.isCaster)
                continue;

            // Personality scoring
            float score = line->weight;
            if (line->HasTag("aggressive"))
                score *= (0.5f + ctx.aggressive * 1.5f);
            if (line->HasTag("calm") || line->HasTag("patient"))
                score *= (0.5f + ctx.patient * 1.5f);
            if (line->HasTag("impatient"))
                score *= (0.5f + (1.0f - ctx.patient) * 1.5f);
            if (line->HasTag("sociable"))
                score *= (0.5f + ctx.sociable * 1.5f);
            if (line->HasTag("loner"))
                score *= (0.5f + ctx.loner * 1.5f);

            eligible.push_back({line, std::max(0.01f, score)});
        }

        if (eligible.empty())
        {
            outSuppressionReason = "All candidate lines suppressed by cooldown or anti-repeat";
            return nullptr;
        }

        // 4. Weighted Random Selection
        float totalWeight = 0.0f;
        for (auto const& c : eligible)
            totalWeight += c.score;

        static std::mt19937 rng(1337);
        std::uniform_real_distribution<float> dist(0.0f, totalWeight);
        float roll = dist(rng);

        float current = 0.0f;
        for (auto const& c : eligible)
        {
            current += c.score;
            if (roll <= current)
                return c.line;
        }

        return eligible.back().line;
    }

    static std::string FormatLineText(
        DialogueLine const& line,
        DialogueContext const& ctx)
    {
        DialogueLocale resolvedLocale = ctx.requestedLocale;
        if (line.locales.find(resolvedLocale) == line.locales.end())
        {
            if (line.locales.find(ctx.serverDefaultLocale) != line.locales.end())
                resolvedLocale = ctx.serverDefaultLocale;
            else if (line.locales.find(DialogueLocale::EnUS) != line.locales.end())
                resolvedLocale = DialogueLocale::EnUS;
        }

        std::string raw = DialogueLocalization::ResolveText(line, ctx.requestedLocale, ctx.serverDefaultLocale);

        std::unordered_map<std::string, std::string> placeholders;
        if (!ctx.speakerName.empty())
            placeholders["botName"] = ctx.speakerName;
        if (!ctx.targetName.empty())
            placeholders["playerName"] = ctx.targetName;
        if (!ctx.targetName.empty())
            placeholders["targetName"] = ctx.targetName;
        if (!ctx.enemyName.empty())
            placeholders["enemyName"] = ctx.enemyName;
        if (!ctx.spellName.empty())
            placeholders["spellName"] = ctx.spellName;
        if (!ctx.zoneName.empty())
            placeholders["zoneName"] = ctx.zoneName;
        if (!ctx.groupLeaderName.empty())
            placeholders["groupLeader"] = ctx.groupLeaderName;

        if (ctx.targetRace != 0)
        {
            placeholders["playerRace"] = DialogueLocalization::GetRaceDisplayName(ctx.targetRace, resolvedLocale);
        }

        return DialogueLocalization::Interpolate(raw, placeholders);
    }
};

#endif // COA_PLAYERBOTS_DIALOGUE_SELECTOR_H
