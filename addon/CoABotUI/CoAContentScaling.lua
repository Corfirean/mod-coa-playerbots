local prefix = "COASCALE"
local profiles, pending, active = {}, {}, false
local unpack = unpack or table.unpack
local function pack(...) return {n = select("#", ...), ...} end
local originalInfo = GetLFGDungeonInfo
local originalChoiceInfo = GetLFDChoiceInfo
local originalOrder = GetLFDChoiceOrder
local originalRandomCount = GetNumRandomDungeons
local originalRandomInfo = GetLFGRandomDungeonInfo
local originalJoinable = IsLFGDungeonJoinable
local originalFilter = LFDList_DefaultFilterFunction
local randoms = {}

local function copy(source)
    local result = {}
    for key, value in pairs(source) do result[key] = value end
    return result
end

local function apply(id, info)
    local profile = active and profiles[id]
    if profile and info and info[1] then
        info[3], info[4] = profile.min, profile.max
        info[5], info[6], info[7] = profile.max, profile.min, profile.max
    end
    return info
end

if originalInfo then
    GetLFGDungeonInfo = function(id)
        local info = apply(id, pack(originalInfo(id)))
        return unpack(info, 1, info.n)
    end
end
if originalChoiceInfo then
    GetLFDChoiceInfo = function(...)
        local info = originalChoiceInfo(...)
        if active and info then
            local scaled = {}
            for id, row in pairs(info) do scaled[id] = apply(id, copy(row)) end
            info = scaled
        end
        return info
    end
end
if originalOrder then
    GetLFDChoiceOrder = function(...)
        local order = originalOrder(...)
        if not active or not order or not LFGDungeonInfo then return order end
        order = copy(order)
        local seen = {}
        for _, id in ipairs(order) do seen[id] = true end
        for id, profile in pairs(profiles) do
            local info = LFGDungeonInfo[id]
            if (profile.type == 1 or profile.type == 5) and info and info[9] and info[9] ~= 0 and profile.max > 0 then
                if not seen[info[9]] and LFGDungeonInfo[info[9]] then
                    order[#order + 1] = info[9]
                    seen[info[9]] = true
                end
                if not seen[id] then order[#order + 1], seen[id] = id, true end
            end
        end
        table.sort(order, function(a, b)
            local ai, bi = LFGDungeonInfo[a], LFGDungeonInfo[b]
            local ag, bg = a < 0 and a or (ai and ai[9] or 0), b < 0 and b or (bi and bi[9] or 0)
            if ag ~= bg then return ag > bg end
            if a < 0 or b < 0 then return a < b end
            local al, bl = ai and ai[3] or 0, bi and bi[3] or 0
            if al ~= bl then return al < bl end
            return a < b
        end)
        return order
    end
end
local function scalingFilter(id)
    if not active or not profiles[id] then
        return originalFilter and originalFilter(id)
    end
    local info = LFGDungeonInfo and LFGDungeonInfo[id]
    return info and info[9] ~= 0 and profiles[id].max > 0
        and (not EXPANSION_LEVEL or EXPANSION_LEVEL >= (info[8] or 0))
end
if originalFilter then
    LFDList_DefaultFilterFunction = scalingFilter
    if LFD_CURRENT_FILTER == originalFilter then LFD_CURRENT_FILTER = scalingFilter end
end
if originalRandomCount and originalRandomInfo then
    GetNumRandomDungeons = function()
        return active and #randoms or originalRandomCount()
    end
    GetLFGRandomDungeonInfo = function(index)
        if not active then return originalRandomInfo(index) end
        local id = randoms[index]
        if id then return id, select(1, originalInfo(id)) end
    end
end
if originalJoinable then
    IsLFGDungeonJoinable = function(id)
        local profile = active and profiles[id]
        if not profile then return originalJoinable(id) end
        local level = UnitLevel("player")
        if level < profile.min or level > profile.max then return false end
        if LFGLockList and LFGLockList[id] and LFGLockList[id] ~= 0 then return false end
        return true
    end
end
local queueButtons = {}
local function updateQueueButtons()
    local native = LFDQueueFrameFindGroupButton
    if not native then return end
    if #queueButtons == 0 then
        for index, choice in ipairs({{"With bots", "BOTS"}, {"Solo / party", "SOLO"}}) do
            local button = CreateFrame("Button", nil, native:GetParent(), "UIPanelButtonTemplate")
            button:SetSize(106, 22)
            button:SetPoint(index == 1 and "RIGHT" or "LEFT", native, "CENTER", index == 1 and -2 or 2, 0)
            button:SetText(choice[1])
            button:SetScript("OnClick", function()
                SendAddonMessage(prefix, "MODE:" .. choice[2], "WHISPER", UnitName("player"))
                native:Click()
            end)
            queueButtons[index] = button
        end
        if hooksecurefunc then hooksecurefunc("LFDQueueFrameFindGroupButton_Update", updateQueueButtons) end
    end
    local mode = GetLFGMode and GetLFGMode()
    local choosing = active and mode ~= "queued" and mode ~= "proposal" and mode ~= "rolecheck"
    if native.SetAlpha then native:SetAlpha(choosing and 0 or 1) end
    if native.EnableMouse then native:EnableMouse(not choosing) end
    for _, button in ipairs(queueButtons) do
        if choosing then
            button:Show()
            if native:IsEnabled() then button:Enable() else button:Disable() end
        else
            button:Hide()
        end
    end
end

local function refresh()
    randoms = {}
    if active and originalInfo then
        for id, profile in pairs(profiles) do
            if profile.type == 6 and profile.max > 0 and originalInfo(id) then
                randoms[#randoms + 1] = id
            end
        end
        table.sort(randoms)
    end
    if LFGDungeonList_Setup then LFGDungeonList_Setup(true) end
    updateQueueButtons()
end
local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:SetScript("OnEvent", function(_, event, channel, message, distribution, sender)
    if event == "PLAYER_ENTERING_WORLD" then
        SendAddonMessage(prefix, "REQUEST", "WHISPER", UnitName("player"))
        return
    end
    if channel ~= prefix or distribution ~= "WHISPER" or sender ~= UnitName("player") then return end
    if message == "OFF" then
        profiles, pending, active = {}, {}, false
        refresh()
    elseif message:match("^BEGIN:%d+$") then
        pending = {}
    elseif message == "END" then
        profiles, active = pending, true
        refresh()
    else
        local id, min, max, kind = message:match("^D:(%d+):(%d+):(%d+):(%d+)$")
        if id then pending[tonumber(id)] = {min = tonumber(min), max = tonumber(max), type = tonumber(kind)} end
    end
end)
