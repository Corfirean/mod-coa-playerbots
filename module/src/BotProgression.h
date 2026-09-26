/*
 * mod-coa-playerbots
 *
 * Everything a bot acquires as a character rather than decides as an AI: the abilities and
 * recipes a real player learns from their books, companions, mounts, profession tools and a food
 * supply that keeps pace with its level. Used by fresh-bot setup and again on every level-up, so a
 * bot that levels in the world keeps learning the way a player does instead of being frozen with
 * whatever it was created with.
 *
 * Learning goes through the same paths a player's own clicks do. This realm runs
 * CoA.AutoProgression = 0, so class abilities are not handed out on level-up; a player
 * asks their Book of Ascension ("Restore my available class abilities"), which is the
 * npc_ascension_training_book gossip in mod-ascension-compat. A bot does exactly that: summons the
 * book briefly and selects that gossip option through ScriptMgr. Recipes come from the Book of
 * Artisans, a real trainer (trainer 200001 via creature 57500), taught through the engine's own
 * Trainer::CanTeachSpell rules. Neither path needs a change to the core or to mod-ascension-compat.
 */

#ifndef COA_PLAYERBOTS_BOT_PROGRESSION_H
#define COA_PLAYERBOTS_BOT_PROGRESSION_H

#include "Define.h"

class Player;

namespace BotProgression
{
    // Class abilities available at the bot's level, via its Book of Ascension.
    void LearnAbilitiesFromBook(Player* bot);

    // Every Book of Artisans recipe the bot's current profession skills and level allow.
    // Returns how many were learned.
    uint32 LearnRecipesFromBook(Player* bot);

    // Book of Artisans and Book of Ascension companions.
    void GrantCompanions(Player* bot);

    // Two ground and two flying mounts for the bot's race and faction. Riding skill itself comes
    // from mod-ascension-compat (CoA.MaxRidingFromStart), exactly as for players.
    void GrantMounts(Player* bot);

    // The tools each profession needs in the bags, upgrading the enchanting rod with skill.
    void GrantProfessionTools(Player* bot);

    // Food and drink for the bot's level, taken from what vendors actually sell.
    void ProvisionFood(Player* bot, uint32 stackSize);

    // Raises profession caps to the new level, then relearns abilities, recipes, tools and food.
    // Talents are applied separately by BotTalentBuilds.
    void OnLevelUp(Player* bot, uint8 newLevel);
}

#endif // COA_PLAYERBOTS_BOT_PROGRESSION_H
