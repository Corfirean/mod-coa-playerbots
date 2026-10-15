/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Guardian Profiles
 * Supports:
 *   - Spec 21: Vanguard (Heavy Protection Shield Tank)
 *   - Spec 19: Gladiator (1H + Shield Physical DPS)
 *   - Spec 20: Inspiration (Formation & Aura Support DPS)
 *   - Spec 0: Default Fallback
 */

#ifndef COA_PLAYERBOTS_PROFILE_GUARDIAN_H
#define COA_PLAYERBOTS_PROFILE_GUARDIAN_H

namespace BotAI
{
    void RegisterGuardianProfiles();

    // Vanguard baseline formation: Tower Formation (800317) when the core lets the tank's strikes
    // be cast inside its stance, otherwise Line Formation (803130). Shared by the profile and the
    // legacy rotation so the two never alternate formations.
    unsigned int VanguardBaselineFormation();
}

#endif // COA_PLAYERBOTS_PROFILE_GUARDIAN_H
