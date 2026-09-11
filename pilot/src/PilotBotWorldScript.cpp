#include "PilotBotMgr.h"
#include "ScriptMgr.h"
#include "WorldScript.h"

class pilot_bot_worldscript : public WorldScript
{
public:
    pilot_bot_worldscript() : WorldScript("pilot_bot_worldscript", { WORLDHOOK_ON_UPDATE }) { }

    void OnUpdate(uint32 diff) override
    {
        sPilotBotMgr->Update(diff);
    }
};

void AddSC_pilot_bot_worldscript()
{
    new pilot_bot_worldscript();
}
