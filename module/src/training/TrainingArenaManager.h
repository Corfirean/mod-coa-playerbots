#ifndef COA_TRAINING_ARENA_MANAGER_H
#define COA_TRAINING_ARENA_MANAGER_H
#include "Define.h"
#include "ObjectGuid.h"
#include "training/TrainingData.h"
#include <atomic>

class ChatHandler;
class Player;
class Unit;
namespace BotAI { struct CombatContext; }

namespace CoaTraining
{
extern std::atomic<bool> Enabled;
void Initialize();
void Command(ChatHandler* handler, std::string const& args);
void Update(uint32 diff);
void Shutdown();
bool Owns(ObjectGuid guid);
bool Running(ObjectGuid guid);
void RecordDamage(Unit* attacker, Unit* victim, uint32 damage);
void RecordHeal(Unit* healer, Unit* target, uint32 effective);
void RecordPeriodicDamage(Unit* attacker, Unit* target, uint32 damage, uint32 spell);
void RecordSpellDamage(Unit* attacker, Unit* target, uint32 spell, uint32 amount, bool periodic);
void RecordSpellHeal(Unit* healer, Unit* target, uint32 spell, uint32 amount);
void RecordAura(Unit* target, uint32 spellId, ObjectGuid caster, char const* event, uint32 stacks);
void BeginDecision(BotAI::CombatContext const& ctx);
void Candidate(ObjectGuid bot, uint32 spell, char const* name, bool valid, float score, char const* reason);
void EndDecision(ObjectGuid bot, uint32 spell, float score, uint32 phase);
void CastResult(Player* bot, uint32 spell, uint32 result, Unit* target = nullptr);
// An interrupt cast by any bot of a running session: `result` is the game's cast result (SPELL_CAST_OK accepted).
void InterruptKick(Player* bot, uint32 spell, Unit* enemy, uint32 result, char const* source, bool applied = false);
void RecordSpellLifecycle(Unit* caster, ObjectGuid target, uint32 spell, char const* phase, uint32 result, bool bySelf);
}
#endif
