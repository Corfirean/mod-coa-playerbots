/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Central Management, Channel Dispatch, and Commands.
 */

#ifndef COA_PLAYERBOTS_BOT_DIALOGUE_MANAGER_H
#define COA_PLAYERBOTS_BOT_DIALOGUE_MANAGER_H

#include "DialogueTypes.h"
#include "DialoguePool.h"
#include "DialogueMemory.h"
#include "DialogueSelector.h"
#include <string>
#include <memory>

class Player;

class BotDialogueManager
{
public:
    static BotDialogueManager& Instance();

    void Initialize(std::string const& dataDir);
    void Reload();
    void LoadAllData(std::string const& dir = "");

    // Semantic beat emission
    bool Emit(DialogueBeat beat, DialogueContext const& ctx, uint32 nowMs = 0);
    bool EmitGroupExchange(
        DialogueBeat initialBeat,
        DialogueBeat responseBeat,
        DialogueContext const& ctxA,
        DialogueContext const& ctxB,
        uint32 nowMs = 0);

    // Delivery to game world
    void Deliver(
        Player* speaker,
        Player* target,
        DialogueChannel channel,
        std::string const& text);

    // Accessors
    DialoguePool& GetPool() { return _pool; }
    DialoguePool const& GetPool() const { return _pool; }
    DialogueMemory& GetMemory() { return _memory; }
    DialogueMemory const& GetMemory() const { return _memory; }

    // Configuration
    bool IsEnabled() const { return _enabled; }
    void SetEnabled(bool v) { _enabled = v; }
    bool IsDebugEnabled() const { return _debug; }
    void SetDebugEnabled(bool v) { _debug = v; }
    bool IsDebugLogging() const { return _debug; }
    void SetDebugLogging(bool v) { _debug = v; }
    DialogueLocale GetDefaultLocale() const { return _defaultLocale; }
    void SetDefaultLocale(DialogueLocale loc) { _defaultLocale = loc; }

    uint32 GetPerBotCooldownSec() const { return _perBotCooldownSec; }
    uint32 GetGroupCooldownSec() const { return _groupCooldownSec; }
    uint32 GetBeatCooldownSec() const { return _beatCooldownSec; }

private:
    BotDialogueManager();
    ~BotDialogueManager() = default;

    DialoguePool _pool;
    DialogueMemory _memory;
    std::string _dataDir;

    bool _enabled = true;
    bool _debug = false;
    DialogueLocale _defaultLocale = DialogueLocale::EnUS;

    uint32 _perBotCooldownSec = 15;
    uint32 _groupCooldownSec = 20;
    uint32 _beatCooldownSec = 25;
};

#define sBotDialogueMgr BotDialogueManager::Instance()

#endif // COA_PLAYERBOTS_BOT_DIALOGUE_MANAGER_H
