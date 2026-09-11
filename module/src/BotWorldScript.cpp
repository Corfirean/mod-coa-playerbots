#include "BotMgr.h"
#include "ScriptMgr.h"
#include "WorldScript.h"

class coa_playerbots_worldscript : public WorldScript
{
public:
    coa_playerbots_worldscript() : WorldScript("coa_playerbots_worldscript", { WORLDHOOK_ON_UPDATE }) { }

    void OnUpdate(uint32 diff) override
    {
        sBotMgr->Update(diff);
    }
};

void AddSC_coa_playerbots_worldscript()
{
    new coa_playerbots_worldscript();
}
