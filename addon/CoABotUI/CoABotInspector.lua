--[[
    CoABotInspector - Live single-bot activity & command panel for WoW 3.3.5a
    Part of CoABotUI. Slash commands: /botwatch, /cbwatch
    Polls COABOT\tGETBOTSTATE:<guidLow> every 1.0s while shown and provides
    one-click buttons for both squad addon-wire commands and GM .botcmd commands.
--]]

local PROTOCOL_PREFIX = "COABOT"
local POLL_INTERVAL = 1.0

local inspectorFrame
local selectedGuidLow = nil
local selectedName = nil
local pollElapsed = 0

local function SendWire(body)
    local playerName = UnitName("player")
    if playerName and playerName ~= "" then
        SendAddonMessage(PROTOCOL_PREFIX, body, "WHISPER", playerName)
    end
end

local function SendDotCmd(cmd)
    if DEFAULT_CHAT_FRAME and DEFAULT_CHAT_FRAME.editBox then
        ChatFrame_OpenChat(cmd, DEFAULT_CHAT_FRAME)
        ChatEdit_SendText(DEFAULT_CHAT_FRAME.editBox, 0)
    else
        SendChatMessage(cmd, "SAY")
    end
end

local function GuidLowFromUnit(unit)
    local guid = UnitGUID(unit)
    if not guid or not UnitIsPlayer(unit) then
        return nil
    end
    local hexLow = string.sub(guid, -8)
    local num = tonumber(hexLow, 16)
    if num and num > 0 then
        return num
    end
    return nil
end

local function CreateActionButton(parent, width, height, label, onClick)
    local btn = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    btn:SetSize(width, height)
    btn:SetText(label)
    btn:SetScript("OnClick", onClick)
    return btn
end

local function RequestInspect(guidLow)
    if guidLow and guidLow > 0 then
        selectedGuidLow = guidLow
        SendWire("GETBOTSTATE:" .. tostring(guidLow))
    end
end

local function SelectFromTarget()
    if UnitExists("target") and UnitIsPlayer("target") and UnitGUID("target") ~= UnitGUID("player") then
        local g = GuidLowFromUnit("target")
        if g then
            selectedName = UnitName("target")
            RequestInspect(g)
            return true
        end
    end
    return false
end

local function ApplyBotState(payload)
    if not inspectorFrame then return end
    -- Format: guid|name|level|role|hpPct|powerPct|inCombat|castName|targetName|moveOwner|inEncounter|isProtected|activity
    local f = {}
    local start = 1
    while true do
        local pos = string.find(payload, "|", start, true)
        if not pos then
            table.insert(f, string.sub(payload, start))
            break
        end
        table.insert(f, string.sub(payload, start, pos - 1))
        start = pos + 1
    end
    if #f < 13 then return end

    local guidLow = tonumber(f[1])
    if selectedGuidLow and guidLow ~= selectedGuidLow then
        return
    end

    local name = f[2]
    local level = f[3]
    local role = f[4]
    local hpPct = tonumber(f[5]) or 0
    local powerPct = tonumber(f[6]) or 0
    local inCombat = (f[7] == "1")
    local castName = f[8]
    local targetName = f[9]
    local moveOwner = f[10]
    local inEncounter = (f[11] == "1")
    local isProtected = (f[12] == "1")
    local activity = f[13]

    selectedName = name

    local badges = ""
    if isProtected then
        badges = badges .. " |cFF66CCFF[Protected]|r"
    end
    if inEncounter then
        badges = badges .. " |cFFFF9900[Encounter]|r"
    end
    if inCombat then
        badges = badges .. " |cFFFF4444[Combat]|r"
    end

    inspectorFrame.headerInfo:SetText(string.format("|cFFFFD100%s|r (Lv %s, %s)  GUID: %s%s", name, level, role, tostring(guidLow), badges))
    inspectorFrame.hpBar:SetValue(hpPct)
    inspectorFrame.hpText:SetText(string.format("HP: %d%%", hpPct))
    inspectorFrame.powerBar:SetValue(powerPct)
    inspectorFrame.powerText:SetText(string.format("Resource: %d%%", powerPct))

    inspectorFrame.activityVal:SetText("|cFF00FF88" .. activity .. "|r")
    inspectorFrame.castVal:SetText(castName == "None" and "|cFF888888Idle|r" or ("|cFFFFCC00" .. castName .. "|r"))
    inspectorFrame.targetVal:SetText(targetName == "None" and "|cFF888888None|r" or ("|cFFFFFFFF" .. targetName .. "|r"))
    inspectorFrame.moveVal:SetText("|cFF66CCFF" .. moveOwner .. "|r")
end

local function BuildInspectorFrame()
    if inspectorFrame then return inspectorFrame end

    local f = CreateFrame("Frame", "CoABotInspectorFrame", UIParent)
    f:SetSize(370, 405)
    f:SetPoint("CENTER", UIParent, "CENTER", 260, 40)
    f:SetMovable(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", f.StartMoving)
    f:SetScript("OnDragStop", f.StopMovingOrSizing)
    f:SetFrameStrata("DIALOG")
    f:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 24,
        insets = { left = 6, right = 6, top = 6, bottom = 6 }
    })
    f:Hide()

    local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 14, -12)
    title:SetText("CoA Bot Inspector")

    local closeBtn = CreateFrame("Button", nil, f, "UIPanelCloseButton")
    closeBtn:SetPoint("TOPRIGHT", -4, -4)

    -- Target / GUID selection row
    local targetBtn = CreateActionButton(f, 95, 22, "Use Target", function()
        if not SelectFromTarget() then
            DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABotInspector]|r Target a bot first (or enter GUID).")
        end
    end)
    targetBtn:SetPoint("TOPLEFT", 14, -38)

    local guidBox = CreateFrame("EditBox", nil, f, "InputBoxTemplate")
    guidBox:SetSize(85, 20)
    guidBox:SetPoint("LEFT", targetBtn, "RIGHT", 10, 0)
    guidBox:SetAutoFocus(false)
    guidBox:SetNumeric(true)
    guidBox:SetScript("OnEnterPressed", function(self)
        local g = tonumber(self:GetText())
        if g and g > 0 then
            RequestInspect(g)
        end
        self:ClearFocus()
    end)

    local goBtn = CreateActionButton(f, 60, 22, "Inspect", function()
        local g = tonumber(guidBox:GetText())
        if g and g > 0 then
            RequestInspect(g)
        else
            SelectFromTarget()
        end
    end)
    goBtn:SetPoint("LEFT", guidBox, "RIGHT", 6, 0)

    local refreshBtn = CreateActionButton(f, 72, 22, "Refresh", function()
        if selectedGuidLow then
            RequestInspect(selectedGuidLow)
        else
            SelectFromTarget()
        end
    end)
    refreshBtn:SetPoint("LEFT", goBtn, "RIGHT", 6, 0)

    -- Header info
    f.headerInfo = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    f.headerInfo:SetPoint("TOPLEFT", 14, -68)
    f.headerInfo:SetWidth(340)
    f.headerInfo:SetJustifyH("LEFT")
    f.headerInfo:SetText("|cFFAAAAAASelect or target a bot to inspect.|r")

    -- HP Bar
    f.hpBar = CreateFrame("StatusBar", nil, f)
    f.hpBar:SetSize(340, 14)
    f.hpBar:SetPoint("TOPLEFT", 14, -88)
    f.hpBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    f.hpBar:SetStatusBarColor(0.15, 0.80, 0.25)
    f.hpBar:SetMinMaxValues(0, 100)
    f.hpBar:SetValue(0)
    f.hpText = f.hpBar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    f.hpText:SetPoint("CENTER")
    f.hpText:SetText("HP: --")

    -- Power Bar
    f.powerBar = CreateFrame("StatusBar", nil, f)
    f.powerBar:SetSize(340, 14)
    f.powerBar:SetPoint("TOPLEFT", 14, -106)
    f.powerBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    f.powerBar:SetStatusBarColor(0.20, 0.55, 0.95)
    f.powerBar:SetMinMaxValues(0, 100)
    f.powerBar:SetValue(0)
    f.powerText = f.powerBar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    f.powerText:SetPoint("CENTER")
    f.powerText:SetText("Resource: --")

    local function AddFieldRow(y, label)
        local lbl = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        lbl:SetPoint("TOPLEFT", 14, y)
        lbl:SetText(label)
        local val = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        val:SetPoint("TOPLEFT", 115, y)
        val:SetWidth(240)
        val:SetJustifyH("LEFT")
        val:SetText("--")
        return val
    end

    f.activityVal = AddFieldRow(-130, "Activity:")
    f.castVal     = AddFieldRow(-148, "Casting:")
    f.targetVal   = AddFieldRow(-166, "Target:")
    f.moveVal     = AddFieldRow(-184, "Move Owner:")

    -- Squad / Wire Commands section
    local squadHdr = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    squadHdr:SetPoint("TOPLEFT", 14, -210)
    squadHdr:SetText("|cFFFFD100Squad Commands (Addon Wire)|r")

    local bFollow = CreateActionButton(f, 80, 22, "Follow", function()
        if selectedGuidLow then SendWire("FOLLOW:" .. selectedGuidLow) end
    end)
    bFollow:SetPoint("TOPLEFT", 14, -228)

    local bStay = CreateActionButton(f, 80, 22, "Stay", function()
        if selectedGuidLow then SendWire("STAY:" .. selectedGuidLow) end
    end)
    bStay:SetPoint("LEFT", bFollow, "RIGHT", 6, 0)

    local bPull = CreateActionButton(f, 80, 22, "Pull", function()
        if selectedGuidLow then SendWire("PULL:" .. selectedGuidLow) end
    end)
    bPull:SetPoint("LEFT", bStay, "RIGHT", 6, 0)

    local bStop = CreateActionButton(f, 80, 22, "Stop Atk", function()
        if selectedGuidLow then SendWire("STOPATTACK:" .. selectedGuidLow) end
    end)
    bStop:SetPoint("LEFT", bPull, "RIGHT", 6, 0)

    local bTank = CreateActionButton(f, 80, 22, "Role: Tank", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":tank") end
    end)
    bTank:SetPoint("TOPLEFT", 14, -254)

    local bHeal = CreateActionButton(f, 80, 22, "Role: Heal", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":healer") end
    end)
    bHeal:SetPoint("LEFT", bTank, "RIGHT", 6, 0)

    local bDps = CreateActionButton(f, 80, 22, "Role: DPS", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":dps") end
    end)
    bDps:SetPoint("LEFT", bHeal, "RIGHT", 6, 0)

    local bAuto = CreateActionButton(f, 80, 22, "Role: Auto", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":auto") end
    end)
    bAuto:SetPoint("LEFT", bDps, "RIGHT", 6, 0)

    local bQuickFill = CreateActionButton(f, 110, 22, "Quick Fill Group", function()
        SendWire("QUICKFILL:0")
    end)
    bQuickFill:SetPoint("TOPLEFT", 14, -280)

    local bTeleport = CreateActionButton(f, 110, 22, "Teleport Bots", function()
        SendWire("TELEPORT:0")
    end)
    bTeleport:SetPoint("LEFT", bQuickFill, "RIGHT", 6, 0)

    -- GM / Admin .botcmd Commands section
    local gmHdr = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    gmHdr:SetPoint("TOPLEFT", 14, -310)
    gmHdr:SetText("|cFFFFD100Admin / Debug Commands (.botcmd)|r")

    local bCheckRole = CreateActionButton(f, 80, 22, "Check Role", function()
        if selectedGuidLow then SendDotCmd(".botcmd checkrole " .. selectedGuidLow) end
    end)
    bCheckRole:SetPoint("TOPLEFT", 14, -328)

    local bGear = CreateActionButton(f, 80, 22, "Fix Gear", function()
        if selectedGuidLow then SendDotCmd(".fixbotgear " .. selectedGuidLow) end
    end)
    bGear:SetPoint("LEFT", bCheckRole, "RIGHT", 6, 0)

    local bMoveDbg = CreateActionButton(f, 80, 22, "Movement", function()
        if selectedGuidLow then SendDotCmd(".botcmd movement " .. selectedGuidLow) end
    end)
    bMoveDbg:SetPoint("LEFT", bGear, "RIGHT", 6, 0)

    local bBrainDbg = CreateActionButton(f, 80, 22, "Brain", function()
        if selectedGuidLow then SendDotCmd(".botcmd brain " .. selectedGuidLow) end
    end)
    bBrainDbg:SetPoint("LEFT", bMoveDbg, "RIGHT", 6, 0)

    local bSuspend = CreateActionButton(f, 108, 22, "Suspend Bot", function()
        if selectedGuidLow then SendDotCmd(".botcmd suspend " .. selectedGuidLow) end
    end)
    bSuspend:SetPoint("TOPLEFT", 14, -356)

    local bResume = CreateActionButton(f, 108, 22, "Resume Bot", function()
        if selectedGuidLow then SendDotCmd(".botcmd resume " .. selectedGuidLow) end
    end)
    bResume:SetPoint("LEFT", bSuspend, "RIGHT", 6, 0)

    local bDespawn = CreateActionButton(f, 108, 22, "Despawn Bot", function()
        if selectedGuidLow then SendDotCmd(".botcmd despawn " .. selectedGuidLow) end
    end)
    bDespawn:SetPoint("LEFT", bResume, "RIGHT", 6, 0)

    f:SetScript("OnUpdate", function(_, elapsed)
        pollElapsed = pollElapsed + elapsed
        if pollElapsed >= POLL_INTERVAL then
            pollElapsed = 0
            if selectedGuidLow then
                SendWire("GETBOTSTATE:" .. tostring(selectedGuidLow))
            end
        end
    end)

    inspectorFrame = f
    return f
end

local eventFrame = CreateFrame("Frame")
eventFrame:RegisterEvent("CHAT_MSG_ADDON")
eventFrame:RegisterEvent("PLAYER_TARGET_CHANGED")
eventFrame:SetScript("OnEvent", function(_, event, prefix, message, _, _)
    if event == "PLAYER_TARGET_CHANGED" then
        if inspectorFrame and inspectorFrame:IsShown() then
            SelectFromTarget()
        end
    elseif event == "CHAT_MSG_ADDON" then
        if prefix == PROTOCOL_PREFIX and type(message) == "string" then
            if string.sub(message, 1, 9) == "BOTSTATE:" then
                ApplyBotState(string.sub(message, 10))
            end
        end
    end
end)

SLASH_COABOTWATCH1 = "/botwatch"
SLASH_COABOTWATCH2 = "/cbwatch"
SlashCmdList["COABOTWATCH"] = function(msg)
    local f = BuildInspectorFrame()
    local trimmed = msg and string.match(msg, "^%s*(.-)%s*$") or ""
    if trimmed ~= "" and tonumber(trimmed) then
        f:Show()
        RequestInspect(tonumber(trimmed))
        return
    end
    if f:IsShown() then
        f:Hide()
    else
        f:Show()
        if not SelectFromTarget() and selectedGuidLow then
            RequestInspect(selectedGuidLow)
        end
    end
end
