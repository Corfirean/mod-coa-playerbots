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

local function SendBotCommand(verb, botGuidLow, arg)
    if not botGuidLow then return end

    local body = verb .. ":" .. tostring(botGuidLow)
    if arg and arg ~= "" then
        body = body .. ":" .. tostring(arg)
    end

    local playerName = UnitName("player")
    if playerName and playerName ~= "" then
        SendAddonMessage(PROTOCOL_PREFIX, body, "WHISPER", playerName)
        DebugLog("Sent -> " .. body)
    else
        Log("|cFFFF4444Error:|r Unable to resolve player name for addon message.")
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
    menu:SetSize(110, #ROLES * 24 + 8)
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
        btn:SetSize(102, 22)
        btn:SetPoint("TOPLEFT", menu, "TOPLEFT", 4, -4 - (i - 1) * 24)

        local hl = btn:CreateTexture(nil, "HIGHLIGHT")
        hl:SetAllPoints()
        hl:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
        hl:SetBlendMode("ADD")

        local text = btn:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        text:SetPoint("LEFT", btn, "LEFT", 8, 0)
        text:SetText(r.color .. r.name .. "|r")

        btn:SetScript("OnClick", function()
            if menu.activeBotGuid and menu.activeRoleButton then
                local botGuid = menu.activeBotGuid
                local chosenRole = r.id

                -- Save role in DB
                CoABotUIDB.roles[botGuid] = chosenRole

                -- Update button text
                menu.activeRoleButton:SetText(r.color .. r.name .. "|r")

                -- Send wire command
                SendBotCommand("SETROLE", botGuid, chosenRole)
            end
            menu:Hide()
        end)

        menu.buttons[i] = btn
    end

    -- Close menu when clicking outside
    menu:SetScript("OnShow", function(self)
        self.timeElapsed = 0
    end)

    return menu
end

local function OpenRoleMenu(parentButton, botGuidLow)
    if not popupMenu then
        popupMenu = CreateRolePopupMenu()
    end

    if popupMenu:IsShown() and popupMenu.activeBotGuid == botGuidLow then
        popupMenu:Hide()
        return
    end

    popupMenu.activeBotGuid = botGuidLow
    popupMenu.activeRoleButton = parentButton
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
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 12, -72 - (index - 1) * 34)

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

    -- Update Window Height dynamically based on row count
    local contentHeight
    if memberCount == 0 then
        contentHeight = 160
        mainFrame.emptyNotice:Show()
        mainFrame.globalBar:Hide()
    else
        contentHeight = 110 + (memberCount * 34)
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

            -- Class-colored name display
            local classColor = RAID_CLASS_COLORS[member.class] or { r = 1, g = 1, b = 1 }
            local colorCode = string.format("|cFF%02x%02x%02x", classColor.r * 255, classColor.g * 255, classColor.b * 255)
            local badge = member.isMock and " |cFF888888[Mock]|r" or ""
            row.nameText:SetText(colorCode .. member.name .. "|r" .. badge)

            -- Active role
            local savedRole = CoABotUIDB.roles[member.lowGuid] or member.defaultRole or "auto"
            local roleInfo = ROLE_BY_ID[savedRole] or ROLE_BY_ID["auto"]
            row.roleBtn:SetText(roleInfo.color .. roleInfo.name .. "|r")

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
        -- Placeholder for server response messages when server listener lands
        DebugLog("Recv <- " .. tostring(arg2) .. " from " .. tostring(arg4))
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
