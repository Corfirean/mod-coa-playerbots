--[[
    CoA World Map Intelligence -- World Map Controller
    Lifecycle, cadence polling, network message handler, request ID isolation, transactional assembly, and marker sync.
--]]

local ADDON_NAME = "CoABotUI"
local PROTOCOL_PREFIX = "COABOT"

CoABotWorldMap = {}
local Map = CoABotWorldMap

-- Transaction states
local TxState = {
    IDLE = 0,
    REQUESTED = 1,
    RECEIVING = 2,
    COMPLETE = 3,
    TIMED_OUT = 4,
    CANCELLED = 5
}
Map.TxState = TxState

local function GetTxTimeout(expectedChunks)
    return math.min(30.0, math.max(4.0, 3.0 + (expectedChunks or 0) * 0.05))
end

-- Isolated Request IDs to prevent cross-channel invalidation
local queryReqId = 1
local detailReqId = 1
local eventsReqId = 1

local activeBots = {}
local activeEvents = {}
local testEventData = nil
local isQueryInFlight = false
local pollTimer = 0

function Map:GetActiveBots()
    return activeBots
end

function Map:GetQueryReqId()
    return queryReqId
end

local function GetCombinedEvents()
    if not testEventData then return activeEvents end
    local list = {}
    for _, ev in ipairs(activeEvents) do
        table.insert(list, ev)
    end
    table.insert(list, testEventData)
    return list
end

function Map:ToggleTestEvent()
    if testEventData then
        testEventData = nil
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFF8000[CoA Map]|r Test encounter removed.")
    else
        local mapName = GetMapInfo()
        local area = mapName and CoABotMapData and CoABotMapData.areas and CoABotMapData.areas[mapName:lower()]
        local wx = 0
        local wy = 0
        if area and area.x1 and area.x2 and area.y1 and area.y2 then
            wx = (area.x1 + area.x2) * 0.5
            wy = (area.y1 + area.y2) * 0.5
        elseif #activeBots > 0 and activeBots[1].worldX then
            wx = activeBots[1].worldX + 60
            wy = activeBots[1].worldY + 60
        end

        testEventData = {
            eventId = 88888,
            eventType = 1, -- Faction Skirmish
            centerX = wx,
            centerY = wy,
            phase = 2, -- Active (In Combat)
            allianceCount = 5,
            hordeCount = 4,
            name = "Alliance & Horde Skirmish (Test Event)"
        }
        DEFAULT_CHAT_FRAME:AddMessage("|cFF00FF00[CoA Map]|r Test encounter spawned on current map. Check world map (M)!")
    end

    if CoABotMapMarkers then
        CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
    end
end

Map.lastMapViewKey = nil
Map.mapChangeDebounce = nil

-- Transactional buffers
local pendingQueryTx = nil
local pendingEventsTx = nil

function Map:GetCurrentMapViewKey()
    local mapName = GetMapInfo()
    local contId = GetCurrentMapContinent and GetCurrentMapContinent() or 0
    local zoneId = GetCurrentMapZone and GetCurrentMapZone() or 0
    local area = mapName and CoABotMapData and CoABotMapData.areas and CoABotMapData.areas[mapName:lower()]
    local areaId = area and area.areaId or 0
    local mapId = area and area.mapId or 0
    return string.format("%d:%d:%d:%s", mapId, contId, zoneId, mapName and mapName:lower() or "")
end

function Map:CancelPendingQuery()
    if pendingQueryTx then
        pendingQueryTx.state = TxState.CANCELLED
        pendingQueryTx = nil
    end
    isQueryInFlight = false
end

-- Cadence intervals per mode (seconds)
local MODE_CADENCE = {
    [0] = 0,    -- OFF (no bot polling)
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

function Map:GetNextQueryReqId()
    queryReqId = queryReqId + 1
    if queryReqId > 2000000000 then queryReqId = 1 end
    return queryReqId
end

function Map:GetNextDetailReqId()
    detailReqId = detailReqId + 1
    if detailReqId > 2000000000 then detailReqId = 1 end
    return detailReqId
end

function Map:GetNextEventsReqId()
    eventsReqId = eventsReqId + 1
    if eventsReqId > 2000000000 then eventsReqId = 1 end
    return eventsReqId
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

    local mapName = GetMapInfo()
    local area = mapName and CoABotMapData and CoABotMapData.areas and CoABotMapData.areas[mapName:lower()]
    local zoneId = GetCurrentMapZone and GetCurrentMapZone() or 0
    local filterAreaId = (zoneId > 0 and area and area.areaId) or 0

    -- 1. Query Bots if mode > 0
    if mode > 0 then
        local reqId = Map:GetNextQueryReqId()
        isQueryInFlight = true
        pendingQueryTx = {
            reqId = reqId,
            state = TxState.REQUESTED,
            startedAt = GetTime(),
            lastActivityAt = GetTime(),
            expectedBots = 0,
            expectedChunks = 0,
            receivedChunks = {},
            receivedChunkCount = 0,
            pins = {}
        }

        local param1 = -1
        local param2 = filterAreaId
        if mode == 1 then
            param1 = CoABotMapSettings:GetRadius()
            param2 = 0
        elseif mode == 2 then
            -- Current Zone: strictly uses player's actual in-game zone (server authority)
            param1 = 0
            param2 = 0
        elseif mode == 3 then
            -- Guild members worldwide (or viewport area if inside a zone)
            param1 = -1
            param2 = filterAreaId
        elseif mode == 4 then
            -- All Allied: query all allied bots on the current continent/map;
            -- client coordinates projection (WorldToMap) precisely clips to visible zone
            param1 = area and area.mapId or 0
            param2 = 0
        end

        SendMessage("MAPQUERY:" .. reqId .. ":" .. mode .. ":" .. param1 .. ":" .. param2)
    else
        Map:ClearBots()
    end

    -- 2. Query World Events independently
    if eventsEnabled then
        local evReqId = Map:GetNextEventsReqId()
        pendingEventsTx = nil

        local mapName = GetMapInfo()
        local area = mapName and CoABotMapData and CoABotMapData.areas and CoABotMapData.areas[mapName:lower()]
        local mapId = area and area.mapId or 0
        SendMessage("MAPEVENTS:" .. evReqId .. ":" .. mapId)
    else
        activeEvents = {}
        if CoABotMapMarkers then
            CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
        end
    end
end

function Map:RequestDetail(botGuidLow)
    if not botGuidLow then return end
    local reqId = Map:GetNextDetailReqId()
    SendMessage("MAPDETAIL:" .. reqId .. ":" .. botGuidLow)
end

function Map:ClearBots()
    activeBots = {}
    pendingQueryTx = nil
    isQueryInFlight = false
    if CoABotMapMarkers then
        CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
    end
end

function Map:Clear()
    activeBots = {}
    pendingQueryTx = nil
    activeEvents = {}
    pendingEventsTx = nil
    isQueryInFlight = false
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
        pendingEventsTx = nil
        if CoABotMapMarkers then
            CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
        end
    else
        pollTimer = 0
        Map:QueryMapData()
    end
end

-- Network response dispatch
function Map:HandleIncomingMessage(body)
    if not body then return end
    local verb, rest = body:match("^([A-Z]+):?(.*)$")
    if not verb then return end

    if verb == "MAPBEGIN" then
        local reqIdStr, totalBots, totalChunks, revision = rest:match("^(%d+):(%d+):(%d+):?(%d*)")
        local reqId = tonumber(reqIdStr) or 0
        if reqId ~= queryReqId then
            return -- Stale or mismatched query
        end

        local now = GetTime()
        pendingQueryTx = {
            reqId = reqId,
            state = TxState.RECEIVING,
            expectedBots = tonumber(totalBots) or 0,
            expectedChunks = tonumber(totalChunks) or 0,
            receivedChunks = {},
            receivedChunkCount = 0,
            pins = {},
            startedAt = now,
            lastActivityAt = now,
            revision = tonumber(revision) or 0
        }

    elseif verb == "MAPBOTS" then
        local reqIdStr, chunkIdxStr, payload = rest:match("^(%d+):(%d+):(.*)$")
        local reqId = tonumber(reqIdStr) or 0
        local chunkIdx = tonumber(chunkIdxStr) or 0

        if not pendingQueryTx or pendingQueryTx.reqId ~= reqId then
            return -- Stale or no active transaction
        end

        pendingQueryTx.lastActivityAt = GetTime()

        if pendingQueryTx.receivedChunks[chunkIdx] then
            return -- Duplicate chunk ignored
        end
        pendingQueryTx.receivedChunks[chunkIdx] = true
        pendingQueryTx.receivedChunkCount = pendingQueryTx.receivedChunkCount + 1

        if payload and payload ~= "" then
            for entry in payload:gmatch("[^|]+") do
                -- Format: <guidLow>,<mapId>,<worldX>,<worldY>,<level>,<classId>,<role>,<flags>,<activity>
                local guid, mid, wx, wy, lvl, cls, role, flags, act =
                    entry:match("^(%d+),(%d+),([%-]?%d+),([%-]?%d+),(%d+),(%d+),(%d+),(%d+),(%d+)")
                if guid then
                    table.insert(pendingQueryTx.pins, {
                        guidLow = tonumber(guid),
                        mapId = tonumber(mid),
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
        if not pendingQueryTx or pendingQueryTx.reqId ~= reqId then
            return -- Stale or mismatched
        end

        local tx = pendingQueryTx
        pendingQueryTx = nil
        isQueryInFlight = false

        -- Strict transactional integrity check
        if tx.receivedChunkCount == tx.expectedChunks and #tx.pins == tx.expectedBots then
            tx.state = TxState.COMPLETE
            activeBots = tx.pins
            if CoABotMapMarkers then
                CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
            end
        else
            -- Chunk loss detected! Discard incomplete snapshot to prevent corrupted display
            tx.state = TxState.TIMED_OUT
        end

    elseif verb == "MAPDETAIL" then
        -- Format: MAPDETAIL:<reqId>:<guid>:<name>:<specId>:<ilvl>:<specName>:<actName>:<tType>:<tItem>:<tTarget>:<tDone>:<tStatus>
        local reqIdStr, guid, name, specId, ilvl, specName, actName, tType, tItem, tTarget, tDone, tStatus =
            rest:match("^(%d+):(%d+):([^:]*):(%d+):(%d+):([^:]*):([^:]*):(%d+):(%d+):(%d+):(%d+):?(.*)$")

        if guid and CoABotMapTooltip then
            local detail = {
                guidLow = tonumber(guid),
                name = name or "",
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
            CoABotMapTooltip.OnDetailReceived(detail.guidLow, detail)
        end

    elseif verb == "MAPEVENTSBEGIN" then
        local reqIdStr, totalEvents, totalChunks = rest:match("^(%d+):(%d+):(%d+)")
        local reqId = tonumber(reqIdStr) or 0
        if reqId ~= eventsReqId then
            return -- Stale
        end

        pendingEventsTx = {
            reqId = reqId,
            expectedEvents = tonumber(totalEvents) or 0,
            expectedChunks = tonumber(totalChunks) or 0,
            receivedChunks = {},
            receivedChunkCount = 0,
            events = {},
            startedAt = GetTime()
        }

    elseif verb == "MAPEVENTS" then
        -- Supports both transactional chunk MAPEVENTS:<reqId>:<chunkIdx>:<payload> and legacy MAPEVENTS:<reqId>:<payload>
        local reqIdStr, chunkIdxStr, payload = rest:match("^(%d+):(%d+):(.*)$")
        if reqIdStr and chunkIdxStr and payload then
            local reqId = tonumber(reqIdStr) or 0
            local chunkIdx = tonumber(chunkIdxStr) or 0

            if pendingEventsTx and pendingEventsTx.reqId == reqId then
                if not pendingEventsTx.receivedChunks[chunkIdx] then
                    pendingEventsTx.receivedChunks[chunkIdx] = true
                    pendingEventsTx.receivedChunkCount = pendingEventsTx.receivedChunkCount + 1

                    for entry in payload:gmatch("[^|]+") do
                        local evId, evType, cx, cy, phase, aCount, hCount, name =
                            entry:match("^(%d+),(%d+),([%-]?%d+),([%-]?%d+),(%d+),(%d+),(%d+),?(.*)$")
                        if evId then
                            table.insert(pendingEventsTx.events, {
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
                return
            end
        end

        -- Direct/legacy fallback format: MAPEVENTS:<reqId>:<payload>
        local legacyReqIdStr, legacyPayload = rest:match("^(%d+):?(.*)$")
        local reqId = tonumber(legacyReqIdStr) or 0
        if reqId == eventsReqId then
            local parsedEvents = {}
            if legacyPayload and legacyPayload ~= "" then
                for entry in legacyPayload:gmatch("[^|]+") do
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
                CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
            end
        end

    elseif verb == "MAPEVENTSEND" then
        local reqIdStr = rest:match("^(%d+)")
        local reqId = tonumber(reqIdStr) or 0
        if not pendingEventsTx or pendingEventsTx.reqId ~= reqId then
            return
        end

        local tx = pendingEventsTx
        pendingEventsTx = nil

        if tx.receivedChunkCount == tx.expectedChunks and #tx.events == tx.expectedEvents then
            activeEvents = tx.events
            if CoABotMapMarkers then
                CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
            end
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
            local newKey = Map:GetCurrentMapViewKey()
            if newKey ~= Map.lastMapViewKey then
                Map.lastMapViewKey = newKey
                -- View changed! Cancel stale query immediately
                Map:CancelPendingQuery()
                -- Update markers immediately with new projection
                if CoABotMapMarkers then
                    CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
                end
                -- Schedule debounced query (150ms) to avoid query storms during fast zoom/pan
                Map.mapChangeDebounce = 0.150
            else
                if CoABotMapMarkers then
                    CoABotMapMarkers:Update(activeBots, GetCombinedEvents())
                end
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

    local now = GetTime()

    -- Sliding-window dynamic transaction timeout
    if pendingQueryTx then
        local timeout = GetTxTimeout(pendingQueryTx.expectedChunks)
        if (now - (pendingQueryTx.lastActivityAt or pendingQueryTx.startedAt)) > timeout then
            pendingQueryTx.state = TxState.TIMED_OUT
            pendingQueryTx = nil
            isQueryInFlight = false
        end
    end
    if pendingEventsTx and (now - (pendingEventsTx.startedAt or now) > 8.0) then
        pendingEventsTx = nil
    end

    -- Handle debounced map change query
    if Map.mapChangeDebounce and Map.mapChangeDebounce > 0 then
        Map.mapChangeDebounce = Map.mapChangeDebounce - elapsed
        if Map.mapChangeDebounce <= 0 then
            Map.mapChangeDebounce = nil
            pollTimer = 0
            Map:QueryMapData()
            return
        end
    end

    local mode = CoABotMapSettings and CoABotMapSettings:GetMode() or 0
    local eventsEnabled = CoABotMapSettings and CoABotMapSettings:GetEventsEnabled() or false

    local interval = 0
    if mode > 0 then
        interval = MODE_CADENCE[mode] or 5.0
    elseif eventsEnabled then
        interval = 5.0 -- Poll events even when bot mode is OFF
    end

    if interval > 0 then
        pollTimer = pollTimer + elapsed
        if pollTimer >= interval then
            -- Backpressure: Do not interrupt active multi-chunk receipt with a new query
            if pendingQueryTx and pendingQueryTx.state == TxState.RECEIVING then
                pollTimer = interval - 1.0
            else
                pollTimer = 0
                Map:QueryMapData()
            end
        end
    end
end)

-- Hook WorldMapFrame OnShow and OnHide
if WorldMapFrame then
    WorldMapFrame:HookScript("OnShow", function()
        if CoABotMapSettings and CoABotMapSettings.CreateWorldMapControls then
            CoABotMapSettings:CreateWorldMapControls()
        end
        if CoABotMapSettings and CoABotMapSettings.UpdateLayout then
            CoABotMapSettings:UpdateLayout()
        end
        Map.lastMapViewKey = Map:GetCurrentMapViewKey()
        Map.mapChangeDebounce = nil
        pollTimer = 0
        Map:QueryMapData()
    end)

    WorldMapFrame:HookScript("OnHide", function()
        pollTimer = 0
        Map.mapChangeDebounce = nil
        Map:CancelPendingQuery()
        if CoABotMapMarkers then
            CoABotMapMarkers:ClearAll()
        end
        if CoABotMapTooltip then
            CoABotMapTooltip.HideTooltip()
        end
    end)

    if WorldMapDetailFrame and WorldMapDetailFrame.HookScript then
        WorldMapDetailFrame:HookScript("OnSizeChanged", function(self, width, height)
            if CoABotMapMarkers and CoABotMapMarkers.RepositionAll then
                CoABotMapMarkers:RepositionAll()
            end
            if CoABotMapSettings and CoABotMapSettings.UpdateLayout then
                CoABotMapSettings:UpdateLayout()
            end
        end)
    end

    if hooksecurefunc then
        pcall(function()
            hooksecurefunc("ToggleWorldMapSize", function()
                if CoABotMapMarkers and CoABotMapMarkers.RepositionAll then
                    CoABotMapMarkers:RepositionAll()
                end
                if CoABotMapSettings and CoABotMapSettings.UpdateLayout then
                    CoABotMapSettings:UpdateLayout()
                end
            end)
        end)
    end
end
