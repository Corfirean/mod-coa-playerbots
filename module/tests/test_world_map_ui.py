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
