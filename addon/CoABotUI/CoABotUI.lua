--[[
    CoA Companions Control Panel (CoABotUI)
    WoW 3.3.5a AddOn for controlling companion playerbots in Conquest of Azeroth
    Wire Protocol: docs/addon-protocol.md
    Channel: SendAddonMessage("COABOT", "<VERB>:<botGuidLow>[:<arg>]", "WHISPER", UnitName("player"))
--]]

local ADDON_NAME = "CoABotUI"
local PROTOCOL_PREFIX = "COABOT"
local VERSION = "2.1.1"

-- Role configuration: names, labels, and display colors
local ROLES = {
    { id = "auto",    name = "Auto",    short = "A",   color = "|cFF888888", r = 0.55, g = 0.55, b = 0.55 },
    { id = "tank",    name = "Tank",    short = "T",   color = "|cFF3399FF", r = 0.20, g = 0.60, b = 1.00 },
    { id = "healer",  name = "Healer",  short = "H",   color = "|cFF33FF33", r = 0.20, g = 1.00, b = 0.20 },
    { id = "dps",     name = "DPS",     short = "DPS", color = "|cFFFF3333", r = 1.00, g = 0.20, b = 0.20 },
    { id = "support", name = "Support", short = "Sup", color = "|cFFFFCC00", r = 1.00, g = 0.80, b = 0.00 },
}

local ROLE_BY_ID = {}
for _, r in ipairs(ROLES) do
    ROLE_BY_ID[r.id] = r
end

-- Group movement formations -- see BotFormations.h server-side and the FORMATION/GETFORMATION
-- verbs in docs/addon-protocol.md. Ids match ParseFormation's canonical spelling.
local FORMATIONS = {
    { id = "rolebased", name = "Role-Based" },
    { id = "shieldwall", name = "Shieldwall" },
    { id = "arrow",      name = "Arrow" },
    { id = "circle",     name = "Circle" },
    { id = "line",       name = "Line" },
    { id = "chaos",      name = "Chaos" },
}
local FORMATION_BY_ID = {}
for _, f in ipairs(FORMATIONS) do
    FORMATION_BY_ID[f.id] = f
end

-- Default Database
local dbDefaults = {
    point = "CENTER",
    relativePoint = "CENTER",
    xOfs = 0,
    yOfs = 100,
    isShown = true,
    isCollapsed = false,
    debug = false,
    activePage = "squad",
    roles = {}, -- [botGuidLow] = "tank"
    autoDungeon = false,
    minimap = { hide = false },
}

-- Forward declarations
local mainFrame
local rows = {}
local activeMembers = {}
local popupMenu
local specMenu
local formationMenu
local ShowSpecSubmenu
local ApplyRoleAvailability
local ShowGuildTaskBoard
local RefreshTaskBoard
local UpdateRoleButtonText
local ShowOrderPicker
local ShowGearPanel
local ShowMainPage
local taskBoardFrame
local orderPickerFrame
local gearPanelFrame
local gearPanelBotGuid
local autoDungeonSynced = false
local currentFormationId = "rolebased"

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

local function ShowItemTooltip(owner, itemEntry, anchor)
    if not itemEntry then return end
    GameTooltip:SetOwner(owner, anchor or "ANCHOR_RIGHT")
    local _, itemLink = GetItemInfo(itemEntry)
    GameTooltip:SetHyperlink(itemLink or ("item:" .. tostring(itemEntry)))
    GameTooltip:Show()
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
-- [botGuidLow] = "Vanguard" etc -- the bot's actual active spec name (ROLES reply's 6th field),
-- empty/nil for a vanilla class (1-11) or an unmapped spec. Shown next to the role so a class
-- with two specs sharing a role (e.g. two DPS specs) doesn't leave the player guessing which one
-- is actually active -- see UpdateRoleButtonText.
local currentSpecNameCache = {}
-- [botGuidLow] = { name=, classId=, level=, task=, professions="Tailoring=225,..." }
local guildRosterCache = {}
-- [botGuidLow] = true once a GETROLES request has been sent, so a bot row only ever asks once
-- per session instead of re-asking on every roster scan.
local rolesRequested = {}

-- [botGuidLow] = { {specId=, role=, name=}, ... } from GETSPECS/SPEC -- empty (no entries at
-- all) for a vanilla-class bot (1-11), since ClassSpecRoles::GetAllSpecs only covers Ascension's
-- custom classes (12-32); the spec submenu simply never opens for those.
local specsCache = {}
-- [botGuidLow] = true once a GETSPECS request has been sent, same one-shot idea as rolesRequested.
local specsRequested = {}

-- [botGuidLow] = { [slotId] = { entry=, name= }, ... } from GETGEAR/GEAR -- re-requested fresh
-- every time the gear panel opens (unlike roles/specs, equipped gear actually changes over a
-- session as bots loot/upgrade, so a one-shot cache would go stale).
local gearCache = {}
-- [botGuidLow] = { legalArmor={"cloth",...}, legalWeapon={"sword",...}, armorPref=, weaponPref= }
-- from GETGEAR/GEARPREFS.
local gearPrefsCache = {}

-- [categoryId] = { {entry=, name=, profession=}, ... } in server arrival order -- GCAT chunks arrive
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

local function RequestSpecs(botGuidLow)
    if not botGuidLow or specsRequested[botGuidLow] then return end
    specsRequested[botGuidLow] = true
    SendBotCommand("GETSPECS", botGuidLow)
end

local function RequestFormation()
    SendGroupCommand("GETFORMATION")
end

-- Always re-requested (not one-shot) -- see gearCache's comment on why equipped gear needs a
-- fresh read every time the panel opens, unlike roles/specs.
local function RequestGear(botGuidLow)
    gearCache[botGuidLow] = {}
    SendBotCommand("GETGEAR", botGuidLow)
end

-- Shows the player's saved preference, plus -- when that preference is "auto" -- the bot's
-- actual current role in parentheses, e.g. "Auto (Healer)". Previously a bot left on Auto just
-- showed the bare word "Auto" with no indication of what it was actually playing as; confirmed
-- live feedback this was confusing. Falls back to just the preference alone until a ROLES
-- reply has arrived for this bot (see RequestRoles/currentRoleCache).
--
-- Also appends the bot's actual active SPEC name (currentSpecNameCache, from ROLES's 6th field)
-- when known -- a role alone doesn't say which spec is active for a class with two+ specs
-- sharing that role (e.g. two DPS specs), which is exactly what the spec-submenu (see
-- ShowSpecSubmenu) lets the player choose between. Empty/nil for a vanilla class (1-11). Shown
-- on its own small line (row.specText) below the name/role line rather than crammed into the
-- role button's own text -- the combined string was long enough to visibly overflow the fixed-
-- width button and run into the Follow button next to it (confirmed live, e.g. "Auto (DPS)
-- [Lightning]Follow" with no gap at all).
function UpdateRoleButtonText(row, botGuidLow, defaultRole)
    local savedRole = CoABotUIDB.roles[botGuidLow] or defaultRole or "auto"
    local roleInfo = ROLE_BY_ID[savedRole] or ROLE_BY_ID["auto"]

    if savedRole == "auto" and currentRoleCache[botGuidLow] then
        local currentInfo = ROLE_BY_ID[currentRoleCache[botGuidLow]] or roleInfo
        row.roleBtn:SetText(roleInfo.color .. "Auto|r " .. currentInfo.color .. "/ " .. currentInfo.short .. "|r")
    else
        row.roleBtn:SetText(roleInfo.color .. roleInfo.name .. "|r")
    end

    if row.specText then
        local specName = currentSpecNameCache[botGuidLow]
        row.specText:SetText((specName and specName ~= "") and ("|cFFFFD100" .. specName .. "|r") or "")
    end
end

local function RequestGuildRoster()
    SendGroupCommand("GUILDROSTER")
end

-- Forward declaration -- HandleIncomingMessage (defined below, before the picker UI exists yet)
-- needs to poke the picker's refresh once new catalog rows stream in; the picker itself is only
-- built lazily the first time it's opened (see ShowOrderPicker).
local RefreshOrderPicker
-- Same idea for the gear panel -- built lazily the first time it's opened (see ShowGearPanel).
local RefreshGearPanel

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

-- "Browse" tab: every online bot you could invite, not limited by the 49 names of /who. The server filters and pages;
-- BOTPAGE starts a new page of results and BOTS lines fill it.
local botBrowser = { page = 0, pages = 1, total = 0, role = "any", minLevel = 0, maxLevel = 255, guildOnly = false, name = "", list = {} }
local botBrowserFrame
local RefreshBotBrowser

local function RequestBotList(page)
    local f = botBrowser
    f.page = page or 0
    local name = (f.name or ""):gsub("[:|,]", "")
    SendRawBody(("GETBOTS:%d:%s:0:%d:%d:%d:%s"):format(f.page, f.role, f.minLevel, f.maxLevel, f.guildOnly and 1 or 0, name))
end

local function ParseBotsChunk(payload)
    for guid, name, classId, level, role, guildmate, className in (payload or ""):gmatch("(%d+),([^,|]*),(%d+),(%d+),([^,|]*),([01]),?([^|]*)") do
        table.insert(botBrowser.list, {
            guid = tonumber(guid), name = name, classId = tonumber(classId), level = tonumber(level),
            role = role, guildmate = guildmate == "1", className = className,
        })
    end
end

-- "Stock" tab: the resources in the guild bank with the limit the officers set. The list is rebuilt from scratch on
-- every request (the server answers with several STOCK: chunks).
local function RequestStock()
    orderCatalog["stock"] = {}
    SendGroupCommand("GETSTOCK")
end

local function ParseStockChunk(payload)
    orderCatalog["stock"] = orderCatalog["stock"] or {}
    for entryStr, countStr, limitStr, name in (payload or ""):gmatch("(%d+),(%d+),(%d+),([^|]+)") do
        table.insert(orderCatalog["stock"], {
            entry = tonumber(entryStr), count = tonumber(countStr), limit = tonumber(limitStr), name = name,
        })
    end
end

-- Appends one GCAT/RCAT chunk's "entry,name|entry,name|..." payload to orderCatalog[category].
-- Item names never contain "," or "|" (no real WoW item does), so this plain gmatch is safe.
local function ParseCatalogChunk(category, itemsCsv, profession)
    orderCatalog[category] = orderCatalog[category] or {}
    local list = orderCatalog[category]
    for entryStr, name in (itemsCsv or ""):gmatch("(%d+),([^|]+)") do
        table.insert(list, { entry = tonumber(entryStr), name = name, profession = profession })
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
        local currentSpecName = parts[6]
        if not botGuidLow then return end

        local set = {}
        for roleId in rolesCsv:gmatch("[^,]+") do
            set[roleId] = true
        end
        rolesCache[botGuidLow] = set
        if currentRole and currentRole ~= "" then
            currentRoleCache[botGuidLow] = currentRole
        end
        currentSpecNameCache[botGuidLow] = currentSpecName

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
            taskItemEntry = tonumber(parts[8]) or 0,
        }
        if RefreshTaskBoard then
            RefreshTaskBoard()
        end

    elseif verb == "BOTPAGE" then
        botBrowser.list = {}
        botBrowser.page = tonumber(parts[2]) or 0
        botBrowser.total = tonumber(parts[3]) or 0
        botBrowser.pages = math.max(1, tonumber(parts[4]) or 1)
        if RefreshBotBrowser then RefreshBotBrowser() end

    elseif verb == "BOTS" then
        ParseBotsChunk(parts[2])
        if RefreshBotBrowser then RefreshBotBrowser() end

    elseif verb == "STOCK" then
        ParseStockChunk(parts[2])
        if RefreshOrderPicker then
            RefreshOrderPicker()
        end

    elseif verb == "GCAT" then
        local category = parts[2]
        if not category or category == "" then return end
        ParseCatalogChunk(category, parts[3])
        if RefreshOrderPicker then
            RefreshOrderPicker()
        end

    elseif verb == "RCAT" then
        if parts[3] and parts[3] ~= "" then
            ParseCatalogChunk("recipe", parts[3], parts[2])
        else
            ParseCatalogChunk("recipe", parts[2], "Other")
        end
        if RefreshOrderPicker then
            RefreshOrderPicker()
        end

    elseif verb == "SPEC" then
        local botGuidLow = tonumber(parts[2])
        local specId = tonumber(parts[3])
        local role = parts[4]
        local name = parts[5]
        if not botGuidLow or not specId or not name then return end
        specsCache[botGuidLow] = specsCache[botGuidLow] or {}
        table.insert(specsCache[botGuidLow], { specId = specId, role = role, name = name })

    elseif verb == "FORMATION" then
        -- Server sends FormationToString's display casing (e.g. "RoleBased") -- FORMATION_BY_ID
        -- keys are lowercase to match the ids this addon sends, so normalize on the way in.
        currentFormationId = (parts[2] or currentFormationId):lower()
        if mainFrame and mainFrame.RefreshFormationButton then
            mainFrame.RefreshFormationButton()
        end

    elseif verb == "GEAR" then
        local botGuidLow = tonumber(parts[2])
        local slot = tonumber(parts[3])
        local entry = tonumber(parts[4])
        local name = parts[5]
        if not botGuidLow or not slot then return end
        gearCache[botGuidLow] = gearCache[botGuidLow] or {}
        gearCache[botGuidLow][slot] = {
            entry = entry,
            name = name or ("item " .. tostring(entry)),
            itemLevel = tonumber(parts[6]) or 0,
        }
        if RefreshGearPanel then RefreshGearPanel() end

    elseif verb == "GEARPREFS" then
        local botGuidLow = tonumber(parts[2])
        if not botGuidLow then return end
        local legalArmor, legalWeapon = {}, {}
        for t in (parts[3] or ""):gmatch("[^,]+") do table.insert(legalArmor, t) end
        for t in (parts[4] or ""):gmatch("[^,]+") do table.insert(legalWeapon, t) end
        gearPrefsCache[botGuidLow] = {
            legalArmor = legalArmor,
            legalWeapon = legalWeapon,
            armorPref = parts[5] or "auto",
            weaponPref = parts[6] or "auto",
        }
        if RefreshGearPanel then RefreshGearPanel() end
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
                else
                    -- Offer a specific-spec pick if this bot's class has more than one spec for
                    -- the role just chosen (e.g. two DPS specs) -- see ShowSpecSubmenu.
                    ShowSpecSubmenu(menu.activeRoleButton, botGuid, chosenRole)
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

-- Second-level menu opened from a role pick when the bot's class has more than one real spec
-- for that role (e.g. two DPS specs) -- lets the player pick a specific one instead of just
-- accepting whichever FindSpecForRole would default to server-side. Rebuilt fresh each time
-- (unlike the fixed 5-row role menu) since the spec count varies per class/role.
local function CreateSpecPopupMenu()
    local menu = CreateFrame("Frame", "CoABotUISpecMenu", UIParent)
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
    return menu
end

-- Shows every spec `botGuidLow`'s class has for `role` (from the cached GETSPECS reply) next to
-- the role popup. Does nothing (no submenu) when there's zero or one matching spec -- the plain
-- SETROLE-driven default from FindSpecForRole is already correct in that case, and forcing an
-- extra click for a class with only one option would just be annoying.
function ShowSpecSubmenu(anchorButton, botGuidLow, role)
    local specs = {}
    for _, s in ipairs(specsCache[botGuidLow] or {}) do
        if s.role == role then
            table.insert(specs, s)
        end
    end
    if #specs < 2 then
        return
    end

    if not specMenu then
        specMenu = CreateSpecPopupMenu()
    end
    for _, btn in ipairs(specMenu.buttons) do
        btn:Hide()
    end
    specMenu:SetSize(150, #specs * 24 + 8)

    for i, s in ipairs(specs) do
        local btn = specMenu.buttons[i]
        if not btn then
            btn = CreateFrame("Button", nil, specMenu)
            btn:SetSize(142, 22)
            local hl = btn:CreateTexture(nil, "HIGHLIGHT")
            hl:SetAllPoints()
            hl:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
            hl:SetBlendMode("ADD")
            btn.text = btn:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            btn.text:SetPoint("LEFT", btn, "LEFT", 8, 0)
            specMenu.buttons[i] = btn
        end
        btn:SetPoint("TOPLEFT", specMenu, "TOPLEFT", 4, -4 - (i - 1) * 24)
        btn.text:SetText(s.name)
        btn:SetScript("OnClick", function()
            SendBotCommand("LEARNSPEC", botGuidLow, s.specId)
            specMenu:Hide()
            if popupMenu then popupMenu:Hide() end
        end)
        btn:Show()
    end

    specMenu:ClearAllPoints()
    specMenu:SetPoint("TOPLEFT", anchorButton, "TOPRIGHT", 2, 0)
    specMenu:Show()
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
        if specMenu then specMenu:Hide() end
        return
    end

    RequestRoles(botGuidLow)
    RequestSpecs(botGuidLow)

    popupMenu.activeBotGuid = botGuidLow
    popupMenu.activeRoleButton = parentButton
    ApplyRoleAvailability(botGuidLow)
    popupMenu:ClearAllPoints()
    popupMenu:SetPoint("TOPLEFT", parentButton, "BOTTOMLEFT", 0, -2)
    popupMenu:Show()
end

-------------------------------------------------------------------------------
-- Formation Popup Menu
-------------------------------------------------------------------------------

local function CreateFormationPopupMenu()
    local menu = CreateFrame("Frame", "CoABotUIFormationMenu", UIParent)
    menu:SetSize(150, #FORMATIONS * 24 + 8)
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
    for i, f in ipairs(FORMATIONS) do
        local btn = CreateFrame("Button", nil, menu)
        btn:SetSize(142, 22)
        btn:SetPoint("TOPLEFT", menu, "TOPLEFT", 4, -4 - (i - 1) * 24)

        local hl = btn:CreateTexture(nil, "HIGHLIGHT")
        hl:SetAllPoints()
        hl:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
        hl:SetBlendMode("ADD")

        local text = btn:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        text:SetPoint("LEFT", btn, "LEFT", 8, 0)
        text:SetText(f.name)
        btn.text = text

        btn:SetScript("OnClick", function()
            currentFormationId = f.id
            SendRawBody("FORMATION:0:" .. f.id)
            if mainFrame and mainFrame.RefreshFormationButton then
                mainFrame.RefreshFormationButton()
            end
            menu:Hide()
        end)
        menu.buttons[i] = btn
    end

    return menu
end

local function OpenFormationMenu(parentButton)
    if not formationMenu then
        formationMenu = CreateFormationPopupMenu()
    end
    if formationMenu:IsShown() then
        formationMenu:Hide()
        return
    end
    formationMenu:ClearAllPoints()
    formationMenu:SetPoint("TOPLEFT", parentButton, "BOTTOMLEFT", 0, -2)
    formationMenu:Show()
end

-- Adds a drag-to-resize grip to the frame's bottom-right corner -- none of this addon's windows
-- were resizable before (fixed pixel sizes throughout), which the user explicitly asked for.
-- `axis` restricts which dimension actually changes ("both" (default), "width", or "height") --
-- the main panel's height is already auto-managed by RefreshUI (fits the current roster), so
-- letting a manual resize fight that every refresh would just snap back; "width" lets the player
-- still widen/narrow it without that fight. `onResize(width, height)`, if given, fires once
-- dragging ends (e.g. to relay a new width into a scroll child that isn't itself anchored to
-- track the parent's size).

-------------------------------------------------------------------------------
-- UI Construction
-------------------------------------------------------------------------------

local function CreateBotRow(parent, index)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(472, 36)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, -(index - 1) * 38)

    -- Row background highlight on mouseover
    local bg = row:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints()
    bg:SetTexture("Interface\\FriendsFrame\\UI-FriendsFrame-HighlightBar")
    bg:SetAlpha(0.12)
    row.bg = bg

    -- Slim class-colored left edge -- lets a glance down the row list tell classes apart without
    -- needing to read each name's color, unlike the plain hover-only highlight bar above.
    local classAccent = row:CreateTexture(nil, "ARTWORK")
    classAccent:SetPoint("TOPLEFT", row, "TOPLEFT", 0, 0)
    classAccent:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 0, 0)
    classAccent:SetWidth(3)
    classAccent:SetTexture(1, 1, 1, 0.9)
    row.classAccent = classAccent

    -- Name & Low GUID Label -- anchored a little above row-center (see specText below it) since
    -- the row is tall enough for two lines now.
    local nameText = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    nameText:SetPoint("LEFT", row, "LEFT", 8, 6)
    nameText:SetWidth(116)
    nameText:SetJustifyH("LEFT")
    row.nameText = nameText

    -- Active spec name, shown on its own line under the name so it never has to share width
    -- with the role button/action buttons -- see UpdateRoleButtonText's comment on why cramming
    -- it into the role button's own text overflowed into the Follow button next to it.
    local specText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    specText:SetPoint("TOPLEFT", nameText, "BOTTOMLEFT", 0, -2)
    specText:SetWidth(116)
    specText:SetJustifyH("LEFT")
    row.specText = specText

    -- Role Selector Button
    local roleBtn = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    roleBtn:SetSize(86, 22)
    roleBtn:SetPoint("LEFT", nameText, "RIGHT", 4, -6)
    roleBtn:SetText("Auto")
    roleBtn:SetScript("OnClick", function(self)
        if row.botGuidLow then
            OpenRoleMenu(self, row.botGuidLow)
        end
    end)
    row.roleBtn = roleBtn

    -- Action Buttons: Follow, Stay, Pull, Stop
    local btnFollow = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnFollow:SetSize(46, 22)
    btnFollow:SetPoint("LEFT", roleBtn, "RIGHT", 4, 0)
    btnFollow:SetText("Follow")
    btnFollow:SetScript("OnClick", function()
        SendBotCommand("FOLLOW", row.botGuidLow)
    end)
    row.btnFollow = btnFollow

    local btnStay = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnStay:SetSize(42, 22)
    btnStay:SetPoint("LEFT", btnFollow, "RIGHT", 3, 0)
    btnStay:SetText("Stay")
    btnStay:SetScript("OnClick", function()
        SendBotCommand("STAY", row.botGuidLow)
    end)
    row.btnStay = btnStay

    local btnPull = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnPull:SetSize(42, 22)
    btnPull:SetPoint("LEFT", btnStay, "RIGHT", 3, 0)
    btnPull:SetText("Pull")
    btnPull:SetScript("OnClick", function()
        SendBotCommand("PULL", row.botGuidLow)
    end)
    row.btnPull = btnPull

    local btnStop = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnStop:SetSize(42, 22)
    btnStop:SetPoint("LEFT", btnPull, "RIGHT", 3, 0)
    btnStop:SetText("Stop")
    btnStop:SetScript("OnClick", function()
        SendBotCommand("STOPATTACK", row.botGuidLow)
    end)
    row.btnStop = btnStop

    -- Gear inspector/preference button -- opens a panel showing what's currently equipped and
    -- lets the player pick a preferred armor/weapon type (see ShowGearPanel).
    local btnGear = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    btnGear:SetSize(42, 22)
    btnGear:SetPoint("LEFT", btnStop, "RIGHT", 3, 0)
    btnGear:SetText("Gear")
    btnGear:SetScript("OnClick", function()
        if row.botGuidLow then
            ShowGearPanel(row.botGuidLow, row.nameText:GetText())
        end
    end)
    row.btnGear = btnGear

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

    if memberCount == 0 then
        mainFrame.emptyNotice:Show()
        mainFrame.globalBar:Hide()
    else
        mainFrame.emptyNotice:Hide()
        mainFrame.globalBar:Show()
    end

    -- Update bot rows
    for i = 1, math.max(memberCount, #rows) do
        local member = members[i]
        if member then
            if not rows[i] then
                rows[i] = CreateBotRow(mainFrame.squadScrollChild, i)
            end

            local row = rows[i]
            row.botGuidLow = member.lowGuid
            RequestRoles(member.lowGuid)

            -- Class-colored name display
            local classColor = RAID_CLASS_COLORS[member.class] or { r = 1, g = 1, b = 1 }
            local colorCode = string.format("|cFF%02x%02x%02x", classColor.r * 255, classColor.g * 255, classColor.b * 255)
            row.nameText:SetText(colorCode .. member.name .. "|r")
            row.classAccent:SetTexture(classColor.r, classColor.g, classColor.b, 0.9)

            -- Active role
            UpdateRoleButtonText(row, member.lowGuid, member.defaultRole)

            row:Show()
        else
            if rows[i] then
                rows[i]:Hide()
            end
        end
    end

    mainFrame.squadScrollChild:SetHeight(math.max(memberCount * 38, 1))

    -- Update target bar
    UpdateTargetStatus(mainFrame.statusBar)
end


local function CreateMainFrame()
    local frame = CreateFrame("Frame", "CoABotUIMainFrame", UIParent)
    frame:SetSize(520, 450)
    frame.expandedWidth = 520
    frame.expandedHeight = 450
    frame:SetFrameStrata("MEDIUM")
    frame:SetClampedToScreen(true)
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 14,
        insets = { left = 3, right = 3, top = 3, bottom = 3 }
    })
    frame:SetBackdropColor(0.035, 0.04, 0.055, 0.97)
    frame:SetBackdropBorderColor(0.32, 0.35, 0.42, 1.0)

    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local pt, _, relPt, x, y = self:GetPoint()
        CoABotUIDB.point, CoABotUIDB.relativePoint = pt, relPt
        CoABotUIDB.xOfs, CoABotUIDB.yOfs = x, y
    end)

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 14, -10)
    title:SetText("|cFFFFD100CoA Companions|r |cFF697080v" .. VERSION .. "|r")
    frame.title = title

    local titleAccent = frame:CreateTexture(nil, "ARTWORK")
    titleAccent:SetPoint("TOPLEFT", frame, "TOPLEFT", 3, -29)
    titleAccent:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -3, -29)
    titleAccent:SetHeight(1)
    titleAccent:SetTexture(0.85, 0.65, 0.13, 0.65)
    frame.titleAccent = titleAccent

    local btnClose = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    btnClose:SetSize(20, 20)
    btnClose:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -4)
    btnClose:SetScript("OnClick", function()
        frame:Hide()
        CoABotUIDB.isShown = false
    end)

    local btnCollapse = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnCollapse:SetSize(20, 20)
    btnCollapse:SetPoint("RIGHT", btnClose, "LEFT", -2, 0)
    btnCollapse:SetText("-")
    frame.btnCollapse = btnCollapse

    local COMPACT_ICONS = {
        { verb = "FOLLOW", icon = "Interface\\Icons\\Ability_Rogue_Sprint", tip = "All Follow" },
        { verb = "STAY", icon = "Interface\\Icons\\Ability_Warrior_ShieldWall", tip = "All Stay" },
        { verb = "PULL", icon = "Interface\\Icons\\Ability_Warrior_Charge", tip = "All Pull" },
        { verb = "STOPATTACK", icon = "Interface\\Buttons\\UI-GroupLoot-Pass-Up", tip = "All Stop" },
    }
    frame.compactButtons = {}
    for i, def in ipairs(COMPACT_ICONS) do
        local verb, tip = def.verb, def.tip
        local btn = CreateFrame("Button", nil, frame)
        btn:SetSize(36, 36)
        btn:SetPoint("LEFT", frame, "LEFT", 6 + (i - 1) * 40, 0)
        btn:SetBackdrop({
            bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 8, edgeSize = 9,
            insets = { left = 2, right = 2, top = 2, bottom = 2 }
        })
        btn:SetBackdropColor(0.025, 0.025, 0.03, 1)
        btn:SetBackdropBorderColor(0.48, 0.44, 0.32, 1)
        local icon = btn:CreateTexture(nil, "ARTWORK")
        icon:SetPoint("TOPLEFT", btn, "TOPLEFT", 4, -4)
        icon:SetPoint("BOTTOMRIGHT", btn, "BOTTOMRIGHT", -4, 4)
        icon:SetTexture(def.icon)
        icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        btn.icon = icon
        local highlight = btn:CreateTexture(nil, "HIGHLIGHT")
        highlight:SetPoint("TOPLEFT", icon, "TOPLEFT", 0, 0)
        highlight:SetPoint("BOTTOMRIGHT", icon, "BOTTOMRIGHT", 0, 0)
        highlight:SetTexture("Interface\\Buttons\\ButtonHilight-Square")
        highlight:SetBlendMode("ADD")
        btn:SetScript("OnClick", function()
            for _, member in ipairs(activeMembers) do
                SendBotCommand(verb, member.lowGuid)
            end
        end)
        btn:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
            GameTooltip:AddLine(tip)
            GameTooltip:Show()
        end)
        btn:SetScript("OnLeave", function() GameTooltip:Hide() end)
        btn:Hide()
        frame.compactButtons[i] = btn
    end

    frame.tabs = {}
    local tabDefs = {
        { id = "squad", label = "Squad" },
        { id = "tasks", label = "Tasks" },
        { id = "orders", label = "Orders" },
        { id = "browse", label = "Browse" },
        { id = "gear", label = "Gear" },
    }
    for i, tabDef in ipairs(tabDefs) do
        local pageId, label = tabDef.id, tabDef.label
        local tab = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
        tab:SetSize(96, 22)
        tab:SetPoint("TOPLEFT", frame, "TOPLEFT", 14 + (i - 1) * 100, -36)
        tab:SetText(label)
        tab.pageId = pageId
        tab.label = label
        tab:SetScript("OnClick", function() ShowMainPage(pageId) end)
        frame.tabs[i] = tab
    end

    local squadPage = CreateFrame("Frame", nil, frame)
    squadPage:SetPoint("TOPLEFT", frame, "TOPLEFT", 12, -66)
    squadPage:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -12, 10)
    frame.squadPage = squadPage

    local globalBar = CreateFrame("Frame", nil, squadPage)
    globalBar:SetSize(492, 24)
    globalBar:SetPoint("TOPLEFT", squadPage, "TOPLEFT", 0, 0)
    local lblAll = globalBar:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    lblAll:SetPoint("LEFT", globalBar, "LEFT", 4, 0)
    lblAll:SetText("|cFF9CA3AFGroup|r")
    local groupButtons = {
        { label = "Follow", verb = "FOLLOW", width = 62 },
        { label = "Stay", verb = "STAY", width = 54 },
        { label = "Pull", verb = "PULL", width = 54 },
        { label = "Stop", verb = "STOPATTACK", width = 54 },
    }
    local previous = lblAll
    for i, def in ipairs(groupButtons) do
        local verb = def.verb
        local btn = CreateFrame("Button", nil, globalBar, "UIPanelButtonTemplate")
        btn:SetSize(def.width, 20)
        btn:SetPoint("LEFT", previous, "RIGHT", i == 1 and 10 or 4, 0)
        btn:SetText(def.label)
        btn:SetScript("OnClick", function()
            for _, member in ipairs(activeMembers) do
                SendBotCommand(verb, member.lowGuid)
            end
        end)
        previous = btn
    end
    frame.globalBar = globalBar

    local utilityBar = CreateFrame("Frame", nil, squadPage)
    utilityBar:SetSize(492, 24)
    utilityBar:SetPoint("TOPLEFT", globalBar, "BOTTOMLEFT", 0, -4)
    local btnQuickFill = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnQuickFill:SetSize(76, 20)
    btnQuickFill:SetPoint("LEFT", utilityBar, "LEFT", 4, 0)
    btnQuickFill:SetText("Quick Fill")
    btnQuickFill:SetScript("OnClick", function()
        SendGroupCommand("QUICKFILL")
        Log("Requested quick-fill for your group.")
    end)

    -- "Invite": one bot of the chosen role, for when Quick Fill (a whole party) is too much - a pocket healer or tank.
    local btnInvite = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnInvite:SetSize(62, 20)
    btnInvite:SetPoint("LEFT", btnQuickFill, "RIGHT", 4, 0)
    btnInvite:SetText("Invite")
    local inviteMenuFrame = CreateFrame("Frame", "CoABotUIInviteMenu", UIParent, "UIDropDownMenuTemplate")
    local function InviteRole(role, label)
        SendRawBody("INVITEROLE:" .. role)
        Log("Looking for a " .. label .. " bot to invite.")
    end
    btnInvite:SetScript("OnClick", function(self)
        EasyMenu({
            { text = "Invite one bot", isTitle = true, notCheckable = true },
            { text = "Tank", notCheckable = true, func = function() InviteRole("tank", "tank") end },
            { text = "Healer", notCheckable = true, func = function() InviteRole("healer", "healer") end },
            { text = "Damage", notCheckable = true, func = function() InviteRole("dps", "damage") end },
        }, inviteMenuFrame, self, 0, 0, "MENU")
    end)
    btnInvite:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
        GameTooltip:SetText("Invite one bot", 1, 0.82, 0)
        GameTooltip:AddLine("Pick a role: a free bot of your faction (guildmates first, closest level) is invited to your group.", 1, 1, 1, true)
        GameTooltip:Show()
    end)
    btnInvite:SetScript("OnLeave", function() GameTooltip:Hide() end)

    local btnAutoDungeon = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnAutoDungeon:SetSize(96, 20)
    btnAutoDungeon:SetPoint("LEFT", btnInvite, "RIGHT", 4, 0)
    local function RefreshAutoDungeonButton()
        btnAutoDungeon:SetText(CoABotUIDB.autoDungeon and "|cFF44FF44Dungeon: ON|r" or "Dungeon: OFF")
    end
    btnAutoDungeon:SetScript("OnClick", function()
        CoABotUIDB.autoDungeon = not CoABotUIDB.autoDungeon
        SendRawBody("AUTODUNGEON:" .. (CoABotUIDB.autoDungeon and "1" or "0"))
        RefreshAutoDungeonButton()
    end)
    RefreshAutoDungeonButton()
    frame.RefreshAutoDungeonButton = RefreshAutoDungeonButton

    local btnFormation = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnFormation:SetSize(124, 20)
    btnFormation:SetPoint("LEFT", btnAutoDungeon, "RIGHT", 4, 0)
    local function RefreshFormationButton()
        local formation = FORMATION_BY_ID[currentFormationId] or FORMATIONS[1]
        btnFormation:SetText("Form: " .. formation.name)
    end
    btnFormation:SetScript("OnClick", function(self) OpenFormationMenu(self) end)
    RefreshFormationButton()
    frame.RefreshFormationButton = RefreshFormationButton

    local btnTeleport = CreateFrame("Button", nil, utilityBar, "UIPanelButtonTemplate")
    btnTeleport:SetSize(108, 20)
    btnTeleport:SetPoint("LEFT", btnFormation, "RIGHT", 4, 0)
    btnTeleport:SetScript("OnClick", function() SendGroupCommand("TELEPORT") end)
    local function RefreshTeleportButton()
        if UnitAffectingCombat("player") then
            btnTeleport:Disable()
            btnTeleport:SetText("|cFF666666Bring to me|r")
        else
            btnTeleport:Enable()
            btnTeleport:SetText("Bring to me")
        end
    end
    RefreshTeleportButton()
    frame.RefreshTeleportButton = RefreshTeleportButton
    frame.utilityBar = utilityBar

    local scrollFrame = CreateFrame("ScrollFrame", "CoABotUISquadScroll", squadPage, "UIPanelScrollFrameTemplate")
    scrollFrame:SetPoint("TOPLEFT", utilityBar, "BOTTOMLEFT", 4, -8)
    scrollFrame:SetPoint("BOTTOMRIGHT", squadPage, "BOTTOMRIGHT", -28, 30)
    local scrollChild = CreateFrame("Frame", nil, scrollFrame)
    scrollChild:SetSize(472, 1)
    scrollFrame:SetScrollChild(scrollChild)
    frame.squadScroll = scrollFrame
    frame.squadScrollChild = scrollChild

    local emptyNotice = CreateFrame("Frame", nil, squadPage)
    emptyNotice:SetPoint("TOPLEFT", scrollFrame, "TOPLEFT", 0, 0)
    emptyNotice:SetPoint("BOTTOMRIGHT", scrollFrame, "BOTTOMRIGHT", 0, 0)
    local emptyText = emptyNotice:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    emptyText:SetPoint("CENTER", emptyNotice, "CENTER", 0, 10)
    emptyText:SetText("|cFF9CA3AFNo companions in your party.\nUse Quick Fill or invite a bot, then this list updates automatically.|r")
    emptyText:SetJustifyH("CENTER")
    frame.emptyNotice = emptyNotice

    local statusBar = squadPage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    statusBar:SetPoint("BOTTOMLEFT", squadPage, "BOTTOMLEFT", 4, 5)
    statusBar:SetJustifyH("LEFT")
    frame.statusBar = statusBar

    local function ApplyCollapsedState(collapsed)
        CoABotUIDB.isCollapsed = collapsed
        if collapsed then
            btnCollapse:SetText("+")
            btnClose:SetSize(28, 28)
            btnClose:ClearAllPoints()
            btnClose:SetPoint("RIGHT", frame, "RIGHT", -6, 0)
            btnCollapse:SetSize(28, 28)
            btnCollapse:ClearAllPoints()
            btnCollapse:SetPoint("RIGHT", btnClose, "LEFT", -4, 0)
            frame:SetSize(236, 48)
            title:Hide()
            titleAccent:Hide()
            for _, tab in ipairs(frame.tabs) do tab:Hide() end
            squadPage:Hide()
            if taskBoardFrame then taskBoardFrame:Hide() end
            if orderPickerFrame then orderPickerFrame:Hide() end
            if botBrowserFrame then botBrowserFrame:Hide() end
            if gearPanelFrame then gearPanelFrame:Hide() end
            for _, btn in ipairs(frame.compactButtons) do btn:Show() end
        else
            btnCollapse:SetText("-")
            btnClose:SetSize(20, 20)
            btnClose:ClearAllPoints()
            btnClose:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -4)
            btnCollapse:SetSize(20, 20)
            btnCollapse:ClearAllPoints()
            btnCollapse:SetPoint("RIGHT", btnClose, "LEFT", -2, 0)
            frame:SetSize(frame.expandedWidth, frame.expandedHeight)
            title:Show()
            titleAccent:Show()
            for _, tab in ipairs(frame.tabs) do tab:Show() end
            for _, btn in ipairs(frame.compactButtons) do btn:Hide() end
            ShowMainPage(CoABotUIDB.activePage or "squad")
        end
    end
    btnCollapse:SetScript("OnClick", function() ApplyCollapsedState(not CoABotUIDB.isCollapsed) end)
    frame.ApplyCollapsedState = ApplyCollapsedState

    return frame
end

-------------------------------------------------------------------------------
-- Guild Task Board (professions/skill/current task per guild bot, + craft orders)
-------------------------------------------------------------------------------

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
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 0, -8 - (index - 1) * 44)

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

    -- Invisible hit-area over taskText -- a FontString itself can't take mouse scripts, so this
    -- transparent Button stands in to show a real item GameTooltip (icon/stats) when the bot is
    -- actually crafting/gathering something (see RefreshTaskBoard's row.taskItemEntry).
    local taskHit = CreateFrame("Button", nil, row)
    taskHit:SetAllPoints(taskText)
    taskHit:SetScript("OnEnter", function(self)
        if not row.taskItemEntry or row.taskItemEntry == 0 then return end
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetItemByID(row.taskItemEntry)
        GameTooltip:Show()
    end)
    taskHit:SetScript("OnLeave", function() GameTooltip:Hide() end)
    row.taskHit = taskHit

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
            row.taskItemEntry = info.taskItemEntry or 0

            ApplyProfessionIcons(row, info.professions)
            row:Show()
        elseif taskRows[i] then
            taskRows[i]:Hide()
        end
    end

    taskBoardFrame.listArea:SetHeight(math.max(#guids * 44 + 8, 1))

    if #guids == 0 then
        taskBoardFrame.emptyText:Show()
    else
        taskBoardFrame.emptyText:Hide()
    end
end

local function CreateGuildTaskBoardFrame()
    local frame = CreateFrame("Frame", "CoABotUITaskBoard", mainFrame)
    frame:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 12, -66)
    frame:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -12, 10)
    frame:Hide()

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -7)
    title:SetText("|cFFFFD100Guild activity|r |cFF8B93A3live bot tasks and professions|r")

    local btnRefresh = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnRefresh:SetSize(70, 20)
    btnRefresh:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -2)
    btnRefresh:SetText("Refresh")
    btnRefresh:SetScript("OnClick", function() RequestGuildRoster() end)

    local btnOrderMaterials = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnOrderMaterials:SetSize(112, 20)
    btnOrderMaterials:SetPoint("RIGHT", btnRefresh, "LEFT", -4, 0)
    btnOrderMaterials:SetText("New order")
    btnOrderMaterials:SetScript("OnClick", function() ShowOrderPicker() end)

    -- "Collect gold": ask a free guild-mate bot to earn that much gold for the guild bank. Guild master and
    -- officers only (checked on the server); the bot's progress shows in its row below. 0 cancels the orders.
    local goldLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    goldLabel:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -33)
    goldLabel:SetText("|cFFFFD100Collect gold for the guild:|r")

    local goldBox = CreateFrame("EditBox", nil, frame)
    goldBox:SetSize(54, 22)
    goldBox:SetPoint("LEFT", goldLabel, "RIGHT", 8, 0)
    goldBox:SetFontObject(GameFontHighlightSmall)
    goldBox:SetJustifyH("CENTER")
    goldBox:SetTextInsets(3, 3, 0, 0)
    goldBox:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 8, edgeSize = 8,
        insets = { left = 2, right = 2, top = 2, bottom = 2 }
    })
    goldBox:SetBackdropColor(0.02, 0.02, 0.025, 0.95)
    goldBox:SetBackdropBorderColor(0.35, 0.37, 0.42, 1.0)
    goldBox:SetAutoFocus(false)
    goldBox:SetNumeric(true)
    goldBox:SetMaxLetters(6)
    goldBox:SetText("100")

    local goldUnit = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    goldUnit:SetPoint("LEFT", goldBox, "RIGHT", 4, 0)
    goldUnit:SetText("|cFFFFD100g|r")

    local btnGold = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnGold:SetSize(56, 22)
    btnGold:SetPoint("LEFT", goldUnit, "RIGHT", 6, 0)
    btnGold:SetText("Order")
    btnGold:SetScript("OnClick", function()
        local amount = tonumber(goldBox:GetText()) or 0
        if amount < 1 then
            Log("Enter how many gold the guild should collect.")
            return
        end
        SendRawBody("GOLDORDER:" .. amount)
        Log("Gold order sent: " .. amount .. " gold for the guild bank.")
    end)

    local btnGoldCancel = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnGoldCancel:SetSize(96, 22)
    btnGoldCancel:SetPoint("LEFT", btnGold, "RIGHT", 4, 0)
    btnGoldCancel:SetText("Cancel orders")
    btnGoldCancel:SetScript("OnClick", function()
        SendRawBody("GOLDORDER:0")
        Log("Gold orders cancelled.")
    end)

    local scrollFrame = CreateFrame("ScrollFrame", "CoABotUITaskScroll", frame, "UIPanelScrollFrameTemplate")
    scrollFrame:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -62)
    scrollFrame:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -28, 4)

    local listArea = CreateFrame("Frame", nil, scrollFrame)
    listArea:SetSize(456, 1)
    scrollFrame:SetScrollChild(listArea)
    frame.listArea = listArea
    frame.scrollFrame = scrollFrame

    local emptyText = listArea:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    emptyText:SetPoint("TOP", listArea, "TOP", 0, -40)
    emptyText:SetText("|cFF888888No guild bots found. Make sure you're in a guild\nwith online companion bots, then click Refresh.|r")
    emptyText:SetJustifyH("CENTER")
    frame.emptyText = emptyText

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

-------------------------------------------------------------------------------
-- Browse tab: find and invite one bot
-------------------------------------------------------------------------------

local ROLE_CYCLE = { "any", "tank", "healer", "dps" }
local ROLE_LABEL = { any = "Any role", tank = "Tank", healer = "Healer", dps = "Damage" }
local browserRows = {}
local BROWSER_ROW_H = 26

local function MakeBox(parent, width, text, numeric, maxLetters)
    local box = CreateFrame("EditBox", nil, parent)
    box:SetSize(width, 22)
    box:SetFontObject(GameFontHighlightSmall)
    box:SetJustifyH("CENTER")
    box:SetTextInsets(3, 3, 0, 0)
    box:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 8, edgeSize = 8,
        insets = { left = 2, right = 2, top = 2, bottom = 2 }
    })
    box:SetBackdropColor(0.02, 0.02, 0.025, 0.95)
    box:SetBackdropBorderColor(0.35, 0.37, 0.42, 1.0)
    box:SetAutoFocus(false)
    box:SetNumeric(numeric and true or false)
    box:SetMaxLetters(maxLetters or 20)
    box:SetText(text or "")
    box:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    return box
end

local function CreateBotBrowserRow(parent, index)
    local row = CreateFrame("Frame", nil, parent)
    row:SetSize(470, BROWSER_ROW_H)
    row:SetPoint("TOPLEFT", parent, "TOPLEFT", 2, -(index - 1) * BROWSER_ROW_H)
    local bg = row:CreateTexture(nil, "BACKGROUND")
    bg:SetAllPoints()
    bg:SetTexture("Interface\\FriendsFrame\\UI-FriendsFrame-HighlightBar")
    bg:SetAlpha((index % 2 == 0) and 0.06 or 0.0)

    row.nameText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    row.nameText:SetPoint("LEFT", row, "LEFT", 4, 0)
    row.nameText:SetWidth(150)
    row.nameText:SetJustifyH("LEFT")

    row.infoText = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    row.infoText:SetPoint("LEFT", row.nameText, "RIGHT", 4, 0)
    row.infoText:SetWidth(170)
    row.infoText:SetJustifyH("LEFT")

    row.btnGroup = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    row.btnGroup:SetSize(54, 20)
    row.btnGroup:SetPoint("LEFT", row.infoText, "RIGHT", 4, 0)
    row.btnGroup:SetText("Group")
    row.btnGroup:SetScript("OnClick", function()
        if not row.guid then return end
        SendRawBody("INVITEBOT:" .. row.guid)
        Log("Invited " .. (row.botName or "bot") .. " to your group.")
    end)

    row.btnGuild = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
    row.btnGuild:SetSize(54, 20)
    row.btnGuild:SetPoint("LEFT", row.btnGroup, "RIGHT", 2, 0)
    row.btnGuild:SetText("Guild")
    row.btnGuild:SetScript("OnClick", function()
        if not row.guid then return end
        SendRawBody("GUILDINVITEBOT:" .. row.guid)
        Log("Invited " .. (row.botName or "bot") .. " to your guild.")
        RequestBotList(botBrowser.page)
    end)
    return row
end

function RefreshBotBrowser()
    if not botBrowserFrame or not botBrowserFrame:IsShown() then return end
    local list = botBrowser.list
    for i = 1, math.max(#list, #browserRows) do
        local info = list[i]
        if info then
            if not browserRows[i] then browserRows[i] = CreateBotBrowserRow(botBrowserFrame.listArea, i) end
            local row = browserRows[i]
            row.guid = info.guid
            row.botName = info.name
            row.nameText:SetText(ClassColorForId(info.classId) .. info.name .. "|r" .. (info.guildmate and " |cFF55FF55*|r" or ""))
            local roleLabel = ROLE_LABEL[info.role] or info.role
            row.infoText:SetText("lvl " .. info.level .. "  " .. roleLabel .. "  " .. (info.className or ""))
            row:Show()
        elseif browserRows[i] then
            browserRows[i]:Hide()
        end
    end
    botBrowserFrame.listArea:SetHeight(math.max(#list * BROWSER_ROW_H + 4, 1))
    botBrowserFrame.pageText:SetText(("Page %d / %d  (%d bots)"):format(botBrowser.page + 1, botBrowser.pages, botBrowser.total))
    if botBrowser.page > 0 then botBrowserFrame.btnPrev:Enable() else botBrowserFrame.btnPrev:Disable() end
    if botBrowser.page + 1 < botBrowser.pages then botBrowserFrame.btnNext:Enable() else botBrowserFrame.btnNext:Disable() end
    if #list == 0 then botBrowserFrame.emptyText:Show() else botBrowserFrame.emptyText:Hide() end
end

local function CreateBotBrowserFrame()
    local frame = CreateFrame("Frame", "CoABotUIBotBrowser", mainFrame)
    frame:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 12, -66)
    frame:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -12, 10)
    frame:Hide()

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -7)
    title:SetText("|cFFFFD100Find a bot|r |cFF8B93A3every free bot of your faction, with filters|r")

    -- row 1: name search and role
    local nameBox = MakeBox(frame, 150, "", false, 24)
    nameBox:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -30)
    nameBox:SetJustifyH("LEFT")
    local nameHint = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    nameHint:SetPoint("LEFT", nameBox, "RIGHT", 6, 0)
    nameHint:SetText("Name")

    local btnRole = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    btnRole:SetSize(96, 22)
    btnRole:SetPoint("LEFT", nameHint, "RIGHT", 12, 0)
    local function RoleText() btnRole:SetText(ROLE_LABEL[botBrowser.role]) end
    RoleText()

    local guildCheck = CreateFrame("CheckButton", "CoABotUIBrowseGuildOnly", frame, "UICheckButtonTemplate")
    guildCheck:SetSize(22, 22)
    guildCheck:SetPoint("LEFT", btnRole, "RIGHT", 10, 0)
    local guildLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    guildLabel:SetPoint("LEFT", guildCheck, "RIGHT", 0, 0)
    guildLabel:SetText("Guild only")

    -- row 2: level range, paging
    local lvlLabel = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    lvlLabel:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -60)
    lvlLabel:SetText("Level")
    local minBox = MakeBox(frame, 34, "", true, 3)
    minBox:SetPoint("LEFT", lvlLabel, "RIGHT", 6, 0)
    local dash = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    dash:SetPoint("LEFT", minBox, "RIGHT", 3, 0)
    dash:SetText("-")
    local maxBox = MakeBox(frame, 34, "", true, 3)
    maxBox:SetPoint("LEFT", dash, "RIGHT", 3, 0)

    frame.btnNext = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    frame.btnNext:SetSize(60, 22)
    frame.btnNext:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -58)
    frame.btnNext:SetText("Next")
    frame.btnNext:SetScript("OnClick", function() RequestBotList(botBrowser.page + 1) end)
    frame.btnPrev = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    frame.btnPrev:SetSize(60, 22)
    frame.btnPrev:SetPoint("RIGHT", frame.btnNext, "LEFT", -4, 0)
    frame.btnPrev:SetText("Prev")
    frame.btnPrev:SetScript("OnClick", function() RequestBotList(math.max(0, botBrowser.page - 1)) end)
    frame.pageText = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    frame.pageText:SetPoint("RIGHT", frame.btnPrev, "LEFT", -10, 0)

    local scrollFrame = CreateFrame("ScrollFrame", "CoABotUIBrowseScroll", frame, "UIPanelScrollFrameTemplate")
    scrollFrame:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -90)
    scrollFrame:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -28, 4)
    local listArea = CreateFrame("Frame", nil, scrollFrame)
    listArea:SetSize(470, 1)
    scrollFrame:SetScrollChild(listArea)
    frame.listArea = listArea

    local emptyText = listArea:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    emptyText:SetPoint("TOP", listArea, "TOP", 0, -40)
    emptyText:SetJustifyH("CENTER")
    emptyText:SetText("|cFF888888No free bots match. Change the filters, or spawn some from the Manager.|r")
    frame.emptyText = emptyText

    -- A changed filter asks the server again after a short pause (typing does not send a request per letter).
    local dirtyAt = nil
    local function Dirty() dirtyAt = GetTime() end
    nameBox:SetScript("OnTextChanged", function(self) botBrowser.name = self:GetText() or ""; Dirty() end)
    minBox:SetScript("OnTextChanged", function(self) botBrowser.minLevel = tonumber(self:GetText()) or 0; Dirty() end)
    maxBox:SetScript("OnTextChanged", function(self)
        local v = tonumber(self:GetText())
        botBrowser.maxLevel = (v and v > 0) and v or 255
        Dirty()
    end)
    guildCheck:SetScript("OnClick", function(self) botBrowser.guildOnly = self:GetChecked() and true or false; Dirty() end)
    btnRole:SetScript("OnClick", function()
        for i, r in ipairs(ROLE_CYCLE) do
            if r == botBrowser.role then
                botBrowser.role = ROLE_CYCLE[(i % #ROLE_CYCLE) + 1]
                break
            end
        end
        RoleText()
        Dirty()
    end)
    frame:SetScript("OnUpdate", function()
        if dirtyAt and GetTime() - dirtyAt > 0.5 then
            dirtyAt = nil
            RequestBotList(0)
        end
    end)
    frame:SetScript("OnShow", function() RequestBotList(botBrowser.page) end)
    return frame
end

function ShowGuildTaskBoard()
    ShowMainPage("tasks")
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
    { id = "stock",   label = "Stock",   verb = "SETSTOCK" },
}
local ORDER_CATEGORY_BY_ID = {}
for _, c in ipairs(ORDER_CATEGORIES) do
    ORDER_CATEGORY_BY_ID[c.id] = c
end

local orderItemRows = {}
local activeOrderCategoryId = "herb"
local activeRecipeProfession = "All"
local recipeProfessionMenu
-- Thirty pixels keeps the icon, quantity field and action button comfortably separated.
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
        ShowItemTooltip(self, self.entry, "ANCHOR_RIGHT")
    end)
    row:SetScript("OnLeave", function() GameTooltip:Hide() end)

    local nameText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    nameText:SetPoint("LEFT", icon, "RIGHT", 6, 0)
    nameText:SetWidth(140)
    nameText:SetJustifyH("LEFT")
    row.nameText = nameText

    local qtyBox = CreateFrame("EditBox", nil, row)
    qtyBox:SetSize(38, 22)
    qtyBox:SetPoint("LEFT", nameText, "RIGHT", 4, 0)
    qtyBox:SetFontObject(GameFontHighlightSmall)
    qtyBox:SetJustifyH("CENTER")
    qtyBox:SetTextInsets(3, 3, 0, 0)
    qtyBox:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 8, edgeSize = 8,
        insets = { left = 2, right = 2, top = 2, bottom = 2 }
    })
    qtyBox:SetBackdropColor(0.02, 0.02, 0.025, 0.95)
    qtyBox:SetBackdropBorderColor(0.35, 0.37, 0.42, 1.0)
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
        if row.categoryId == "stock" then
            -- Keep this many in the guild bank; the bots sell the rest. 0 removes the limit (officers only, checked
            -- on the server).
            local keep = tonumber(qtyBox:GetText()) or 0
            if keep < 1 then
                SendRawBody("CLEARSTOCK:" .. row.entry)
                Log("Stock limit removed: " .. row.name .. ".")
            else
                SendRawBody("SETSTOCK:" .. row.entry .. ":" .. keep)
                Log("Stock limit set: keep " .. keep .. " x " .. row.name .. " in the guild bank.")
            end
            RequestStock()
            return
        end
        local count = tonumber(qtyBox:GetText()) or 1
        if count < 1 then count = 1 end
        SendRawBody(category.verb .. ":" .. row.entry .. ":" .. count)
        local verbLabel = (category.verb == "CRAFTORDER") and "Craft order" or "Gather order"
        Log(verbLabel .. " sent: " .. row.name .. " x" .. count .. ".")
    end)
    btnOrder:SetScript("OnEnter", function(self)
        if row.categoryId ~= "stock" then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText("Keep this many in the guild bank", 1, 0.82, 0)
        GameTooltip:AddLine("A free guild bot takes the surplus out of the bank and sells it in town. 0 removes the limit. Guild master and officers only.", 1, 1, 1, true)
        GameTooltip:Show()
    end)
    btnOrder:SetScript("OnLeave", function() GameTooltip:Hide() end)
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
        local professionMatches = activeOrderCategoryId ~= "recipe"
            or activeRecipeProfession == "All"
            or item.profession == activeRecipeProfession
        if professionMatches and (filter == "" or item.name:lower():find(filter, 1, true)) then
            table.insert(filtered, item)
        end
    end

    if orderPickerFrame.recipeProfessionBtn then
        if activeOrderCategoryId == "recipe" then
            orderPickerFrame.recipeProfessionBtn:SetText("Profession: " .. activeRecipeProfession)
            orderPickerFrame.recipeProfessionBtn:Show()
        else
            orderPickerFrame.recipeProfessionBtn:Hide()
            if recipeProfessionMenu then recipeProfessionMenu:Hide() end
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
            if activeOrderCategoryId == "stock" then
                row.nameText:SetText(item.name .. " |cFF888888(" .. (item.count or 0) .. ")|r")
                local shown = (item.limit and item.limit > 0) and item.limit or (item.count or 0)
                row.qtyBox:SetMaxLetters(6)
                row.qtyBox:SetText(tostring(shown))
                row.btnOrder:SetText("Limit")
            else
                row.nameText:SetText(item.name)
                row.qtyBox:SetMaxLetters(4)
                row.btnOrder:SetText("Order")
            end
            local icon = GetItemIcon and GetItemIcon(item.entry)
            row.icon:SetTexture(icon or "Interface\\Icons\\INV_Misc_QuestionMark")
            row:Show()
        elseif orderItemRows[i] then
            orderItemRows[i]:Hide()
        end
    end

    orderPickerFrame.scrollChild:SetHeight(math.max(#filtered * ROW_HEIGHT, 1))

    if #items == 0 and activeOrderCategoryId == "stock" then
        orderPickerFrame.emptyText:SetText("|cFF888888No resources in the guild bank yet (or still loading).\nSet how many of each to keep; the bots sell the rest.|r")
        orderPickerFrame.emptyText:Show()
    elseif #items == 0 then
        orderPickerFrame.emptyText:SetText("|cFF888888Loading catalog from server...|r")
        orderPickerFrame.emptyText:Show()
    elseif #filtered == 0 then
        local emptyReason = activeOrderCategoryId == "recipe" and activeRecipeProfession ~= "All"
            and ("No " .. activeRecipeProfession .. " recipes match this filter.")
            or "No items match your search."
        orderPickerFrame.emptyText:SetText("|cFF888888" .. emptyReason .. "|r")
        orderPickerFrame.emptyText:Show()
    else
        orderPickerFrame.emptyText:Hide()
    end
end

local function OpenRecipeProfessionMenu(anchorButton)
    if not recipeProfessionMenu then
        recipeProfessionMenu = CreateFrame("Frame", "CoABotUIRecipeProfessionMenu", mainFrame)
        recipeProfessionMenu:SetFrameStrata("DIALOG")
        recipeProfessionMenu:SetBackdrop({
            bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 8, edgeSize = 10,
            insets = { left = 3, right = 3, top = 3, bottom = 3 }
        })
        recipeProfessionMenu:SetBackdropColor(0.035, 0.04, 0.055, 0.98)
        recipeProfessionMenu:SetBackdropBorderColor(0.4, 0.42, 0.48, 1.0)
        recipeProfessionMenu.buttons = {}
    end

    local professions, seen = { "All" }, { All = true }
    for _, item in ipairs(orderCatalog.recipe or {}) do
        local profession = item.profession or "Other"
        if not seen[profession] then
            seen[profession] = true
            table.insert(professions, profession)
        end
    end
    table.sort(professions, function(a, b)
        if a == "All" then return b ~= "All" end
        if b == "All" then return false end
        return a < b
    end)

    for _, btn in ipairs(recipeProfessionMenu.buttons) do btn:Hide() end
    local columns = (#professions > 8) and 2 or 1
    local rowsPerColumn = math.ceil(#professions / columns)
    recipeProfessionMenu:SetSize(columns * 138 + 8, rowsPerColumn * 24 + 8)
    for i, profession in ipairs(professions) do
        local professionId = profession
        local btn = recipeProfessionMenu.buttons[i]
        if not btn then
            btn = CreateFrame("Button", nil, recipeProfessionMenu)
            btn:SetSize(134, 22)
            btn.text = btn:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            btn.text:SetPoint("LEFT", btn, "LEFT", 7, 0)
            local highlight = btn:CreateTexture(nil, "HIGHLIGHT")
            highlight:SetAllPoints()
            highlight:SetTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
            highlight:SetBlendMode("ADD")
            recipeProfessionMenu.buttons[i] = btn
        end
        local column = math.floor((i - 1) / rowsPerColumn)
        local row = (i - 1) % rowsPerColumn
        btn:ClearAllPoints()
        btn:SetPoint("TOPLEFT", recipeProfessionMenu, "TOPLEFT", 4 + column * 138, -4 - row * 24)
        btn.text:SetText(profession == activeRecipeProfession
            and ("|cFFFFD100" .. profession .. "|r") or profession)
        btn:SetScript("OnClick", function()
            activeRecipeProfession = professionId
            recipeProfessionMenu:Hide()
            RefreshOrderPicker()
        end)
        btn:Show()
    end

    recipeProfessionMenu:ClearAllPoints()
    recipeProfessionMenu:SetPoint("TOPRIGHT", anchorButton, "BOTTOMRIGHT", 0, -3)
    recipeProfessionMenu:Show()
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
    if categoryId == "stock" then
        RequestStock()
    end
    RefreshOrderPicker()
end

local function CreateOrderPickerFrame()
    local frame = CreateFrame("Frame", "CoABotUIOrderPicker", mainFrame)
    frame:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 12, -66)
    frame:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -12, 10)
    frame:Hide()

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -7)
    title:SetText("|cFFFFD100Orders|r |cFF8B93A3choose an item and quantity|r")

    -- Category sidebar
    local sidebar = CreateFrame("Frame", nil, frame)
    sidebar:SetSize(90, 330)
    sidebar:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, -34)

    frame.categoryButtons = {}
    for i, category in ipairs(ORDER_CATEGORIES) do
        local categoryId = category.id
        local btn = CreateFrame("Button", nil, sidebar, "UIPanelButtonTemplate")
        btn:SetSize(86, 24)
        btn:SetPoint("TOPLEFT", sidebar, "TOPLEFT", 0, -(i - 1) * 27)
        btn:SetText(category.label)
        btn.categoryId = category.id
        btn.categoryLabel = category.label
        btn.text = btn:GetFontString()
        btn:SetScript("OnClick", function() SelectOrderCategory(categoryId) end)
        frame.categoryButtons[i] = btn
    end

    -- Search box
    local searchBox = CreateFrame("EditBox", nil, frame, "InputBoxTemplate")
    searchBox:SetSize(180, 20)
    searchBox:SetPoint("TOPLEFT", sidebar, "TOPRIGHT", 14, 0)
    searchBox:SetAutoFocus(false)
    searchBox:SetMaxLetters(40)
    searchBox:SetScript("OnTextChanged", function() RefreshOrderPicker() end)
    searchBox:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    frame.searchBox = searchBox

    local searchHint = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    searchHint:SetPoint("LEFT", searchBox, "RIGHT", 8, 0)
    searchHint:SetText("Search")

    local recipeProfessionBtn = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    recipeProfessionBtn:SetSize(144, 22)
    recipeProfessionBtn:SetPoint("LEFT", searchHint, "RIGHT", 8, 0)
    recipeProfessionBtn:SetScript("OnClick", function(self) OpenRecipeProfessionMenu(self) end)
    recipeProfessionBtn:Hide()
    frame.recipeProfessionBtn = recipeProfessionBtn

    -- Item list fills the remaining page while the outer shell stays a predictable size.
    local scrollFrame = CreateFrame("ScrollFrame", "CoABotUIOrderScroll", frame, "UIPanelScrollFrameTemplate")
    scrollFrame:SetPoint("TOPLEFT", searchBox, "BOTTOMLEFT", -4, -10)
    scrollFrame:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -28, 4)
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
    ShowMainPage("orders")
end

-------------------------------------------------------------------------------
-- Gear Panel (inspect equipped items, set armor/weapon type preference)
-------------------------------------------------------------------------------

-- WotLK EquipmentSlots order (see docs/addon-protocol.md's GEAR verb) -- EQUIPMENT_SLOT_BODY (3,
-- the shirt) and EQUIPMENT_SLOT_TABARD (18) are never sent by the server (cosmetic-only, no
-- "type" concept) so they're simply absent from this table's gaps rather than mapped to "?".
local GEAR_SLOT_NAMES = {
    [0] = "Head", [1] = "Neck", [2] = "Shoulders", [4] = "Chest", [5] = "Waist",
    [6] = "Legs", [7] = "Feet", [8] = "Wrist", [9] = "Hands", [10] = "Finger 1",
    [11] = "Finger 2", [12] = "Trinket 1", [13] = "Trinket 2", [14] = "Back",
    [15] = "Main Hand", [16] = "Off Hand", [17] = "Ranged",
}
local GEAR_SLOT_ORDER = { 0, 2, 4, 14, 5, 6, 8, 9, 7, 1, 10, 11, 12, 13, 15, 16, 17 }

local ARMOR_TYPE_OPTIONS = { "auto", "cloth", "leather", "mail", "plate" }
local WEAPON_TYPE_OPTIONS = { "auto", "sword", "mace", "axe", "fist", "dagger", "staff" }
local function Capitalize(s)
    return s:sub(1, 1):upper() .. s:sub(2)
end

local function CreateGearPreferenceRow(parent, yOffset, label, options)
    local lbl = parent:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    lbl:SetPoint("TOPLEFT", parent, "TOPLEFT", 14, yOffset)
    lbl:SetText("|cFFCCCCCC" .. label .. ":|r")

    local row = { buttons = {}, options = options }
    for i, opt in ipairs(options) do
        local btn = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
        btn:SetSize(54, 20)
        if i == 1 then
            btn:SetPoint("TOPLEFT", lbl, "BOTTOMLEFT", 0, -4)
        else
            btn:SetPoint("LEFT", row.buttons[i - 1], "RIGHT", 3, 0)
        end
        btn:SetText(Capitalize(opt))
        btn.optionId = opt
        row.buttons[i] = btn
    end
    return row
end

-- Every preference remains clickable. Types the server has not confirmed yet are muted rather
-- than disabled, so a stale/empty capability reply can never lock the whole control row.
local function ApplyGearPreferenceRow(row, legalTypes, currentPref, botGuidLow, isWeapon)
    local legalSet = { auto = true }
    for _, t in ipairs(legalTypes or {}) do
        legalSet[t] = true
    end

    for _, btn in ipairs(row.buttons) do
        local isCurrent = btn.optionId == currentPref
        btn:Enable()
        if isCurrent then
            btn:SetText("|cFF44FF44" .. Capitalize(btn.optionId) .. "|r")
        elseif legalSet[btn.optionId] then
            btn:SetText(Capitalize(btn.optionId))
        else
            btn:SetText("|cFF9A9A9A" .. Capitalize(btn.optionId) .. "|r")
        end
        btn:SetScript("OnClick", function()
            local prefs = gearPrefsCache[botGuidLow]
            if prefs then
                if isWeapon then prefs.weaponPref = btn.optionId else prefs.armorPref = btn.optionId end
                ApplyGearPreferenceRow(row, legalTypes, btn.optionId, botGuidLow, isWeapon)
            end
            SendBotCommand("SETGEARPREF", botGuidLow, (isWeapon and "weapon" or "armor") .. ":" .. btn.optionId)
        end)
        btn:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_TOP")
            GameTooltip:AddLine(Capitalize(self.optionId))
            if self.optionId ~= "auto" and not legalSet[self.optionId] then
                GameTooltip:AddLine("The server has not confirmed this proficiency yet.", 0.75, 0.75, 0.75, true)
            end
            GameTooltip:Show()
        end)
        btn:SetScript("OnLeave", function() GameTooltip:Hide() end)
    end
end

local function CreateGearPanelFrame()
    local frame = CreateFrame("Frame", "CoABotUIGearPanel", mainFrame)
    frame:SetPoint("TOPLEFT", mainFrame, "TOPLEFT", 12, -66)
    frame:SetPoint("BOTTOMRIGHT", mainFrame, "BOTTOMRIGHT", -12, 10)
    frame:Hide()
    frame:SetScript("OnShow", function(self)
        self.itemInfoElapsed = 0
        self.itemInfoRetries = 6
    end)
    frame:SetScript("OnUpdate", function(self, elapsed)
        if not self.itemInfoRetries or self.itemInfoRetries <= 0 then return end
        self.itemInfoElapsed = (self.itemInfoElapsed or 0) + elapsed
        if self.itemInfoElapsed >= 0.5 then
            self.itemInfoElapsed = 0
            self.itemInfoRetries = self.itemInfoRetries - 1
            RefreshGearPanel()
        end
    end)

    local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    title:SetPoint("TOPLEFT", frame, "TOPLEFT", 4, -7)
    frame.title = title

    local averageItemLevel = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    averageItemLevel:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -4, -7)
    averageItemLevel:SetText("|cFF8B93A3Average iLvl: --|r")
    frame.averageItemLevel = averageItemLevel

    frame.gearSlots = {}
    for i = 1, #GEAR_SLOT_ORDER do
        local col = (i - 1) % 2
        local row = math.floor((i - 1) / 2)
        local itemButton = CreateFrame("Button", nil, frame)
        itemButton:SetSize(236, 26)
        itemButton:SetPoint("TOPLEFT", frame, "TOPLEFT", 4 + col * 242, -32 - row * 28)
        itemButton:SetBackdrop({
            bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 8, edgeSize = 8,
            insets = { left = 2, right = 2, top = 2, bottom = 2 }
        })
        itemButton:SetBackdropColor(0.02, 0.02, 0.025, (row % 2 == 0) and 0.55 or 0.35)
        itemButton:SetBackdropBorderColor(0.22, 0.24, 0.28, 0.9)

        local icon = itemButton:CreateTexture(nil, "ARTWORK")
        icon:SetSize(22, 22)
        icon:SetPoint("LEFT", itemButton, "LEFT", 2, 0)
        icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        itemButton.icon = icon

        local text = itemButton:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        text:SetPoint("LEFT", icon, "RIGHT", 6, 0)
        text:SetWidth(202)
        text:SetJustifyH("LEFT")
        itemButton.text = text
        itemButton.slot = GEAR_SLOT_ORDER[i]
        itemButton:SetScript("OnEnter", function(self)
            if self.itemEntry then ShowItemTooltip(self, self.itemEntry, "ANCHOR_RIGHT") end
        end)
        itemButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
        frame.gearSlots[i] = itemButton
    end

    local prefY = -286
    frame.armorRow = CreateGearPreferenceRow(frame, prefY, "Preferred Armor Type", ARMOR_TYPE_OPTIONS)
    frame.weaponRow = CreateGearPreferenceRow(frame, prefY - 40, "Preferred Weapon Type", WEAPON_TYPE_OPTIONS)

    return frame
end

RefreshGearPanel = function()
    if not gearPanelFrame or not gearPanelFrame:IsShown() or not gearPanelBotGuid then return end

    local gear = gearCache[gearPanelBotGuid] or {}
    local totalItemLevel, resolvedItems = 0, 0
    for i, slot in ipairs(GEAR_SLOT_ORDER) do
        local entry = gear[slot]
        local itemButton = gearPanelFrame.gearSlots[i]
        if entry then
            local _, _, quality, clientItemLevel, _, itemType, itemSubType, _, _, icon = GetItemInfo(entry.entry)
            local itemLevel = entry.itemLevel > 0 and entry.itemLevel or clientItemLevel
            local detail = itemSubType or itemType or "Item"
            itemButton.itemEntry = entry.entry
            itemButton.icon:SetTexture(icon or (GetItemIcon and GetItemIcon(entry.entry))
                or "Interface\\Icons\\INV_Misc_QuestionMark")
            itemButton.icon:SetVertexColor(1, 1, 1, 1)
            if itemLevel then
                totalItemLevel = totalItemLevel + itemLevel
                resolvedItems = resolvedItems + 1
            end
            itemButton.text:SetText("|cFFB8BEC9" .. GEAR_SLOT_NAMES[slot] .. "|r  " .. detail
                .. (itemLevel and ("  |cFFFFD100i" .. itemLevel .. "|r") or "  |cFF777777i...|r"))
            local qualityColor = quality and ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[quality]
            if qualityColor then
                itemButton:SetBackdropBorderColor(qualityColor.r, qualityColor.g, qualityColor.b, 0.95)
            else
                itemButton:SetBackdropBorderColor(0.32, 0.34, 0.38, 0.9)
            end
        else
            itemButton.itemEntry = nil
            itemButton.icon:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
            itemButton.icon:SetVertexColor(0.35, 0.35, 0.35, 0.7)
            itemButton.text:SetText("|cFF666666" .. GEAR_SLOT_NAMES[slot] .. "  Empty|r")
            itemButton:SetBackdropBorderColor(0.18, 0.2, 0.24, 0.8)
        end
    end

    if resolvedItems > 0 then
        gearPanelFrame.averageItemLevel:SetText("|cFFB8BEC9Average iLvl:|r |cFFFFD100"
            .. math.floor(totalItemLevel / resolvedItems + 0.5) .. "|r")
    else
        gearPanelFrame.averageItemLevel:SetText("|cFF8B93A3Average iLvl: loading...|r")
    end

    local prefs = gearPrefsCache[gearPanelBotGuid]
    if prefs then
        ApplyGearPreferenceRow(gearPanelFrame.armorRow, prefs.legalArmor, prefs.armorPref, gearPanelBotGuid, false)
        ApplyGearPreferenceRow(gearPanelFrame.weaponRow, prefs.legalWeapon, prefs.weaponPref, gearPanelBotGuid, true)
    end
end

ShowGearPanel = function(botGuidLow, botName)
    if not gearPanelFrame then
        gearPanelFrame = CreateGearPanelFrame()
    end
    gearPanelBotGuid = botGuidLow
    gearPanelFrame.title:SetText("|cFFFFD100Gear|r  " .. tostring(botName or "?"))

    -- Clear stale text from whichever bot's gear was shown last, until the fresh GETGEAR replies
    -- land -- avoids briefly showing a *different* bot's equipment under this one's name.
    for _, itemButton in ipairs(gearPanelFrame.gearSlots) do
        itemButton.itemEntry = nil
        itemButton.icon:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark")
        itemButton.text:SetText("|cFF666666Loading...|r")
    end

    ShowMainPage("gear")
    RequestGear(botGuidLow)
    RefreshGearPanel()
end

ShowMainPage = function(pageId)
    if not mainFrame then return end
    if pageId ~= "squad" and pageId ~= "tasks" and pageId ~= "orders" and pageId ~= "browse" and pageId ~= "gear" then
        pageId = "squad"
    end

    CoABotUIDB.activePage = pageId
    mainFrame.activePage = pageId
    mainFrame.squadPage:Hide()
    if taskBoardFrame then taskBoardFrame:Hide() end
    if orderPickerFrame then orderPickerFrame:Hide() end
    if botBrowserFrame then botBrowserFrame:Hide() end
    if gearPanelFrame then gearPanelFrame:Hide() end

    for _, tab in ipairs(mainFrame.tabs) do
        if tab.pageId == pageId then
            tab:LockHighlight()
            tab:SetText("|cFFFFD100" .. tab.label .. "|r")
        else
            tab:UnlockHighlight()
            tab:SetText(tab.label)
        end
    end

    if pageId == "squad" then
        mainFrame.squadPage:Show()
        RefreshUI()
    elseif pageId == "tasks" then
        if not taskBoardFrame then taskBoardFrame = CreateGuildTaskBoardFrame() end
        taskBoardFrame:Show()
        RequestGuildRoster()
        RefreshTaskBoard()
    elseif pageId == "orders" then
        if not orderPickerFrame then orderPickerFrame = CreateOrderPickerFrame() end
        orderPickerFrame:Show()
        RequestOrderCatalogs()
        SelectOrderCategory(activeOrderCategoryId)
    elseif pageId == "browse" then
        if not botBrowserFrame then botBrowserFrame = CreateBotBrowserFrame() end
        botBrowserFrame:Show()
    else
        if not gearPanelFrame then gearPanelFrame = CreateGearPanelFrame() end
        gearPanelFrame:Show()
        if not gearPanelBotGuid then
            gearPanelFrame.title:SetText("|cFFFFD100Gear|r  |cFF8B93A3select Gear on a companion row|r")
        end
        RefreshGearPanel()
    end
end

-------------------------------------------------------------------------------
-- Minimap Button (LibDBIcon-1.0)
-------------------------------------------------------------------------------

local function CreateMinimapButton()
    local LibStub = _G.LibStub
    if not LibStub then return end -- Libs missing/failed to load -- fail quiet, slash command still works
    local ldb = LibStub:GetLibrary("LibDataBroker-1.1", true)
    local icon = LibStub:GetLibrary("LibDBIcon-1.0", true)
    if not ldb or not icon then return end

    local launcher = ldb:NewDataObject(ADDON_NAME, {
        type = "launcher",
        text = "CoA Bot Companion Control",
        icon = "Interface\\Icons\\INV_Misc_GroupLooking",
        OnClick = function(_, button)
            if button == "RightButton" then
                -- Quick-menu: toggle Auto Dungeon without opening the full panel.
                CoABotUIDB.autoDungeon = not CoABotUIDB.autoDungeon
                SendRawBody("AUTODUNGEON:" .. (CoABotUIDB.autoDungeon and "1" or "0"))
                if mainFrame and mainFrame.RefreshAutoDungeonButton then
                    mainFrame.RefreshAutoDungeonButton()
                end
                Log("Auto Dungeon Mode " .. (CoABotUIDB.autoDungeon and "|cFF44FF44enabled|r" or "|cFFFF4444disabled|r") .. " (right-click minimap icon).")
            else
                if mainFrame:IsShown() then
                    mainFrame:Hide()
                    CoABotUIDB.isShown = false
                else
                    mainFrame:Show()
                    CoABotUIDB.isShown = true
                    if not CoABotUIDB.isCollapsed then
                        ShowMainPage(CoABotUIDB.activePage or "squad")
                    end
                end
            end
        end,
        OnTooltipShow = function(tooltip)
            tooltip:AddLine("|cFFFFD100CoA Bot Companion Control|r")
            tooltip:AddLine("|cFFCCCCCCLeft-click|r to toggle the panel", 1, 1, 1)
            tooltip:AddLine("|cFFCCCCCCRight-click|r to toggle Auto Dungeon", 1, 1, 1)
        end,
    })

    icon:Register(ADDON_NAME, launcher, CoABotUIDB.minimap)
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
eventFrame:RegisterEvent("PLAYER_REGEN_DISABLED")
eventFrame:RegisterEvent("PLAYER_REGEN_ENABLED")

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

        CreateMinimapButton()
        mainFrame.ApplyCollapsedState(CoABotUIDB.isCollapsed)
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
            RequestFormation()
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

    elseif event == "PLAYER_REGEN_DISABLED" or event == "PLAYER_REGEN_ENABLED" then
        if mainFrame and mainFrame.RefreshTeleportButton then
            mainFrame.RefreshTeleportButton()
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
            if not CoABotUIDB.isCollapsed then
                ShowMainPage(CoABotUIDB.activePage or "squad")
            end
        end
    elseif cmd == "show" then
        mainFrame:Show()
        CoABotUIDB.isShown = true
        if not CoABotUIDB.isCollapsed then
            ShowMainPage(CoABotUIDB.activePage or "squad")
        end
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
