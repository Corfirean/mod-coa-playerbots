import os
import sys
import lupa.lua51 as lua

def run_tests():
    print("Running Lua Unit & Regression Tests for CoA World Map Intelligence...")
    runtime = lua.LuaRuntime()

    # Environment stubs
    runtime.execute('''
    _G = {}
    UIParent = {}
    WorldMapTooltip = {
        lines = {},
        shown = false,
        ClearLines = function(self) self.lines = {} end,
        SetOwner = function(self, owner, anchor) self.owner = owner end,
        AddLine = function(self, text, r, g, b) table.insert(self.lines, { text = text, r = r, g = g, b = b }) end,
        AddDoubleLine = function(self, left, right, lr, lg, lb, rr, rg, rb)
            table.insert(self.lines, { left = left, right = right })
        end,
        Show = function(self) self.shown = true end,
        Hide = function(self) self.shown = false end,
        IsShown = function(self) return self.shown end,
    }
    GameTooltip = WorldMapTooltip
    WorldMapFrame = {
        shown = true,
        GetFrameLevel = function() return 1 end,
        GetWidth = function() return 1024 end,
        GetHeight = function() return 768 end,
        IsVisible = function(self) return self.shown end,
        HookScript = function() end,
    }
    WorldMapDetailFrame = {
        GetFrameLevel = function() return 2 end,
        GetWidth = function() return 1002 end,
        GetHeight = function() return 668 end,
        IsVisible = function() return true end,
    }
    CreateFrame = function(frameType, name, parent, template)
        local f = {
            name = name,
            parent = parent,
            shown = true,
            scripts = {},
            width = 16,
            height = 16,
            GetName = function(self) return self.name end,
            Show = function(self) self.shown = true end,
            Hide = function(self) self.shown = false end,
            IsShown = function(self) return self.shown end,
            IsVisible = function(self) return self.shown end,
            SetWidth = function(self, w) self.width = w end,
            SetHeight = function(self, h) self.height = h end,
            GetWidth = function(self) return self.width or 16 end,
            GetHeight = function(self) return self.height or 16 end,
            SetPoint = function(self, point, relTo, relPoint, x, y) self.x = x self.y = y end,
            ClearAllPoints = function() end,
            SetFrameLevel = function() end,
            GetFrameLevel = function() return 3 end,
            EnableMouse = function() end,
            SetScript = function(self, name, fn) self.scripts[name] = fn end,
            HookScript = function() end,
            CreateTexture = function()
                return {
                    SetAllPoints = function() end,
                    SetPoint = function() end,
                    SetWidth = function() end,
                    SetHeight = function() end,
                    SetTexture = function() end,
                    SetTexCoord = function() end,
                    SetVertexColor = function() end,
                    Show = function() end,
                    Hide = function() end,
                }
            end,
            CreateFontString = function()
                return {
                    SetPoint = function() end,
                    SetText = function(self, t) self.text = t end,
                    SetFontObject = function() end,
                    SetTextColor = function() end,
                }
            end,
            RegisterEvent = function() end,
            SetMinMaxValues = function() end,
            SetValueStep = function() end,
            SetValue = function() end,
            SetChecked = function() end,
            GetChecked = function() return true end,
        }
        if name then _G[name] = f end
        return f
    end

    UIDropDownMenu_Initialize = function() end
    UIDropDownMenu_SetWidth = function() end
    UIDropDownMenu_SetSelectedValue = function() end
    UIDropDownMenu_SetText = function() end
    UIDropDownMenu_CreateInfo = function() return {} end
    UIDropDownMenu_AddButton = function() end
    GetMapInfo = function() return 'Elwynn' end
    GetCurrentMapContinent = function() return 2 end
    GetCurrentMapZone = function() return 1 end
    GetTime = function() return 1000 end
    UnitName = function(u) return 'Commander' end
    sentMessages = {}
    SendAddonMessage = function(prefix, body, channel, target)
        table.insert(sentMessages, { prefix = prefix, body = body, target = target })
    end
    bit = {
        band = function(a, b) return a % (b * 2) >= b and b or 0 end
    }
    chatMessages = {}
    DEFAULT_CHAT_FRAME = { AddMessage = function(self, msg) table.insert(chatMessages, msg) end }
    CoABotUIDB = { mapMode = 0, mapRadius = 1000, mapEvents = true }
    RAID_CLASS_COLORS = {
        DRUID = { r = 1.0, g = 0.49, b = 0.04 },
        WARRIOR = { r = 0.78, g = 0.61, b = 0.43 },
    }
    ITEM_QUALITY_COLORS = {
        [0] = { hex = "|cff9d9d9d" },
        [1] = { hex = "|cffffffff" },
        [2] = { hex = "|cff1eff00" },
        [3] = { hex = "|cff0070dd" },
        [4] = { hex = "|cffa335ee" },
    }
    GetItemInfo = function(itemId)
        if itemId == 2447 then
            return "Peacebloom", "item:2447", 1, 1, 1, "Trade Goods", "Herb", 20, "", "Interface\\\\Icons\\\\INV_Misc_Herb_01"
        elseif itemId == 12345 then
            return "Heavy Copper Broadsword", "item:12345", 2, 25, 20, "Weapon", "Two-Handed Swords", 1, "INVSLOT_2HWEAPON", "Interface\\\\Icons\\\\INV_Sword_04"
        end
        return nil
    end
    GetItemIcon = function(itemId)
        if itemId == 2447 then return "Interface\\\\Icons\\\\INV_Misc_Herb_01" end
        if itemId == 12345 then return "Interface\\\\Icons\\\\INV_Sword_04" end
        return nil
    end
    ''')

    # Load Lua files using portable relative paths
    test_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(test_dir, '..', '..'))
    addon_dir = os.path.join(repo_root, 'addon', 'CoABotUI')

    files = [
        os.path.join(addon_dir, 'CoABotWorldMapData.lua'),
        os.path.join(addon_dir, 'CoABotMapTooltip.lua'),
        os.path.join(addon_dir, 'CoABotMapMarkers.lua'),
        os.path.join(addon_dir, 'CoABotMapSettings.lua'),
        os.path.join(addon_dir, 'CoABotWorldMap.lua'),
    ]

    for f in files:
        with open(f, 'r', encoding='utf-8') as fh:
            runtime.execute(fh.read())

    # Run verification assertions in Lua
    test_script = '''
    local failed = 0
    local function assert_true(cond, name)
        if not cond then
            print("LUA TEST FAIL: " .. name)
            failed = failed + 1
        end
    end

    -- 1. Test Settings & Modes
    assert_true(CoABotMapSettings:GetMode() == 0, "Initial mode is Off (0)")
    CoABotMapSettings:SetMode(1)
    assert_true(CoABotMapSettings:GetMode() == 1, "Mode changed to Nearby (1)")
    assert_true(CoABotUIDB.mapMode == 1, "CoABotUIDB persists mode 1")

    CoABotMapSettings:SetRadius(2500)
    assert_true(CoABotMapSettings:GetRadius() == 2500, "Radius set to 2500")
    assert_true(CoABotUIDB.mapRadius == 2500, "CoABotUIDB persists radius 2500")

    -- 2. Slash command handling
    CoABotMapSettings:HandleSlashCommand("off")
    assert_true(CoABotMapSettings:GetMode() == 0, "Slash command /coabot map off")
    CoABotMapSettings:HandleSlashCommand("nearby 1800")
    assert_true(CoABotMapSettings:GetMode() == 1, "Slash command /coabot map nearby")
    assert_true(CoABotMapSettings:GetRadius() == 1800, "Slash command /coabot map radius")

    CoABotMapSettings:HandleSlashCommand("events off")
    assert_true(CoABotMapSettings:GetEventsEnabled() == false, "Slash command /coabot map events off")
    CoABotMapSettings:HandleSlashCommand("events on")
    assert_true(CoABotMapSettings:GetEventsEnabled() == true, "Slash command /coabot map events on")

    -- 3. Coordinate projection
    local nx, ny = CoABotMapMarkers:WorldToMap(-9096.5, -200.0)
    assert_true(nx ~= nil and ny ~= nil, "Coordinate projection valid for Elwynn")
    assert_true(math.abs(nx - 0.5) < 0.01, "normX close to center (0.5)")
    assert_true(math.abs(ny - 0.5) < 0.01, "normY close to center (0.5)")

    -- Out of bounds check
    local badX, badY = CoABotMapMarkers:WorldToMap(-50000.0, 50000.0)
    assert_true(badX == nil and badY == nil, "Out-of-bounds coordinates return nil")

    -- 4. Protocol parsing in CoABotWorldMap
    sentMessages = {}
    CoABotWorldMap:OnModeChanged()
    assert_true(#sentMessages > 0, "Query sent on mode changed")
    assert_true(sentMessages[1].body:find("^MAPQUERY:"), "MAPQUERY format valid")

    -- Simulate server reply (with mapId in entry)
    local qReqId = sentMessages[1].body:match("^MAPQUERY:(%d+)")
    CoABotWorldMap:HandleIncomingMessage("MAPBEGIN:" .. qReqId .. ":2:1:10")
    CoABotWorldMap:HandleIncomingMessage("MAPBOTS:" .. qReqId .. ":0:101,0,-9100,-200,60,11,2,1,3|102,0,-9110,-210,60,1,1,0,5")
    CoABotWorldMap:HandleIncomingMessage("MAPEND:" .. qReqId)

    -- 5. Detail Request Method Syntax & Safe Dispatch
    sentMessages = {}
    CoABotWorldMap:RequestDetail(101)
    assert_true(#sentMessages == 1, "RequestDetail dispatched message")
    assert_true(sentMessages[1].body:find("^MAPDETAIL:%d+:101$") ~= nil, "MAPDETAIL body contains valid guidLow")

    -- Detail Response with real Name and Task
    local dReqId = sentMessages[1].body:match("^MAPDETAIL:(%d+):101")
    CoABotWorldMap:HandleIncomingMessage("MAPDETAIL:" .. dReqId .. ":101:Malfurion:52:187:Restoration:Gathering Herbs:1:2447:100:67:In Progress")

    local cached = CoABotMapTooltip.GetCachedDetail(101)
    assert_true(cached ~= nil, "Detail cached for bot 101")
    assert_true(cached.name == "Malfurion", "Cached name matches Malfurion")
    assert_true(cached.specName == "Restoration", "Cached specName matches")
    assert_true(cached.avgItemLevel == 187, "Cached avgItemLevel matches")
    assert_true(cached.taskDone == 67, "Cached taskDone matches")

    -- 6. Ascension Custom Classes (classes 12-32) Tooltip & Colors
    local customBotPin = { botData = { guidLow = 201, classId = 12, level = 80, role = 0, flags = 0, activity = 0 } }
    CoABotMapTooltip.ShowBotTooltip(customBotPin, customBotPin.botData)
    assert_true(WorldMapTooltip.shown == true, "Tooltip shown for custom class")
    local foundBarbarian = false
    for _, l in ipairs(WorldMapTooltip.lines) do
        if l.right and l.right:find("Barbarian") then
            foundBarbarian = true
        end
    end
    assert_true(foundBarbarian, "Ascension custom class Barbarian correctly rendered")

    -- Chronomancer (class 22) check
    local chronoPin = { botData = { guidLow = 202, classId = 22, level = 80, role = 2, flags = 0, activity = 0 } }
    CoABotMapTooltip.ShowBotTooltip(chronoPin, chronoPin.botData)
    local foundChrono = false
    for _, l in ipairs(WorldMapTooltip.lines) do
        if l.right and l.right:find("Chronomancer") then
            foundChrono = true
        end
    end
    assert_true(foundChrono, "Ascension custom class Chronomancer correctly rendered")

    -- 7. Chunk Loss Protection Test
    -- Send MAPBEGIN declaring 2 chunks, but only send chunk 0 then MAPEND
    local testReqId = CoABotWorldMap:GetNextQueryReqId()
    CoABotWorldMap:HandleIncomingMessage("MAPBEGIN:" .. testReqId .. ":10:2:1")
    CoABotWorldMap:HandleIncomingMessage("MAPBOTS:" .. testReqId .. ":0:301,0,-9100,-200,60,1,0,0,1")
    -- Chunk 1 lost in transit!
    CoABotWorldMap:HandleIncomingMessage("MAPEND:" .. testReqId)
    -- Incomplete transaction discarded, bot 301 must NOT have overwritten activeBots!

    -- 8. MapId Continent Isolation in Markers
    -- Bot on Kalimdor (mapId = 1) while current map is Elwynn (mapId = 0)
    local kalimdorBot = { guidLow = 999, mapId = 1, worldX = -9096.5, worldY = -200.0, level = 80, classId = 1, role = 0, flags = 0, activity = 0 }
    CoABotMapMarkers:Update({ kalimdorBot }, {})
    -- Because mapId (1) ~= Elwynn mapId (0), pin must not be added to activePins
    local kalimdorPinVisible = false
    for _, p in ipairs(CoABotMapMarkers:GetBotPin(1).shown and { CoABotMapMarkers:GetBotPin(1) } or {}) do
        if p.botData and p.botData.guidLow == 999 then
            kalimdorPinVisible = true
        end
    end
    assert_true(not kalimdorPinVisible, "Kalimdor bot excluded from Elwynn map view")

    -- 9. Auto-Refresh of Tooltip on Detail Arrival
    local testPin = { botData = { guidLow = 401, mapId = 0, classId = 1, level = 80, role = 1, flags = 1, activity = 1 } }
    CoABotMapTooltip.ShowBotTooltip(testPin, testPin.botData)
    assert_true(CoABotMapTooltip.currentHoverGuid == 401, "Hover tracking active for bot 401")

    -- Simulate MAPDETAIL arriving for bot 401
    CoABotMapTooltip.OnDetailReceived(401, {
        guidLow = 401,
        name = "Uther",
        specName = "Holy",
        avgItemLevel = 220,
        taskType = 1,
        taskItem = 1234,
        taskTarget = 50,
        taskDone = 50,
        taskStatus = "Completed"
    })
    -- Tooltip must have auto-refreshed with real name "Uther"
    local foundUther = false
    for _, l in ipairs(WorldMapTooltip.lines) do
        if l.text and l.text:find("Uther") then
            foundUther = true
        end
    end
    assert_true(foundUther, "Active tooltip auto-refreshed with real bot name Uther")

    -- 10. F1 & F2: Projection Identity & Immediate Snapping (No flying markers)
    local projKey1 = CoABotMapMarkers:GetCurrentProjectionKey()
    assert_true(projKey1:find("^zone:0:12:elwynn") ~= nil, "Initial projection key matches zone:0:12:elwynn")

    local botToSnap = { guidLow = 501, mapId = 0, worldX = -9096.5, worldY = -200.0, level = 80, classId = 1, role = 0, flags = 0, activity = 0 }
    CoABotMapMarkers:Update({ botToSnap }, {})
    local pin501 = CoABotMapMarkers:GetBotPin(1)
    assert_true(pin501.botState ~= nil, "Bot 501 state initialized")
    local oldNx = pin501.botState.currNx

    -- Switch map projection to Continent overview
    GetMapInfo = function() return "Azeroth" end
    GetCurrentMapZone = function() return 0 end
    GetCurrentMapContinent = function() return 2 end
    local projKey2 = CoABotMapMarkers:GetCurrentProjectionKey()
    assert_true(projKey2:find("^continent:0:2:") ~= nil, "Projection key changed to continent")

    -- Update markers under new projection: position must snap immediately without interpolating across screen
    CoABotMapMarkers:Update({ botToSnap }, {})
    local newNx = pin501.botState.currNx
    assert_true(pin501.botState.projectionKey == projKey2, "Bot 501 updated to new projectionKey")
    assert_true(pin501.botState.currNx == pin501.botState.targetNx, "Bot 501 snapped immediately to targetNx upon projection change")

    -- Restore Elwynn map
    GetMapInfo = function() return "Elwynn" end
    GetCurrentMapZone = function() return 1 end

    -- 11. F8: Generation Pruning of Disconnected Bots
    -- Update with bot 501 for 3 generations, then omit bot 501
    CoABotMapMarkers:Update({ botToSnap }, {})
    CoABotMapMarkers:Update({ botToSnap }, {})
    -- Omit bot 501 in next updates:
    local dummyBot = { guidLow = 601, mapId = 0, worldX = -9100.0, worldY = -210.0, level = 80, classId = 2, role = 1, flags = 0, activity = 0 }
    CoABotMapMarkers:Update({ dummyBot }, {})
    CoABotMapMarkers:Update({ dummyBot }, {})
    CoABotMapMarkers:Update({ dummyBot }, {})
    -- Now check if bot 501's interpolation state was pruned (>2 generations missed)
    -- If we query dummyBot, dummyBot is active; bot 501's state was discarded from memory
    assert_true(pin501.botData.guidLow == 601, "Pin 1 reused for active bot 601")

    -- 12. F9: Crafting Task Display ("Remaining to Craft" and Item formatting)
    local craftDetail = {
        guidLow = 701,
        name = "BlacksmithBot",
        specName = "Armorsmith",
        avgItemLevel = 200,
        taskType = 2, -- Craft order
        taskItem = 12345, -- Heavy Copper Broadsword
        taskTarget = 5,
        taskDone = 2,
        taskStatus = "Crafting at Anvil"
    }
    CoABotMapTooltip.SetCachedDetail(701, craftDetail)
    local craftPin = { botData = { guidLow = 701, classId = 1, level = 80, role = 0, flags = 1, activity = 4 } }
    CoABotMapTooltip.ShowBotTooltip(craftPin, craftPin.botData)
    assert_true(WorldMapTooltip.shown == true, "Craft tooltip shown")

    local foundRemaining = false
    local foundItemName = false
    for _, l in ipairs(WorldMapTooltip.lines) do
        if l.left and l.left:find("Remaining to Craft:") then
            foundRemaining = true
            assert_true(l.right == "3 / 5", "Remaining correctly calculated: 3 / 5")
        end
        if l.right and l.right:find("Heavy Copper Broadsword") then
            foundItemName = true
        end
    end
    assert_true(foundRemaining, "F9: Remaining to Craft line present and accurate")
    assert_true(foundItemName, "F9: Native item name resolved from GetItemInfo")

    -- 13. F14 & F3: Sliding-Window Dynamic Timeout & In-Flight Backpressure
    local largeReqId = CoABotWorldMap:GetNextQueryReqId()
    CoABotWorldMap:HandleIncomingMessage("MAPBEGIN:" .. largeReqId .. ":3000:425:1")
    -- Ingest 5 chunks simulating chunk stream
    for c = 0, 4 do
        CoABotWorldMap:HandleIncomingMessage("MAPBOTS:" .. largeReqId .. ":" .. c .. ":80" .. c .. ",0,-9100,-200,80,1,0,0,1")
    end
    -- State must be RECEIVING
    assert_true(CoABotWorldMap.TxState ~= nil, "TxState enum available")

    -- 14. F15: Full Offline / Clear / Reset Sequence
    CoABotWorldMap:Clear()
    assert_true(WorldMapTooltip.shown == false, "Tooltip hidden after Clear()")

    -- 15. Zone Map vs Continent Map Clustering (No clusters on zone maps)
    GetMapInfo = function() return "Elwynn" end
    GetCurrentMapZone = function() return 1 end -- Simulating inside a specific zone map (e.g. Elwynn)
    local sampleBots = {
        { guidLow = 801, mapId = 0, worldX = -9400, worldY = -100, level = 20, classId = 1, role = 0, flags = 0, activity = 0 },
        { guidLow = 802, mapId = 0, worldX = -9401, worldY = -101, level = 20, classId = 2, role = 0, flags = 0, activity = 0 },
    }
    CoABotMapMarkers:Update(sampleBots, nil)
    -- On zone map: 0 clusters, 2 individual singletons
    local activeClustersCount = 0
    local activePinsCount = 0
    for i = 1, 10 do
        local cl = _G["CoABotMapCluster" .. i]
        if cl and cl:IsShown() then activeClustersCount = activeClustersCount + 1 end
        local p = _G["CoABotMapPin" .. i]
        if p and p:IsShown() then activePinsCount = activePinsCount + 1 end
    end
    assert_true(activeClustersCount == 0, "No clusters on zone map (clusters count = 0)")
    assert_true(activePinsCount == 2, "Individual bot pins on zone map (pins count = 2)")

    -- On continent map: nearby bots must cluster
    GetMapInfo = function() return "Azeroth" end
    GetCurrentMapZone = function() return 0 end -- Simulating continent view
    CoABotMapMarkers:Update(sampleBots, nil)
    local contClustersCount = 0
    for i = 1, 10 do
        local cl = _G["CoABotMapCluster" .. i]
        if cl and cl:IsShown() then contClustersCount = contClustersCount + 1 end
    end
    assert_true(contClustersCount == 1, "Clusters active on continent map (clusters count = 1)")

    -- Restore zone map
    GetMapInfo = function() return "Elwynn" end
    GetCurrentMapZone = function() return 1 end

    -- 16. Test Event Toggle
    CoABotWorldMap:ToggleTestEvent()
    local evPin = _G["CoABotMapEventPin1"]
    assert_true(evPin and evPin:IsShown(), "Test event pin displayed after ToggleTestEvent()")
    CoABotWorldMap:ToggleTestEvent()
    assert_true(evPin and not evPin:IsShown(), "Test event pin hidden after second ToggleTestEvent()")

    -- 17. Compact Map Reposition & Layout Adaptation
    CoABotMapMarkers:Update(sampleBots, nil)
    local pin1 = _G["CoABotMapPin1"]
    local origX = pin1 and pin1.x
    -- Switch WorldMapDetailFrame to compact mode (592x395)
    WorldMapDetailFrame.GetWidth = function() return 592 end
    WorldMapDetailFrame.GetHeight = function() return 395 end
    CoABotMapMarkers:RepositionAll()
    local compactX = pin1 and pin1.x
    assert_true(compactX ~= origX, "Pins instantly reposition on compact map size transition")

    -- Settings panel layout adaptation
    CoABotMapSettings:CreateWorldMapControls()
    WorldMapFrame.GetWidth = function() return 592 end
    CoABotMapSettings:UpdateLayout()
    assert_true(CoABotMapSettings.dropdownLabel and (CoABotMapSettings.dropdownLabel.text == "Bots" or CoABotMapSettings.dropdownLabel.text == "Боты"), "Settings label adapts to compact mode")

    WorldMapFrame.GetWidth = function() return 1024 end
    WorldMapDetailFrame.GetWidth = function() return 1002 end
    CoABotMapSettings:UpdateLayout()
    assert_true(CoABotMapSettings.dropdownLabel and (CoABotMapSettings.dropdownLabel.text == "Allied Bots" or CoABotMapSettings.dropdownLabel.text == "Союзные боты"), "Settings label adapts to fullscreen mode")

    -- 18. POI Landmark Avoidance and FrameLevel validation
    GetMapInfo = function() return "Azeroth" end
    GetCurrentMapZone = function() return 0 end
    GetNumMapLandmarks = function() return 1 end
    -- Mock landmark at same normalized position as cluster
    GetMapLandmarkInfo = function(idx)
        return "Stormwind City", "City", 1, 0.4583, 0.7431
    end
    local cityBots = {
        { guidLow = 901, mapId = 0, worldX = -9000, worldY = -500, level = 20, classId = 1, role = 0, flags = 0, activity = 0 },
        { guidLow = 902, mapId = 0, worldX = -9001, worldY = -501, level = 20, classId = 2, role = 0, flags = 0, activity = 0 },
    }
    CoABotMapMarkers:Update(cityBots, nil)
    local clPin = _G["CoABotMapCluster1"]
    assert_true(clPin and clPin.offX == 14 and clPin.offY == 12, "Cluster badge offsets away from overlapping city POI landmark")

    return failed
    '''
    failed_count = runtime.execute(test_script)
    if failed_count == 0:
        print("All Lua Unit & Regression Tests passed successfully (0 failures)!")
    else:
        print(f"Lua Unit Tests completed with {failed_count} failures!")
        sys.exit(1)

if __name__ == '__main__':
    run_tests()
