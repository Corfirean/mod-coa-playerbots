#include "training/TrainingSession.h"
#include "Creature.h"
#include "CreatureAI.h"

namespace CoaTraining
{
class TrainingBossAI final : public CreatureAI
{
public:
    explicit TrainingBossAI(Creature* boss) : CreatureAI(boss) { }
    void UpdateAI(uint32) override { }
    void AttackStart(Unit* target) override { me->Attack(target, true); }
    void EnterEvadeMode(EvadeReason) override { }
    void DamageTaken(Unit*, uint32& damage, DamageEffectType, SpellSchoolMask) override
    {
        if (damage >= me->GetHealth()) me->SetHealth(me->GetMaxHealth());
        damage = std::min(damage, me->GetHealth() - 1);
    }
};
void InstallBossAI(Creature* boss)
{
    boss->SetLootRewardDisabled(true);
    boss->ReplaceAllNpcFlags(UNIT_NPC_FLAG_NONE);
    boss->SetLootMode(0);
    boss->SetLootRecipient(nullptr);
    boss->SetRegeneratingHealth(false);
    boss->SetReactState(REACT_PASSIVE);
    boss->SetNoCallAssistance(true);
    boss->SetNoSearchAssistance(true);
    boss->SetMaxHealth(1000000000);
    boss->SetHealth(boss->GetMaxHealth());
    boss->AIM_Initialize(new TrainingBossAI(boss));
}
}
