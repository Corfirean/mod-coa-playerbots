/*
 * mod-coa-playerbots
 *
 * Cooperation in the open world: what a decent player does for a stranger. An ungrouped bot on
 * its way somewhere, or searching an area, notices
 *  - a friendly player (bot or real) losing a fight to a mob that player has tagged, and steps in
 *    -- the mob's loot, credit and experience stay theirs (SocialRules::ShouldAssist);
 *  - a friendly corpse it can resurrect, and casts its resurrection spell on it. A real player
 *    gets the usual accept prompt; a dead bot accepts during its own resurrect grace period
 *    (BotAI::UpdateDeathHandling).
 *
 * Both are rare, cheap checks (a small player scan every few seconds, per bot, staggered), never
 * interrupt the bot's own fight, and never take anything from the person helped. The fight itself
 * belongs to the combat engine: WorldSocial only calls Attack() and steps back, exactly like a
 * quest task does. Sociability decides how often a bot bothers.
 */

#ifndef COA_PLAYERBOTS_WORLD_SOCIAL_H
#define COA_PLAYERBOTS_WORLD_SOCIAL_H

#include "WorldBrainState.h"

class Player;

namespace WorldSocial
{
    // Looks around for someone to help. True when the bot started helping this tick (the brain
    // then leaves the bot alone for the tick).
    bool TryHelp(Player* bot, BrainState& state);

    // True while a help action started by TryHelp still owns the bot (a resurrection cast in
    // progress): the brain must not walk the bot away and break the cast.
    bool IsHelping(Player* bot, BrainState& state);
}

#endif // COA_PLAYERBOTS_WORLD_SOCIAL_H
