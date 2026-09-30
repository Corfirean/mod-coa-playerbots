#include "BotBossKnowledge.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "StringFormat.h"
#include <unordered_map>

namespace
{
    std::unordered_map<uint32, std::vector<BotBossKnowledge::KnownAbility>> _cache;

    // SmartAI constants (SmartScriptMgr.h) -- named locally rather than pulling that header's
    // full definitions in for a handful of values.
    constexpr uint8 SMART_SOURCE_CREATURE = 0;
    constexpr uint8 SMART_SOURCE_TIMED_ACTIONLIST = 9;
    constexpr uint8 SMART_EVENT_HEALTH_PCT = 2;   // event_param1/2 = HPMin%/HPMax% on the boss itself
    constexpr uint8 SMART_EVENT_LINK = 61;        // internal-only: no real trigger of its own
    constexpr uint8 SMART_ACTION_CAST = 11;
    constexpr uint8 SMART_ACTION_SELF_CAST = 85;
    constexpr uint8 SMART_ACTION_CROSS_CAST = 86;
    constexpr uint8 SMART_ACTION_CALL_TIMED_ACTIONLIST = 80;
    constexpr uint8 SMART_ACTION_INVOKER_CAST = 134;

    bool IsCastAction(uint8 actionType)
    {
        return actionType == SMART_ACTION_CAST || actionType == SMART_ACTION_SELF_CAST ||
            actionType == SMART_ACTION_CROSS_CAST || actionType == SMART_ACTION_INVOKER_CAST;
    }

    // Static fallback table for the rare boss the fleet actually reaches with no usable SmartAI
    // data at all (a real C++ BossAI/EventMap encounter, not scripted through smart_scripts).
    // Empty on purpose today -- see BotBossKnowledge.h's header comment for why this is filled in
    // per confirmed gap rather than attempted up front.
    std::vector<BotBossKnowledge::KnownAbility> StaticDescriptor(uint32 /*creatureEntry*/)
    {
        return {};
    }

    std::vector<BotBossKnowledge::KnownAbility> LoadFromSmartAI(uint32 creatureEntry)
    {
        std::vector<BotBossKnowledge::KnownAbility> abilities;

        QueryResult result = WorldDatabase.Query(Acore::StringFormat(
            "SELECT id, link, event_type, event_param1, event_param2, action_type, action_param1 "
            "FROM smart_scripts WHERE entryorguid = {} AND source_type = {}",
            creatureEntry, SMART_SOURCE_CREATURE));

        if (!result)
            return abilities;

        // Keep every row, not just cast rows -- SMART_EVENT_LINK resolution below needs to find
        // whichever OTHER row links to a cast row to recover its real trigger.
        struct Row
        {
            uint16 id;
            uint16 link;
            uint8 eventType;
            uint32 eventParam1;
            uint32 eventParam2;
            uint8 actionType;
            uint32 actionParam1;
        };
        std::vector<Row> rows;
        do
        {
            Field* f = result->Fetch();
            rows.push_back(Row{ f[0].Get<uint16>(), f[1].Get<uint16>(), f[2].Get<uint8>(),
                f[3].Get<uint32>(), f[4].Get<uint32>(), f[5].Get<uint8>(), f[6].Get<uint32>() });
        } while (result->NextRow());

        for (Row const& row : rows)
        {
            // A row that hands off to a timed action list has its own casts living in separate
            // source_type=9 rows, keyed by the list id this row's action_param1 names.
            if (row.actionType == SMART_ACTION_CALL_TIMED_ACTIONLIST)
            {
                QueryResult timed = WorldDatabase.Query(Acore::StringFormat(
                    "SELECT action_type, action_param1 FROM smart_scripts "
                    "WHERE entryorguid = {} AND source_type = {}",
                    row.actionParam1, SMART_SOURCE_TIMED_ACTIONLIST));
                if (timed)
                {
                    do
                    {
                        Field* tf = timed->Fetch();
                        if (IsCastAction(tf[0].Get<uint8>()))
                        {
                            uint32 spellId = tf[1].Get<uint32>();
                            if (spellId)
                            {
                                // Timed-list casts run on a fixed schedule, not an HP window --
                                // hasHpWindow stays false, meaning "can cast at any time."
                                abilities.push_back(BotBossKnowledge::KnownAbility{ spellId, false, 0.0f, 100.0f });
                            }
                        }
                    } while (timed->NextRow());
                }
                continue;
            }

            if (!IsCastAction(row.actionType) || !row.actionParam1)
                continue;

            uint8 eventType = row.eventType;
            uint32 p1 = row.eventParam1;
            uint32 p2 = row.eventParam2;

            // This row's own event is link-only (no real trigger data): find whichever other row
            // links TO this one's id and inherit its trigger instead (single hop -- covers the
            // overwhelmingly common "event row -> linked cast row" shape without chasing
            // arbitrarily deep chains).
            if (eventType == SMART_EVENT_LINK)
            {
                for (Row const& other : rows)
                {
                    if (other.link == row.id)
                    {
                        eventType = other.eventType;
                        p1 = other.eventParam1;
                        p2 = other.eventParam2;
                        break;
                    }
                }
            }

            BotBossKnowledge::KnownAbility ability;
            ability.spellId = row.actionParam1;
            if (eventType == SMART_EVENT_HEALTH_PCT)
            {
                ability.hasHpWindow = true;
                ability.hpMinPct = float(p1);
                ability.hpMaxPct = float(p2);
            }
            abilities.push_back(ability);
        }

        return abilities;
    }
}

namespace BotBossKnowledge
{
    std::vector<KnownAbility> const& GetKnownAbilities(uint32 creatureEntry)
    {
        auto it = _cache.find(creatureEntry);
        if (it != _cache.end())
            return it->second;

        std::vector<KnownAbility> abilities = LoadFromSmartAI(creatureEntry);
        if (abilities.empty())
            abilities = StaticDescriptor(creatureEntry);

        return _cache.emplace(creatureEntry, std::move(abilities)).first->second;
    }
}
