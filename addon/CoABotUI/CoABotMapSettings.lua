--[[
    CoA World Map Intelligence -- Settings & Native Blizzard UI Controls
    Dropdown, Radius Slider, Events Checkbox on WorldMapFrame, and Slash Commands.
--]]

CoABotMapSettings = {}
local Settings = CoABotMapSettings

local MODES = {
    [0] = { id = 0, name = "Off" },
    [1] = { id = 1, name = "Nearby" },
    [2] = { id = 2, name = "Current Zone" },
    [3] = { id = 3, name = "Guild Only" },
    [4] = { id = 4, name = "All Allied" },
}

local settingsPanel
local modeDropdown
local radiusSlider
local eventsCheckbox
local debounceTimer = 0
local pendingRadius = nil

function Settings:GetMode()
    if not CoABotUIDB then return 0 end
    return CoABotUIDB.mapMode or 0
end

function Settings:SetMode(modeId)
    if not CoABotUIDB then return end
    modeId = tonumber(modeId) or 0
    if modeId < 0 or modeId > 4 then modeId = 0 end
    CoABotUIDB.mapMode = modeId

    if modeDropdown then
        UIDropDownMenu_SetSelectedValue(modeDropdown, modeId)
        UIDropDownMenu_SetText(modeDropdown, MODES[modeId].name)
    end

    if radiusSlider then
        if modeId == 1 then
            radiusSlider:Show()
        else
            radiusSlider:Hide()
        end
    end

    if CoABotWorldMap then
        CoABotWorldMap:OnModeChanged()
    end
end

function Settings:GetRadius()
    if not CoABotUIDB then return 1000 end
    return CoABotUIDB.mapRadius or 1000
end

function Settings:SetRadius(radius)
    if not CoABotUIDB then return end
    radius = math.max(100, math.min(5000, tonumber(radius) or 1000))
    CoABotUIDB.mapRadius = radius
    if radiusSlider and radiusSlider.text then
        radiusSlider.text:SetText("Radius: " .. tostring(radius) .. " yd")
    end
    if CoABotWorldMap then
        CoABotWorldMap:OnRadiusChanged()
    end
end

function Settings:GetEventsEnabled()
    if not CoABotUIDB then return true end
    if CoABotUIDB.mapEvents == nil then return true end
    return CoABotUIDB.mapEvents
end

function Settings:SetEventsEnabled(enabled)
    if not CoABotUIDB then return end
    CoABotUIDB.mapEvents = (enabled == true)
    if eventsCheckbox then
        eventsCheckbox:SetChecked(CoABotUIDB.mapEvents)
    end
    if CoABotWorldMap then
        CoABotWorldMap:OnEventsToggled()
    end
end

-- Initialize dropdown menu items
local function InitializeModeDropdown(self, level)
    local currentMode = Settings:GetMode()
    for id = 0, 4 do
        local info = UIDropDownMenu_CreateInfo()
        info.text = MODES[id].name
        info.value = id
        info.func = function(btn)
            Settings:SetMode(btn.value)
        end
        info.checked = (currentMode == id)
        UIDropDownMenu_AddButton(info, level)
    end
end

-- Create UI components on WorldMapFrame
function Settings:CreateWorldMapControls()
    if settingsPanel or not WorldMapFrame then return end

    -- Container frame anchored to WorldMapFrame top/right controls
    settingsPanel = CreateFrame("Frame", "CoABotWorldMapSettingsPanel", WorldMapFrame)
    settingsPanel:SetWidth(320)
    settingsPanel:SetHeight(32)
    settingsPanel:SetPoint("TOPRIGHT", WorldMapFrame, "TOPRIGHT", -50, -36)
    settingsPanel:SetFrameLevel(WorldMapFrame:GetFrameLevel() + 5)

    -- DropDown Menu
    modeDropdown = CreateFrame("Frame", "CoABotMapModeDropDown", settingsPanel, "UIDropDownMenuTemplate")
    modeDropdown:SetPoint("LEFT", settingsPanel, "LEFT", -15, 0)
    UIDropDownMenu_SetWidth(modeDropdown, 110)
    UIDropDownMenu_Initialize(modeDropdown, InitializeModeDropdown)
    UIDropDownMenu_SetSelectedValue(modeDropdown, Settings:GetMode())
    UIDropDownMenu_SetText(modeDropdown, MODES[Settings:GetMode()].name)

    -- DropDown Title Label (native Blizzard header style matching Zone/Continent dropdowns)
    local dropdownLabel = modeDropdown:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    dropdownLabel:SetPoint("BOTTOMLEFT", modeDropdown, "TOPLEFT", 20, 2)
    local isRu = (GetLocale and GetLocale() == "ruRU")
    dropdownLabel:SetText(isRu and "Союзные боты" or "Allied Bots")

    -- Events Checkbox
    eventsCheckbox = CreateFrame("CheckButton", "CoABotMapEventsCheck", settingsPanel, "UICheckButtonTemplate")
    eventsCheckbox:SetPoint("LEFT", modeDropdown, "RIGHT", -10, 0)
    eventsCheckbox:SetWidth(24)
    eventsCheckbox:SetHeight(24)
    local cbText = _G[eventsCheckbox:GetName() .. "Text"]
    if cbText then
        cbText:SetText("Events")
        cbText:SetFontObject("GameFontNormalSmall")
    end
    eventsCheckbox:SetChecked(Settings:GetEventsEnabled())
    eventsCheckbox:SetScript("OnClick", function(self)
        Settings:SetEventsEnabled(self:GetChecked() == 1 or self:GetChecked() == true)
    end)

    -- Nearby Radius Slider (shown only when Nearby mode is active)
    radiusSlider = CreateFrame("Slider", "CoABotMapRadiusSlider", settingsPanel, "OptionsSliderTemplate")
    radiusSlider:SetPoint("TOP", modeDropdown, "BOTTOM", 15, -4)
    radiusSlider:SetWidth(120)
    radiusSlider:SetHeight(14)
    radiusSlider:SetMinMaxValues(100, 5000)
    radiusSlider:SetValueStep(100)
    radiusSlider:SetValue(Settings:GetRadius())

    local lowText = _G[radiusSlider:GetName() .. "Low"]
    local highText = _G[radiusSlider:GetName() .. "High"]
    local sliderTitle = _G[radiusSlider:GetName() .. "Text"]
    if lowText then lowText:SetText("100") end
    if highText then highText:SetText("5000") end
    if sliderTitle then
        sliderTitle:SetText("Radius: " .. tostring(Settings:GetRadius()) .. " yd")
        radiusSlider.text = sliderTitle
    end

    radiusSlider:SetScript("OnValueChanged", function(self, value)
        local val = math.floor((value + 50) / 100) * 100
        if sliderTitle then
            sliderTitle:SetText("Radius: " .. tostring(val) .. " yd")
        end
        pendingRadius = val
        debounceTimer = 0.5 -- 500ms debounce
    end)

    if Settings:GetMode() == 1 then
        radiusSlider:Show()
    else
        radiusSlider:Hide()
    end

    -- Debounce frame for slider
    local debouncer = CreateFrame("Frame")
    debouncer:SetScript("OnUpdate", function(self, elapsed)
        if debounceTimer > 0 then
            debounceTimer = debounceTimer - elapsed
            if debounceTimer <= 0 and pendingRadius then
                Settings:SetRadius(pendingRadius)
                pendingRadius = nil
            end
        end
    end)
end

-- Slash command handler extension
function Settings:HandleSlashCommand(argStr)
    local sub, val = (argStr or ""):match("^%s*(%S+)%s*(.*)$")
    sub = (sub or ""):lower()

    if sub == "" or sub == "status" then
        local m = MODES[Settings:GetMode()].name
        local ev = Settings:GetEventsEnabled() and "|cFF00FF00ON|r" or "|cFFFF0000OFF|r"
        local rad = tostring(Settings:GetRadius()) .. " yd"
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r Mode: |cFFFFFFFF" .. m .. "|r, Radius: |cFFFFFFFF" .. rad .. "|r, Events: " .. ev)
    elseif sub == "off" then
        Settings:SetMode(0)
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r Bot Visibility set to |cFFFFFFFFOff|r.")
    elseif sub == "nearby" then
        Settings:SetMode(1)
        local r = tonumber(val)
        if r then Settings:SetRadius(r) end
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r Bot Visibility set to |cFFFFFFFFNearby|r (" .. Settings:GetRadius() .. " yd).")
    elseif sub == "zone" then
        Settings:SetMode(2)
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r Bot Visibility set to |cFFFFFFFFCurrent Zone|r.")
    elseif sub == "guild" then
        Settings:SetMode(3)
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r Bot Visibility set to |cFFFFFFFFGuild Only|r.")
    elseif sub == "all" then
        Settings:SetMode(4)
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r Bot Visibility set to |cFFFFFFFFAll Allied|r.")
    elseif sub == "events" then
        local v = (val or ""):lower()
        if v == "on" or v == "1" or v == "true" then
            Settings:SetEventsEnabled(true)
            DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r World Events display |cFF00FF00Enabled|r.")
        elseif v == "off" or v == "0" or v == "false" then
            Settings:SetEventsEnabled(false)
            DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r World Events display |cFFFF0000Disabled|r.")
        else
            Settings:SetEventsEnabled(not Settings:GetEventsEnabled())
            DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100[CoABot Map Intelligence]|r World Events display toggled " .. (Settings:GetEventsEnabled() and "|cFF00FF00ON|r" or "|cFFFF0000OFF|r") .. ".")
        end
    elseif sub == "testevent" then
        if CoABotWorldMap and CoABotWorldMap.ToggleTestEvent then
            CoABotWorldMap:ToggleTestEvent()
        end
    else
        DEFAULT_CHAT_FRAME:AddMessage("|cFFFFD100CoABot Map Commands:|r")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map - Show current map intelligence status")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map off - Disable bot map pins")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map nearby [radius] - Show nearby allied bots")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map zone - Show all allied bots in current zone")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map guild - Show all guild companion bots")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map all - Show all allied bots on map")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map events [on|off] - Toggle dynamic world events")
        DEFAULT_CHAT_FRAME:AddMessage("  /coabot map testevent - Spawn or remove test event on current map")
    end
end
