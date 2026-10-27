/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Multi-Locale Resolution, Fallback Chains,
 * Placeholder Interpolation, and Race Name Mapping.
 */

#ifndef COA_PLAYERBOTS_DIALOGUE_LOCALIZATION_H
#define COA_PLAYERBOTS_DIALOGUE_LOCALIZATION_H

#include "DialogueLine.h"
#include "DialogueTypes.h"
#include <string>
#include <unordered_map>
#include <vector>

class DialogueLocalization
{
public:
    static std::string ResolveText(
        DialogueLine const& line,
        DialogueLocale requestedLocale,
        DialogueLocale serverDefaultLocale = DialogueLocale::EnUS)
    {
        // 1. Requested Locale
        auto it = line.locales.find(requestedLocale);
        if (it != line.locales.end() && !it->second.empty())
            return it->second;

        // 2. Server Default Locale
        if (serverDefaultLocale != requestedLocale)
        {
            it = line.locales.find(serverDefaultLocale);
            if (it != line.locales.end() && !it->second.empty())
                return it->second;
        }

        // 3. Fallback: enUS
        if (serverDefaultLocale != DialogueLocale::EnUS && requestedLocale != DialogueLocale::EnUS)
        {
            it = line.locales.find(DialogueLocale::EnUS);
            if (it != line.locales.end() && !it->second.empty())
                return it->second;
        }

        // 4. Any available non-empty translation
        for (auto const& [loc, text] : line.locales)
        {
            if (!text.empty())
                return text;
        }

        // 5. Safe debug key fallback (never crashes, never returns empty string)
        return "[dialogue." + line.id + "]";
    }

    static std::string Interpolate(
        std::string const& text,
        std::unordered_map<std::string, std::string> const& placeholders)
    {
        if (text.empty() || placeholders.empty())
            return text;

        std::string result = text;
        for (auto const& [key, value] : placeholders)
        {
            std::string token = "{" + key + "}";
            size_t pos = 0;
            while ((pos = result.find(token, pos)) != std::string::npos)
            {
                result.replace(pos, token.length(), value);
                pos += value.length();
            }
        }
        return result;
    }

    static std::string GetRaceDisplayName(uint8 race, DialogueLocale loc)
    {
        // WoW Standard Races (1..11)
        switch (race)
        {
            case 1: // RACE_HUMAN
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "человек";
                    case DialogueLocale::DeDE: return "Mensch";
                    case DialogueLocale::FrFR: return "humain";
                    case DialogueLocale::EsES: return "humano";
                    case DialogueLocale::ZhCN: return "人类";
                    default:                   return "human";
                }
            case 2: // RACE_ORC
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "орк";
                    case DialogueLocale::DeDE: return "Ork";
                    case DialogueLocale::FrFR: return "orc";
                    case DialogueLocale::EsES: return "orco";
                    case DialogueLocale::ZhCN: return "兽人";
                    default:                   return "orc";
                }
            case 3: // RACE_DWARF
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "дворф";
                    case DialogueLocale::DeDE: return "Zwerg";
                    case DialogueLocale::FrFR: return "nain";
                    case DialogueLocale::EsES: return "enano";
                    case DialogueLocale::ZhCN: return "矮人";
                    default:                   return "dwarf";
                }
            case 4: // RACE_NIGHTELF
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "ночной эльф";
                    case DialogueLocale::DeDE: return "Nachtelf";
                    case DialogueLocale::FrFR: return "elfe de la nuit";
                    case DialogueLocale::EsES: return "elfo de la noche";
                    case DialogueLocale::ZhCN: return "暗夜精灵";
                    default:                   return "night elf";
                }
            case 5: // RACE_UNDEAD_PLAYER
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "отрёкшийся";
                    case DialogueLocale::DeDE: return "Verlassener";
                    case DialogueLocale::FrFR: return "mort-vivant";
                    case DialogueLocale::EsES: return "renegado";
                    case DialogueLocale::ZhCN: return "亡灵";
                    default:                   return "undead";
                }
            case 6: // RACE_TAUREN
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "таурен";
                    case DialogueLocale::DeDE: return "Tauren";
                    case DialogueLocale::FrFR: return "tauren";
                    case DialogueLocale::EsES: return "tauren";
                    case DialogueLocale::ZhCN: return "牛头人";
                    default:                   return "tauren";
                }
            case 7: // RACE_GNOME
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "гном";
                    case DialogueLocale::DeDE: return "Gnom";
                    case DialogueLocale::FrFR: return "gnome";
                    case DialogueLocale::EsES: return "gnomo";
                    case DialogueLocale::ZhCN: return "侏儒";
                    default:                   return "gnome";
                }
            case 8: // RACE_TROLL
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "тролль";
                    case DialogueLocale::DeDE: return "Troll";
                    case DialogueLocale::FrFR: return "troll";
                    case DialogueLocale::EsES: return "trol";
                    case DialogueLocale::ZhCN: return "巨魔";
                    default:                   return "troll";
                }
            case 10: // RACE_BLOODELF
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "эльф крови";
                    case DialogueLocale::DeDE: return "Blutelf";
                    case DialogueLocale::FrFR: return "elfe de sang";
                    case DialogueLocale::EsES: return "elfo de sangre";
                    case DialogueLocale::ZhCN: return "血精灵";
                    default:                   return "blood elf";
                }
            case 11: // RACE_DRAENEI
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "дреней";
                    case DialogueLocale::DeDE: return "Draenei";
                    case DialogueLocale::FrFR: return "draeneï";
                    case DialogueLocale::EsES: return "draenei";
                    case DialogueLocale::ZhCN: return "德莱尼";
                    default:                   return "draenei";
                }
            default:
                switch (loc)
                {
                    case DialogueLocale::RuRU: return "чужак";
                    case DialogueLocale::DeDE: return "Fremder";
                    case DialogueLocale::FrFR: return "étranger";
                    case DialogueLocale::EsES: return "forastero";
                    case DialogueLocale::ZhCN: return "异类";
                    default:                   return "stranger";
                }
        }
    }

    static bool ValidatePlaceholders(DialogueLine const& line, std::vector<std::string>& outErrors)
    {
        static const std::unordered_map<std::string, bool> validPlaceholders = {
            {"{playerName}", true},
            {"{playerRace}", true},
            {"{botName}", true},
            {"{targetName}", true},
            {"{zoneName}", true},
            {"{spellName}", true},
            {"{enemyName}", true},
            {"{groupLeader}", true}
        };

        bool valid = true;
        std::vector<std::string> basePlaceholders = line.ExtractPlaceholders(DialogueLocale::EnUS);

        for (auto const& token : basePlaceholders)
        {
            if (validPlaceholders.find(token) == validPlaceholders.end())
            {
                outErrors.push_back("Line '" + line.id + "' has unknown placeholder: " + token);
                valid = false;
            }
        }

        // Check consistency across translated locales
        for (auto const& [loc, text] : line.locales)
        {
            std::vector<std::string> locPlaceholders = line.ExtractPlaceholders(loc);
            for (auto const& token : locPlaceholders)
            {
                if (validPlaceholders.find(token) == validPlaceholders.end())
                {
                    outErrors.push_back("Line '" + line.id + "' (" + std::string(DialogueUtils::LocaleToCode(loc)) + ") has unknown placeholder: " + token);
                    valid = false;
                }
            }
        }

        return valid;
    }
};

#endif // COA_PLAYERBOTS_DIALOGUE_LOCALIZATION_H
