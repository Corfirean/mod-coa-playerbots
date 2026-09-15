--[[
    CoA Bot Control Panel (CoABotUI)
    WoW 3.3.5a AddOn for controlling companion playerbots in Conquest of Azeroth
    Wire Protocol: docs/addon-protocol.md
    Channel: SendAddonMessage("COABOT", "<VERB>:<botGuidLow>[:<arg>]", "WHISPER", UnitName("player"))
--]]

local ADDON_NAME = "CoABotUI"
local PROTOCOL_PREFIX = "COABOT"
local VERSION = "1.0.0"

-- Role configuration: names, labels, and display colors
local ROLES = {
    { id = "auto",    name = "Auto",    color = "|cFF888888", r = 0.55, g = 0.55, b = 0.55 },
    { id = "tank",    name = "Tank",    color = "|cFF3399FF", r = 0.20, g = 0.60, b = 1.00 },
    { id = "healer",  name = "Healer",  color = "|cFF33FF33", r = 0.20, g = 1.00, b = 0.20 },
    { id = "dps",     name = "DPS",     color = "|cFFFF3333", r = 1.00, g = 0.20, b = 0.20 },
    { id = "support", name = "Support", color = "|cFFFFCC00", r = 1.00, g = 0.80, b = 0.00 },
}

local ROLE_BY_ID = {}
for _, r in ipairs(ROLES) do
    ROLE_BY_ID[r.id] = r
end

-- Default Database
local dbDefaults = {
    point = "CENTER",
    relativePoint = "CENTER",
    xOfs = 0,
    yOfs = 100,
    isShown = true,
    isCollapsed = false,
    debug = true,
    testMode = false,
    roles = {}, -- [botGuidLow] = "tank"
}

-- Forward declarations
local mainFrame
local rows = {}
local activeMembers = {}
local popupMenu
local ApplyRoleAvailability
local ShowGuildTaskBoard
local RefreshTaskBoard
local UpdateRoleButtonText

-- Register prefix for client engines that support it
if RegisterAddonMessagePrefix then
    RegisterAddonMessagePrefix(PROTOCOL_PREFIX)
end

-------------------------------------------------------------------------------
-- Wire Protocol & Message Sending
-------------------------------------------------------------------------------

local function Log(msg)
    DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[" .. ADDON_NAME .. "]|r " .. tostring(msg))
end

local function DebugLog(msg)
    if CoABotUIDB and CoABotUIDB.debug then
        DEFAULT_CHAT_FRAME:AddMessage("|cFF00FF00[" .. ADDON_NAME .. " Wire]|r " .. tostring(msg))
    end
end

local function SendRawBody(body)
    local playerName = UnitName("player")
    if playerName and playerName ~= "" then
        SendAddonMessage(PROTOCOL_PREFIX, body, "WHISPER", playerName)
        DebugLog("Sent -> " .. body)
    else
        Log("|cFFFF4444Error:|r Unable to resolve player name for addon message.")
    end
end

local function SendBotCommand(verb, botGuidLow, arg)
    if not botGuidLow then return end

    local body = verb .. ":" .. tostring(botGuidLow)
    if arg and arg ~= "" then
        body = body .. ":" .. tostring(arg)
    end

    SendRawBody(body)
end

-- Verbs that act on the sender's whole group/guild rather than one specific bot use a "0"
-- placeholder in the botGuidLow slot -- see docs/addon-protocol.md (QUICKFILL/GUILDROSTER).
local function SendGroupCommand(verb)
    SendRawBody(verb .. ":0")
end

-------------------------------------------------------------------------------
-- Server Reply Cache (GETROLES/ROLES, GUILDROSTER/ROSTER)
-------------------------------------------------------------------------------

-- [botGuidLow] = { dps = true, tank = true, ... }
local rolesCache = {}
-- [botGuidLow] = "dps"/"tank"/"healer"/"support" -- the bot's *current* effective role,
-- separate from rolesCache (which roles its class *could* hold) and from the player's saved
-- preference in CoABotUIDB.roles (which might just be "auto").
local currentRoleCache = {}
-- [botGuidLow] = { name=, classId=, level=, task=, professions="Tailoring=225,..." }
local guildRosterCache = {}
-- [botGuidLow] = true once a GETROLES request has been sent, so a bot row only ever asks once
-- per session instead of re-asking on every roster scan.
local rolesRequested = {}

local function RequestRoles(botGuidLow)
    if not botGuidLow or rolesRequested[botGuidLow] then return end
    rolesRequested[botGuidLow] = true
    SendBotCommand("GETROLES", botGuidLow)
end

-- Shows the player's saved preference, plus -- when that preference is "auto" -- the bot's
-- actual current role in parentheses, e.g. "Auto (Healer)". Previously a bot left on Auto just
-- showed the bare word "Auto" with no indication of what it was actually playing as; confirmed
-- live feedback this was confusing. Falls back to just the preference alone until a ROLES
-- reply has arrived for this bot (see RequestRoles/currentRoleCache).
function UpdateRoleButtonText(row, botGuidLow, defaultRole)
    local savedRole = CoABotUIDB.roles[botGuidLow] or defaultRole or "auto"
    local roleInfo = ROLE_BY_ID[savedRole] or ROLE_BY_ID["auto"]

    if savedRole == "auto" and currentRoleCache[botGuidLow] then
        local currentInfo = ROLE_BY_ID[currentRoleCache[botGuidLow]] or roleInfo
        row.roleBtn:SetText(roleInfo.color .. "Auto|r " .. currentInfo.color .. "(" .. currentInfo.name .. ")|r")
    else
        row.roleBtn:SetText(roleInfo.color .. roleInfo.name .. "|r")
    end
end

local function RequestGuildRoster()
    SendGroupCommand("GUILDROSTER")
end

-- Splits on ":" without merging empty fields (Lua's gmatch on "[^:]+" would skip an empty
-- trailing field, e.g. a bot with no known professions) -- ROSTER's last field is often empty.
local function SplitColonKeepEmpty(str)
    local parts = {}
    local start = 1
    while true do
        local sep = str:find(":", start, true)
        if not sep then
            table.insert(parts, str:sub(start))
            break
        end
        table.insert(parts, str:sub(start, sep - 1))
        start = sep + 1
    end
    return parts
end

-- Dispatches one incoming "VERB:..." body from the server (see docs/addon-protocol.md's
-- "Server -> client replies" section). Only ROLES and ROSTER exist as of this writing.
local function HandleIncomingMessage(body)
    local parts = SplitColonKeepEmpty(body)
    local verb = parts[1]

    if verb == "ROLES" then
        local botGuidLow = tonumber(parts[2])
        local rolesCsv = parts[3] or ""
        local currentRole = parts[4]
        if not botGuidLow then return end

        local set = {}
        for roleId in rolesCsv:gmatch("[^,]+") do
            set[roleId] = true
        end
        rolesCache[botGuidLow] = set
        if currentRole and currentRole ~= "" then
            currentRoleCache[botGuidLow] = currentRole
        end

        -- If the role picker happens to be open for exactly this bot right now, re-apply
        -- greying immediately instead of waiting for the next OpenRoleMenu call.
        if popupMenu and popupMenu:IsShown() and popupMenu.activeBotGuid == botGuidLow then
            ApplyRoleAvailability(botGuidLow)
        end

        -- Refresh this bot's row text too, if it's currently visible -- otherwise "Auto"
        -- would sit there unlabeled until the next full RefreshUI (roster change/reopen).
        for _, row in ipairs(rows) do
            if row.botGuidLow == botGuidLow and row:IsShown() then
                UpdateRoleButtonText(row, botGuidLow)
                break
            end
        end

    elseif verb == "ROSTER" then
        local botGuidLow = tonumber(parts[2])
        if not botGuidLow then return end
        guildRosterCache[botGuidLow] = {
            name = parts[3] or "?",
            classId = tonumber(parts[4]) or 0,
            level = tonumber(parts[5]) or 0,
            task = parts[6] or "idle",
            professions = parts[7] or "",
        }
        if RefreshTaskBoard then
            RefreshTaskBoard()
        end
    end
end

-------------------------------------------------------------------------------
-- Mock Bots for Test & Preview Mode
-------------------------------------------------------------------------------

local MOCK_BOTS = {
    { name = "Startest",   guid = "0x0000000000000008", lowGuid = 8,  class = "DRUID",   defaultRole = "tank" },
    { name = "WdoctorBot", guid = "0x000000000000000E", lowGuid = 14, class = "SHAMAN",  defaultRole = "healer" },
    { name = "WhunterBot", guid = "0x000000000000000F", lowGuid = 15, class = "WARRIOR", defaultRole = "tank" },
    { name = "XorothBot",  guid = "0x0000000000000011", lowGuid = 17, class = "WARLOCK", defaultRole = "tank" },
    { name = "Shaniel",    guid = "0x0000000000000002", lowGuid = 2,  class = "HUNTER",  defaultRole = "dps" },
}

-------------------------------------------------------------------------------
-- Roster Scanning
-------------------------------------------------------------------------------

local function ExtractLowGuid(guidStr)
    if not guidStr then return 0 end
    -- Handle 0x... hex strings in 3.3.5a
    if guidStr:sub(1, 2) == "0x" then
        return tonumber(guidStr:sub(-8), 16) or 0
    end
    -- Fallback for string numbers
    return tonumber(guidStr) or 0
end

local function ScanRoster()
    local members = {}
    local numRaid = GetNumRaidMembers()
    local numParty = GetNumPartyMembers()

    if numRaid > 0 then
        for i = 1, numRaid do
            local unit = "raid" .. i
            if not UnitIsUnit(unit, "player") then
                local name = UnitName(unit)
                local guid = UnitGUID(unit)
                if name and guid then
                    local lowGuid = ExtractLowGuid(guid)
                    local _, classFileName = UnitClass(unit)
                    table.insert(members, {
                        unit = unit,
                        name = name,
                        guid = guid,
                        lowGuid = lowGuid,
                        class = classFileName or "WARRIOR",
                        isOnline = UnitIsConnected(unit),
                        isMock = false,
                    })
                end
            end
        end
    elseif numParty > 0 then
        for i = 1, numParty do
            local unit = "party" .. i
            local name = UnitName(unit)
            local guid = UnitGUID(unit)
            if name and guid then
                local lowGuid = ExtractLowGuid(guid)
                local _, classFileName = UnitClass(unit)
                table.insert(members, {
                    unit = unit,
                    name = name,
                    guid = guid,
                    lowGuid = lowGuid,
                    class = classFileName or "WARRIOR",
                    isOnline = UnitIsConnected(unit),
                    isMock = false,
                })
            end
        end
    end

    -- If solo and test mode is active, use mock roster
    if #members == 0 and CoABotUIDB and CoABotUIDB.testMode then
        for _, mock in ipairs(MOCK_BOTS) do
            table.insert(members, {
                unit = nil,
                name = mock.name,
                guid = mock.guid,
                lowGuid = mock.lowGuid,
                class = mock.class,
                defaultRole = mock.defaultRole,
                isOnline = true,
                isMock = true,
            })
        end
    end

    activeMembers = members
    return members
end

-------------------------------------------------------------------------------
-- Role Popup Menu
-------------------------------------------------------------------------------

local function CreateRolePopupMenu()
    local menu = CreateFrame("Frame", "CoABotUIRoleMenu", UIParent)
    menu:SetSize(140, #ROLES * 24 + 8)
    menu:SetFrameStrata("DIALOG")
    menu:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 12,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    })
    menu:SetBackdropColor(0.08, 0.08, 0.12, 0.95)
    menu:SetBackdropBorderColor(0.5, 0.5, 0.5, 1.0)
    menu:Hide()
    menu:EnableMouse(true)

    menu.buttons = {}
    for i, r in ipairs(ROLES) do
        local btn = CreateFrame("Button", nil, menu)
        btn:SetSize(132, 22)
        btn:SetPoint("TOPLEFT", menu, "TOPLEFT", 4, -4 - (i - 1) * 24)

        local hl = btn:CreateTexture(nil, "HIGHLIGHT")
        hl:SetAllPoints()
        hl:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
        hl:SetBlendMode("ADD")

        local text = btn:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        text:SetPoint("LEFT", btn, "LEFT", 8, 0)
        text:SetText(r.color .. r.name .. "|r")

        btn:SetScript("OnClick", function(self)
            if self.disabledForBot then return end
            if menu.activeBotGuid and menu.activeRoleButton then
                local botGuid = menu.activeBotGuid
                local chosenRole = r.id

                -- Save role in DB
                CoABotUIDB.roles[botGuid] = chosenRole

                -- Update button text
                menu.activeRoleButton:SetText(r.color .. r.name .. "|r")

                -- Send wire command
                SendBotCommand("SETROLE", botGuid, chosenRole)

                -- Re-request GETROLES right after so an "Auto" pick picks up its resolved
                -- current role (see UpdateRoleButtonText) instead of showing a bare "Auto"
                -- forever (this session only ever asks once per bot otherwise -- see
                -- rolesRequested). No C_Timer in 3.3.5a -- SendAddonMessage delivery/processing
                -- order between these two whispers is enough without an artificial delay.
                if chosenRole == "auto" then
                    rolesRequested[botGuid] = nil
                    RequestRoles(botGuid)
                end
            end
            menu:Hide()
        end)

        btn.text = text
        btn.roleId = r.id
        btn.roleColor = r.color
        btn.roleName = r.name
        menu.buttons[i] = btn
    end

    -- Close menu when clicking outside
    menu:SetScript("OnShow", function(self)
        self.timeElapsed = 0
    end)

    return menu
end

-- Greys out (but doesn't hide -- the row still shows what's not possible) any role button
-- this bot's class can't actually hold, per the cached GETROLES/ROLES reply. "auto" is always
-- left enabled (resetting to auto-detected is always valid). Until a reply has arrived for
-- this bot, every button stays enabled -- see RequestRoles's comment on why this fails open
-- rather than blocking on a round-trip the player would have to wait for.
function ApplyRoleAvailability(botGuidLow)
    local known = rolesCache[botGuidLow]
    for _, btn in ipairs(popupMenu.buttons) do
        local available = (btn.roleId == "auto") or not known or known[btn.roleId]
        btn.disabledForBot = not available
        if available then
            btn.text:SetText(btn.roleColor .. btn.roleName .. "|r")
        else
            btn.text:SetText("|cFF555555" .. btn.roleName .. " (n/a)|r")
        end
    end
end

local function OpenRoleMenu(parentButton, botGuidLow)
    if not popupMenu then
        popupMenu = CreateRolePopupMenu()
    end

    if popupMenu:IsShown() and popupMenu.activeBotGuid == botGuidLow then
        popupMenu:Hide()
        return
    end

    RequestRoles(botGuidLow)

    popupMenu.activeBotGuid = botGuidLow
    popupMenu.activeRoleButton = parentButton
    ApplyRoleAvailability(botGuidLow)
    popupMenu:ClearAllPoints()
    popupMenu:SetPoint("TOPLEFT", parentButton, "BOTTOMLEFT", 0, -2)
    popupMenu:Show()
end

-------------------------------------------------------------------------------
-- UI Construction
-------------------------------------------------------------------------------

local function CreateBotRow(parent, index)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(476, 32)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 12, -100 - (index - 1) * 34)

    -- Row background highlight on mouseover
    local bg = row:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints()
    bg:SetTexture("Interface\\FriendsFrame\\UI-FriendsFrame-HighlightBar")
    bg:SetAlpha(0.12)
    row.bg = bg

    -- Name & Low GUID Label
    local nameText = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    nameText:SetPoint("LEFT", row, "LEFT", 6, 0)
    nameText:SetWidth(140)
    nameText:SetJustifyH("LEFT")
    row.nameText = nameText

    -- Role Selector Button
    local roleBtn = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    roleBtn:SetSize(72, 22)
    roleBtn:SetPoint("LEFT", nameText, "RIGHT", 4, 0)
    roleBtn:SetText("Auto")
    roleBtn:SetScript("OnClick", function(self)
        if row.botGuidLow then
            OpenRoleMenu(self, row.botGuidLow)
        end
    end)
    row.roleBtn = roleBtn

    -- Action Buttons: Follow, Stay, Pull, Stop
    local btnFollow = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnFollow:SetSize(54, 22)
    btnFollow:SetPoint("LEFT", roleBtn, "RIGHT", 6, 0)
    btnFollow:SetText("Follow")
    btnFollow:SetScript("OnClick", function()
        SendBotCommand("FOLLOW", row.botGuidLow)
    end)
    row.btnFollow = btnFollow

    local btnStay = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnStay:SetSize(48, 22)
    btnStay:SetPoint("LEFT", btnFollow, "RIGHT", 4, 0)
    btnStay:SetText("Stay")
    btnStay:SetScript("OnClick", function()
        SendBotCommand("STAY", row.botGuidLow)
    end)
    row.btnStay = btnStay

    local btnPull = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnPull:SetSize(48, 22)
    btnPull:SetPoint("LEFT", btnStay, "RIGHT", 4, 0)
    btnPull:SetText("Pull")
    btnPull:SetScript("OnClick", function()
        SendBotCommand("PULL", row.botGuidLow)
    end)
    row.btnPull = btnPull

    local btnStop = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnStop:SetSize(48, 22)
    btnStop:SetPoint("LEFT", btnPull, "RIGHT", 4, 0)
    btnStop:SetText("Stop")
    btnStop:SetScript("OnClick", function()
        SendBotCommand("STOPATTACK", row.botGuidLow)
    end)
    row.btnStop = btnStop

    return row
end

local function UpdateTargetStatus(statusBar)
    if not statusBar then return end
    if UnitExists("target") then
        local targetName = UnitName("target") or "Unknown"
        local isEnemy = UnitCanAttack("player", "target")
        local color = isEnemy and "|cFFFF4444" or "|cFF44FF44"
        statusBar:SetText("Target: " .. color .. targetName .. "|r  (Ready for Pull)")
    else
        statusBar:SetText("Target: |cFF888888None (Select a target to Pull)|r")
    end
end

local function RefreshUI()
    if not mainFrame then return end

    local members = ScanRoster()
    local memberCount = #members

    -- Utility Bar (Quick Fill / Guild Tasks) doesn't depend on having any bots already in
    -- your group/raid -- Quick Fill's whole purpose is to work from an empty or partial group.
    mainFrame.utilityBar:Show()

    -- Update Window Height dynamically based on row count
    local contentHeight
    if memberCount == 0 then
        contentHeight = 188
        mainFrame.emptyNotice:Show()
        mainFrame.globalBar:Hide()
    else
        contentHeight = 138 + (memberCount * 34)
        mainFrame.emptyNotice:Hide()
        mainFrame.globalBar:Show()
    end

    if not CoABotUIDB.isCollapsed then
        mainFrame:SetHeight(contentHeight)
    end

    -- Update bot rows
    for i = 1, math.max(memberCount, #rows) do
        local member = members[i]
        if member then
            if not rows[i] then
                rows[i] = CreateBotRow(mainFrame, i)
            end

            local row = rows[i]
            row.botGuidLow = member.lowGuid
            RequestRoles(member.lowGuid)

            -- Class-colored name display
            local classColor = RAID_CLASS_COLORS[member.class] or { r = 1, g = 1, b = 1 }
            local colorCode = string.format("|cFF%02x%02x%02x", classColor.r * 255, classColor.g * 255, classColor.b * 255)
            local badge = member.isMock and " |cFF888888[Mock]|r" or ""
            row.nameText:SetText(colorCode .. member.name .. "|r" .. badge)

            -- Active role
            UpdateRoleButtonText(row, member.lowGuid, member.defaultRole)

            row:Show()
        else
            if rows[i] then
                rows[i]:Hide()
            end
        end
    end

    -- Update target bar
    UpdateTargetStatus(mainFrame.statusBar)

    -- Update test mode button styling
    if CoABotUIDB.testMode then
        mainFrame.btnTest:SetText("|cFF00FF00Test Mode|r")
    else
        mainFrame.btnTest:SetText("Test Mode")
    end
end

local function CreateMainFrame()
    local frame = CreateFrame("Frame", "CoABotUIMainFrame", UIParent)
    frame:SetSize(500, 200)
    frame:SetFrameStrata("MEDIUM")
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")

    -- Backing dialog texture
    frame:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 }
    })
    frame:SetBackdropColor(0.05, 0.05, 0.08, 0.95)

    -- Draggable Header Script
    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local pt, _, relPt, x, y = self:GetPoint()
        CoABotUIDB.point = pt
        CoABotUIDB.relativePoint = relPt
        CoABotUIDB.xOfs = x
        CoABotUIDB.yOfs = y
    end)

    -- Title Bar Text
    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 14, -10)
    title:SetText("|cFFFFD100CoA Bot Companion Control|r |cFF888888v" .. VERSION .. "|r")
    frame.title = title

    -- Close Button [X]
    local btnClose = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    btnClose:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -2, -2)
    btnClose:SetScript("OnClick", function()
        frame:Hide()
        CoABotUIDB.isShown = false
    end)

    -- Minimize/Collapse Button [-] / [+]
    local btnCollapse = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnCollapse:SetSize(22, 20)
    btnCollapse:SetPoint("RIGHT", btnClose, "LEFT", -2, 0)
    btnCollapse:SetText("-")
    btnCollapse:SetScript("OnClick", function(self)
        CoABotUIDB.isCollapsed = not CoABotUIDB.isCollapsed
        if CoABotUIDB.isCollapsed then
            self:SetText("+")
            frame:SetHeight(32)
            if frame.globalBar then frame.globalBar:Hide() end
            if frame.utilityBar then frame.utilityBar:Hide() end
            if frame.statusBar then frame.statusBar:Hide() end
            if frame.emptyNotice then frame.emptyNotice:Hide() end
            for _, r in ipairs(rows) do r:Hide() end
        else
            self:SetText("-")
            if frame.statusBar then frame.statusBar:Show() end
            RefreshUI()
        end
    end)
    frame.btnCollapse = btnCollapse

    -- Test Mode Toggle Button
    local btnTest = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnTest:SetSize(80, 20)
    btnTest:SetPoint("RIGHT", btnCollapse, "LEFT", -6, 0)
    btnTest:SetText("Test Mode")
    btnTest:SetScript("OnClick", function()
        CoABotUIDB.testMode = not CoABotUIDB.testMode
        Log("Test Mode " .. (CoABotUIDB.testMode and "|cFF00FF00Enabled|r (previewing mock bots)" or "|cFFFF4444Disabled|r"))
        RefreshUI()
    end)
    frame.btnTest = btnTest

    -- Global Command Bar (All Bots)
    local globalBar = CreateFrame("Frame", nil, frame)
    globalBar:SetSize(476, 26)
    globalBar:SetPoint("TOPLEFT", frame, "TOPLEFT", 12, -36)

    local lblAll = globalBar:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    lblAll:SetPoint("LEFT", globalBar, "LEFT", 4, 0)
    lblAll:SetText("|cFFCCCCCCAll Bots:|r")

    local btnAllFollow = CreateFrame("Button", nil, globalBar, "UIPanelButtonTemplate")
    btnAllFollow:SetSize(66, 20)
    btnAllFollow:SetPoint("LEFT", lblAll, "RIGHT", 8, 0)
    btnAllFollow:SetText("All Follow")
    btnAllFollow:SetScript("OnClick", function()
        for _, m in ipairs(activeMembers) do
            SendBotCommand("FOLLOW", m.lowGuid)
        end
    end)

    local btnAllStay = CreateFrame("Button", nil, globalBar, "UIPanelButtonTemplate")
    btnAllStay:SetSize(60, 20)
    btnAllStay:SetPoint("LEFT", btnAllFollow, "RIGHT", 4, 0)
    btnAllStay:SetText("All Stay")
    btnAllStay:SetScript("OnClick", function()
        for _, m in ipairs(activeMembers) do
            SendBotCommand("STAY", m.lowGuid)
        end
    end)

    local btnAllPull = CreateFrame("Button", nil, globalBar, "UIPanelButtonTemplate")
    btnAllPull:SetSize(60, 20)
    btnAllPull:SetPoint("LEFT", btnAllStay, "RIGHT", 4, 0)
    btnAllPull:SetText("All Pull")
    btnAllPull:SetScript("OnClick", function()
        for _, m in ipairs(activeMembers) do
            SendBotCommand("PULL", m.lowGuid)
        end
    end)

    local btnAllStop = CreateFrame("Button", nil, globalBar, "UIPanelButtonTemplate")
    btnAllStop:SetSize(60, 20)
    btnAllStop:SetPoint("LEFT", btnAllPull, "RIGHT", 4, 0)
    btnAllStop:SetText("All Stop")
    btnAllStop:SetScript("OnClick", function()
        for _, m in ipairs(activeMembers) do
            SendBotCommand("STOPATTACK", m.lowGuid)
        end
    end)

    frame.globalBar = globalBar

    -- Utility Bar: group-wide/guild-wide actions that don't target one specific bot
    local utilityBar = CreateFrame("Frame", nil, frame)
    utilityBar:SetSize(476, 26)
    utilityBar:SetPoint("TOPLEFT", frame, "TOPLEFT", 12, -64)

    local btnQuickFill = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnQuickFill:SetSize(110, 20)
    btnQuickFill:SetPoint("LEFT", utilityBar, "LEFT", 4, 0)
    btnQuickFill:SetText("Quick Fill Group")
    btnQuickFill:SetScript("OnClick", function()
        SendGroupCommand("QUICKFILL")
        Log("Requested quick-fill (tank/healer/dps) for your group.")
    end)

    local btnGuildTasks = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnGuildTasks:SetSize(110, 20)
    btnGuildTasks:SetPoint("LEFT", btnQuickFill, "RIGHT", 6, 0)
    btnGuildTasks:SetText("Guild Tasks")
    btnGuildTasks:SetScript("OnClick", function()
        ShowGuildTaskBoard()
    end)

    frame.utilityBar = utilityBar

    -- Target Status Bar (Footer)
    local statusBar = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    statusBar:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 14, 10)
    statusBar:SetJustifyH("LEFT")
    frame.statusBar = statusBar

    -- Empty Roster Notice
    local emptyNotice = CreateFrame("Frame", nil, frame)
    emptyNotice:SetSize(460, 80)
    emptyNotice:SetPoint("CENTER", frame, "CENTER", 0, -15)

    local emptyText = emptyNotice:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    emptyText:SetPoint("TOP", emptyNotice, "TOP", 0, 0)
    emptyText:SetText("|cFF888888No companion bots found in your party or raid.\nJoin a group with bots or click below to preview controls.|r")
    emptyText:SetJustifyH("CENTER")

    local btnEnableTest = CreateFrame("Button", nil, emptyNotice, "UIPanelButtonTemplate")
    btnEnableTest:SetSize(130, 22)
    btnEnableTest:SetPoint("TOP", emptyText, "BOTTOM", 0, -10)
    btnEnableTest:SetText("Enable Test Mode")
    btnEnableTest:SetScript("OnClick", function()
        CoABotUIDB.testMode = true
        RefreshUI()
    end)

    frame.emptyNotice = emptyNotice

    return frame
end

-------------------------------------------------------------------------------
-- Guild Task Board (professions/skill/current task per guild bot, + craft orders)
-------------------------------------------------------------------------------

local taskBoardFrame
local taskRows = {}
local CLASS_FILE_NAMES_BY_ID = {
    [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
    [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID",
}

-- Ascension's custom classes (12-32) ride on top of one of the 9 real WoW classes, but the
-- classId ROSTER sends is Player::getClass() -- which, unlike UnitClass()'s client-visible
-- name, IS the real underlying class byte here (server-side getClass() isn't reskinned to a
-- fictional CoA class id anywhere in this module -- see BotMgr/ClassSpecRoles, both key
-- entirely off this same raw class byte). Only used for a plausible class color; falls back
-- to white for any id this table doesn't recognize rather than guessing.
local function ClassColorForId(classId)
    local fileName = CLASS_FILE_NAMES_BY_ID[classId]
    local c = fileName and RAID_CLASS_COLORS[fileName]
    if c then
        return string.format("|cFF%02x%02x%02x", c.r * 255, c.g * 255, c.b * 255)
    end
    return "|cFFFFFFFF"
end

local function CreateTaskRow(parent, index)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(456, 40)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 12, -8 - (index - 1) * 44)

    local bg = row:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints()
    bg:SetTexture("Interface\\FriendsFrame\\UI-FriendsFrame-HighlightBar")
    bg:SetAlpha(0.10)

    local nameText = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    nameText:SetPoint("TOPLEFT", row, "TOPLEFT", 4, -2)
    nameText:SetWidth(200)
    nameText:SetJustifyH("LEFT")
    row.nameText = nameText

    local taskText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    taskText:SetPoint("TOPRIGHT", row, "TOPRIGHT", -4, -2)
    taskText:SetWidth(240)
    taskText:SetJustifyH("RIGHT")
    row.taskText = taskText

    local profText = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    profText:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 4, 2)
    profText:SetWidth(440)
    profText:SetJustifyH("LEFT")
    row.profText = profText

    return row
end

local function FormatProfessions(csv)
    if not csv or csv == "" then
        return "|cFF666666No known professions|r"
    end
    local out = {}
    for pair in csv:gmatch("[^,]+") do
        local prof, skill = pair:match("([^=]+)=(%d+)")
        if prof then
            table.insert(out, prof .. " (" .. skill .. ")")
        end
    end
    return "|cFF999999" .. table.concat(out, ", ") .. "|r"
end

function RefreshTaskBoard()
    if not taskBoardFrame or not taskBoardFrame:IsShown() then return end

    -- Stable order (by botGuidLow) so rows don't visibly reshuffle as replies stream in.
    local guids = {}
    for guid in pairs(guildRosterCache) do
        table.insert(guids, guid)
    end
    table.sort(guids)

    for i = 1, math.max(#guids, #taskRows) do
        local guid = guids[i]
        if guid then
            if not taskRows[i] then
                taskRows[i] = CreateTaskRow(taskBoardFrame.listArea, i)
            end
            local row = taskRows[i]
            local info = guildRosterCache[guid]
            local color = ClassColorForId(info.classId)
            row.nameText:SetText(color .. info.name .. "|r |cFF888888(lvl " .. info.level .. ")|r")

            local isIdle = (info.task == "idle")
            local taskColor = isIdle and "|cFF888888" or "|cFF55FF55"
            row.taskText:SetText(taskColor .. info.task .. "|r")

            row.profText:SetText(FormatProfessions(info.professions))
            row:Show()
        elseif taskRows[i] then
            taskRows[i]:Hide()
        end
    end

    if #guids == 0 then
        taskBoardFrame.emptyText:Show()
    else
        taskBoardFrame.emptyText:Hide()
    end
end

local function CreateGuildTaskBoardFrame()
    local frame = CreateFrame("Frame", "CoABotUITaskBoard", UIParent)
    frame:SetSize(500, 420)
    frame:SetPoint("CENTER", UIParent, "CENTER", 0, -40)
    frame:SetFrameStrata("MEDIUM")
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide()

    frame:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 }
    })
    frame:SetBackdropColor(0.05, 0.05, 0.08, 0.95)

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 14, -10)
    title:SetText("|cFFFFD100Guild Task Board|r")

    local btnClose = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    btnClose:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -2, -2)
    btnClose:SetScript("OnClick", function() frame:Hide() end)

    local btnRefresh = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnRefresh:SetSize(70, 20)
    btnRefresh:SetPoint("RIGHT", btnClose, "LEFT", -4, 0)
    btnRefresh:SetText("Refresh")
    btnRefresh:SetScript("OnClick", function() RequestGuildRoster() end)

    -- Roster/task list area
    local listArea = CreateFrame("Frame", nil, frame)
    listArea:SetSize(476, 260)
    listArea:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, -36)
    frame.listArea = listArea

    local emptyText = listArea:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    emptyText:SetPoint("TOP", listArea, "TOP", 0, -40)
    emptyText:SetText("|cFF888888No guild bots found. Make sure you're in a guild\nwith online companion bots, then click Refresh.|r")
    emptyText:SetJustifyH("CENTER")
    frame.emptyText = emptyText

    -- Craft Order form
    local formLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    formLabel:SetPoint("TOPLEFT", listArea, "BOTTOMLEFT", 12, -10)
    formLabel:SetText("|cFFFFD100Craft Order|r")

    local hintText = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    hintText:SetPoint("TOPLEFT", formLabel, "BOTTOMLEFT", 0, -4)
    hintText:SetText("Shift-click an item into the box, or type its item id.")

    local itemBox = CreateFrame("EditBox", nil, frame, "InputBoxTemplate")
    itemBox:SetSize(160, 20)
    itemBox:SetPoint("TOPLEFT", hintText, "BOTTOMLEFT", 6, -8)
    itemBox:SetAutoFocus(false)
    itemBox:SetNumeric(false)
    itemBox:SetMaxLetters(80)
    frame.itemBox = itemBox

    local countLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    countLabel:SetPoint("LEFT", itemBox, "RIGHT", 14, 0)
    countLabel:SetText("Count:")

    local countBox = CreateFrame("EditBox", nil, frame, "InputBoxTemplate")
    countBox:SetSize(40, 20)
    countBox:SetPoint("LEFT", countLabel, "RIGHT", 6, 0)
    countBox:SetAutoFocus(false)
    countBox:SetNumeric(true)
    countBox:SetMaxLetters(4)
    countBox:SetText("1")
    frame.countBox = countBox

    -- Accepts either a shift-clicked item link (extracts the numeric id out of the
    -- |Hitem:12345:... payload) or a plain numeric id typed/pasted in directly.
    local function ResolveItemId()
        local text = itemBox:GetText() or ""
        local id = text:match("item:(%d+)") or text:match("^%s*(%d+)%s*$")
        return id and tonumber(id) or nil
    end

    -- Shift-clicking an item normally only inserts its link into whichever *chat* edit box
    -- has focus (via the client calling the global ChatEdit_InsertLink) -- a plain custom
    -- EditBox like this one is never consulted unless that global is taught about it, same
    -- trick most 3.3.5 addons with a custom item-link input use.
    itemBox:SetScript("OnEditFocusGained", function(self) self.isFocusedForLinks = true end)
    itemBox:SetScript("OnEditFocusLost", function(self) self.isFocusedForLinks = false end)
    local origChatEditInsertLink = ChatEdit_InsertLink
    ChatEdit_InsertLink = function(text)
        if itemBox.isFocusedForLinks then
            itemBox:Insert(text)
            return true
        end
        return origChatEditInsertLink(text)
    end

    local btnOrder = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnOrder:SetSize(70, 22)
    btnOrder:SetPoint("LEFT", countBox, "RIGHT", 14, 0)
    btnOrder:SetText("Order")
    btnOrder:SetScript("OnClick", function()
        local itemId = ResolveItemId()
        if not itemId then
            Log("|cFFFF4444Craft order:|r couldn't find an item id in \"" .. (itemBox:GetText() or "") .. "\".")
            return
        end
        local count = tonumber(countBox:GetText()) or 1
        if count < 1 then count = 1 end
        SendRawBody("CRAFTORDER:" .. itemId .. ":" .. count)
        Log("Craft order sent: item " .. itemId .. " x" .. count .. " (any guild-mate bot that knows the recipe will pick it up).")
        itemBox:SetText("")
        countBox:SetText("1")
    end)

    return frame
end

function ShowGuildTaskBoard()
    if not taskBoardFrame then
        taskBoardFrame = CreateGuildTaskBoardFrame()
    end
    taskBoardFrame:Show()
    RequestGuildRoster()
    RefreshTaskBoard()
end

-------------------------------------------------------------------------------
-- Event Dispatcher
-------------------------------------------------------------------------------

local eventFrame = CreateFrame("Frame")
eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:RegisterEvent("PARTY_MEMBERS_CHANGED")
eventFrame:RegisterEvent("RAID_ROSTER_UPDATE")
eventFrame:RegisterEvent("PLAYER_TARGET_CHANGED")
eventFrame:RegisterEvent("PLAYER_ENTERING_WORLD")
eventFrame:RegisterEvent("CHAT_MSG_ADDON")

eventFrame:SetScript("OnEvent", function(self, event, arg1, arg2, arg3, arg4)
    if event == "ADDON_LOADED" and arg1 == ADDON_NAME then
        -- Initialize SavedVariables
        if not CoABotUIDB then
            CoABotUIDB = {}
        end
        for k, v in pairs(dbDefaults) do
            if CoABotUIDB[k] == nil then
                CoABotUIDB[k] = v
            end
        end

        -- Create Main Frame
        mainFrame = CreateMainFrame()

        -- Restore saved position
        mainFrame:ClearAllPoints()
        mainFrame:SetPoint(CoABotUIDB.point, UIParent, CoABotUIDB.relativePoint, CoABotUIDB.xOfs, CoABotUIDB.yOfs)

        if CoABotUIDB.isShown then
            mainFrame:Show()
        else
            mainFrame:Hide()
        end

        RefreshUI()
        Log("Loaded v" .. VERSION .. ". Type |cFFFFD100/coabot|r for commands.")

    elseif event == "PARTY_MEMBERS_CHANGED" or event == "RAID_ROSTER_UPDATE" or event == "PLAYER_ENTERING_WORLD" then
        if mainFrame and mainFrame:IsShown() and not CoABotUIDB.isCollapsed then
            RefreshUI()
        end

    elseif event == "PLAYER_TARGET_CHANGED" then
        if mainFrame and mainFrame.statusBar and mainFrame:IsShown() then
            UpdateTargetStatus(mainFrame.statusBar)
        end

    elseif event == "CHAT_MSG_ADDON" and arg1 == PROTOCOL_PREFIX then
        DebugLog("Recv <- " .. tostring(arg2) .. " from " .. tostring(arg4))
        if arg2 then
            HandleIncomingMessage(arg2)
        end
    end
end)

-------------------------------------------------------------------------------
-- Slash Commands
-------------------------------------------------------------------------------

SLASH_COABOT1 = "/coabot"
SLASH_COABOT2 = "/coabots"

SlashCmdList["COABOT"] = function(msg)
    local cmd = (msg or ""):lower():match("^%s*(%S+)") or ""

    if cmd == "toggle" or cmd == "" then
        if mainFrame:IsShown() then
            mainFrame:Hide()
            CoABotUIDB.isShown = false
        else
            mainFrame:Show()
            CoABotUIDB.isShown = true
            RefreshUI()
        end
    elseif cmd == "show" then
        mainFrame:Show()
        CoABotUIDB.isShown = true
        RefreshUI()
    elseif cmd == "hide" then
        mainFrame:Hide()
        CoABotUIDB.isShown = false
    elseif cmd == "test" then
        CoABotUIDB.testMode = not CoABotUIDB.testMode
        Log("Test Mode " .. (CoABotUIDB.testMode and "|cFF00FF00Enabled|r" or "|cFFFF4444Disabled|r"))
        if not mainFrame:IsShown() then
            mainFrame:Show()
            CoABotUIDB.isShown = true
        end
        RefreshUI()
    elseif cmd == "reset" then
        mainFrame:ClearAllPoints()
        mainFrame:SetPoint("CENTER", UIParent, "CENTER", 0, 100)
        CoABotUIDB.point = "CENTER"
        CoABotUIDB.relativePoint = "CENTER"
        CoABotUIDB.xOfs = 0
        CoABotUIDB.yOfs = 100
        Log("Frame position reset to center.")
    elseif cmd == "debug" then
        CoABotUIDB.debug = not CoABotUIDB.debug
        Log("Wire debug logging " .. (CoABotUIDB.debug and "|cFF00FF00Enabled|r" or "|cFFFF4444Disabled|r"))
    else
        Log("Commands:")
        Log("  |cFFFFD100/coabot|r - Toggle bot control panel")
        Log("  |cFFFFD100/coabot test|r - Toggle test mode (mock bot preview)")
        Log("  |cFFFFD100/coabot reset|r - Reset panel to screen center")
        Log("  |cFFFFD100/coabot debug|r - Toggle wire message debug logging")
    end
end
