/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

void AddSC_pilot_commandscript();
void AddSC_pilot_bot_worldscript();

// cf. the naming convention https://github.com/azerothcore/azerothcore-wotlk/blob/master/doc/changelog/master.md#how-to-upgrade-4
// module folder name is mod-coa-playerbots-pilot, '-' replaced with '_'
void Addmod_coa_playerbots_pilotScripts()
{
    AddSC_pilot_commandscript();
    AddSC_pilot_bot_worldscript();
}
