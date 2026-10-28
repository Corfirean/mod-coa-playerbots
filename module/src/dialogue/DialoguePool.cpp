/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Line Pool, JSON Loader, and Defaults Implementation.
 */

#include "DialoguePool.h"
#include "DialogueLocalization.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

DialoguePool::DialoguePool()
{
}

void DialoguePool::Clear()
{
    _pool.clear();
    _mysteryPool.clear();
    _linesById.clear();
}

bool DialoguePool::IsMysteryBeat(DialogueBeat beat)
{
    return (beat == DialogueBeat::MysteryShathYarWhisper ||
            beat == DialogueBeat::MysteryShathYarThreat ||
            beat == DialogueBeat::MysteryShathYarIncantation ||
            beat == DialogueBeat::MysteryShathYarDeparture);
}

bool DialoguePool::AddLine(DialogueLine const& line, std::string& outError)
{
    if (line.id.empty())
    {
        outError = "DialogueLine id is empty";
        return false;
    }

    if (line.beat == DialogueBeat::None)
    {
        outError = "DialogueLine '" + line.id + "' has DialogueBeat::None";
        return false;
    }

    if (line.locales.empty())
    {
        outError = "DialogueLine '" + line.id + "' has no localized text";
        return false;
    }

    std::vector<std::string> placeholderErrors;
    if (!DialogueLocalization::ValidatePlaceholders(line, placeholderErrors))
    {
        if (!placeholderErrors.empty())
            outError = placeholderErrors[0];
        return false;
    }

    _linesById[line.id] = line;
    if (IsMysteryBeat(line.beat))
    {
        _mysteryPool[line.beat].push_back(line);
    }
    else
    {
        _pool[line.beat].push_back(line);
    }
    return true;
}

std::vector<DialogueLine const*> DialoguePool::GetLinesForBeat(DialogueBeat beat, bool isMysteryQuery) const
{
    std::vector<DialogueLine const*> result;
    if (IsMysteryBeat(beat) || isMysteryQuery)
    {
        auto it = _mysteryPool.find(beat);
        if (it != _mysteryPool.end())
        {
            for (auto const& l : it->second)
                result.push_back(&l);
        }
    }
    else
    {
        auto it = _pool.find(beat);
        if (it != _pool.end())
        {
            for (auto const& l : it->second)
                result.push_back(&l);
        }
    }
    return result;
}

DialogueLine const* DialoguePool::FindLineById(std::string const& id) const
{
    auto it = _linesById.find(id);
    return it != _linesById.end() ? &it->second : nullptr;
}

size_t DialoguePool::TotalLines() const
{
    return _linesById.size();
}

size_t DialoguePool::BeatCount() const
{
    return _pool.size() + _mysteryPool.size();
}

// ============================================================================
// Lightweight Robust JSON Parser for Dialogue Lines
// ============================================================================
namespace
{
    void SkipWhitespace(std::string const& s, size_t& i)
    {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',' || s[i] == ':'))
            ++i;
    }

    std::string ParseJsonString(std::string const& s, size_t& i)
    {
        SkipWhitespace(s, i);
        if (i >= s.size() || s[i] != '"')
            return "";
        ++i; // skip opening quote
        std::string res;
        while (i < s.size())
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                ++i;
                if (s[i] == 'n') res += '\n';
                else if (s[i] == 't') res += '\t';
                else if (s[i] == '"') res += '"';
                else if (s[i] == '\\') res += '\\';
                else res += s[i];
            }
            else if (s[i] == '"')
            {
                ++i;
                break;
            }
            else
            {
                res += s[i];
            }
            ++i;
        }
        return res;
    }
}

bool DialoguePool::LoadFromJsonString(std::string const& jsonStr, std::string& outError)
{
    size_t i = 0;
    SkipWhitespace(jsonStr, i);
    if (i >= jsonStr.size() || (jsonStr[i] != '[' && jsonStr[i] != '{'))
    {
        outError = "Invalid JSON: missing root array or object";
        return false;
    }

    bool inArray = (jsonStr[i] == '[');
    ++i;

    while (i < jsonStr.size())
    {
        SkipWhitespace(jsonStr, i);
        if (i >= jsonStr.size() || jsonStr[i] == ']' || jsonStr[i] == '}')
            break;

        if (jsonStr[i] == '{')
        {
            ++i;
            DialogueLine line;
            while (i < jsonStr.size())
            {
                SkipWhitespace(jsonStr, i);
                if (i >= jsonStr.size() || jsonStr[i] == '}')
                {
                    if (i < jsonStr.size()) ++i;
                    break;
                }

                std::string key = ParseJsonString(jsonStr, i);
                SkipWhitespace(jsonStr, i);

                if (key == "id")
                {
                    line.id = ParseJsonString(jsonStr, i);
                }
                else if (key == "beat")
                {
                    std::string beatStr = ParseJsonString(jsonStr, i);
                    line.beat = DialogueUtils::StringToBeat(beatStr);
                }
                else if (key == "weight")
                {
                    SkipWhitespace(jsonStr, i);
                    size_t nextPos = 0;
                    try {
                        line.weight = std::stof(jsonStr.substr(i), &nextPos);
                        i += nextPos;
                    } catch (...) { ++i; }
                }
                else if (key == "cooldownSec")
                {
                    SkipWhitespace(jsonStr, i);
                    size_t nextPos = 0;
                    try {
                        line.cooldownSec = static_cast<uint32>(std::stoul(jsonStr.substr(i), &nextPos));
                        i += nextPos;
                    } catch (...) { ++i; }
                }
                else if (key == "nonCanonicalStyled")
                {
                    SkipWhitespace(jsonStr, i);
                    if (jsonStr.compare(i, 4, "true") == 0)
                    {
                        line.nonCanonicalStyled = true;
                        i += 4;
                    }
                    else if (jsonStr.compare(i, 5, "false") == 0)
                    {
                        line.nonCanonicalStyled = false;
                        i += 5;
                    }
                }
                else if (key == "tags")
                {
                    SkipWhitespace(jsonStr, i);
                    if (i < jsonStr.size() && jsonStr[i] == '[')
                    {
                        ++i;
                        while (i < jsonStr.size())
                        {
                            SkipWhitespace(jsonStr, i);
                            if (i >= jsonStr.size() || jsonStr[i] == ']')
                            {
                                if (i < jsonStr.size()) ++i;
                                break;
                            }
                            std::string tag = ParseJsonString(jsonStr, i);
                            if (!tag.empty())
                                line.tags.push_back(tag);
                        }
                    }
                }
                else if (key == "locales")
                {
                    SkipWhitespace(jsonStr, i);
                    if (i < jsonStr.size() && jsonStr[i] == '{')
                    {
                        ++i;
                        while (i < jsonStr.size())
                        {
                            SkipWhitespace(jsonStr, i);
                            if (i >= jsonStr.size() || jsonStr[i] == '}')
                            {
                                if (i < jsonStr.size()) ++i;
                                break;
                            }
                            std::string locKey = ParseJsonString(jsonStr, i);
                            SkipWhitespace(jsonStr, i);
                            std::string text = ParseJsonString(jsonStr, i);
                            DialogueLocale loc = DialogueUtils::NormalizeLocale(locKey);
                            line.locales[loc] = text;
                        }
                    }
                }
                else
                {
                    // Skip unknown value
                    SkipWhitespace(jsonStr, i);
                    if (jsonStr[i] == '"')
                        ParseJsonString(jsonStr, i);
                    else
                        while (i < jsonStr.size() && jsonStr[i] != ',' && jsonStr[i] != '}')
                            ++i;
                }
            }

            if (!line.id.empty() && line.beat != DialogueBeat::None)
            {
                std::string addErr;
                AddLine(line, addErr);
            }
        }
        else
        {
            ++i;
        }
    }

    return true;
}

bool DialoguePool::LoadFromJsonFile(std::string const& filePath, std::string& outError)
{
    std::ifstream f(filePath);
    if (!f.is_open())
    {
        outError = "Could not open file: " + filePath;
        return false;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    return LoadFromJsonString(buffer.str(), outError);
}

bool DialoguePool::LoadFromDirectory(std::string const& dirPath, std::string& outError)
{
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
    {
        outError = "Directory does not exist: " + dirPath;
        return false;
    }

    bool anyLoaded = false;
    for (auto const& entry : fs::directory_iterator(dirPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            std::string fileErr;
            if (LoadFromJsonFile(entry.path().string(), fileErr))
                anyLoaded = true;
        }
    }
    return anyLoaded;
}

// ============================================================================
// Built-in Authored Defaults (Always Available, Zero-Dependency Fallback)
// ============================================================================
void DialoguePool::LoadDefaults()
{
    // ------------------------------------------------------------------------
    // Resource & Downtime
    // ------------------------------------------------------------------------
    {
        DialogueLine l;
        l.id = "group.need_mana.001";
        l.beat = DialogueBeat::NeedMana;
        l.tags = {"caster", "calm"};
        l.weight = 1.0f;
        l.cooldownSec = 45;
        l.locales[DialogueLocale::EnUS] = "Give me a moment. I need mana.";
        l.locales[DialogueLocale::RuRU] = "Минуту. Мне нужно восстановить ману.";
        l.locales[DialogueLocale::DeDE] = "Einen Moment. Ich brauche Mana.";
        l.locales[DialogueLocale::FrFR] = "Un instant. J'ai besoin de mana.";
        l.locales[DialogueLocale::EsES] = "Un momento. Necesito maná.";
        l.locales[DialogueLocale::ZhCN] = "稍等一下，我需要法力值。";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "group.need_mana.002";
        l.beat = DialogueBeat::NeedMana;
        l.tags = {"caster", "impatient"};
        l.weight = 1.0f;
        l.cooldownSec = 45;
        l.locales[DialogueLocale::EnUS] = "Hold on. Out of mana.";
        l.locales[DialogueLocale::RuRU] = "Стойте. Мана закончилась.";
        l.locales[DialogueLocale::DeDE] = "Wartet. Kein Mana mehr.";
        l.locales[DialogueLocale::FrFR] = "Attendez. Plus de mana.";
        l.locales[DialogueLocale::EsES] = "Esperad. Sin maná.";
        l.locales[DialogueLocale::ZhCN] = "等等，没蓝了。";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "group.need_drink.001";
        l.beat = DialogueBeat::NeedDrink;
        l.tags = {"caster", "calm"};
        l.weight = 1.0f;
        l.cooldownSec = 40;
        l.locales[DialogueLocale::EnUS] = "Wait up, need to drink.";
        l.locales[DialogueLocale::RuRU] = "Подождите, мне нужно попить.";
        l.locales[DialogueLocale::DeDE] = "Wartet kurz, muss etwas trinken.";
        l.locales[DialogueLocale::FrFR] = "Attendez, je dois boire un coup.";
        l.locales[DialogueLocale::EsES] = "Esperad, tengo que beber algo.";
        l.locales[DialogueLocale::ZhCN] = "等等，我喝口水。";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "group.need_heal.001";
        l.beat = DialogueBeat::NeedHeal;
        l.tags = {"health", "urgent"};
        l.weight = 1.0f;
        l.cooldownSec = 30;
        l.locales[DialogueLocale::EnUS] = "I need healing before we move!";
        l.locales[DialogueLocale::RuRU] = "Мне нужно подлечиться перед боем!";
        l.locales[DialogueLocale::DeDE] = "Ich brauche Heilung, bevor wir weitergehen!";
        l.locales[DialogueLocale::FrFR] = "J'ai besoin de soins avant de continuer !";
        l.locales[DialogueLocale::EsES] = "¡Necesito curación antes de continuar!";
        l.locales[DialogueLocale::ZhCN] = "在继续前进前我需要治疗！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "group.ready_again.001";
        l.beat = DialogueBeat::ReadyAgain;
        l.tags = {"calm", "ready"};
        l.weight = 1.0f;
        l.cooldownSec = 20;
        l.locales[DialogueLocale::EnUS] = "Ready to go.";
        l.locales[DialogueLocale::RuRU] = "Готов.";
        l.locales[DialogueLocale::DeDE] = "Wieder bereit.";
        l.locales[DialogueLocale::FrFR] = "Prêt à repartir.";
        l.locales[DialogueLocale::EsES] = "Listo.";
        l.locales[DialogueLocale::ZhCN] = "准备好了。";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "group.leader_wait.001";
        l.beat = DialogueBeat::LeaderWaitAck;
        l.tags = {"leader", "calm"};
        l.weight = 1.0f;
        l.cooldownSec = 20;
        l.locales[DialogueLocale::EnUS] = "Understood. Take your time.";
        l.locales[DialogueLocale::RuRU] = "Понял. Ждём.";
        l.locales[DialogueLocale::DeDE] = "Verstanden. Nehmt euch Zeit.";
        l.locales[DialogueLocale::FrFR] = "Compris. Prenez votre temps.";
        l.locales[DialogueLocale::EsES] = "Entendido. Tómate tu tiempo.";
        l.locales[DialogueLocale::ZhCN] = "明白，慢慢来。";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "group.leader_proceed.001";
        l.beat = DialogueBeat::LeaderProceed;
        l.tags = {"leader", "command"};
        l.weight = 1.0f;
        l.cooldownSec = 20;
        l.locales[DialogueLocale::EnUS] = "Moving out!";
        l.locales[DialogueLocale::RuRU] = "Идём дальше!";
        l.locales[DialogueLocale::DeDE] = "Weiter geht's!";
        l.locales[DialogueLocale::FrFR] = "En route !";
        l.locales[DialogueLocale::EsES] = "¡En marcha!";
        l.locales[DialogueLocale::ZhCN] = "出发！";
        std::string err; AddLine(l, err);
    }

    // ------------------------------------------------------------------------
    // Tactical Combat Callouts
    // ------------------------------------------------------------------------
    {
        DialogueLine l;
        l.id = "tactical.interrupt.001";
        l.beat = DialogueBeat::Interrupt;
        l.tags = {"combat", "tactical", "dps"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Interrupt {spellName}!";
        l.locales[DialogueLocale::RuRU] = "Сбиваем {spellName}!";
        l.locales[DialogueLocale::DeDE] = "Unterbrecht {spellName}!";
        l.locales[DialogueLocale::FrFR] = "Interrompez {spellName} !";
        l.locales[DialogueLocale::EsES] = "¡Interrumpid {spellName}!";
        l.locales[DialogueLocale::ZhCN] = "打断 {spellName}！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.kill_adds.001";
        l.beat = DialogueBeat::KillAdds;
        l.tags = {"combat", "tactical", "dps"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Reinforcements! Switch to the adds!";
        l.locales[DialogueLocale::RuRU] = "Подкрепление! Переключаемся на аддов!";
        l.locales[DialogueLocale::DeDE] = "Verstärkung! Geht auf die Adds!";
        l.locales[DialogueLocale::FrFR] = "Renforts ! Passez sur les adds !";
        l.locales[DialogueLocale::EsES] = "¡Refuerzos! ¡Cambiad a los esbirros!";
        l.locales[DialogueLocale::ZhCN] = "增援来了！转火小怪！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.move_ground.001";
        l.beat = DialogueBeat::MoveFromGround;
        l.tags = {"combat", "critical", "warning"};
        l.weight = 1.0f;
        l.cooldownSec = 10;
        l.locales[DialogueLocale::EnUS] = "Move out of the hazard!";
        l.locales[DialogueLocale::RuRU] = "Не стойте в луже! Выходите!";
        l.locales[DialogueLocale::DeDE] = "Raus aus der Fläche!";
        l.locales[DialogueLocale::FrFR] = "Sortez de la zone !";
        l.locales[DialogueLocale::EsES] = "¡Salid de la zona peligrosa!";
        l.locales[DialogueLocale::ZhCN] = "快离开危险区域！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.spread_out.001";
        l.beat = DialogueBeat::SpreadOut;
        l.tags = {"combat", "critical", "movement"};
        l.weight = 1.0f;
        l.cooldownSec = 12;
        l.locales[DialogueLocale::EnUS] = "Spread out!";
        l.locales[DialogueLocale::RuRU] = "Разбегаемся!";
        l.locales[DialogueLocale::DeDE] = "Verteilt euch!";
        l.locales[DialogueLocale::FrFR] = "Dispersez-vous !";
        l.locales[DialogueLocale::EsES] = "¡Dispersaos!";
        l.locales[DialogueLocale::ZhCN] = "分散！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.stack_together.001";
        l.beat = DialogueBeat::StackTogether;
        l.tags = {"combat", "critical", "movement"};
        l.weight = 1.0f;
        l.cooldownSec = 12;
        l.locales[DialogueLocale::EnUS] = "Stack up together!";
        l.locales[DialogueLocale::RuRU] = "Собираемся вместе!";
        l.locales[DialogueLocale::DeDE] = "Zusammenstellen!";
        l.locales[DialogueLocale::FrFR] = "Rassemblez-vous !";
        l.locales[DialogueLocale::EsES] = "¡Juntaos!";
        l.locales[DialogueLocale::ZhCN] = "集合！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.tank_danger.001";
        l.beat = DialogueBeat::TankInDanger;
        l.tags = {"combat", "critical", "healer"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Tank is dropping! Pop defensives!";
        l.locales[DialogueLocale::RuRU] = "Танк просел! Защитные!";
        l.locales[DialogueLocale::DeDE] = "Der Tank fällt! Defensive ziehen!";
        l.locales[DialogueLocale::FrFR] = "Le tank faiblit ! Utilisez vos défenses !";
        l.locales[DialogueLocale::EsES] = "¡El tanque cae! ¡Usad defensas!";
        l.locales[DialogueLocale::ZhCN] = "坦克血崩！开减伤！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.target_switch.001";
        l.beat = DialogueBeat::TargetSwitch;
        l.tags = {"combat", "tactical", "dps"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Focus {targetName} now!";
        l.locales[DialogueLocale::RuRU] = "Смена цели! Бейте {targetName}!";
        l.locales[DialogueLocale::DeDE] = "Fokus auf {targetName}!";
        l.locales[DialogueLocale::FrFR] = "Focalisez {targetName} maintenant !";
        l.locales[DialogueLocale::EsES] = "¡Concentrad el fuego en {targetName}!";
        l.locales[DialogueLocale::ZhCN] = "立即集火 {targetName}！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.phase_change.001";
        l.beat = DialogueBeat::PhaseChange;
        l.tags = {"combat", "tactical"};
        l.weight = 1.0f;
        l.cooldownSec = 20;
        l.locales[DialogueLocale::EnUS] = "Phase change! Watch out!";
        l.locales[DialogueLocale::RuRU] = "Смена фазы боя! Будьте начеку!";
        l.locales[DialogueLocale::DeDE] = "Phasenwechsel! Passt auf!";
        l.locales[DialogueLocale::FrFR] = "Changement de phase ! Attention !";
        l.locales[DialogueLocale::EsES] = "¡Cambio de fase! ¡Cuidado!";
        l.locales[DialogueLocale::ZhCN] = "阶段转换！注意站位！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.incoming_aoe.001";
        l.beat = DialogueBeat::IncomingAoE;
        l.tags = {"combat", "tactical", "defensive"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Incoming AoE damage! Defensives up!";
        l.locales[DialogueLocale::RuRU] = "Массовый урон! Защитные способности!";
        l.locales[DialogueLocale::DeDE] = "Flächenschaden kommt! Defensivfähigkeiten bereit!";
        l.locales[DialogueLocale::FrFR] = "Gros dégâts de zone ! Utilisez vos défenses !";
        l.locales[DialogueLocale::EsES] = "¡Daño en área entrante! ¡Habilidades defensivas!";
        l.locales[DialogueLocale::ZhCN] = "全团范围伤害！开减伤保命！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.stop_damage.001";
        l.beat = DialogueBeat::StopDamage;
        l.tags = {"combat", "tactical"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Stop damage! Don't hit the boss!";
        l.locales[DialogueLocale::RuRU] = "Стоп урон! Не бейте босса!";
        l.locales[DialogueLocale::DeDE] = "Schadensstopp! Greift den Boss nicht an!";
        l.locales[DialogueLocale::FrFR] = "Arrêtez les attaques ! Ne frappez pas le boss !";
        l.locales[DialogueLocale::EsES] = "¡Paren el daño! ¡No golpeéis al jefe!";
        l.locales[DialogueLocale::ZhCN] = "停手！所有人停手别打！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.tank_swap.001";
        l.beat = DialogueBeat::TankSwap;
        l.tags = {"combat", "tactical", "tank"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Tank swap! Taunt off me!";
        l.locales[DialogueLocale::RuRU] = "Смена танков! Забирай босса!";
        l.locales[DialogueLocale::DeDE] = "Tankwechsel! Spotte ab!";
        l.locales[DialogueLocale::FrFR] = "Changement de tank ! Reprends l'aggro !";
        l.locales[DialogueLocale::EsES] = "¡Cambio de tanque! ¡Provoca al jefe!";
        l.locales[DialogueLocale::ZhCN] = "换坦！快嘲讽接怪！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "tactical.run_away.001";
        l.beat = DialogueBeat::RunAway;
        l.tags = {"combat", "tactical", "movement"};
        l.weight = 1.0f;
        l.cooldownSec = 15;
        l.locales[DialogueLocale::EnUS] = "Run away! Get out of there!";
        l.locales[DialogueLocale::RuRU] = "Бегите! Уходите оттуда!";
        l.locales[DialogueLocale::DeDE] = "Lauft weg! Bringt euch in Sicherheit!";
        l.locales[DialogueLocale::FrFR] = "Fuyez ! Sortez de là !";
        l.locales[DialogueLocale::EsES] = "¡Corred! ¡Salid de ahí!";
        l.locales[DialogueLocale::ZhCN] = "快跑！离开那里！";
        std::string err; AddLine(l, err);
    }

    // ------------------------------------------------------------------------
    // Player-Specific & Nemesis Memory
    // ------------------------------------------------------------------------
    {
        DialogueLine l;
        l.id = "nemesis.greeting.001";
        l.beat = DialogueBeat::NemesisGreeting;
        l.tags = {"nemesis", "personal", "threat"};
        l.weight = 1.0f;
        l.cooldownSec = 60;
        l.locales[DialogueLocale::EnUS] = "I remember you, {playerName}.";
        l.locales[DialogueLocale::RuRU] = "Я помню тебя, {playerName}.";
        l.locales[DialogueLocale::DeDE] = "Ich erinnere mich an dich, {playerName}.";
        l.locales[DialogueLocale::FrFR] = "Je me souviens de toi, {playerName}.";
        l.locales[DialogueLocale::EsES] = "Me acuerdo de ti, {playerName}.";
        l.locales[DialogueLocale::ZhCN] = "我记得你，{playerName}。";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "nemesis.threat.001";
        l.beat = DialogueBeat::NemesisThreat;
        l.tags = {"nemesis", "aggressive"};
        l.weight = 1.0f;
        l.cooldownSec = 60;
        l.locales[DialogueLocale::EnUS] = "You won't get lucky this time, {playerRace}.";
        l.locales[DialogueLocale::RuRU] = "В этот раз тебе не повезёт, {playerRace}.";
        l.locales[DialogueLocale::DeDE] = "Diesmal hast du kein Glück, {playerRace}.";
        l.locales[DialogueLocale::FrFR] = "La chance ne sera pas de ton côté cette fois, {playerRace}.";
        l.locales[DialogueLocale::EsES] = "Esta vez no tendrás suerte, {playerRace}.";
        l.locales[DialogueLocale::ZhCN] = "这一次你不会走运了，{playerRace}。";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "nemesis.revenge.001";
        l.beat = DialogueBeat::NemesisRevenge;
        l.tags = {"nemesis", "grudge"};
        l.weight = 1.2f;
        l.cooldownSec = 60;
        l.locales[DialogueLocale::EnUS] = "You took me down last time, {playerName}... payback time!";
        l.locales[DialogueLocale::RuRU] = "В прошлый раз ты одолел меня, {playerName}... время расплаты!";
        l.locales[DialogueLocale::DeDE] = "Letztes Mal hast du mich erwischt, {playerName}... Zeit für Rache!";
        l.locales[DialogueLocale::FrFR] = "Tu m'as eu la dernière fois, {playerName}... c'est l'heure de payer !";
        l.locales[DialogueLocale::EsES] = "La última vez me venciste, {playerName}... ¡hora de la revancha!";
        l.locales[DialogueLocale::ZhCN] = "上次你打败了我，{playerName}……受死吧！";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "nemesis.confidence.001";
        l.beat = DialogueBeat::NemesisConfidence;
        l.tags = {"nemesis", "taunt"};
        l.weight = 1.0f;
        l.cooldownSec = 60;
        l.locales[DialogueLocale::EnUS] = "Back for more punishment, {playerName}?";
        l.locales[DialogueLocale::RuRU] = "Вернулся за добавкой, {playerName}?";
        l.locales[DialogueLocale::DeDE] = "Wieder da für noch mehr Prügel, {playerName}?";
        l.locales[DialogueLocale::FrFR] = "De retour pour une autre leçon, {playerName} ?";
        l.locales[DialogueLocale::EsES] = "¿Has vuelto a por más, {playerName}?";
        l.locales[DialogueLocale::ZhCN] = "又来挨打了，{playerName}？";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "nemesis.recurring.001";
        l.beat = DialogueBeat::NemesisRecurring;
        l.tags = {"nemesis", "rivalry"};
        l.weight = 1.0f;
        l.cooldownSec = 60;
        l.locales[DialogueLocale::EnUS] = "How many times must we do this, {playerRace}?";
        l.locales[DialogueLocale::RuRU] = "Сколько раз нам ещё придётся повторить это, {playerRace}?";
        l.locales[DialogueLocale::DeDE] = "Wie oft müssen wir das noch wiederholen, {playerRace}?";
        l.locales[DialogueLocale::FrFR] = "Combien de fois devrons-nous répéter cela, {playerRace} ?";
        l.locales[DialogueLocale::EsES] = "¿Cuántas veces tendremos que repetir esto, {playerRace}?";
        l.locales[DialogueLocale::ZhCN] = "我们还要重复多少次，{playerRace}？";
        std::string err; AddLine(l, err);
    }

    // ------------------------------------------------------------------------
    // Mystery Cultist Shath'Yar (Isolated Pool)
    // ------------------------------------------------------------------------
    {
        DialogueLine l;
        l.id = "mystery.shathyar.whisper.001";
        l.beat = DialogueBeat::MysteryShathYarWhisper;
        l.tags = {"mystery", "shathyar", "cultist"};
        l.weight = 1.0f;
        l.cooldownSec = 60;
        l.nonCanonicalStyled = false;
        // Authentic Old God lore phrase
        l.locales[DialogueLocale::EnUS] = "Y'knath k'th'ryh w'ssh...";
        l.locales[DialogueLocale::RuRU] = "Y'knath k'th'ryh w'ssh...";
        l.locales[DialogueLocale::DeDE] = "Y'knath k'th'ryh w'ssh...";
        l.locales[DialogueLocale::FrFR] = "Y'knath k'th'ryh w'ssh...";
        l.locales[DialogueLocale::EsES] = "Y'knath k'th'ryh w'ssh...";
        l.locales[DialogueLocale::ZhCN] = "Y'knath k'th'ryh w'ssh...";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "mystery.shathyar.threat.001";
        l.beat = DialogueBeat::MysteryShathYarThreat;
        l.tags = {"mystery", "shathyar", "cultist"};
        l.weight = 1.0f;
        l.cooldownSec = 60;
        l.nonCanonicalStyled = false;
        l.locales[DialogueLocale::EnUS] = "H'un gnak rii gul'kafh an'q";
        l.locales[DialogueLocale::RuRU] = "H'un gnak rii gul'kafh an'q";
        l.locales[DialogueLocale::DeDE] = "H'un gnak rii gul'kafh an'q";
        l.locales[DialogueLocale::FrFR] = "H'un gnak rii gul'kafh an'q";
        l.locales[DialogueLocale::EsES] = "H'un gnak rii gul'kafh an'q";
        l.locales[DialogueLocale::ZhCN] = "H'un gnak rii gul'kafh an'q";
        std::string err; AddLine(l, err);
    }
    {
        DialogueLine l;
        l.id = "mystery.shathyar.incant.001";
        l.beat = DialogueBeat::MysteryShathYarIncantation;
        l.tags = {"mystery", "shathyar", "rune"};
        l.weight = 1.0f;
        l.cooldownSec = 60;
        l.nonCanonicalStyled = true;
        l.locales[DialogueLocale::EnUS] = "Shath'magar... thor'zhel fhtagn.";
        l.locales[DialogueLocale::RuRU] = "Shath'magar... thor'zhel fhtagn.";
        l.locales[DialogueLocale::DeDE] = "Shath'magar... thor'zhel fhtagn.";
        l.locales[DialogueLocale::FrFR] = "Shath'magar... thor'zhel fhtagn.";
        l.locales[DialogueLocale::EsES] = "Shath'magar... thor'zhel fhtagn.";
        l.locales[DialogueLocale::ZhCN] = "Shath'magar... thor'zhel fhtagn.";
        std::string err; AddLine(l, err);
    }
}
