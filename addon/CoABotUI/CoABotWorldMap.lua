--[[
    CoA World Map Intelligence -- World Map Controller
    Lifecycle, cadence polling, network message handler, stale protection, and marker sync.
--]]

local ADDON_NAME = "CoABotUI"
local PROTOCOL_PREFIX = "COABOT"

CoABotWorldMap = {}
local Map = CoABotWorldMap

local currentReqId = 1
local pendingBots = {}
local activeBots = {}
local activeEvents = {}
local isQueryInFlight = false
local pollTimer = 0

-- Cadence intervals per mode (seconds)
local MODE_CADENCE = {
    [0] = 0,    -- OFF (no polling)
    [1] = 3.0,  -- NEARBY
    [2] = 5.0,  -- CURRENT ZONE
    [3] = 10.0, -- GUILD ONLY
    [4] = 15.0, -- ALL ALLIED
}

local function SendMessage(body)
    local playerName = UnitName("player")
    if playerName and playerName ~= "" then
        SendAddonMessage(PROTOCOL_PREFIX, body, "WHISPER", playerName)
    end
end

function Map:GetNextReqId()
    currentReqId = currentReqId + 1
    if currentReqId > 2000000000 then
        currentReqId = 1
    end
    return currentReqId
end

-- Issue map query
function Map:QueryMapData()
    if not WorldMapFrame or not WorldMapFrame:IsVisible() then
        return
    end

    local mode = CoABotMapSettings and CoABotMapSettings:GetMode() or 0
    local eventsEnabled = CoABotMapSettings and CoABotMapSettings:GetEventsEnabled() or false

    if mode == 0 and not eventsEnabled then
        Map:Clear()
        return
    end

    local reqId = Map:GetNextReqId()
    isQueryInFlight = true

    -- Query Bots
    if mode > 0 then
        local param1 = 0
        local param2 = 0
        if mode == 1 then
            param1 = CoABotMapSettings:GetRadius()
        elseif mode == 2 then
            param1 = GetCurrentMapZone and GetCurrentMapZone() or 0
        elseif mode == 4 then
            local mapName = GetMapInfo()
            local area = mapName and CoABotMapData and CoABotMapData.areas and CoABotMapData.areas[mapName:lower()]
            param1 = area and area.mapId or 0
        end

        SendMessage("MAPQUERY:" .. reqId .. ":" .. mode .. ":" .. param1 .. ":" .. param2)
    else
        Map:ClearBots()
    end

    -- Query Events
    if eventsEnabled then
        local mapName = GetMapInfo()
        local area = mapName and CoABotMapData and CoABotMapData.areas and CoABotMapData.areas[mapName:lower()]
        local mapId = area and area.mapId or 0
        SendMessage("MAPEVENTS:" .. reqId .. ":" .. mapId)
    else
        activeEvents = {}
        if CoABotMapMarkers then
            CoABotMapMarkers:Update(activeBots, activeEvents)
        end
    end
end

function Map:RequestDetail(botGuidLow)
    if not botGuidLow then return end
    local reqId = Map:GetNextReqId()
    SendMessage("MAPDETAIL:" .. reqId .. ":" .. botGuidLow)
end

function Map:ClearBots()
    activeBots = {}
    pendingBots = {}
    if CoABotMapMarkers then
        CoABotMapMarkers:Update(activeBots, activeEvents)
    end
end

function Map:Clear()
    activeBots = {}
    pendingBots = {}
    activeEvents = {}
    if CoABotMapMarkers then
        CoABotMapMarkers:ClearAll()
    end
end

function Map:OnModeChanged()
    pollTimer = 0
    local mode = CoABotMapSettings and CoABotMapSettings:GetMode() or 0
    if mode == 0 then
        Map:ClearBots()
    else
        Map:QueryMapData()
    end
end

function Map:OnRadiusChanged()
    pollTimer = 0
    Map:QueryMapData()
end

function Map:OnEventsToggled()
    local enabled = CoABotMapSettings and CoABotMapSettings:GetEventsEnabled() or false
    if not enabled then
        activeEvents = {}
        if CoABotMapMarkers then
            CoABotMapMarkers:Update(activeBots, activeEvents)
        end
    else
        Map:QueryMapData()
    end
end

-- Network response dispatch
function Map:HandleIncomingMessage(body)
    if not body then return end
    local verb, rest = body:match("^([A-Z]+):?(.*)$")
    if not verb then return end

    if verb == "MAPBEGIN" then
        local reqIdStr, totalBots, totalChunks = rest:match("^(%d+):(%d+):(%d+)")
        local reqId = tonumber(reqIdStr) or 0
        if reqId < currentReqId then
            return -- Stale
        end
        pendingBots = {}

    elseif verb == "MAPBOTS" then
        local reqIdStr, chunkIdx, payload = rest:match("^(%d+):(%d+):(.*)$")
        local reqId = tonumber(reqIdStr) or 0
        if reqId < currentReqId then
            return -- Stale
        end

        if payload and payload ~= "" then
            for entry in payload:gmatch("[^|]+") do
                local guid, wx, wy, lvl, cls, role, flags, act = entry:match("^(%d+),([%-]?%d+),([%-]?%d+),(%d+),(%d+),(%d+),(%d+),(%d+)")
                if guid then
                    table.insert(pendingBots, {
                        guidLow = tonumber(guid),
                        worldX = tonumber(wx),
                        worldY = tonumber(wy),
                        level = tonumber(lvl),
                        classId = tonumber(cls),
                        role = tonumber(role),
                        flags = tonumber(flags),
                        activity = tonumber(act)
                    })
                end
            end
        end

    elseif verb == "MAPEND" then
        local reqIdStr = rest:match("^(%d+)")
        local reqId = tonumber(reqIdStr) or 0
        if reqId < currentReqId then
            return -- Stale
        end
        isQueryInFlight = false
        activeBots = pendingBots
        pendingBots = {}

        if CoABotMapMarkers then
            CoABotMapMarkers:Update(activeBots, activeEvents)
        end

    elseif verb == "MAPDETAIL" then
        local reqIdStr, guid, specId, ilvl, specName, actName, tType, tItem, tTarget, tDone, tStatus =
            rest:match("^(%d+):(%d+):(%d+):(%d+):([^:]*):([^:]*):(%d+):(%d+):(%d+):(%d+):?(.*)$")

        if guid and CoABotMapTooltip then
            local detail = {
                guidLow = tonumber(guid),
                specId = tonumber(specId) or 0,
                avgItemLevel = tonumber(ilvl) or 0,
                specName = specName or "",
                activityName = actName or "",
                taskType = tonumber(tType) or 0,
                taskItem = tonumber(tItem) or 0,
                taskTarget = tonumber(tTarget) or 0,
                taskDone = tonumber(tDone) or 0,
                taskStatus = tStatus or ""
            }
            CoABotMapTooltip.SetCachedDetail(detail.guidLow, detail)
        end

    elseif verb == "MAPEVENTS" then
        local reqIdStr, payload = rest:match("^(%d+):(.*)$")
        local reqId = tonumber(reqIdStr) or 0
        if reqId < currentReqId then
            return -- Stale
        end

        local parsedEvents = {}
        if payload and payload ~= "" then
            for entry in payload:gmatch("[^|]+") do
                local evId, evType, cx, cy, phase, aCount, hCount, name =
                    entry:match("^(%d+),(%d+),([%-]?%d+),([%-]?%d+),(%d+),(%d+),(%d+),?(.*)$")
                if evId then
                    table.insert(parsedEvents, {
                        eventId = tonumber(evId),
                        eventType = tonumber(evType),
                        centerX = tonumber(cx),
                        centerY = tonumber(cy),
                        phase = tonumber(phase),
                        allianceCount = tonumber(aCount),
                        hordeCount = tonumber(hCount),
                        name = name or "Encounter"
                    })
                end
            end
        end

        activeEvents = parsedEvents
        if CoABotMapMarkers then
            CoABotMapMarkers:Update(activeBots, activeEvents)
        end
    end
end

-- Map Lifecycle Frame
local mapFrame = CreateFrame("Frame")
mapFrame:RegisterEvent("ADDON_LOADED")
mapFrame:RegisterEvent("WORLD_MAP_UPDATE")
mapFrame:RegisterEvent("ZONE_CHANGED_NEW_AREA")

mapFrame:SetScript("OnEvent", function(self, event, arg1)
    if event == "ADDON_LOADED" and arg1 == ADDON_NAME then
        if CoABotMapSettings and CoABotMapSettings.CreateWorldMapControls then
            CoABotMapSettings:CreateWorldMapControls()
        end
    elseif event == "WORLD_MAP_UPDATE" or event == "ZONE_CHANGED_NEW_AREA" then
        if WorldMapFrame and WorldMapFrame:IsVisible() then
            if CoABotMapMarkers then
                CoABotMapMarkers:Update(activeBots, activeEvents)
            end
        end
    end
end)

-- Polling tick on WorldMapFrame visible
local ticker = CreateFrame("Frame")
ticker:SetScript("OnUpdate", function(self, elapsed)
    if not WorldMapFrame or not WorldMapFrame:IsVisible() then
        return
    end

    local mode = CoABotMapSettings and CoABotMapSettings:GetMode() or 0
    local interval = MODE_CADENCE[mode] or 0

    if interval > 0 then
        pollTimer = pollTimer + elapsed
        if pollTimer >= interval then
            pollTimer = 0
            Map:QueryMapData()
        end
    end
end)

-- Hook WorldMapFrame OnShow and OnHide
if WorldMapFrame then
    WorldMapFrame:HookScript("OnShow", function()
        if CoABotMapSettings and CoABotMapSettings.CreateWorldMapControls then
            CoABotMapSettings:CreateWorldMapControls()
        end
        pollTimer = 0
        Map:QueryMapData()
    end)

    WorldMapFrame:HookScript("OnHide", function()
        pollTimer = 0
        if CoABotMapMarkers then
            CoABotMapMarkers:ClearAll()
        end
        if CoABotMapTooltip then
            CoABotMapTooltip.HideTooltip()
        end
    end)
end
