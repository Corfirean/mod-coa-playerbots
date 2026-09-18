/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

void AddSC_botcmd_commandscript();
void AddSC_coa_playerbots_worldscript();
void AddSC_coa_bot_addon_chat_script();
void AddSC_coa_bot_bg_fill_script();
void AddSC_coa_bot_lfg_fill_script();
void AddSC_coa_bot_petition_script();
void AddSC_coa_bot_restore_group_script();
void AddSC_coa_bot_leaderless_group_script();

// cf. the naming convention https://github.com/azerothcore/azerothcore-wotlk/blob/master/doc/changelog/master.md#how-to-upgrade-4
// module folder name is mod-coa-playerbots, '-' replaced with '_'
void Addmod_coa_playerbotsScripts()
{
    AddSC_botcmd_commandscript();
    AddSC_coa_playerbots_worldscript();
    AddSC_coa_bot_addon_chat_script();
    AddSC_coa_bot_bg_fill_script();
    AddSC_coa_bot_lfg_fill_script();
    AddSC_coa_bot_petition_script();
    AddSC_coa_bot_restore_group_script();
    AddSC_coa_bot_leaderless_group_script();
}
