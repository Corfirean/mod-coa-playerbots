--[[
    CoA World Map Intelligence -- Tooltips
    Native Blizzard-styled tooltips for map pins, bot details, guild assignments, and world events.
--]]

local ADDON_NAME = "CoABotUI"

CoABotMapTooltip = {}
local Tooltip = CoABotMapTooltip

-- Cache for on-demand details: [botGuidLow] = { name, specId, avgIlvl, specName, activityName, taskType, taskItem, taskTarget, taskDone, taskStatus, cachedAt }
Tooltip.detailCache = {}
Tooltip.pendingRequests = {} -- [botGuidLow] = timestamp
Tooltip.DETAIL_TTL = 30      -- seconds
Tooltip.PENDING_TIMEOUT = 5.0 -- seconds

Tooltip.currentHoverPin = nil
Tooltip.currentHoverGuid = nil

function Tooltip.GetCachedDetail(botGuidLow)
    return Tooltip.detailCache[botGuidLow]
end

function Tooltip.SetCachedDetail(botGuidLow, detail)
    detail.cachedAt = GetTime()
    Tooltip.detailCache[botGuidLow] = detail
end

local CLASS_FILE_NAMES = {
    [1] = "WARRIOR", [2] = "PALADIN", [3] = "HUNTER", [4] = "ROGUE", [5] = "PRIEST",
    [6] = "DEATHKNIGHT", [7] = "SHAMAN", [8] = "MAGE", [9] = "WARLOCK", [11] = "DRUID",
}

local CLASS_NAMES = {
    [1] = "Warrior", [2] = "Paladin", [3] = "Hunter", [4] = "Rogue", [5] = "Priest",
    [6] = "Death Knight", [7] = "Shaman", [8] = "Mage", [9] = "Warlock", [11] = "Druid",
    [12] = "Barbarian", [13] = "Witch Doctor", [14] = "Felsworn", [15] = "Witch Hunter",
    [16] = "Stormbringer", [17] = "Knight of Xoroth", [18] = "Guardian", [19] = "Templar",
    [20] = "Bloodmage", [21] = "Ranger", [22] = "Chronomancer", [23] = "Necromancer",
    [24] = "Pyromancer", [25] = "Cultist", [26] = "Starcaller", [27] = "Sun Cleric",
    [28] = "Tinker", [29] = "Venomancer", [30] = "Reaper", [31] = "Primalist", [32] = "Runemaster",
}

local CUSTOM_CLASS_COLORS = {
    [12] = { r = 0.85, g = 0.40, b = 0.20 }, -- Barbarian: Copper
    [13] = { r = 0.20, g = 0.75, b = 0.50 }, -- Witch Doctor: Voodoo teal
    [14] = { r = 0.40, g = 0.85, b = 0.10 }, -- Felsworn: Fel green
    [15] = { r = 0.70, g = 0.55, b = 0.35 }, -- Witch Hunter: Leather
    [16] = { r = 0.10, g = 0.65, b = 0.95 }, -- Stormbringer: Lightning cyan
    [17] = { r = 0.85, g = 0.20, b = 0.20 }, -- Knight of Xoroth: Searing crimson
    [18] = { r = 0.75, g = 0.75, b = 0.60 }, -- Guardian: Iron shield
    [19] = { r = 0.95, g = 0.85, b = 0.40 }, -- Templar: Radiant holy gold
    [20] = { r = 0.90, g = 0.15, b = 0.25 }, -- Bloodmage: Sanguine red
    [21] = { r = 0.45, g = 0.80, b = 0.30 }, -- Ranger: Forest green
    [22] = { r = 0.90, g = 0.75, b = 0.20 }, -- Chronomancer: Bronze time
    [23] = { r = 0.30, g = 0.80, b = 0.50 }, -- Necromancer: Unholy pestilence
    [24] = { r = 1.00, g = 0.45, b = 0.05 }, -- Pyromancer: Fire orange
    [25] = { r = 0.70, g = 0.30, b = 0.85 }, -- Cultist: Void shadow
    [26] = { r = 0.40, g = 0.65, b = 1.00 }, -- Starcaller: Astral blue
    [27] = { r = 1.00, g = 0.85, b = 0.30 }, -- Sun Cleric: Solar gold
    [28] = { r = 0.85, g = 0.65, b = 0.40 }, -- Tinker: Copper gear
    [29] = { r = 0.40, g = 0.90, b = 0.30 }, -- Venomancer: Toxic acid
    [30] = { r = 0.65, g = 0.35, b = 0.85 }, -- Reaper: Spectral violet
    [31] = { r = 0.85, g = 0.55, b = 0.20 }, -- Primalist: Earth amber
    [32] = { r = 0.25, g = 0.85, b = 0.85 }, -- Runemaster: Runic turquoise
}

function Tooltip.GetClassColor(classId)
    local fn = CLASS_FILE_NAMES[classId]
    local c = fn and RAID_CLASS_COLORS and RAID_CLASS_COLORS[fn]
    if c then
        return c.r, c.g, c.b
    end
    local custom = CUSTOM_CLASS_COLORS[classId]
    if custom then
        return custom.r, custom.g, custom.b
    end
    return 1.0, 1.0, 1.0
end

local function GetClassColorHex(classId)
    local r, g, b = Tooltip.GetClassColor(classId)
    return string.format("|cFF%02x%02x%02x", math.floor(r * 255), math.floor(g * 255), math.floor(b * 255))
end

local function FormatItemDisplay(itemId)
    if not itemId or itemId <= 0 then
        return "Item #" .. tostring(itemId)
    end
    local itemName, itemLink, itemRarity, itemLevel, itemMinLevel, itemType, itemSubType, itemStackCount, itemEquipLoc, itemTexture
    if GetItemInfo then
        itemName, itemLink, itemRarity, itemLevel, itemMinLevel, itemType, itemSubType, itemStackCount, itemEquipLoc, itemTexture = GetItemInfo(itemId)
    end
    local icon = itemTexture or (GetItemIcon and GetItemIcon(itemId))
    local iconStr = icon and string.format("|T%s:14:14:0:0|t ", icon) or ""
    if itemName then
        local colorHex = "|cffffffff"
        if itemRarity and ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[itemRarity] then
            colorHex = ITEM_QUALITY_COLORS[itemRarity].hex or "|cffffffff"
        end
        return iconStr .. colorHex .. itemName .. "|r"
    else
        return iconStr .. "Item #" .. tostring(itemId)
    end
end
Tooltip.FormatItemDisplay = FormatItemDisplay

local ACTIVITY_NAMES = {
    [0] = "Idle",
    [1] = "Questing",
    [2] = "Traveling",
    [3] = "Gathering Herbs/Ore",
    [4] = "Crafting",
    [5] = "In Combat",
    [6] = "Resting",
    [7] = "Group Adventure",
    [8] = "In Dungeon",
    [9] = "World Encounter",
    [10] = "Wandering",
}

function Tooltip.ShowBotTooltip(pinFrame, botData)
    if not botData then return end

    Tooltip.currentHoverPin = pinFrame
    Tooltip.currentHoverGuid = botData.guidLow

    local tt = WorldMapTooltip or GameTooltip
    tt:SetOwner(pinFrame, "ANCHOR_RIGHT")
    tt:ClearLines()

    local detail = Tooltip.GetCachedDetail(botData.guidLow)

    local nameColor = GetClassColorHex(botData.classId)
    local realName = (detail and detail.name and detail.name ~= "" and detail.name) or botData.name
    local nameStr = realName or ("Bot-" .. tostring(botData.guidLow))
    tt:AddLine(nameColor .. nameStr .. "|r — Level " .. tostring(botData.level), 1, 1, 1)

    -- Class & Role
    local roleStr = (botData.role == 1 and "Tank") or (botData.role == 2 and "Healer") or (botData.role == 3 and "Support") or "DPS"
    local classEntry = CLASS_NAMES[botData.classId] or CLASS_FILE_NAMES[botData.classId] or "Unknown"
    tt:AddDoubleLine("Class:", classEntry .. " (" .. roleStr .. ")", 0.8, 0.8, 0.8, 1, 1, 1)

    -- Specialization & iLvl from cache
    if detail then
        if detail.specName and detail.specName ~= "" and detail.specName ~= "Unknown" then
            tt:AddDoubleLine("Specialization:", detail.specName, 0.8, 0.8, 0.8, 1, 1, 1)
        end
        if detail.avgItemLevel and detail.avgItemLevel > 0 then
            tt:AddDoubleLine("Item Level:", tostring(detail.avgItemLevel), 0.8, 0.8, 0.8, 0.2, 1.0, 0.2)
        end
    end

    -- Current Activity
    local actStr = (detail and detail.activityName and detail.activityName ~= "" and detail.activityName) or ACTIVITY_NAMES[botData.activity] or "Idle"
    tt:AddDoubleLine("Activity:", actStr, 0.8, 0.8, 0.8, 1, 0.82, 0)

    -- Guild Companion section
    local isGuild = bit.band(botData.flags or 0, 1) ~= 0
    if isGuild then
        tt:AddLine(" ")
        tt:AddLine("Guild Companion", 1.0, 0.82, 0.0) -- Blizzard Gold

        if detail and detail.taskType and detail.taskType > 0 then
            local taskTitle = "Assignment:"
            if detail.taskType == 1 then
                -- Collect Item
                tt:AddDoubleLine(taskTitle, "Collect " .. FormatItemDisplay(detail.taskItem), 0.9, 0.9, 0.9, 1, 1, 1)
                local pct = 0
                if detail.taskTarget > 0 then
                    pct = math.min(100, math.floor((detail.taskDone * 100) / detail.taskTarget))
                end
                tt:AddDoubleLine("Progress:", string.format("%d / %d (%d%%)", detail.taskDone, detail.taskTarget, pct), 0.8, 0.8, 0.8, 0.2, 1.0, 0.2)
                tt:AddDoubleLine("Status:", detail.taskStatus or "In Progress", 0.8, 0.8, 0.8, 1, 1, 1)
            elseif detail.taskType == 2 then
                -- Craft Item order: Display remaining to craft without false percentage
                local remaining = math.max(0, detail.taskTarget - detail.taskDone)
                tt:AddDoubleLine(taskTitle, "Craft " .. FormatItemDisplay(detail.taskItem), 0.9, 0.9, 0.9, 1, 1, 1)
                tt:AddDoubleLine("Remaining to Craft:", tostring(remaining) .. " / " .. tostring(detail.taskTarget), 0.8, 0.8, 0.8, 1, 0.82, 0)
                tt:AddDoubleLine("Status:", detail.taskStatus or "In Progress", 0.8, 0.8, 0.8, 1, 1, 1)
            elseif detail.taskType == 3 then
                -- Earn Gold
                tt:AddDoubleLine(taskTitle, "Earn Gold for Guild", 0.9, 0.9, 0.9, 1, 1, 1)
                local pct = 0
                if detail.taskTarget > 0 then
                    pct = math.min(100, math.floor((detail.taskDone * 100) / detail.taskTarget))
                end
                tt:AddDoubleLine("Progress:", string.format("%d / %d (%d%%)", detail.taskDone, detail.taskTarget, pct), 0.8, 0.8, 0.8, 0.2, 1.0, 0.2)
                tt:AddDoubleLine("Status:", detail.taskStatus or "In Progress", 0.8, 0.8, 0.8, 1, 1, 1)
            end
        else
            tt:AddLine("No current assignment", 0.6, 0.6, 0.6)
        end
    end

    tt:Show()

    -- Request detail refresh if missing or older than TTL
    local now = GetTime()
    local isExpired = (not detail) or ((now - detail.cachedAt) > Tooltip.DETAIL_TTL)
    if isExpired then
        local pendingTime = Tooltip.pendingRequests[botData.guidLow]
        if not pendingTime or ((now - pendingTime) > Tooltip.PENDING_TIMEOUT) then
            Tooltip.pendingRequests[botData.guidLow] = now
            if CoABotWorldMap and CoABotWorldMap.RequestDetail then
                CoABotWorldMap:RequestDetail(botData.guidLow)
            end
        end
    end
end

function Tooltip.OnDetailReceived(guidLow, detail)
    if not guidLow or not detail then return end

    Tooltip.pendingRequests[guidLow] = nil
    Tooltip.SetCachedDetail(guidLow, detail)

    -- Auto-refresh active tooltip if the user is still hovering over this specific bot!
    local tt = WorldMapTooltip or GameTooltip
    if tt and tt:IsShown() and Tooltip.currentHoverGuid == guidLow and Tooltip.currentHoverPin then
        local pin = Tooltip.currentHoverPin
        if pin.botData and pin.botData.guidLow == guidLow then
            Tooltip.ShowBotTooltip(pin, pin.botData)
        end
    end
end

function Tooltip.ShowClusterTooltip(clusterFrame, botList)
    if not botList or #botList == 0 then return end

    Tooltip.currentHoverPin = clusterFrame
    Tooltip.currentHoverGuid = nil

    local tt = WorldMapTooltip or GameTooltip
    tt:SetOwner(clusterFrame, "ANCHOR_RIGHT")
    tt:ClearLines()

    local guildCount = 0
    for _, b in ipairs(botList) do
        if bit.band(b.flags or 0, 1) ~= 0 then
            guildCount = guildCount + 1
        end
    end

    tt:AddLine("Allied Bots Cluster", 1, 0.82, 0)
    tt:AddDoubleLine("Total Bots:", tostring(#botList), 0.8, 0.8, 0.8, 1, 1, 1)
    if guildCount > 0 then
        tt:AddDoubleLine("Guild Companions:", tostring(guildCount), 1.0, 0.82, 0.0, 1, 1, 1)
    end
    tt:AddLine(" ")

    local maxShow = math.min(#botList, 8)
    for i = 1, maxShow do
        local b = botList[i]
        local nameColor = GetClassColorHex(b.classId)
        local roleLetter = (b.role == 1 and "[T]") or (b.role == 2 and "[H]") or (b.role == 3 and "[S]") or "[D]"
        local actStr = ACTIVITY_NAMES[b.activity] or "Idle"
        local isGuild = bit.band(b.flags or 0, 1) ~= 0
        local guildMarker = isGuild and " |cFFFFD100*|r" or ""

        local d = Tooltip.GetCachedDetail(b.guidLow)
        local bName = (d and d.name and d.name ~= "" and d.name) or b.name or ("Bot-" .. b.guidLow)
        tt:AddDoubleLine(nameColor .. roleLetter .. " " .. bName .. guildMarker .. "|r", "Lvl " .. b.level .. " (" .. actStr .. ")", 1, 1, 1, 0.7, 0.7, 0.7)
    end

    if #botList > maxShow then
        tt:AddLine(string.format("... and %d more", #botList - maxShow), 0.5, 0.5, 0.5)
    end

    tt:AddLine(" ")
    tt:AddLine("Zoom into zone map to inspect individual bots.", 0.6, 0.8, 1.0)
    tt:Show()
end

function Tooltip.ShowEventTooltip(eventFrame, eventData)
    if not eventData then return end

    Tooltip.currentHoverPin = eventFrame
    Tooltip.currentHoverGuid = nil

    local tt = WorldMapTooltip or GameTooltip
    tt:SetOwner(eventFrame, "ANCHOR_RIGHT")
    tt:ClearLines()

    tt:AddLine(eventData.name or "World Encounter", 1, 0.2, 0.2)
    tt:AddLine("Dynamic World Event", 1, 0.82, 0)
    tt:AddLine(" ")

    tt:AddDoubleLine("Alliance Participants:", tostring(eventData.allianceCount or 0), 0.2, 0.6, 1.0, 1, 1, 1)
    tt:AddDoubleLine("Horde Participants:", tostring(eventData.hordeCount or 0), 1.0, 0.2, 0.2, 1, 1, 1)
    tt:AddLine(" ")

    local statusStr = (eventData.phase == 2 and "Active (In Combat)") or (eventData.phase == 3 and "Resolving") or "Active"
    tt:AddDoubleLine("Status:", statusStr, 0.8, 0.8, 0.8, 0.2, 1.0, 0.2)
    tt:Show()
end

function Tooltip.HideTooltip()
    Tooltip.currentHoverPin = nil
    Tooltip.currentHoverGuid = nil
    local tt = WorldMapTooltip or GameTooltip
    if tt then
        tt:Hide()
    end
end

-- Asynchronous item cache fill watcher
local itemInfoWatcher = CreateFrame("Frame")
itemInfoWatcher:RegisterEvent("GET_ITEM_INFO_RECEIVED")
itemInfoWatcher:SetScript("OnEvent", function(self, event, itemId)
    if Tooltip.currentHoverPin and Tooltip.currentHoverGuid then
        local pin = Tooltip.currentHoverPin
        if pin.botData and pin.botData.guidLow == Tooltip.currentHoverGuid then
            local detail = Tooltip.GetCachedDetail(Tooltip.currentHoverGuid)
            if detail and detail.taskItem == itemId then
                Tooltip.ShowBotTooltip(pin, pin.botData)
            end
        end
    end
end)
