#include "training/TrainingArenaManager.h"
#include "UnitScript.h"
#include "SpellAuras.h"
#include "Unit.h"
#include "SpellInfo.h"
#include "Config.h"
#include "AllSpellScript.h"
#include "Spell.h"

// Observe native cast checks, dispatch and cancellation without altering results.
// An accepted AI request is not evidence that its heal reached the selected ally.
class coa_training_spellscript final : public AllSpellScript
{
public:
    coa_training_spellscript() : AllSpellScript("coa_training_spellscript",
        {ALLSPELLHOOK_ON_SPELL_CHECK_CAST, ALLSPELLHOOK_ON_CAST_CANCEL, ALLSPELLHOOK_ON_CAST,
            ALLSPELLHOOK_ON_SUCCESSFUL_DISPEL}) { }
    void OnSpellSuccessfulDispel(Spell* spell, Unit* target, SpellEffIndex, uint32 count) override
    {
        CoaTraining::RecordSpellLifecycle(spell->GetCaster()->ToUnit(), target->GetGUID(),
            spell->GetSpellInfo()->Id, "dispel_success", count, false);
    }
    void OnSpellCheckCast(Spell* spell, bool strict, SpellCastResult& result) override
    {
        if (result != SPELL_CAST_OK)
            CoaTraining::RecordSpellLifecycle(spell->GetCaster()->ToUnit(), spell->m_targets.GetUnitTargetGUID(),
                spell->GetSpellInfo()->Id, strict ? "script_check_rejected_strict" : "script_check_rejected_late", uint32(result), false);
    }
    void OnSpellCastCancel(Spell* spell, Unit* caster, SpellInfo const* info, bool bySelf) override
    {
        CoaTraining::RecordSpellLifecycle(caster, spell->m_targets.GetUnitTargetGUID(), info->Id, "cancel", 0, bySelf);
    }
    void OnSpellCast(Spell* spell, Unit* caster, SpellInfo const* info, bool) override
    {
        CoaTraining::RecordSpellLifecycle(caster, spell->m_targets.GetUnitTargetGUID(), info->Id, "dispatch", uint32(SPELL_CAST_OK), false);
    }
};

class coa_training_unitscript final : public UnitScript
{
public:
    coa_training_unitscript() : UnitScript("coa_training_unitscript", true,
        {UNITHOOK_ON_DAMAGE, UNITHOOK_ON_HEAL, UNITHOOK_ON_AURA_APPLY, UNITHOOK_ON_AURA_REMOVE,
            UNITHOOK_ON_PERIODIC_DAMAGE_RESULT}) { }
    void OnDamage(Unit* attacker, Unit* victim, uint32& amount) override
    {
        CoaTraining::RecordDamage(attacker, victim, amount);
    }
    void OnHeal(Unit* healer, Unit* target, uint32& amount) override
    {
        CoaTraining::RecordHeal(healer, target, amount);
    }
    void OnPeriodicDamageResult(Unit* target, Unit* attacker, uint32 damage, SpellInfo const* info) override
    {
        CoaTraining::RecordPeriodicDamage(attacker, target, damage, info ? info->Id : 0);
    }
    void OnAuraApply(Unit* target, Aura* aura) override
    {
        CoaTraining::RecordAura(target, aura->GetId(), aura->GetCasterGUID(), "apply", aura->GetStackAmount());
    }
    void OnAuraRemove(Unit* target, AuraApplication* application, AuraRemoveMode) override
    {
        Aura* aura = application->GetBase();
        CoaTraining::RecordAura(target, aura->GetId(), aura->GetCasterGUID(), "remove", aura->GetStackAmount());
    }
};
void AddSC_coa_training()
{
    if (sConfigMgr->GetOption<bool>("CoaBots.Training.Enable", false))
    {
        new coa_training_unitscript();
        new coa_training_spellscript();
    }
}
