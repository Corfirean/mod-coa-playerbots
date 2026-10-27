#include "EncounterNarrator.h"
#include "Chat.h"
#include "EncounterConfig.h"
#include "GameTime.h"
#include "Map.h"
#include "Player.h"
#include "SharedDefines.h"
#include <cmath>

namespace
{
    EncounterNarratorGate _gate;

    uint32 NowMs()
    {
        return static_cast<uint32>(GameTime::GetGameTimeMS().count());
    }

    float Dist2D(Player const* a, Player const* b)
    {
        return std::hypot(a->GetPositionX() - b->GetPositionX(), a->GetPositionY() - b->GetPositionY());
    }

    static constexpr char const* ASSASSIN_ATMOSPHERE[] = {
        "Кажется, за мной кто-то следит...",
        "Холодок пробегает по спине. В тенях кто-то затаился...",
        "Слишком тихо. Чьи-то шаги едва слышны позади..."
    };

    static constexpr char const* WATCHER_ATMOSPHERE[] = {
        "Мне показалось, или там кто-то был?..",
        "Странное чувство. Будто за мной наблюдают.",
        "На мгновение вдали мелькнул чей-то силуэт."
    };

    static constexpr char const* MYSTERY_ATMOSPHERE[] = {
        "На земле остаётся странный символ.",
        "Ты не узнаёшь этих знаков.",
        "Воздух едва заметно дрожит, а на камнях проступает незнакомая руна."
    };

    static constexpr char const* BOUNTY_SAY[] = {
        "За твою голову назначена награда.",
        "Ничего личного — просто контракт за твою голову.",
        "Твои расправы не остались незамеченными. Пора платить по счетам."
    };

    static constexpr char const* NEMESIS_SAY[] = {
        "Опять ты.",
        "На этот раз всё закончится иначе.",
        "Судьба снова свела нас на одной тропе."
    };

    static constexpr char const* DESERTER_SAY[] = {
        "Я туда не вернусь.",
        "Пусть командиры сами ведут эту войну.",
        "Хватит с меня маршей и чужих приказов..."
    };

    static constexpr char const* RIVAL_PARTY_SAY[] = {
        "Мы опять встретились.",
        "Мир становится слишком маленьким.",
        "Снова ты на нашем пути? Посмотрим, кто доберётся первым."
    };

    static constexpr char const* SKIRMISH_YELL[] = {
        "К оружию! Не дайте им уйти!",
        "Держать строй! Сметём их!",
        "За наших павших — в атаку!"
    };

    static constexpr char const* AMBUSH_YELL[] = {
        "Сейчас! Замыкайте кольцо!",
        "Засада! Ни шагу назад!",
        "Они угодили прямо в ловушку!"
    };
}

namespace EncounterNarrator
{
    EncounterNarratorGate& Gate()
    {
        return _gate;
    }

    char const* SelectLine(EncounterType type, NarratorChannel channel, uint32 variantSeed)
    {
        if (channel == NarratorChannel::PlayerAtmosphere)
        {
            switch (type)
            {
                case EncounterType::AssassinContract:
                    return ASSASSIN_ATMOSPHERE[variantSeed % (sizeof(ASSASSIN_ATMOSPHERE) / sizeof(ASSASSIN_ATMOSPHERE[0]))];
                case EncounterType::TheWatcher:
                    return WATCHER_ATMOSPHERE[variantSeed % (sizeof(WATCHER_ATMOSPHERE) / sizeof(WATCHER_ATMOSPHERE[0]))];
                case EncounterType::MysteryEncounter:
                    return MYSTERY_ATMOSPHERE[variantSeed % (sizeof(MYSTERY_ATMOSPHERE) / sizeof(MYSTERY_ATMOSPHERE[0]))];
                default:
                    return nullptr;
            }
        }

        switch (type)
        {
            case EncounterType::BountyHunter:
                return BOUNTY_SAY[variantSeed % (sizeof(BOUNTY_SAY) / sizeof(BOUNTY_SAY[0]))];
            case EncounterType::Nemesis:
                return NEMESIS_SAY[variantSeed % (sizeof(NEMESIS_SAY) / sizeof(NEMESIS_SAY[0]))];
            case EncounterType::Deserter:
                return DESERTER_SAY[variantSeed % (sizeof(DESERTER_SAY) / sizeof(DESERTER_SAY[0]))];
            case EncounterType::RivalAdventuringParty:
                return RIVAL_PARTY_SAY[variantSeed % (sizeof(RIVAL_PARTY_SAY) / sizeof(RIVAL_PARTY_SAY[0]))];
            case EncounterType::FactionSkirmish:
                return SKIRMISH_YELL[variantSeed % (sizeof(SKIRMISH_YELL) / sizeof(SKIRMISH_YELL[0]))];
            case EncounterType::PatrolAmbush:
                return AMBUSH_YELL[variantSeed % (sizeof(AMBUSH_YELL) / sizeof(AMBUSH_YELL[0]))];
            default:
                return nullptr;
        }
    }

    bool TrySendPlayerAtmosphere(
        Player* targetPlayer,
        Player const* sourceBot,
        EncounterType type,
        uint32 stalkElapsedMs,
        uint32 variantSeed)
    {
        if (!targetPlayer || !sourceBot || !targetPlayer->IsInWorld() || !sourceBot->IsInWorld())
            return false;
        if (!targetPlayer->GetSession())
            return false;

        EncounterConfig const& cfg = EncounterSettings::Get();
        uint32 now = NowMs();
        float dist = Dist2D(targetPlayer, sourceBot);

        if (!_gate.CanSendToPlayer(
                targetPlayer->GetGUID(),
                type,
                sourceBot->GetMapId(),
                targetPlayer->GetMapId(),
                dist,
                stalkElapsedMs,
                cfg,
                now))
        {
            return false;
        }

        char const* line = SelectLine(type, NarratorChannel::PlayerAtmosphere, variantSeed);
        if (!line)
            return false;

        _gate.RecordPlayerMessage(targetPlayer->GetGUID(), cfg, now);
        ChatHandler(targetPlayer->GetSession()).SendSysMessage(line);
        return true;
    }

    bool TryBotSpeak(
        Player* bot,
        Player* specificListener,
        EncounterType type,
        NarratorChannel channel,
        uint32 variantSeed)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        EncounterConfig const& cfg = EncounterSettings::Get();
        uint32 now = NowMs();

        uint32 listenerMapId = bot->GetMapId();
        float dist = 0.0f;

        if (specificListener)
        {
            if (!specificListener->IsInWorld())
                return false;
            listenerMapId = specificListener->GetMapId();
            dist = Dist2D(bot, specificListener);
        }

        if (!_gate.CanBotSpeak(bot->GetGUID(), bot->GetMapId(), listenerMapId, dist, cfg, now))
            return false;

        char const* line = SelectLine(type, channel, variantSeed);
        if (!line)
            return false;

        _gate.RecordBotSpeech(bot->GetGUID(), cfg, now);
        if (specificListener)
            _gate.RecordPlayerMessage(specificListener->GetGUID(), cfg, now);

        switch (channel)
        {
            case NarratorChannel::BotSay:
                bot->Say(line, LANG_UNIVERSAL);
                break;
            case NarratorChannel::BotYell:
                bot->Yell(line, LANG_UNIVERSAL);
                break;
            case NarratorChannel::BotWhisper:
                if (specificListener)
                    bot->Whisper(line, LANG_UNIVERSAL, specificListener);
                else
                    bot->Say(line, LANG_UNIVERSAL);
                break;
            case NarratorChannel::BotTextEmote:
                bot->TextEmote(line);
                break;
            default:
                return false;
        }

        return true;
    }
}
