import sys
import lupa.lua51 as lua

def run_tests():
    print("Running Lua Unit Tests for CoA World Map Intelligence...")
    runtime = lua.LuaRuntime()

    # Environment stubs
    runtime.execute('''
    _G = {}
    UIParent = {}
    WorldMapTooltip = {
        lines = {},
        ClearLines = function(self) self.lines = {} end,
        SetOwner = function(self, owner, anchor) self.owner = owner end,
        AddLine = function(self, text, r, g, b) table.insert(self.lines, { text = text, r = r, g = g, b = b }) end,
        AddDoubleLine = function(self, left, right, lr, lg, lb, rr, rg, rb)
            table.insert(self.lines, { left = left, right = right })
        end,
        Show = function(self) self.shown = true end,
        Hide = function(self) self.shown = false end,
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

    files = [
        r'C:\games\source\world-map-intelligence\addon\CoABotUI\CoABotWorldMapData.lua',
        r'C:\games\source\world-map-intelligence\addon\CoABotUI\CoABotMapTooltip.lua',
        r'C:\games\source\world-map-intelligence\addon\CoABotUI\CoABotMapMarkers.lua',
        r'C:\games\source\world-map-intelligence\addon\CoABotUI\CoABotMapSettings.lua',
        r'C:\games\source\world-map-intelligence\addon\CoABotUI\CoABotWorldMap.lua',
    ]

    for f in files:
        content = open(f, encoding='utf-8').read()
        runtime.execute(content)

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

    -- Simulate server reply
    local reqId = sentMessages[1].body:match("^MAPQUERY:(%d+)")
    CoABotWorldMap:HandleIncomingMessage("MAPBEGIN:" .. reqId .. ":2:1:10")
    CoABotWorldMap:HandleIncomingMessage("MAPBOTS:" .. reqId .. ":0:101,-9100,-200,60,11,2,1,3|102,-9110,-210,60,1,1,0,5")
    CoABotWorldMap:HandleIncomingMessage("MAPEND:" .. reqId)

    -- Stale reqId rejection
    CoABotWorldMap:HandleIncomingMessage("MAPBEGIN:1:50:5:1") -- Old reqId
    -- should be safely ignored without breaking active bots

    -- 5. Detail & Tooltip
    CoABotWorldMap:HandleIncomingMessage("MAPDETAIL:" .. reqId .. ":101:52:187:Restoration:Gathering Herbs:1:2447:100:67:In Progress")
    local cached = CoABotMapTooltip.GetCachedDetail(101)
    assert_true(cached ~= nil, "Detail cached for bot 101")
    assert_true(cached.specName == "Restoration", "Cached specName matches")
    assert_true(cached.avgItemLevel == 187, "Cached avgItemLevel matches")
    assert_true(cached.taskDone == 67, "Cached taskDone matches")

    local dummyPin = { botData = { guidLow = 101, classId = 11, level = 60, role = 2, flags = 1, activity = 3 } }
    CoABotMapTooltip.ShowBotTooltip(dummyPin, dummyPin.botData)
    assert_true(WorldMapTooltip.shown == true, "Tooltip shown")
    assert_true(#WorldMapTooltip.lines > 3, "Tooltip has lines populated")

    return failed
    '''
    failed_count = runtime.execute(test_script)
    if failed_count == 0:
        print("All Lua Unit Tests passed successfully (0 failures)!")
    else:
        print(f"Lua Unit Tests completed with {failed_count} failures!")
        sys.exit(1)

if __name__ == '__main__':
    run_tests()
