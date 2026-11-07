-- vanilla skirmish vs AI (stand only): DOW2.exe -dev -nomovies -init skirm.lua
MAP    = "2p_calderisrefinery"
RACES  = { "space_marines", "eldar" }
HEROES = { 0, 0 }
DIFF   = { 0, 0 }
WIN    = "vptickerwin-annihilate"
local function try(name, fn, ...) local ok, r = pcall(fn, ...); print("SKIRM " .. name .. " -> " .. tostring(ok) .. " " .. tostring(r)); return ok end
function SCRIPT_Before() try("SetGameSkirmish", SetGameSkirmish, MAP); return 1 end
function SCRIPT_Connect()
	for i = 0, GetPlayerCount() - 1 do
		if i > 0 then try("SetPlayerToAI " .. i, SetPlayerToAI, i) end
		try("SetPlayerRace " .. i, SetPlayerRace, i, RACES[i + 1])
		try("SetPlayerHeroChoice " .. i, SetPlayerHeroChoice, i, HEROES[i + 1])
		try("SetPlayerAIDifficulty " .. i, SetPlayerAIDifficulty, i, DIFF[i + 1])
		try("SetPlayerTeam " .. i, SetPlayerTeam, i, i % 2)
		try("SetPlayerReady " .. i, SetPlayerReady, i)
	end
	return 1
end
function SCRIPT_GameStart() return 1 end
