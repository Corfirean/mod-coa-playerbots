--[[
    CoA World Map Intelligence -- Tooltips
    Native Blizzard-styled tooltips for map pins, bot details, guild assignments, and world events.
--]]

local ADDON_NAME = "CoABotUI"

CoABotMapTooltip = {}
local Tooltip = CoABotMapTooltip

-- Cache for on-demand details: [botGuidLow] = { specId, avgIlvl, specName, activityName, taskType, taskItem, taskTarget, taskDone, taskStatus, cachedAt }
Tooltip.detailCache = {}

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

local function GetClassColorHex(classId)
    local fn = CLASS_FILE_NAMES[classId]
    local c = fn and RAID_CLASS_COLORS and RAID_CLASS_COLORS[fn]
    if c then
        return string.format("|cFF%02x%02x%02x", c.r * 255, c.g * 255, c.b * 255)
    end
    return "|cFFFFFFFF"
end

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

    local tt = WorldMapTooltip or GameTooltip
    tt:SetOwner(pinFrame, "ANCHOR_RIGHT")
    tt:ClearLines()

    local nameColor = GetClassColorHex(botData.classId)
    local nameStr = botData.name or ("Bot-" .. tostring(botData.guidLow))
    tt:AddLine(nameColor .. nameStr .. "|r — Level " .. tostring(botData.level), 1, 1, 1)

    -- Class & Role
    local roleStr = (botData.role == 1 and "Tank") or (botData.role == 2 and "Healer") or (botData.role == 3 and "Support") or "DPS"
    local classEntry = CLASS_FILE_NAMES[botData.classId] or "Unknown"
    tt:AddDoubleLine("Class:", classEntry .. " (" .. roleStr .. ")", 0.8, 0.8, 0.8, 1, 1, 1)

    -- Check detail cache
    local detail = Tooltip.GetCachedDetail(botData.guidLow)
    if detail then
        if detail.specName and detail.specName ~= "" and detail.specName ~= "Unknown" then
            tt:AddDoubleLine("Specialization:", detail.specName, 0.8, 0.8, 0.8, 1, 1, 1)
        end
        if detail.avgItemLevel and detail.avgItemLevel > 0 then
            tt:AddDoubleLine("Item Level:", tostring(detail.avgItemLevel), 0.8, 0.8, 0.8, 0.2, 1.0, 0.2)
        end
    end

    -- Current Activity
    local actStr = (detail and detail.activityName) or ACTIVITY_NAMES[botData.activity] or "Idle"
    tt:AddDoubleLine("Activity:", actStr, 0.8, 0.8, 0.8, 1, 0.82, 0)

    -- Guild Companion section
    local isGuild = bit.band(botData.flags or 0, 1) ~= 0
    if isGuild then
        tt:AddLine(" ")
        tt:AddLine("Guild Companion", 1.0, 0.82, 0.0) -- Blizzard Gold

        if detail and detail.taskType and detail.taskType > 0 then
            local taskTitle = "Assignment:"
            local taskDesc = ""
            if detail.taskType == 1 then
                taskDesc = "Collect Item #" .. tostring(detail.taskItem)
            elseif detail.taskType == 2 then
                taskDesc = "Craft Item #" .. tostring(detail.taskItem)
            elseif detail.taskType == 3 then
                taskDesc = "Earn Gold for Guild"
            end
            tt:AddDoubleLine(taskTitle, taskDesc, 0.9, 0.9, 0.9, 1, 1, 1)

            local pct = 0
            if detail.taskTarget > 0 then
                pct = math.min(100, math.floor((detail.taskDone * 100) / detail.taskTarget))
            end
            tt:AddDoubleLine("Progress:", string.format("%d / %d (%d%%)", detail.taskDone, detail.taskTarget, pct), 0.8, 0.8, 0.8, 0.2, 1.0, 0.2)
            tt:AddDoubleLine("Status:", detail.taskStatus or "In Progress", 0.8, 0.8, 0.8, 1, 1, 1)
        else
            tt:AddLine("No current assignment", 0.6, 0.6, 0.6)
        end
    end

    tt:Show()

    -- Request detail refresh if missing or older than 30s
    if not detail or (GetTime() - detail.cachedAt > 30) then
        if CoABotWorldMap and CoABotWorldMap.RequestDetail then
            CoABotWorldMap.RequestDetail(botData.guidLow)
        end
    end
end

function Tooltip.ShowClusterTooltip(clusterFrame, botList)
    if not botList or #botList == 0 then return end

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

        tt:AddDoubleLine(nameColor .. roleLetter .. " " .. (b.name or ("Bot-" .. b.guidLow)) .. guildMarker .. "|r", "Lvl " .. b.level .. " (" .. actStr .. ")", 1, 1, 1, 0.7, 0.7, 0.7)
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
    local tt = WorldMapTooltip or GameTooltip
    if tt then
        tt:Hide()
    end
end
