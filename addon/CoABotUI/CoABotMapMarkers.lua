--[[
    CoA World Map Intelligence -- Map Markers & Clustering
    Marker pools, Blizzard native pins, coordinate projection, identity-bound smooth interpolation,
    spatial-hash clustering, and mapId continent validation.
--]]

CoABotMapMarkers = {}
local Markers = CoABotMapMarkers

local pinPool = {}
local clusterPool = {}
local eventPool = {}
local activePins = {}
local activeClusters = {}
local activeEvents = {}

-- Identity-bound interpolation state: [guidLow] = { currNx, currNy, targetNx, targetNy, mapId, lastUpdate }
local botPositionState = {}

local CLUSTER_RADIUS_PIXELS = 22
local INTERPOLATION_SPEED = 3.0 -- fraction per second

-- World-to-Map coordinate transform
function Markers:WorldToMap(worldX, worldY)
    if not CoABotMapData or not CoABotMapData.areas then
        return nil, nil
    end

    local mapName = GetMapInfo()
    local area = mapName and CoABotMapData.areas[mapName:lower()]

    -- Fallback to continent if zone not found or mapName is continent
    if not area then
        local contId = GetCurrentMapContinent and GetCurrentMapContinent()
        local contKey = contId and CoABotMapData.continents and CoABotMapData.continents[contId]
        if contKey then
            area = CoABotMapData.areas[contKey]
        end
    end

    if not area or not area.y1 or not area.y2 or not area.x1 or not area.x2 then
        return nil, nil
    end

    local dy = area.y2 - area.y1
    local dx = area.x2 - area.x1
    if math.abs(dy) < 0.001 or math.abs(dx) < 0.001 then
        return nil, nil
    end

    -- In WoW engine:
    -- normX (horizontal U, 0..1) = (worldY - y1) / (y2 - y1)
    -- normY (vertical V, 0..1)   = (worldX - x1) / (x2 - x1)
    local normX = (worldY - area.y1) / dy
    local normY = (worldX - area.x1) / dx

    if normX < 0.0 or normX > 1.0 or normY < 0.0 or normY > 1.0 then
        return nil, nil
    end

    return normX, normY
end

-- Pin creation / pooling
local function CreateBotPin(index)
    local parent = WorldMapDetailFrame or WorldMapButton or UIParent
    local pin = CreateFrame("Button", "CoABotMapPin" .. index, parent)
    pin:SetWidth(16)
    pin:SetHeight(16)
    pin:EnableMouse(true)
    pin:SetFrameLevel(parent:GetFrameLevel() + 5)

    -- Background icon (Blizzard native PartyRaidBlips)
    local icon = pin:CreateTexture(nil, "BACKGROUND")
    icon:SetAllPoints()
    icon:SetTexture("Interface\\Minimap\\PartyRaidBlips")
    icon:SetTexCoord(0.0, 0.125, 0.0, 0.125)
    pin.icon = icon

    -- Highlight glow / Guild ring
    local border = pin:CreateTexture(nil, "OVERLAY")
    border:SetPoint("CENTER", pin, "CENTER", 0, 0)
    border:SetWidth(20)
    border:SetHeight(20)
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
    pin.border = border

    pin:SetScript("OnEnter", function(self)
        if self.botData and CoABotMapTooltip then
            CoABotMapTooltip.ShowBotTooltip(self, self.botData)
        end
    end)
    pin:SetScript("OnLeave", function(self)
        if CoABotMapTooltip then
            CoABotMapTooltip.HideTooltip()
        end
    end)

    return pin
end

local function CreateClusterPin(index)
    local parent = WorldMapDetailFrame or WorldMapButton or UIParent
    local cluster = CreateFrame("Button", "CoABotMapCluster" .. index, parent)
    cluster:SetWidth(24)
    cluster:SetHeight(24)
    cluster:EnableMouse(true)
    cluster:SetFrameLevel(parent:GetFrameLevel() + 6)

    local bg = cluster:CreateTexture(nil, "BACKGROUND")
    bg:SetPoint("CENTER", cluster, "CENTER", 0, 0)
    bg:SetWidth(22)
    bg:SetHeight(22)
    bg:SetTexture("Interface\\Minimap\\UI-Minimap-Background")
    bg:SetVertexColor(0.1, 0.1, 0.2, 0.9)
    cluster.bg = bg

    local border = cluster:CreateTexture(nil, "BORDER")
    border:SetPoint("CENTER", cluster, "CENTER", 0, 0)
    border:SetWidth(28)
    border:SetHeight(28)
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
    cluster.border = border

    local countText = cluster:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    countText:SetPoint("CENTER", cluster, "CENTER", 0, 0)
    cluster.countText = countText

    cluster:SetScript("OnEnter", function(self)
        if self.botList and CoABotMapTooltip then
            CoABotMapTooltip.ShowClusterTooltip(self, self.botList)
        end
    end)
    cluster:SetScript("OnLeave", function(self)
        if CoABotMapTooltip then
            CoABotMapTooltip.HideTooltip()
        end
    end)

    return cluster
end

local function CreateEventPin(index)
    local parent = WorldMapDetailFrame or WorldMapButton or UIParent
    local pin = CreateFrame("Button", "CoABotMapEventPin" .. index, parent)
    pin:SetWidth(26)
    pin:SetHeight(26)
    pin:EnableMouse(true)
    pin:SetFrameLevel(parent:GetFrameLevel() + 7)

    local icon = pin:CreateTexture(nil, "ARTWORK")
    icon:SetAllPoints()
    icon:SetTexture("Interface\\Icons\\Ability_DualWield")
    pin.icon = icon

    local border = pin:CreateTexture(nil, "OVERLAY")
    border:SetPoint("CENTER", pin, "CENTER", 0, 0)
    border:SetWidth(32)
    border:SetHeight(32)
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
    border:SetVertexColor(1, 0.2, 0.2, 1)
    pin.border = border

    pin:SetScript("OnEnter", function(self)
        if self.eventData and CoABotMapTooltip then
            CoABotMapTooltip.ShowEventTooltip(self, self.eventData)
        end
    end)
    pin:SetScript("OnLeave", function(self)
        if CoABotMapTooltip then
            CoABotMapTooltip.HideTooltip()
        end
    end)

    return pin
end

function Markers:GetBotPin(index)
    if not pinPool[index] then
        pinPool[index] = CreateBotPin(index)
    end
    return pinPool[index]
end

function Markers:GetClusterPin(index)
    if not clusterPool[index] then
        clusterPool[index] = CreateClusterPin(index)
    end
    return clusterPool[index]
end

function Markers:GetEventPin(index)
    if not eventPool[index] then
        eventPool[index] = CreateEventPin(index)
    end
    return eventPool[index]
end

function Markers:ClearAll()
    for _, pin in ipairs(pinPool) do pin:Hide() end
    for _, cl in ipairs(clusterPool) do cl:Hide() end
    for _, ev in ipairs(eventPool) do ev:Hide() end
    activePins = {}
    activeClusters = {}
    activeEvents = {}
end

-- Spatial Grid Clustering: O(N) spatial hash
local function ClusterPointsSpatial(projectedList, clusterRadius)
    local clusters = {}
    local singletons = {}

    local cellSize = clusterRadius
    local grid = {} -- [cellKey] = { item1, item2, ... }

    local function MakeKey(cx, cy)
        return cx .. ":" .. cy
    end

    for _, p in ipairs(projectedList) do
        local cx = math.floor(p.px / cellSize)
        local cy = math.floor(p.py / cellSize)
        local key = MakeKey(cx, cy)
        if not grid[key] then grid[key] = {} end
        table.insert(grid[key], p)
    end

    local radiusSq = clusterRadius * clusterRadius

    for _, p1 in ipairs(projectedList) do
        if not p1.clustered then
            local group = { p1 }
            local cx = math.floor(p1.px / cellSize)
            local cy = math.floor(p1.py / cellSize)

            -- Search 3x3 neighboring cells
            for dx = -1, 1 do
                for dy = -1, 1 do
                    local nkey = MakeKey(cx + dx, cy + dy)
                    local cell = grid[nkey]
                    if cell then
                        for _, p2 in ipairs(cell) do
                            if p2 ~= p1 and not p2.clustered then
                                local distSq = (p1.px - p2.px)^2 + (p1.py - p2.py)^2
                                if distSq <= radiusSq then
                                    p2.clustered = true
                                    table.insert(group, p2)
                                end
                            end
                        end
                    end
                end
            end

            if #group > 1 then
                p1.clustered = true
                local avgNx, avgNy = 0, 0
                local botObjects = {}
                local hasGuild = false
                for _, m in ipairs(group) do
                    avgNx = avgNx + m.nx
                    avgNy = avgNy + m.ny
                    table.insert(botObjects, m.bot)
                    if bit.band(m.bot.flags or 0, 1) ~= 0 then
                        hasGuild = true
                    end
                end
                table.insert(clusters, {
                    nx = avgNx / #group,
                    ny = avgNy / #group,
                    bots = botObjects,
                    hasGuild = hasGuild
                })
            else
                table.insert(singletons, p1)
            end
        end
    end

    return clusters, singletons
end

-- Clustering & Rendering update
function Markers:Update(botList, eventList)
    Markers:ClearAll()

    local parent = WorldMapDetailFrame or WorldMapButton
    if not parent or not parent:IsVisible() then
        return
    end

    local mapWidth = parent:GetWidth() or 1002
    local mapHeight = parent:GetHeight() or 668
    if mapWidth <= 0 or mapHeight <= 0 then
        return
    end

    -- Identify current map area and mapId
    local mapName = GetMapInfo()
    local area = mapName and CoABotMapData and CoABotMapData.areas and CoABotMapData.areas[mapName:lower()]
    local currentMapId = area and area.mapId

    -- 1. Project valid bots to screen space with mapId continent validation
    local projected = {}
    if botList then
        for _, b in ipairs(botList) do
            -- Strict Map ID Validation: if bot is on a different map/continent, do not render on this map!
            if currentMapId == nil or b.mapId == nil or b.mapId == currentMapId then
                local nx, ny = Markers:WorldToMap(b.worldX, b.worldY)
                if nx and ny then
                    table.insert(projected, {
                        bot = b,
                        nx = nx,
                        ny = ny,
                        px = nx * mapWidth,
                        py = ny * mapHeight,
                        clustered = false
                    })
                end
            end
        end
    end

    -- 2. Cluster dense bots via Spatial Grid
    local clusters, singletons = ClusterPointsSpatial(projected, CLUSTER_RADIUS_PIXELS)

    -- 3. Position Cluster Pins
    for idx, cl in ipairs(clusters) do
        local pin = Markers:GetClusterPin(idx)
        pin.botList = cl.bots
        pin.countText:SetText(tostring(#cl.bots))
        if cl.hasGuild then
            pin.border:SetVertexColor(1.0, 0.82, 0.0, 1.0) -- Gold border
            pin.bg:SetVertexColor(0.2, 0.15, 0.05, 0.95)
        else
            pin.border:SetVertexColor(0.8, 0.8, 0.8, 0.8)
            pin.bg:SetVertexColor(0.1, 0.1, 0.2, 0.9)
        end
        pin:ClearAllPoints()
        pin:SetPoint("CENTER", parent, "TOPLEFT", cl.nx * mapWidth, -cl.ny * mapHeight)
        pin:Show()
        table.insert(activeClusters, pin)
    end

    -- 4. Position Singleton Pins with identity-bound interpolation
    local now = GetTime()
    for idx, item in ipairs(singletons) do
        local pin = Markers:GetBotPin(idx)
        pin.botData = item.bot

        -- Bind interpolation state to character identity (guidLow), decoupled from recycled frame
        local guidLow = item.bot.guidLow
        local st = botPositionState[guidLow]
        if not st or st.mapId ~= item.bot.mapId then
            st = {
                currNx = item.nx,
                currNy = item.ny,
                targetNx = item.nx,
                targetNy = item.ny,
                mapId = item.bot.mapId,
                lastUpdate = now
            }
            botPositionState[guidLow] = st
        else
            st.targetNx = item.nx
            st.targetNy = item.ny
            st.lastUpdate = now

            -- Snap immediately if jumped across map (> 0.2 fraction) or teleported
            local dist = math.sqrt((st.currNx - item.nx)^2 + (st.currNy - item.ny)^2)
            if dist > 0.2 then
                st.currNx = item.nx
                st.currNy = item.ny
            end
        end
        pin.botState = st

        -- Blizzard native color styling
        local r, g, b = 1.0, 1.0, 1.0
        if CoABotMapTooltip and CoABotMapTooltip.GetClassColor then
            r, g, b = CoABotMapTooltip.GetClassColor(item.bot.classId)
        end
        pin.icon:SetVertexColor(r, g, b, 1.0)

        local isGuild = bit.band(item.bot.flags or 0, 1) ~= 0
        if isGuild then
            pin.border:SetVertexColor(1.0, 0.82, 0.0, 1.0) -- Gold ring
            pin.border:Show()
        else
            pin.border:Hide()
        end

        pin:ClearAllPoints()
        pin:SetPoint("CENTER", parent, "TOPLEFT", st.currNx * mapWidth, -st.currNy * mapHeight)
        pin:Show()
        table.insert(activePins, pin)
    end

    -- 5. Position Event Pins
    if eventList then
        local evIdx = 1
        for _, ev in ipairs(eventList) do
            local enx, eny = Markers:WorldToMap(ev.centerX, ev.centerY)
            if enx and eny then
                local pin = Markers:GetEventPin(evIdx)
                pin.eventData = ev
                pin:ClearAllPoints()
                pin:SetPoint("CENTER", parent, "TOPLEFT", enx * mapWidth, -eny * mapHeight)
                pin:Show()
                table.insert(activeEvents, pin)
                evIdx = evIdx + 1
            end
        end
    end
end

-- Interpolation updater
local interpolator = CreateFrame("Frame")
interpolator:SetScript("OnUpdate", function(self, elapsed)
    if #activePins == 0 then return end
    local parent = WorldMapDetailFrame or WorldMapButton
    if not parent or not parent:IsVisible() then return end

    local mapWidth = parent:GetWidth() or 1002
    local mapHeight = parent:GetHeight() or 668
    local step = elapsed * INTERPOLATION_SPEED

    for _, pin in ipairs(activePins) do
        local st = pin.botState
        if st and st.targetNx and st.currNx then
            if math.abs(st.currNx - st.targetNx) > 0.0005 or math.abs(st.currNy - st.targetNy) > 0.0005 then
                st.currNx = st.currNx + (st.targetNx - st.currNx) * math.min(1, step)
                st.currNy = st.currNy + (st.targetNy - st.currNy) * math.min(1, step)
                pin:ClearAllPoints()
                pin:SetPoint("CENTER", parent, "TOPLEFT", st.currNx * mapWidth, -st.currNy * mapHeight)
            end
        end
    end
end)
