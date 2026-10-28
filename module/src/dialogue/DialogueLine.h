/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Line Definition and Metadata.
 */

#ifndef COA_PLAYERBOTS_DIALOGUE_LINE_H
#define COA_PLAYERBOTS_DIALOGUE_LINE_H

#include "DialogueTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

struct DialogueLine
{
    std::string id;
    DialogueBeat beat = DialogueBeat::None;
    std::vector<std::string> tags;
    float weight = 1.0f;
    uint32 cooldownSec = 60;
    std::unordered_map<DialogueLocale, std::string> locales;
    bool nonCanonicalStyled = false;

    bool HasTag(std::string_view tag) const
    {
        for (auto const& t : tags)
        {
            if (t == tag)
                return true;
        }
        return false;
    }

    std::vector<std::string> ExtractPlaceholders(DialogueLocale loc = DialogueLocale::EnUS) const
    {
        std::vector<std::string> placeholders;
        auto it = locales.find(loc);
        if (it == locales.end() && !locales.empty())
            it = locales.begin();
        if (it == locales.end())
            return placeholders;

        std::string const& text = it->second;
        size_t start = 0;
        while ((start = text.find('{', start)) != std::string::npos)
        {
            size_t end = text.find('}', start + 1);
            if (end != std::string::npos)
            {
                placeholders.push_back(text.substr(start, end - start + 1));
                start = end + 1;
            }
            else
            {
                break;
            }
        }
        return placeholders;
    }
};

#endif // COA_PLAYERBOTS_DIALOGUE_LINE_H
