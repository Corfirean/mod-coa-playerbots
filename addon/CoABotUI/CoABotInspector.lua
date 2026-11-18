--[[
    CoABotInspector - Live single-bot activity, bot roster filter/teleport,
    random dynamic encounter triggers, and command panel for WoW 3.3.5a.
    Part of CoABotUI. Slash commands: /botwatch, /cbwatch
--]]

local PROTOCOL_PREFIX = "COABOT"
local POLL_INTERVAL = 1.0

local ENCOUNTER_TYPES = {
    "assassin", "watcher", "skirmish", "ambush", "bounty", "nemesis",
    "duel", "caravan", "deserter", "hunter_prey", "rival_party",
    "rare_hunter", "mystery"
}

local ROSTER_FILTERS = {
    { id = "all",       label = "All" },
    { id = "combat",    label = "Combat" },
    { id = "quest",     label = "Quest" },
    { id = "gather",    label = "Gather" },
    { id = "encounter", label = "Event" },
    { id = "idle",      label = "Idle" },
    { id = "grouped",   label = "Group" },
    { id = "protected", label = "Prot" },
    { id = "dead",      label = "Dead" },
}

local inspectorFrame
local inspectorPage
local rosterPage
local selectedGuidLow = nil
local selectedName = nil
local pollElapsed = 0

local currentRosterPage = 0
local totalRosterPages = 1
local totalRosterMatched = 0
local currentRosterFilter = "all"
local rosterRowsData = {}
local rosterRowFrames = {}
local filterButtons = {}

local function SendWire(body)
    local playerName = UnitName("player")
    if playerName and playerName ~= "" then
        SendAddonMessage(PROTOCOL_PREFIX, body, "WHISPER", playerName)
    end
end

local function SendDotCmd(cmd)
    SendChatMessage(cmd, "SAY")
end

local function SplitFields(payload)
    local f = {}
    local start = 1
    while true do
        local pos = string.find(payload, ":", start, true)
        if not pos then
            table.insert(f, string.sub(payload, start))
            break
        end
        table.insert(f, string.sub(payload, start, pos - 1))
        start = pos + 1
    end
    return f
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

local lastRosterReqAt = 0
local function RequestRoster()
    local now = GetTime and GetTime() or 0
    if now > 0 and (now - lastRosterReqAt) < 0.25 then
        return
    end
    lastRosterReqAt = now
    rosterRowsData = {}
    for _, fb in ipairs(filterButtons) do
        if fb.filterId == currentRosterFilter then
            fb:LockHighlight()
        else
            fb:UnlockHighlight()
        end
    end
    if rosterPage and rosterPage.pageText then
        rosterPage.pageText:SetText("Loading...")
    end
    for _, rowFrame in ipairs(rosterRowFrames) do
        rowFrame:Hide()
    end
    SendWire("GETBOTROSTER:" .. tostring(currentRosterPage) .. ":" .. currentRosterFilter)
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

local function ShowTab(tabName)
    if not inspectorFrame then return end
    if tabName == "roster" then
        inspectorPage:Hide()
        rosterPage:Show()
        RequestRoster()
    else
        rosterPage:Hide()
        inspectorPage:Show()
        if selectedGuidLow then
            RequestInspect(selectedGuidLow)
        end
    end
end

local function RefreshRosterUI()
    if not rosterPage then return end
    rosterPage.pageText:SetText(string.format("Page %d / %d (%d bots)",
        currentRosterPage + 1, math.max(1, totalRosterPages), totalRosterMatched))

    for _, fb in ipairs(filterButtons) do
        if fb.filterId == currentRosterFilter then
            fb:LockHighlight()
        else
            fb:UnlockHighlight()
        end
    end

    for i, rowFrame in ipairs(rosterRowFrames) do
        local data = rosterRowsData[i]
        if data then
            local protBadge = data.prot and " |cFF66CCFF[P]|r" or ""
            rowFrame.text:SetText(string.format("|cFFFFD100%s|r (%s %s)%s - |cFF00FF88%s|r",
                data.name, data.level, data.role, protBadge, data.activity))
            rowFrame.botGuid = data.guid
            rowFrame.botName = data.name
            rowFrame:Show()
        else
            rowFrame:Hide()
        end
    end
end

local function ApplyBotState(payload)
    if not inspectorFrame or not inspectorPage then return end
    local f = SplitFields(payload)
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
    if isProtected then badges = badges .. " |cFF66CCFF[Protected]|r" end
    if inEncounter then badges = badges .. " |cFFFF9900[Encounter]|r" end
    if inCombat then badges = badges .. " |cFFFF4444[Combat]|r" end

    inspectorPage.headerInfo:SetText(string.format("|cFFFFD100%s|r (Lv %s, %s)  GUID: %s%s", name, level, role, tostring(guidLow), badges))
    inspectorPage.hpBar:SetValue(hpPct)
    inspectorPage.hpText:SetText(string.format("HP: %d%%", hpPct))
    inspectorPage.powerBar:SetValue(powerPct)
    inspectorPage.powerText:SetText(string.format("Resource: %d%%", powerPct))

    inspectorPage.activityVal:SetText("|cFF00FF88" .. activity .. "|r")
    inspectorPage.castVal:SetText(castName == "None" and "|cFF888888Idle|r" or ("|cFFFFCC00" .. castName .. "|r"))
    inspectorPage.targetVal:SetText(targetName == "None" and "|cFF888888None|r" or ("|cFFFFFFFF" .. targetName .. "|r"))
    inspectorPage.moveVal:SetText("|cFF66CCFF" .. moveOwner .. "|r")
end

local function BuildInspectorFrame()
    if inspectorFrame then return inspectorFrame end

    local f = CreateFrame("Frame", "CoABotInspectorFrame", UIParent)
    f:SetSize(410, 495)
    f:SetPoint("CENTER", UIParent, "CENTER", 260, 20)
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

    -- Top navigation tabs
    local tabInspect = CreateActionButton(f, 100, 22, "Inspector", function() ShowTab("inspector") end)
    tabInspect:SetPoint("TOPLEFT", 14, -36)
    local tabRoster = CreateActionButton(f, 100, 22, "Bot List", function() ShowTab("roster") end)
    tabRoster:SetPoint("LEFT", tabInspect, "RIGHT", 6, 0)

    -- =========================================================================
    -- TAB 1: Inspector + Commands + Random Events
    -- =========================================================================
    inspectorPage = CreateFrame("Frame", nil, f)
    inspectorPage:SetPoint("TOPLEFT", 0, -64)
    inspectorPage:SetPoint("BOTTOMRIGHT", 0, 0)

    local targetBtn = CreateActionButton(inspectorPage, 88, 22, "Use Target", function()
        if not SelectFromTarget() then
            DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABotInspector]|r Target a bot first (or enter GUID).")
        end
    end)
    targetBtn:SetPoint("TOPLEFT", 14, -2)

    local guidBox = CreateFrame("EditBox", nil, inspectorPage, "InputBoxTemplate")
    guidBox:SetSize(75, 20)
    guidBox:SetPoint("LEFT", targetBtn, "RIGHT", 8, 0)
    guidBox:SetAutoFocus(false)
    guidBox:SetNumeric(true)
    guidBox:SetScript("OnEnterPressed", function(self)
        local g = tonumber(self:GetText())
        if g and g > 0 then RequestInspect(g) end
        self:ClearFocus()
    end)

    local goBtn = CreateActionButton(inspectorPage, 62, 22, "Inspect", function()
        local g = tonumber(guidBox:GetText())
        if g and g > 0 then RequestInspect(g) else SelectFromTarget() end
    end)
    goBtn:SetPoint("LEFT", guidBox, "RIGHT", 6, 0)

    local refreshBtn = CreateActionButton(inspectorPage, 62, 22, "Refresh", function()
        if selectedGuidLow then RequestInspect(selectedGuidLow) else SelectFromTarget() end
    end)
    refreshBtn:SetPoint("LEFT", goBtn, "RIGHT", 6, 0)

    local tpBtn = CreateActionButton(inspectorPage, 70, 22, "TP to Bot", function()
        if selectedGuidLow then
            SendWire("TPTOBOT:" .. tostring(selectedGuidLow))
        end
    end)
    tpBtn:SetPoint("LEFT", refreshBtn, "RIGHT", 6, 0)

    inspectorPage.headerInfo = inspectorPage:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    inspectorPage.headerInfo:SetPoint("TOPLEFT", 14, -30)
    inspectorPage.headerInfo:SetWidth(380)
    inspectorPage.headerInfo:SetJustifyH("LEFT")
    inspectorPage.headerInfo:SetText("|cFFAAAAAASelect or target a bot to inspect.|r")

    inspectorPage.hpBar = CreateFrame("StatusBar", nil, inspectorPage)
    inspectorPage.hpBar:SetSize(380, 14)
    inspectorPage.hpBar:SetPoint("TOPLEFT", 14, -48)
    inspectorPage.hpBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    inspectorPage.hpBar:SetStatusBarColor(0.15, 0.80, 0.25)
    inspectorPage.hpBar:SetMinMaxValues(0, 100)
    inspectorPage.hpBar:SetValue(0)
    inspectorPage.hpText = inspectorPage.hpBar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    inspectorPage.hpText:SetPoint("CENTER")
    inspectorPage.hpText:SetText("HP: --")

    inspectorPage.powerBar = CreateFrame("StatusBar", nil, inspectorPage)
    inspectorPage.powerBar:SetSize(380, 14)
    inspectorPage.powerBar:SetPoint("TOPLEFT", 14, -66)
    inspectorPage.powerBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    inspectorPage.powerBar:SetStatusBarColor(0.20, 0.55, 0.95)
    inspectorPage.powerBar:SetMinMaxValues(0, 100)
    inspectorPage.powerBar:SetValue(0)
    inspectorPage.powerText = inspectorPage.powerBar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    inspectorPage.powerText:SetPoint("CENTER")
    inspectorPage.powerText:SetText("Resource: --")

    local function AddFieldRow(y, label)
        local lbl = inspectorPage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        lbl:SetPoint("TOPLEFT", 14, y)
        lbl:SetText(label)
        local val = inspectorPage:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        val:SetPoint("TOPLEFT", 115, y)
        val:SetWidth(275)
        val:SetJustifyH("LEFT")
        val:SetText("--")
        return val
    end

    inspectorPage.activityVal = AddFieldRow(-88, "Activity:")
    inspectorPage.castVal     = AddFieldRow(-104, "Casting:")
    inspectorPage.targetVal   = AddFieldRow(-120, "Target:")
    inspectorPage.moveVal     = AddFieldRow(-136, "Move Owner:")

    -- Squad Commands
    local squadHdr = inspectorPage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    squadHdr:SetPoint("TOPLEFT", 14, -158)
    squadHdr:SetText("|cFFFFD100Squad Commands (Addon Wire)|r")

    local bFollow = CreateActionButton(inspectorPage, 90, 22, "Follow", function()
        if selectedGuidLow then SendWire("FOLLOW:" .. selectedGuidLow) end
    end)
    bFollow:SetPoint("TOPLEFT", 14, -174)
    local bStay = CreateActionButton(inspectorPage, 90, 22, "Stay", function()
        if selectedGuidLow then SendWire("STAY:" .. selectedGuidLow) end
    end)
    bStay:SetPoint("LEFT", bFollow, "RIGHT", 6, 0)
    local bPull = CreateActionButton(inspectorPage, 90, 22, "Pull", function()
        if selectedGuidLow then SendWire("PULL:" .. selectedGuidLow) end
    end)
    bPull:SetPoint("LEFT", bStay, "RIGHT", 6, 0)
    local bStop = CreateActionButton(inspectorPage, 90, 22, "Stop Atk", function()
        if selectedGuidLow then SendWire("STOPATTACK:" .. selectedGuidLow) end
    end)
    bStop:SetPoint("LEFT", bPull, "RIGHT", 6, 0)

    local bTank = CreateActionButton(inspectorPage, 90, 22, "Role: Tank", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":tank") end
    end)
    bTank:SetPoint("TOPLEFT", 14, -200)
    local bHeal = CreateActionButton(inspectorPage, 90, 22, "Role: Heal", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":healer") end
    end)
    bHeal:SetPoint("LEFT", bTank, "RIGHT", 6, 0)
    local bDps = CreateActionButton(inspectorPage, 90, 22, "Role: DPS", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":dps") end
    end)
    bDps:SetPoint("LEFT", bHeal, "RIGHT", 6, 0)
    local bAuto = CreateActionButton(inspectorPage, 90, 22, "Role: Auto", function()
        if selectedGuidLow then SendWire("SETROLE:" .. selectedGuidLow .. ":auto") end
    end)
    bAuto:SetPoint("LEFT", bDps, "RIGHT", 6, 0)

    local bQuickFill = CreateActionButton(inspectorPage, 120, 22, "Quick Fill Group", function()
        SendWire("QUICKFILL:0")
    end)
    bQuickFill:SetPoint("TOPLEFT", 14, -226)
    local bTeleport = CreateActionButton(inspectorPage, 120, 22, "Bring Bots Here", function()
        SendWire("TELEPORT:0")
    end)
    bTeleport:SetPoint("LEFT", bQuickFill, "RIGHT", 6, 0)

    -- Admin / Debug Commands
    local gmHdr = inspectorPage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    gmHdr:SetPoint("TOPLEFT", 14, -254)
    gmHdr:SetText("|cFFFFD100Admin / Debug Commands (.botcmd)|r")

    local bCheckRole = CreateActionButton(inspectorPage, 90, 22, "Check Role", function()
        if selectedGuidLow then SendDotCmd(".botcmd checkrole " .. selectedGuidLow) end
    end)
    bCheckRole:SetPoint("TOPLEFT", 14, -270)
    local bGear = CreateActionButton(inspectorPage, 90, 22, "Fix Gear", function()
        if selectedGuidLow then SendDotCmd(".fixbotgear " .. selectedGuidLow) end
    end)
    bGear:SetPoint("LEFT", bCheckRole, "RIGHT", 6, 0)
    local bMoveDbg = CreateActionButton(inspectorPage, 90, 22, "Movement", function()
        if selectedGuidLow then SendDotCmd(".botcmd movement " .. selectedGuidLow) end
    end)
    bMoveDbg:SetPoint("LEFT", bGear, "RIGHT", 6, 0)
    local bBrainDbg = CreateActionButton(inspectorPage, 90, 22, "Brain", function()
        if selectedGuidLow then SendDotCmd(".botcmd brain " .. selectedGuidLow) end
    end)
    bBrainDbg:SetPoint("LEFT", bMoveDbg, "RIGHT", 6, 0)

    local bSuspend = CreateActionButton(inspectorPage, 120, 22, "Suspend Bot", function()
        if selectedGuidLow then SendDotCmd(".botcmd suspend " .. selectedGuidLow) end
    end)
    bSuspend:SetPoint("TOPLEFT", 14, -296)
    local bResume = CreateActionButton(inspectorPage, 120, 22, "Resume Bot", function()
        if selectedGuidLow then SendDotCmd(".botcmd resume " .. selectedGuidLow) end
    end)
    bResume:SetPoint("LEFT", bSuspend, "RIGHT", 6, 0)
    local bDespawn = CreateActionButton(inspectorPage, 120, 22, "Despawn Bot", function()
        if selectedGuidLow then SendDotCmd(".botcmd despawn " .. selectedGuidLow) end
    end)
    bDespawn:SetPoint("LEFT", bResume, "RIGHT", 6, 0)

    -- Random Events (Testing)
    local evtHdr = inspectorPage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    evtHdr:SetPoint("TOPLEFT", 14, -326)
    evtHdr:SetText("|cFFFF9900Dynamic Events (Testing)|r")

    local bRandEvt = CreateActionButton(inspectorPage, 120, 22, "Random Event", function()
        local pick = ENCOUNTER_TYPES[math.random(1, #ENCOUNTER_TYPES)]
        SendDotCmd(".botcmd encounter start " .. pick)
    end)
    bRandEvt:SetPoint("TOPLEFT", 14, -344)

    local bDuel = CreateActionButton(inspectorPage, 62, 22, "Duel", function()
        SendDotCmd(".botcmd encounter start duel")
    end)
    bDuel:SetPoint("LEFT", bRandEvt, "RIGHT", 6, 0)

    local bAmbush = CreateActionButton(inspectorPage, 62, 22, "Ambush", function()
        SendDotCmd(".botcmd encounter start ambush")
    end)
    bAmbush:SetPoint("LEFT", bDuel, "RIGHT", 6, 0)

    local bSkirmish = CreateActionButton(inspectorPage, 62, 22, "Skirmish", function()
        SendDotCmd(".botcmd encounter start skirmish")
    end)
    bSkirmish:SetPoint("LEFT", bAmbush, "RIGHT", 6, 0)

    local bCaravan = CreateActionButton(inspectorPage, 62, 22, "Caravan", function()
        SendDotCmd(".botcmd encounter start caravan")
    end)
    bCaravan:SetPoint("LEFT", bSkirmish, "RIGHT", 6, 0)

    local bWatcher = CreateActionButton(inspectorPage, 88, 22, "Watcher", function()
        SendDotCmd(".botcmd encounter start watcher")
    end)
    bWatcher:SetPoint("TOPLEFT", 14, -370)

    local bMystery = CreateActionButton(inspectorPage, 88, 22, "Mystery", function()
        SendDotCmd(".botcmd encounter start mystery")
    end)
    bMystery:SetPoint("LEFT", bWatcher, "RIGHT", 6, 0)

    local bRival = CreateActionButton(inspectorPage, 94, 22, "Rival Party", function()
        SendDotCmd(".botcmd encounter start rival_party")
    end)
    bRival:SetPoint("LEFT", bMystery, "RIGHT", 6, 0)

    local bHunter = CreateActionButton(inspectorPage, 94, 22, "Hunter&Prey", function()
        SendDotCmd(".botcmd encounter start hunter_prey")
    end)
    bHunter:SetPoint("LEFT", bRival, "RIGHT", 6, 0)

    local bEvtStop = CreateActionButton(inspectorPage, 120, 22, "Stop All Events", function()
        SendDotCmd(".botcmd encounter stop all")
    end)
    bEvtStop:SetPoint("TOPLEFT", 14, -396)

    local bEvtCd = CreateActionButton(inspectorPage, 120, 22, "Reset Cooldowns", function()
        SendDotCmd(".botcmd encounter cooldown")
    end)
    bEvtCd:SetPoint("LEFT", bEvtStop, "RIGHT", 6, 0)

    local bEvtList = CreateActionButton(inspectorPage, 120, 22, "List Active", function()
        SendDotCmd(".botcmd encounter list")
    end)
    bEvtList:SetPoint("LEFT", bEvtCd, "RIGHT", 6, 0)

    -- =========================================================================
    -- TAB 2: Bot List with Status Filter & Teleport
    -- =========================================================================
    rosterPage = CreateFrame("Frame", nil, f)
    rosterPage:SetPoint("TOPLEFT", 0, -64)
    rosterPage:SetPoint("BOTTOMRIGHT", 0, 0)
    rosterPage:Hide()

    local fx, fy = 14, -2
    for idx, flt in ipairs(ROSTER_FILTERS) do
        local btn = CreateActionButton(rosterPage, 58, 20, flt.label, function()
            currentRosterFilter = flt.id
            currentRosterPage = 0
            RequestRoster()
        end)
        btn.filterId = flt.id
        btn:SetPoint("TOPLEFT", fx, fy)
        table.insert(filterButtons, btn)
        fx = fx + 62
        if idx == 6 then
            fx = 14
            fy = -24
        end
    end

    local bRosterRefresh = CreateActionButton(rosterPage, 70, 20, "Refresh", function()
        RequestRoster()
    end)
    bRosterRefresh:SetPoint("TOPLEFT", 200, -24)

    for i = 1, 12 do
        local row = CreateFrame("Frame", nil, rosterPage)
        row:SetSize(382, 26)
        row:SetPoint("TOPLEFT", 14, -50 - (i - 1) * 28)

        row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        row.text:SetPoint("LEFT", 0, 0)
        row.text:SetWidth(270)
        row.text:SetJustifyH("LEFT")

        row.tpBtn = CreateActionButton(row, 40, 20, "TP", function()
            if row.botGuid then
                SendWire("TPTOBOT:" .. tostring(row.botGuid))
            end
        end)
        row.tpBtn:SetPoint("RIGHT", row, "RIGHT", -62, 0)

        row.inspectBtn = CreateActionButton(row, 58, 20, "Inspect", function()
            if row.botGuid then
                selectedGuidLow = row.botGuid
                selectedName = row.botName
                ShowTab("inspector")
            end
        end)
        row.inspectBtn:SetPoint("RIGHT", row, "RIGHT", 0, 0)

        row:Hide()
        table.insert(rosterRowFrames, row)
    end

    local prevBtn = CreateActionButton(rosterPage, 65, 22, "< Prev", function()
        if currentRosterPage > 0 then
            currentRosterPage = currentRosterPage - 1
            RequestRoster()
        end
    end)
    prevBtn:SetPoint("BOTTOMLEFT", 14, 14)

    rosterPage.pageText = rosterPage:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    rosterPage.pageText:SetPoint("CENTER", rosterPage, "BOTTOM", 0, 25)
    rosterPage.pageText:SetText("Page 1 / 1 (0 bots)")

    local nextBtn = CreateActionButton(rosterPage, 65, 22, "Next >", function()
        if currentRosterPage + 1 < totalRosterPages then
            currentRosterPage = currentRosterPage + 1
            RequestRoster()
        end
    end)
    nextBtn:SetPoint("BOTTOMRIGHT", -14, 14)

    f:SetScript("OnUpdate", function(_, elapsed)
        pollElapsed = pollElapsed + elapsed
        if pollElapsed >= POLL_INTERVAL then
            pollElapsed = 0
            if inspectorPage:IsShown() and selectedGuidLow then
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
        if inspectorFrame and inspectorFrame:IsShown() and inspectorPage and inspectorPage:IsShown() then
            SelectFromTarget()
        end
    elseif event == "CHAT_MSG_ADDON" then
        if prefix == PROTOCOL_PREFIX and type(message) == "string" then
            if string.sub(message, 1, 9) == "BOTSTATE:" then
                ApplyBotState(string.sub(message, 10))
            elseif string.sub(message, 1, 14) == "BOTROSTER_HDR:" then
                local f = SplitFields(string.sub(message, 15))
                currentRosterPage = tonumber(f[1]) or 0
                totalRosterPages = tonumber(f[2]) or 1
                totalRosterMatched = tonumber(f[3]) or 0
                rosterRowsData = {}
                RefreshRosterUI()
            elseif string.sub(message, 1, 14) == "BOTROSTER_ROW:" then
                local f = SplitFields(string.sub(message, 15))
                if #f >= 7 then
                    table.insert(rosterRowsData, {
                        guid = tonumber(f[1]),
                        name = f[2],
                        level = f[3],
                        role = f[4],
                        tag = f[5],
                        prot = (f[6] == "1"),
                        activity = f[7],
                    })
                    RefreshRosterUI()
                end
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
        ShowTab("inspector")
        RequestInspect(tonumber(trimmed))
        return
    end
    if f:IsShown() then
        f:Hide()
    else
        f:Show()
        ShowTab("inspector")
        if not SelectFromTarget() and selectedGuidLow then
            RequestInspect(selectedGuidLow)
        end
    end
end
