/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- configuration loader.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_CONFIG_H
#define COA_PLAYERBOTS_ENCOUNTER_CONFIG_H

#include "EncounterTypes.h"

namespace EncounterSettings
{
    void LoadFromConfig();
    EncounterConfig const& Get();
    EncounterConfig& MutableForTest();
}

#endif // COA_PLAYERBOTS_ENCOUNTER_CONFIG_H
