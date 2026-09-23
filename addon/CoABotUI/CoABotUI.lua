--[[
    CoA Companions Control Panel (CoABotUI)
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
    roles = {}, -- [botGuidLow] = "tank"
    autoDungeon = false,
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
local ShowOrderPicker
local autoDungeonSynced = false

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

-- [categoryId] = { {entry=, name=}, ... } in server arrival order -- GCAT chunks arrive
-- RequiredLevel-sorted (low-level materials first, see BotMgr::GetGatherCatalog), RCAT chunks
-- arrive name-sorted (see BotMgr::GetRecipeCatalog) -- kept as a plain array instead of an
-- entry-keyed dict specifically to preserve that server-side ordering for display.
local orderCatalog = {}
-- [verb] = true once that catalog request has been sent, so it's only ever asked for once per
-- session (same one-shot idea as rolesRequested) -- both catalogs are realm-wide/guild-wide
-- data that doesn't change while this session is open.
local catalogRequested = {}

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

-- Forward declaration -- HandleIncomingMessage (defined below, before the picker UI exists yet)
-- needs to poke the picker's refresh once new catalog rows stream in; the picker itself is only
-- built lazily the first time it's opened (see ShowOrderPicker).
local RefreshOrderPicker

local function RequestOrderCatalogs()
    if not catalogRequested["gather"] then
        catalogRequested["gather"] = true
        SendGroupCommand("GETGATHERCATALOG")
    end
    if not catalogRequested["recipe"] then
        catalogRequested["recipe"] = true
        SendGroupCommand("GETRECIPECATALOG")
    end
end

-- Appends one GCAT/RCAT chunk's "entry,name|entry,name|..." payload to orderCatalog[category].
-- Item names never contain "," or "|" (no real WoW item does), so this plain gmatch is safe.
local function ParseCatalogChunk(category, itemsCsv)
    orderCatalog[category] = orderCatalog[category] or {}
    local list = orderCatalog[category]
    for entryStr, name in (itemsCsv or ""):gmatch("(%d+),([^|]+)") do
        table.insert(list, { entry = tonumber(entryStr), name = name })
    end
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

    elseif verb == "GCAT" then
        local category = parts[2]
        if not category or category == "" then return end
        ParseCatalogChunk(category, parts[3])
        if RefreshOrderPicker then
            RefreshOrderPicker()
        end

    elseif verb == "RCAT" then
        ParseCatalogChunk("recipe", parts[2])
        if RefreshOrderPicker then
            RefreshOrderPicker()
        end
    end
end

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
                })
            end
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
            row.nameText:SetText(colorCode .. member.name .. "|r")

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

    -- Auto Dungeon Mode: while on, a grouped Tank bot pulls the nearest pack on its own inside
    -- a dungeon/raid instead of waiting for the player to engage first (see BotMgr::
    -- SetAutoDungeonMode / the AUTODUNGEON verb in docs/addon-protocol.md). Persisted in
    -- CoABotUIDB so it survives a relog, but the *server*-side state is what actually matters
    -- (re-sent here on login -- see the ADDON_LOADED handler) since the server has no memory of
    -- a client-only setting across a restart of its own.
    local btnAutoDungeon = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnAutoDungeon:SetSize(130, 20)
    btnAutoDungeon:SetPoint("LEFT", btnGuildTasks, "RIGHT", 6, 0)
    local function RefreshAutoDungeonButton()
        if CoABotUIDB.autoDungeon then
            btnAutoDungeon:SetText("|cFF44FF44Auto Dungeon: ON|r")
        else
            btnAutoDungeon:SetText("Auto Dungeon: OFF")
        end
    end
    btnAutoDungeon:SetScript("OnClick", function()
        CoABotUIDB.autoDungeon = not CoABotUIDB.autoDungeon
        SendRawBody("AUTODUNGEON:" .. (CoABotUIDB.autoDungeon and "1" or "0"))
        Log(CoABotUIDB.autoDungeon
            and "Auto Dungeon Mode enabled -- your Tank bot will pull on its own inside instances."
            or "Auto Dungeon Mode disabled.")
        RefreshAutoDungeonButton()
    end)
    RefreshAutoDungeonButton()
    frame.RefreshAutoDungeonButton = RefreshAutoDungeonButton

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
    emptyText:SetText("|cFF888888No companion bots found in your party or raid.\nJoin a group with bots to control them here.|r")
    emptyText:SetJustifyH("CENTER")

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

-- Standard Blizzard trade-icon texture names for the 14 professions BotMgr::PROFESSION_SKILLS
-- sends (see docs/addon-protocol.md's ROSTER verb) -- these are static client art asset paths,
-- unrelated to this realm's own data, so nothing here needs server-side verification. Shown as
-- small icons with a hover tooltip instead of a wall of "Name (123)" text, which was overflowing
-- and overlapping other rows once every bot actually had all 14 professions granted.
local PROFESSION_ICONS = {
    ["Blacksmithing"]  = "Interface\\Icons\\Trade_BlackSmithing",
    ["Leatherworking"] = "Interface\\Icons\\Trade_LeatherWorking",
    ["Alchemy"]        = "Interface\\Icons\\Trade_Alchemy",
    ["Herbalism"]      = "Interface\\Icons\\Trade_Herbalism",
    ["Mining"]         = "Interface\\Icons\\Trade_Mining",
    ["Tailoring"]      = "Interface\\Icons\\Trade_Tailoring",
    ["Engineering"]    = "Interface\\Icons\\Trade_Engineering",
    ["Enchanting"]     = "Interface\\Icons\\Trade_Engraving",
    ["Skinning"]       = "Interface\\Icons\\INV_Misc_Pelt_Wolf_01",
    ["Jewelcrafting"]  = "Interface\\Icons\\INV_Misc_Gem_01",
    ["Inscription"]    = "Interface\\Icons\\INV_Inscription_Tradeskill01",
    ["First Aid"]      = "Interface\\Icons\\Spell_Holy_SealOfSacrifice2",
    ["Cooking"]        = "Interface\\Icons\\Trade_Cooking",
    ["Fishing"]        = "Interface\\Icons\\Trade_Fishing",
}
local MAX_PROFESSION_ICONS = 14

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

    -- Profession icon strip -- a fixed pool of icon buttons reused across refreshes (same
    -- pattern as the bot-row/order-row pools elsewhere in this file), each with its own
    -- OnEnter/OnLeave tooltip showing "Profession (skill)".
    row.profIcons = {}
    for i = 1, MAX_PROFESSION_ICONS do
        local icon = CreateFrame("Button", nil, row)
        icon:SetSize(16, 16)
        icon:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 4 + (i - 1) * 18, 2)
        icon.tex = icon:CreateTexture(nil, "ARTWORK")
        icon.tex:SetAllPoints()
        icon.tex:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        icon:SetScript("OnEnter", function(self)
            if not self.profName then return end
            GameTooltip:SetOwner(self, "ANCHOR_TOP")
            GameTooltip:AddLine(self.profName)
            GameTooltip:AddLine("Skill: " .. tostring(self.profSkill), 1, 1, 1)
            GameTooltip:Show()
        end)
        icon:SetScript("OnLeave", function() GameTooltip:Hide() end)
        icon:Hide()
        row.profIcons[i] = icon
    end

    local noProfText = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    noProfText:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 4, 2)
    noProfText:SetText("|cFF666666No known professions|r")
    noProfText:Hide()
    row.noProfText = noProfText

    return row
end

-- Populates row's profession icon pool from a "Name=skill,Name=skill,..." CSV, showing the
-- "No known professions" text instead when empty.
local function ApplyProfessionIcons(row, csv)
    local pairs_ = {}
    for pair in (csv or ""):gmatch("[^,]+") do
        local prof, skill = pair:match("([^=]+)=(%d+)")
        if prof then
            table.insert(pairs_, { name = prof, skill = skill })
        end
    end

    for i = 1, MAX_PROFESSION_ICONS do
        local icon = row.profIcons[i]
        local entry = pairs_[i]
        if entry then
            icon.tex:SetTexture(PROFESSION_ICONS[entry.name] or "Interface\\Icons\\INV_Misc_QuestionMark")
            icon.profName = entry.name
            icon.profSkill = entry.skill
            icon:Show()
        else
            icon:Hide()
        end
    end

    if #pairs_ == 0 then
        row.noProfText:Show()
    else
        row.noProfText:Hide()
    end
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

            ApplyProfessionIcons(row, info.professions)
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

    -- Order Materials / Craft Order launcher -- opens the icon-menu picker (ShowOrderPicker)
    -- instead of making the player type/shift-click a raw item id, per the user's explicit ask.
    local btnOrderMaterials = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnOrderMaterials:SetSize(160, 24)
    btnOrderMaterials:SetPoint("TOPLEFT", listArea, "BOTTOMLEFT", 12, -12)
    btnOrderMaterials:SetText("|cFFFFD100Order Materials...|r")
    btnOrderMaterials:SetScript("OnClick", function() ShowOrderPicker() end)

    local refreshTimer = 0
    frame:SetScript("OnUpdate", function(self, elapsed)
        refreshTimer = refreshTimer + elapsed
        if refreshTimer >= 3.0 then
            refreshTimer = 0
            RequestGuildRoster()
        end
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
-- Order Picker (icon-menu Gather/Craft orders -- replaces typing a raw item id)
-------------------------------------------------------------------------------

-- The first 6 are gather-order categories (BotMgr::GetGatherCatalog / GATHERORDER, any online
-- guild-mate bot can pick these up since every bot already has every gathering profession
-- maxed -- see GrantAllProfessions). "recipe" is the craft-order catalog (BotMgr::
-- GetRecipeCatalog / CRAFTORDER) -- only items a guild-mate bot can *actually* craft right now.
local ORDER_CATEGORIES = {
    { id = "herb",    label = "Herbs",   verb = "GATHERORDER" },
    { id = "ore",     label = "Ore",     verb = "GATHERORDER" },
    { id = "cloth",   label = "Cloth",   verb = "GATHERORDER" },
    { id = "leather", label = "Leather", verb = "GATHERORDER" },
    { id = "meat",    label = "Meat",    verb = "GATHERORDER" },
    { id = "fish",    label = "Fish",    verb = "GATHERORDER" },
    { id = "recipe",  label = "Recipes", verb = "CRAFTORDER" },
}
local ORDER_CATEGORY_BY_ID = {}
for _, c in ipairs(ORDER_CATEGORIES) do
    ORDER_CATEGORY_BY_ID[c.id] = c
end

local orderPickerFrame
local orderItemRows = {}
local activeOrderCategoryId = "herb"
-- InputBoxTemplate's decorative border textures render taller than whatever height an EditBox
-- is given (confirmed live: at ROW_HEIGHT 24 with an 18px qtyBox, the border visibly bled into
-- neighboring rows as a stray horizontal bar) -- 30px matches CreateBotRow's own proven-safe
-- row height elsewhere in this file (32px rows comfortably fit a 22px-tall button there).
local ROW_HEIGHT = 30

local function CreateOrderItemRow(parent, index)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(278, ROW_HEIGHT)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 2, -(index - 1) * ROW_HEIGHT)

    local bg = row:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints()
    bg:SetTexture("Interface\\FriendsFrame\\UI-FriendsFrame-HighlightBar")
    bg:SetAlpha((index % 2 == 0) and 0.06 or 0.0)

    local icon = row:CreateTexture(nil, "ARTWORK")
    icon:SetSize(20, 20)
    icon:SetPoint("LEFT", row, "LEFT", 2, 0)
    icon:SetTexCoord(0.08, 0.92, 0.08, 0.92) -- trims the default icon border artifact
    row.icon = icon

    -- Hover tooltip over the whole row -- a real item tooltip via SetItemByID needs no data
    -- beyond the entry id already on the row, so no extra server round-trip is needed for this.
    -- Textures (unlike icon here) have no EnableMouse/SetScript -- the row itself is the real
    -- Frame, so the hit-test area belongs on it, not the icon texture.
    row:EnableMouse(true)
    row:SetScript("OnEnter", function(self)
        if not self.entry then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetItemByID(self.entry)
        GameTooltip:Show()
    end)
    row:SetScript("OnLeave", function() GameTooltip:Hide() end)

    local nameText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    nameText:SetPoint("LEFT", icon, "RIGHT", 6, 0)
    nameText:SetWidth(140)
    nameText:SetJustifyH("LEFT")
    row.nameText = nameText

    local qtyBox = CreateFrame("EditBox", nil, row, "InputBoxTemplate")
    qtyBox:SetSize(34, 20)
    qtyBox:SetPoint("LEFT", nameText, "RIGHT", 4, 0)
    qtyBox:SetAutoFocus(false)
    qtyBox:SetNumeric(true)
    qtyBox:SetMaxLetters(4)
    qtyBox:SetText("1")
    row.qtyBox = qtyBox

    local btnOrder = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnOrder:SetSize(56, 22)
    btnOrder:SetPoint("LEFT", qtyBox, "RIGHT", 8, 0)
    btnOrder:SetText("Order")
    btnOrder:SetScript("OnClick", function()
        local category = ORDER_CATEGORY_BY_ID[row.categoryId]
        if not category or not row.entry then return end
        local count = tonumber(qtyBox:GetText()) or 1
        if count < 1 then count = 1 end
        SendRawBody(category.verb .. ":" .. row.entry .. ":" .. count)
        local verbLabel = (category.verb == "CRAFTORDER") and "Craft order" or "Gather order"
        Log(verbLabel .. " sent: " .. row.name .. " x" .. count .. ".")
    end)
    row.btnOrder = btnOrder

    return row
end

-- Filters activeOrderCategoryId's catalog by the search box's text (case-insensitive substring
-- on the item name) and lays out one row per match, reusing the row pool exactly like
-- CreateBotRow/CreateTaskRow do elsewhere in this file.
function RefreshOrderPicker()
    if not orderPickerFrame or not orderPickerFrame:IsShown() then return end

    local items = orderCatalog[activeOrderCategoryId] or {}
    local filter = (orderPickerFrame.searchBox:GetText() or ""):lower()

    local filtered = {}
    for _, item in ipairs(items) do
        if filter == "" or item.name:lower():find(filter, 1, true) then
            table.insert(filtered, item)
        end
    end

    for i = 1, math.max(#filtered, #orderItemRows) do
        local item = filtered[i]
        if item then
            if not orderItemRows[i] then
                orderItemRows[i] = CreateOrderItemRow(orderPickerFrame.scrollChild, i)
            end
            local row = orderItemRows[i]
            row.entry = item.entry
            row.name = item.name
            row.categoryId = activeOrderCategoryId
            row.nameText:SetText(item.name)
            local icon = GetItemIcon and GetItemIcon(item.entry)
            row.icon:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
            row:Show()
        elseif orderItemRows[i] then
            orderItemRows[i]:Hide()
        end
    end

    orderPickerFrame.scrollChild:SetHeight(math.max(#filtered * ROW_HEIGHT, 1))

    if #items == 0 then
        orderPickerFrame.emptyText:SetText("|cFF888888Loading catalog from server...|r")
        orderPickerFrame.emptyText:Show()
    elseif #filtered == 0 then
        orderPickerFrame.emptyText:SetText("|cFF888888No items match your search.|r")
        orderPickerFrame.emptyText:Show()
    else
        orderPickerFrame.emptyText:Hide()
    end
end

local function SelectOrderCategory(categoryId)
    activeOrderCategoryId = categoryId
    for _, btn in ipairs(orderPickerFrame.categoryButtons) do
        if btn.categoryId == categoryId then
            btn:LockHighlight()
            btn.text:SetText("|cFFFFD100" .. btn.categoryLabel .. "|r")
        else
            btn:UnlockHighlight()
            btn.text:SetText(btn.categoryLabel)
        end
    end
    orderPickerFrame.searchBox:SetText("")
    RefreshOrderPicker()
end

local function CreateOrderPickerFrame()
    local frame = CreateFrame("Frame", "CoABotUIOrderPicker", UIParent)
    frame:SetSize(500, 440)
    frame:SetPoint("CENTER", UIParent, "CENTER", 0, -20)
    frame:SetFrameStrata("DIALOG")
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
    title:SetText("|cFFFFD100Order Materials|r")

    local btnClose = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    btnClose:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -2, -2)
    btnClose:SetScript("OnClick", function() frame:Hide() end)

    -- Category sidebar
    local sidebar = CreateFrame("Frame", nil, frame)
    sidebar:SetSize(90, 360)
    sidebar:SetPoint("TOPLEFT", frame, "TOPLEFT", 10, -36)

    frame.categoryButtons = {}
    for i, category in ipairs(ORDER_CATEGORIES) do
        local btn = CreateFrame("Button", nil, sidebar, "UIPanelButtonTemplate")
        btn:SetSize(86, 24)
        btn:SetPoint("TOPLEFT", sidebar, "TOPLEFT", 0, -(i - 1) * 27)
        btn:SetText(category.label)
        btn.categoryId = category.id
        btn.categoryLabel = category.label
        btn.text = btn:GetFontString()
        btn:SetScript("OnClick", function() SelectOrderCategory(category.id) end)
        frame.categoryButtons[i] = btn
    end

    -- Search box
    local searchBox = CreateFrame("EditBox", nil, frame, "InputBoxTemplate")
    searchBox:SetSize(280, 20)
    searchBox:SetPoint("TOPLEFT", sidebar, "TOPRIGHT", 14, -2)
    searchBox:SetAutoFocus(false)
    searchBox:SetMaxLetters(40)
    searchBox:SetScript("OnTextChanged", function() RefreshOrderPicker() end)
    searchBox:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    frame.searchBox = searchBox

    local searchHint = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    searchHint:SetPoint("LEFT", searchBox, "RIGHT", 8, 0)
    searchHint:SetText("Search")

    -- Item list (scrollable)
    local scrollFrame = CreateFrame("ScrollFrame", "CoABotUIOrderScroll", frame, "UIPanelScrollFrameTemplate")
    scrollFrame:SetSize(300, 330)
    scrollFrame:SetPoint("TOPLEFT", searchBox, "BOTTOMLEFT", -4, -10)
    frame.scrollFrame = scrollFrame

    local scrollChild = CreateFrame("Frame", nil, scrollFrame)
    scrollChild:SetSize(280, 1)
    scrollFrame:SetScrollChild(scrollChild)
    frame.scrollChild = scrollChild

    local emptyText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    emptyText:SetPoint("TOP", scrollFrame, "TOP", 0, -20)
    emptyText:SetJustifyH("CENTER")
    frame.emptyText = emptyText

    return frame
end

function ShowOrderPicker()
    if not orderPickerFrame then
        orderPickerFrame = CreateOrderPickerFrame()
    end
    orderPickerFrame:Show()
    RequestOrderCatalogs()
    SelectOrderCategory(activeOrderCategoryId)
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

        -- The server has no memory of this client-only setting across its own restart --
        -- re-sync once per login/reload (not every zone transition) so a saved "ON" from a
        -- previous session actually takes effect server-side again instead of just looking on.
        if event == "PLAYER_ENTERING_WORLD" and not autoDungeonSynced then
            autoDungeonSynced = true
            if CoABotUIDB.autoDungeon then
                SendRawBody("AUTODUNGEON:1")
            end
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
        Log("  |cFFFFD100/coabot reset|r - Reset panel to screen center")
        Log("  |cFFFFD100/coabot debug|r - Toggle wire message debug logging")
    end
end
