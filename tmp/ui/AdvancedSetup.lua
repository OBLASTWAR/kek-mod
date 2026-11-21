-------------------------------------------------
-- edit: MP voting system for EUI & vanilla UI
-------------------------------------------------
-- edit: Sorting Civs by Short Description
-- for EUI & vanilla UI
-------------------------------------------------
-- Advanced Settings Screen
-------------------------------------------------
--[[
	Design Overview
	The UI is made up of a table of *ScreenOptions* which are tables themselves consisting of a common interface.
	These common methods are called one after the other when certain events occur to ensure that the data is 
	up-to-date and valid.
	
	ScreenOption overview
	Each option structure contains the following functions:
		FullSync()		--	Refreshes all possible values and current value from the game.
							This is also where event handlers for controls are typically assigned.
		PartialSync()	--	Refreshes only the current value from the game.
		Validate(args)	--	Verifies that current value is a valid value (if not, will assign args.Valid = false)

	When a value is changed for whatever reason, validation is performed to determine whether 
	or not we can enable/disable the start button.

	When the screen is shown, a partial sync is performed to ensure values are update.
	
	Full syncs are performed either when the screen is initialized or when mods change the game state.
----------------------------------------------------------------------------------------------------------------------]]
include( "IconSupport" );
include( "UniqueBonuses" );
include( "InstanceManager" );

-------------------------------------------------
-- Globals
-------------------------------------------------
g_SlotInstances = {};	-- Container for all player slots 
g_GameOptionsManager = InstanceManager:new("GameOptionInstance", "GameOptionRoot", Controls.GameOptionsStack);
g_DropDownOptionsManager = InstanceManager:new("DropDownOptionInstance", "DropDownOptionRoot", Controls.DropDownOptionsStack);
g_VictoryCondtionsManager = InstanceManager:new("GameOptionInstance", "GameOptionRoot", Controls.VictoryConditionsStack);

------------------------------------------------------------------------------------------------------
-- Complex Methods
-- Pulled out from Screen Options since they were so long
------------------------------------------------------------------------------------------------------
-- Refreshes all dynamic drop down game options
-- TeamerBalance rule checkboxes (sorted below the normal options).
g_TeamerBalanceRules = {
	GAMEOPTION_SAME_TURN_WONDERS = true,
	GAMEOPTION_SAME_TURN_PANTHEONS = true,
	GAMEOPTION_NO_RELIGIOUS_CS = true,
	GAMEOPTION_FAIR_RUINS = true,
	GAMEOPTION_EXTRA_LUXURY_HAPPINESS = true,
	GAMEOPTION_REVEAL_OWN_HALF = true,
	GAMEOPTION_MIRRORED_CITY_STATES = true,
	GAMEOPTION_NO_RUIN_UPGRADE = true,
	GAMEOPTION_FAIR_BARBARIAN_CAMPS = true,
	GAMEOPTION_FAIR_CS_QUESTS = true,
	GAMEOPTION_GUARANTEED_PROPHET = true,
	GAMEOPTION_RELIGION_FOR_ALL = true,
	GAMEOPTION_CS_WORKERS = true,
	GAMEOPTION_NO_GLOBAL_CS_QUESTS = true,
	GAMEOPTION_CS_QUESTS_OWN_SIDE = true,
	GAMEOPTION_TEAM_BELIEFS = true,
	GAMEOPTION_CAPTURE_POP_FLOOR = true,
};

-- TeamerBalance war block dropdowns, int game options read by the DLL (CvGame::IsFairWarBlocked):
--   GAMEOPTION_NO_WAR_TURNS: 0 = off, else no war between players before that many turns.
--   GAMEOPTION_NO_WAR_ERA:   0 = off, else no war until any player reaches that era (era ID).
function AddFairIntDropDown(strOption, strNameKey, strHelpKey, values)
	local gameOption = g_DropDownOptionsManager:GetInstance();
	local toolTip = Locale.ConvertTextKey(strHelpKey);

	gameOption.OptionName:SetText(Locale.ConvertTextKey(strNameKey));
	gameOption.OptionName:SetToolTipString(toolTip);
	gameOption.OptionDropDown:SetDisabled(false);

	local dropDownButton = gameOption.OptionDropDown:GetButton();
	dropDownButton:SetToolTipString(toolTip);

	gameOption.OptionDropDown:ClearEntries();
	for _, entry in ipairs(values) do
		local controlTable = {};
		gameOption.OptionDropDown:BuildEntry( "InstanceOne", controlTable );
		controlTable.Button:SetText(entry.Name);
		controlTable.Button:RegisterCallback(Mouse.eLClick, function()
			dropDownButton:SetText(entry.Name);
			PreGame.SetGameOption(strOption, entry.Value);
		end);
	end

	local current = values[1];
	for _, entry in ipairs(values) do
		if(PreGame.GetGameOption(strOption) == entry.Value) then
			current = entry;
		end
	end
	dropDownButton:SetText(current.Name);

	gameOption.OptionDropDown:CalculateInternals();
end

-- TeamerBalance game mode (GAMEOPTION_TEAMER_MODE: 0 = Normal KEK, 1 = TeamerBalance). The
-- TeamerBalance options only show (and only count, see CvGame::isTeamerOption) in TeamerBalance mode.
-- Picking the mode sets its defaults.
g_TeamerModeDefaults = {
	GAMEOPTION_SAME_TURN_WONDERS = 1,
	GAMEOPTION_SAME_TURN_PANTHEONS = 1,
	GAMEOPTION_NO_RELIGIOUS_CS = 1,
	GAMEOPTION_FAIR_RUINS = 1,
	GAMEOPTION_EXTRA_LUXURY_HAPPINESS = 1,
	GAMEOPTION_MIRRORED_CITY_STATES = 1,
	GAMEOPTION_NO_RUIN_UPGRADE = 1,
	GAMEOPTION_FAIR_BARBARIAN_CAMPS = 1,
	GAMEOPTION_FAIR_CS_QUESTS = 1,
	GAMEOPTION_GUARANTEED_PROPHET = 0,
	GAMEOPTION_RELIGION_FOR_ALL = 0,
	GAMEOPTION_CS_WORKERS = 0,
	GAMEOPTION_NO_GLOBAL_CS_QUESTS = 1,
	GAMEOPTION_CS_QUESTS_OWN_SIDE = 1,
	GAMEOPTION_TEAM_BELIEFS = 1,
	GAMEOPTION_CAPTURE_POP_FLOOR = 0,
	GAMEOPTION_BARB_CAMPS_PER_PLAYER = 2,
	GAMEOPTION_REVEAL_OWN_HALF = 0,
	GAMEOPTION_BAN_BUILDING_GREAT_LIBRARY = 1,
	GAMEOPTION_BAN_BUILDING_GREAT_WALL = 1,
	GAMEOPTION_BAN_BUILDING_TERRACOTTA_ARMY = 1,
	GAMEOPTION_NO_WAR_TURNS = 0,
	GAMEOPTION_NO_WAR_ERA = 0,
};

-- Game modes (GAMEOPTION_TEAMER_MODE): 0 = KEK, 1 = TeamerBalance, 2 = KEK+ Balance (hidden in the
-- mode list for now). The DLL reads an option only in a mode that shows it.
g_ExtraOptions = {	-- every mode
	GAMEOPTION_SAME_TURN_WONDERS = true,
	GAMEOPTION_GUARANTEED_PROPHET = true,
	GAMEOPTION_RELIGION_FOR_ALL = true,
	GAMEOPTION_CAPTURE_POP_FLOOR = true,
	GAMEOPTION_CS_WORKERS = true,
};
-- Recommended settings: what picking a mode ticks (the TeamerBalance rules keep g_TeamerModeDefaults).
g_ModeRecommended = {
	GAMEOPTION_SAME_TURN_WONDERS = 0,
	GAMEOPTION_GUARANTEED_PROPHET = 1,
	GAMEOPTION_RELIGION_FOR_ALL = 0,
	GAMEOPTION_CAPTURE_POP_FLOOR = 0,
	GAMEOPTION_CS_WORKERS = 1,
};

function GetTeamerMode()
	local iMode = PreGame.GetGameOption("GAMEOPTION_TEAMER_MODE");
	if(iMode ~= nil and iMode >= 1 and iMode <= 2) then
		return iMode;
	end
	return 0;
end

-- Any mode but plain KEK (shows the extra options).
function IsTeamerMode()
	return GetTeamerMode() ~= 0;
end
-- TeamerBalance (the fairness rules, wonder bans, war blocks).
function IsTeamerFair()
	return GetTeamerMode() == 1;
end

-- True when some team has more than one civ (team vs team, not 1v1).
function IsTeamGame()
	local sizes = {};
	for i = 0, GameDefines.MAX_MAJOR_CIVS - 1 do
		local status = PreGame.GetSlotStatus(i);
		if(status == SlotStatus.SS_TAKEN or status == SlotStatus.SS_COMPUTER) then
			local t = PreGame.GetTeam(i);
			sizes[t] = (sizes[t] or 0) + 1;
			if(sizes[t] > 1) then return true; end
		end
	end
	return false;
end

function IsShownInMode(strType)
	-- Extra luxury copies: 1v1 only, hidden in team vs team.
	if(strType == "GAMEOPTION_EXTRA_LUXURY_HAPPINESS" and IsTeamGame()) then
		return false;
	end
	if(g_ExtraOptions[strType]) then
		return IsTeamerMode();
	end
	return IsTeamerFair();
end

function IsTeamerOptionType(strType)
	return g_TeamerModeDefaults[strType] ~= nil or string.find(strType, "^GAMEOPTION_BAN_BUILDING_") ~= nil;
end

function SetTeamerMode(iMode)
	PreGame.SetGameOption("GAMEOPTION_TEAMER_MODE", iMode);
	for strType, iDefault in pairs(g_TeamerModeDefaults) do
		local iValue = 0;
		if(iMode ~= 0 and IsShownInMode(strType)) then
			if(g_ModeRecommended[strType] ~= nil) then
				iValue = g_ModeRecommended[strType];
			else
				iValue = iDefault;
			end
		end
		PreGame.SetGameOption(strType, iValue);
	end
end

function AddTeamerModeDropDown(bCanEdit, fnChanged)
	local gameOption = g_DropDownOptionsManager:GetInstance();
	local toolTip = Locale.ConvertTextKey("TXT_KEY_GAME_OPTION_TEAMER_MODE_HELP");
	gameOption.OptionName:SetText(Locale.ConvertTextKey("TXT_KEY_GAME_OPTION_TEAMER_MODE"));
	gameOption.OptionName:SetToolTipString(toolTip);
	gameOption.OptionDropDown:SetDisabled(not bCanEdit);
	local dropDownButton = gameOption.OptionDropDown:GetButton();
	dropDownButton:SetToolTipString(toolTip);
	local values = {
		{ Value = 0, Name = Locale.ConvertTextKey("TXT_KEY_GAME_OPTION_TEAMER_MODE_NORMAL") },
		{ Value = 1, Name = Locale.ConvertTextKey("TXT_KEY_GAME_OPTION_TEAMER_MODE_TEAMER") },
	};
	gameOption.OptionDropDown:ClearEntries();
	for _, entry in ipairs(values) do
		local controlTable = {};
		gameOption.OptionDropDown:BuildEntry( "InstanceOne", controlTable );
		controlTable.Button:SetText(entry.Name);
		controlTable.Button:RegisterCallback(Mouse.eLClick, function()
			dropDownButton:SetText(entry.Name);
			if(PreGame.GetGameOption("GAMEOPTION_TEAMER_MODE") ~= entry.Value) then
				SetTeamerMode(entry.Value);
				fnChanged();
			end
		end);
	end
	for _, entry in ipairs(values) do
		if(entry.Value == GetTeamerMode()) then dropDownButton:SetText(entry.Name) end
	end
	gameOption.OptionDropDown:CalculateInternals();
end

function AddFairWarBlockDropDowns()
	local strOff = Locale.ConvertTextKey("TXT_KEY_GAME_OPTION_NO_WAR_OFF");

	local turns = { { Value = 0, Name = strOff } };
	for _, n in ipairs({ 10, 20, 30, 40, 50, 60, 75, 100, 125, 150 }) do
		table.insert(turns, { Value = n, Name = tostring(n) });
	end
	AddFairIntDropDown("GAMEOPTION_NO_WAR_TURNS", "TXT_KEY_GAME_OPTION_NO_WAR_TURNS", "TXT_KEY_GAME_OPTION_NO_WAR_TURNS_HELP", turns);

	local eras = { { Value = 0, Name = strOff } };
	for era in GameInfo.Eras() do
		if(era.ID > 0 and era.ID <= 5) then	-- Classical .. Modern
			table.insert(eras, { Value = era.ID, Name = Locale.ConvertTextKey(era.Description) });
		end
	end
	table.sort(eras, function(a, b) return a.Value < b.Value end);
	AddFairIntDropDown("GAMEOPTION_NO_WAR_ERA", "TXT_KEY_GAME_OPTION_NO_WAR_ERA", "TXT_KEY_GAME_OPTION_NO_WAR_ERA_HELP", eras);

	-- Fair barbarian camps: how many camps each civ gets at the start.
	local camps = {};
	for n = 0, 6 do
		table.insert(camps, { Value = n, Name = tostring(n) });
	end
	AddFairIntDropDown("GAMEOPTION_BARB_CAMPS_PER_PLAYER", "TXT_KEY_GAME_OPTION_BARB_CAMPS_PER_PLAYER", "TXT_KEY_GAME_OPTION_BARB_CAMPS_PER_PLAYER_HELP", camps);
end

function RefreshDropDownGameOptions()
	g_DropDownOptionsManager:ResetInstances();

	local currentMapScript = PreGame.GetMapScript();
	if(PreGame.IsRandomMapScript())then
		currentMapScript = nil;
	end
	
	local options = {};
	for option in DB.Query("select * from MapScriptOptions where exists (select 1 from MapScriptOptionPossibleValues where FileName = MapScriptOptions.FileName and OptionID = MapScriptOptions.OptionID) and Hidden = 0 and FileName = ?", currentMapScript) do
		options[option.OptionID] = {
			ID = option.OptionID,
			Name = Locale.ConvertTextKey(option.Name),
			ToolTip = (option.Description) and Locale.ConvertTextKey(option.Description) or nil,
			Disabled = (option.ReadOnly == 1) and true or false,
			DefaultValue = option.DefaultValue,
			SortPriority = option.SortPriority,
			Values = {},
		}; 
	end
	
	for possibleValue in DB.Query("select * from MapScriptOptionPossibleValues where FileName = ? order by SortIndex ASC", currentMapScript) do
		if(options[possibleValue.OptionID] ~= nil) then
			table.insert(options[possibleValue.OptionID].Values, {
				Name	= Locale.ConvertTextKey(possibleValue.Name),
				ToolTip = (possibleValue.Description) and Locale.ConvertTextKey(possibleValue.Description) or nil,
				Value	= possibleValue.Value,
			});
		end
	end
	
	local sortedOptions = {};
	for k,v in pairs(options) do
		table.insert(sortedOptions, v);
	end
	 
	-- Sort the options
	table.sort(sortedOptions, function(a, b) 
		if(a.SortPriority == b.SortPriority) then
			return Locale.Compare(a.Name, b.Name) == -1; 
		else
			return a.SortPriority < b.SortPriority;
		end
	end);
	
	-- Update the UI!
	for _, option in ipairs(sortedOptions) do
	
		local gameOption = g_DropDownOptionsManager:GetInstance();
				
		gameOption.OptionName:SetText(option.Name);
								
		if(option.ToolTip ~= nil) then
			gameOption.OptionName:SetToolTipString(option.ToolTip);
		else
			gameOption.OptionName:SetToolTipString();
		end
		
		gameOption.OptionDropDown:SetDisabled(option.Disabled);
		local dropDownButton = gameOption.OptionDropDown:GetButton();
		
		gameOption.OptionDropDown:ClearEntries();
		for _, possibleValue in ipairs(option.Values) do
			controlTable = {};
			gameOption.OptionDropDown:BuildEntry( "InstanceOne", controlTable );
			controlTable.Button:SetText(possibleValue.Name);
			
			if(possibleValue.ToolTip ~= nil) then
				controlTable.Button:SetToolTipString(possibleValue.ToolTip);
			else
				controlTable.Button:SetToolTipString();
			end
			
			
			controlTable.Button:RegisterCallback(Mouse.eLClick, function()
				dropDownButton:SetText(possibleValue.Name);
				if(possibleValue.ToolTip) then
					dropDownButton:SetToolTipString(possibleValue.ToolTip);
				else
					dropDownButton:SetToolTipString();
				end
				PreGame.SetMapOption(option.ID, possibleValue.Value);
			end);
		end
		
		--Assign the currently selected value.
		local savedValue = PreGame.GetMapOption(option.ID);
		local defaultValue;
		if(savedValue ~= -1) then
			defaultValue = option.Values[savedValue];
		else
			defaultValue = option.Values[option.DefaultValue];
		end
		
		if(defaultValue ~= nil) then
			dropDownButton:SetText(defaultValue.Name);
			
			if(defaultValue.ToolTip ~= nil) then
				dropDownButton:SetToolTipString(defaultValue.ToolTip);
			else
				dropDownButton:SetToolTipString();
			end
		end
	
		if(option.Disabled) then
			dropDownButton:SetDisabled(true);
		end
		
		gameOption.OptionDropDown:CalculateInternals();
	end
	
	AddTeamerModeDropDown(true, function()
		RefreshDropDownGameOptions();
		RefreshCheckBoxGameOptions();
	end);
	if(IsTeamerFair()) then
		AddFairWarBlockDropDowns();
	end

	Controls.DropDownOptionsStack:CalculateSize();
	Controls.DropDownOptionsStack:ReprocessAnchoring();

	Controls.OptionsScrollPanel:CalculateInternalSize();
end
------------------------------------------------------------------------------------------------------
-- Refreshes all dynamic checkbox game options
function RefreshCheckBoxGameOptions()
	g_GameOptionsManager:ResetInstances();

	---------------------------------
	-- General Game Options
	local options = {};
	-- First, Gather a list of all options
	for option in GameInfo.GameOptions{Visible = 1} do
		local option = {
			Type = option.Type,
			Name = Locale.ConvertTextKey(option.Description),
			ToolTip = (option.Help) and Locale.ConvertTextKey(option.Help) or nil,
			Checked = (option.Default == 1) and true or false,
			Disabled = false,
			GameOption = true,
			SortPriority = 0, 
			SupportsSinglePlayer = option.SupportsSinglePlayer,
		};

		-- TeamerBalance options (one checkbox per rule, all unticked = normal KEK) go to the bottom:
		-- the rules first, then the wonder bans.
		if( g_TeamerBalanceRules[option.Type] ) then
			option.SortPriority = 2;
		elseif( string.find( option.Type, "^GAMEOPTION_BAN_" ) ) then
			option.SortPriority = 3;
		end
		
		local savedValue = PreGame.GetGameOption(option.Type);
		if(savedValue ~= nil) then
			option.Checked = savedValue == 1;
		end
		
		-- Only display options that support singleplayer.
		if( option.SupportsSinglePlayer ) then
			table.insert(options, option);
		end
	end
	
	for option in DB.Query("select * from MapScriptOptions where not exists (select 1 from MapScriptOptionPossibleValues where FileName = MapScriptOptions.FileName and OptionID = MapScriptOptions.OptionID) and Hidden = 0 and FileName = ?", PreGame.GetMapScript()) do
		local option = {
			ID = option.OptionID,
			Name = Locale.ConvertTextKey(option.Name),
			ToolTip = (option.Description) and Locale.ConvertTextKey(option.Description) or nil,
			Checked = (option.DefaultValue == 1) and true or false,
			Disabled = (option.ReadOnly == 1) and true or false,
			GameOption = false,
			SortPriority = option.SortPriority,
		};
		
		local savedValue = PreGame.GetMapOption(option.ID);
		if(savedValue ~= nil) then
			option.Checked = savedValue == 1;
		end
		
		table.insert(options, option);
	end
	
	-- Sort the options alphabetically
	-- TeamerBalance options only show in TeamerBalance game mode.
	if(not IsTeamerMode()) then
		-- ...and are never left ticked outside it (the game ignores them there anyway).
		if(true) then
			local bCleared = false;
			for strType, _ in pairs(g_TeamerModeDefaults) do
				if(PreGame.GetGameOption(strType) ~= nil and PreGame.GetGameOption(strType) ~= 0 and PreGame.GetGameOption(strType) ~= -1) then
					PreGame.SetGameOption(strType, 0);
					bCleared = true;
				end
			end
			if(bCleared) then
				-- single player: nothing to send
			end
		end
		local kept = {};
		for _, option in ipairs(options) do
			if(not option.Type or not IsTeamerOptionType(option.Type)) then
				table.insert(kept, option);
			end
		end
		options = kept;
	end

	if(IsTeamerMode()) then
		local shown = {};
		for _, option in ipairs(options) do
			if(not option.Type or not IsTeamerOptionType(option.Type) or IsShownInMode(option.Type)) then
				table.insert(shown, option);
			end
		end
		options = shown;
	end

	table.sort(options, function(a, b) 
		if(a.SortPriority == b.SortPriority) then
			return Locale.Compare(a.Name, b.Name) == -1;
		else
			return a.SortPriority < b.SortPriority;
		end
	end);
	
	-- Add Options to UI.
	local sizeY = 0;
	for _, option in ipairs(options) do
		local gameOption = g_GameOptionsManager:GetInstance();
		
		local gameOptionTextButton = gameOption.GameOptionRoot:GetTextButton();
		gameOptionTextButton:SetText(option.Name);
								
		if(option.ToolTip ~= nil) then
			gameOption.GameOptionRoot:SetToolTipString(option.ToolTip);
		end
		
		gameOption.GameOptionRoot:SetDisabled(option.Disabled);
		gameOption.GameOptionRoot:SetCheck(option.Checked);
		sizeY = sizeY + gameOption.GameOptionRoot:GetSizeY();
		if(option.GameOption == true) then
			gameOption.GameOptionRoot:RegisterCheckHandler( function(bCheck)
				PreGame.SetGameOption(option.Type, bCheck);
			end);
		else
			gameOption.GameOptionRoot:RegisterCheckHandler( function(bCheck)
				PreGame.SetMapOption(option.ID, bCheck);
			end);
		end	
	end	
	
	Controls.CityStateStack:CalculateSize();
	Controls.CityStateStack:ReprocessAnchoring();
	
	Controls.DropDownOptionsStack:CalculateSize();
	Controls.DropDownOptionsStack:ReprocessAnchoring();
		
	Controls.MaxTurnStack:CalculateSize();
	Controls.MaxTurnStack:ReprocessAnchoring();
	
	Controls.GameOptionsStack:CalculateSize();
	Controls.GameOptionsStack:ReprocessAnchoring();
	
	Controls.GameOptionsFullStack:CalculateSize();
	Controls.GameOptionsFullStack:ReprocessAnchoring();

	Controls.OptionsScrollPanel:CalculateInternalSize();
end
------------------------------------------------------------------------------------------------------
-- Refreshes all of the player data.
function RefreshPlayerList()

	RefreshHumanPlayer();

	local count = 1;
	if(PreGame.IsRandomWorldSize()) then
		Controls.ListingScrollPanel:SetHide(true);
		Controls.UnknownPlayers:SetHide(false);
		Controls.AddAIButton:SetDisabled(true);
	else
		Controls.UnknownPlayers:SetHide(true);
		Controls.ListingScrollPanel:SetHide(false);
		Controls.AddAIButton:SetDisabled(false);
		
		for i = 1, GameDefines.MAX_MAJOR_CIVS-1 do
			if( PreGame.GetSlotStatus( i ) ~= SlotStatus.SS_COMPUTER ) then
				g_SlotInstances[i].Root:SetHide( true );
			else
    			-- Player Listing Entry
    			local controlTable = g_SlotInstances[i];
				controlTable.Root:SetHide( false );
				controlTable.PlayerNameLabel:SetHide( true );
				
				if(i ~= 1) then --Don't allow player to delete first AI so games will always have at least 2 players
					controlTable.RemoveButton:SetHide(false);
					controlTable.RemoveButton:RegisterCallback( Mouse.eLClick, function()
					
						if( PreGame.GetSlotStatus(i) == SlotStatus.SS_COMPUTER) then
							PreGame.SetSlotStatus(i, SlotStatus.SS_CLOSED);
						end
						PerformPartialSync();
					
					end);
				else
					controlTable.RemoveButton:SetHide(true);
				end
	            
				local civIndex = PreGame.GetCivilization( i );

				if( civIndex ~= -1 ) then
					local civ = GameInfo.Civilizations[ civIndex ];

					controlTable.CivNumberIndex:LocalizeAndSetText("TXT_KEY_NUMBERING_FORMAT", count + 1);
					
					-- Use the Civilization_Leaders table to cross reference from this civ to the Leaders table
					local leader = GameInfo.Leaders[GameInfo.Civilization_Leaders( "CivilizationType = '" .. civ.Type .. "'" )().LeaderheadType];
					local leaderDescription = leader.Description;

					--controlTable.LeaderName:SetText( Locale.ConvertTextKey( leaderDescription ) );
					controlTable.CivPulldown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_LEADER_CIV", Locale.ConvertTextKey(leaderDescription), Locale.ConvertTextKey(civ.ShortDescription));

					IconHookup( leader.PortraitIndex, 64, leader.IconAtlas, controlTable.Portrait );
					IconHookup( civ.PortraitIndex, 64, civ.IconAtlas, controlTable.Icon );
			
				else
  					controlTable.CivNumberIndex:LocalizeAndSetText("TXT_KEY_NUMBERING_FORMAT", count + 1);
					controlTable.CivPulldown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_CIV");

					IconHookup( 22, 64, "LEADER_ATLAS", controlTable.Portrait );
					SimpleCivIconHookup(-1, 64, controlTable.Icon);
				end
				count = count + 1;
			end
		end
	end
	
	Controls.CivCount:SetText(  Locale.ConvertTextKey("TXT_KEY_AD_SETUP_CIVILIZATION", count) );

	Controls.SlotStack:CalculateSize();
	Controls.SlotStack:ReprocessAnchoring();
	Controls.ListingScrollPanel:CalculateInternalSize();

end
------------------------------------------------------------------------------------------------------
-- Refreshes all of the human player data (called by RefreshPlayerList)
function RefreshHumanPlayer()
	local civIndex = PreGame.GetCivilization(0);

    if( civIndex ~= -1 ) then
        local civ = GameInfo.Civilizations[civIndex];

        -- Use the Civilization_Leaders table to cross reference from this civ to the Leaders table
        local leader = GameInfo.Leaders[GameInfo.Civilization_Leaders( "CivilizationType = '" .. civ.Type .. "'" )().LeaderheadType];
		local leaderDescription = leader.Description;

		local name = PreGame.GetLeaderName(0);
		local civName = PreGame.GetCivilizationShortDescription(0);

		if(name ~= "") then
			Controls.CivPulldown:SetHide(true);
			Controls.CivName:SetHide(false);
			Controls.CivName:SetText( Locale.ConvertTextKey( "TXT_KEY_RANDOM_LEADER_CIV", name, civName ));
			Controls.RemoveButton:SetHide(false);
		else
			Controls.CivName:SetHide(true);
			Controls.RemoveButton:SetHide(true);
			Controls.CivPulldown:SetHide(false);
			Controls.CivPulldown:GetButton():SetText( Locale.ConvertTextKey( "TXT_KEY_RANDOM_LEADER_CIV", Locale.ConvertTextKey( leaderDescription ), Locale.ConvertTextKey( civ.ShortDescription ) ) );
		end
		
		IconHookup( leader.PortraitIndex, 64, leader.IconAtlas, Controls.Portrait );
		IconHookup( civ.PortraitIndex, 64, civ.IconAtlas, Controls.Icon );
	else
		Controls.CivNumberIndex:LocalizeAndSetText("TXT_KEY_NUMBERING_FORMAT", 1);
		local name = PreGame.GetLeaderName(0);
		local civName = PreGame.GetCivilizationShortDescription(0);
		
		if(name ~= "") then
			Controls.CivPulldown:SetHide(true);
			Controls.CivName:SetHide(false);
			Controls.CivName:SetText( Locale.ConvertTextKey( "TXT_KEY_RANDOM_LEADER_CIV", name, civName ));
			Controls.RemoveButton:SetHide(false);
		else
			Controls.CivName:SetHide(true);
			Controls.RemoveButton:SetHide(true);
			Controls.CivPulldown:SetHide(false);
			Controls.CivPulldown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_CIV");
		end

		IconHookup( 22, 64, "LEADER_ATLAS", Controls.Portrait );
		SimpleCivIconHookup(-1, 64, Controls.Icon);
    end
end
------------------------------------------------------------------------------------------------------

------------------------------------------------------------------------------------------------------
-- Screen Options
------------------------------------------------------------------------------------------------------
ScreenOptions = {

----------------------------------------------------------------
["Civs"] = {
	FullSync = function()
	
		function GetPlayableCivInfo()
			local civs = {};
			local sql = [[Select	Civilizations.ID as CivID, 
									Leaders.ID as LeaderID, 
									Civilizations.Description, 
									Civilizations.ShortDescription, 
									Leaders.Description as LeaderDescription 
									from Civilizations, Leaders, Civilization_Leaders 
									where Civilizations.Playable = 1 and CivilizationType = Civilizations.Type and LeaderheadType = Leaders.Type]];
	
			for row in DB.Query(sql) do
				table.insert(civs, {
					CivID = row.CivID,
					LeaderID = row.LeaderID,
					LeaderDescription = Locale.Lookup(row.LeaderDescription),
					ShortDescription = Locale.Lookup(row.ShortDescription),
					Description = row.Description,
				});
			end
			
-- Sorting Civs by Short Description
			table.sort(civs, function(a,b)
				local astr = Locale.ConvertTextKey(a.ShortDescription);
				local bstr = Locale.ConvertTextKey(b.ShortDescription);
				local a0, b0
				if astr:match("The ") then
					a0 = astr:sub(5)
				else
					a0 = astr
				end
				if bstr:match("The ") then
					b0 = bstr:sub(5)
				else
					b0 = bstr
				end
				return Locale.Compare(a0, b0) == -1;
			end);
-- Sorting Civs by Short Description END
			
			return civs;
		end
	
		function PopulateCivPulldown( playableCivs, pullDown, playerID )

			pullDown:ClearEntries();
			-- set up the random slot
			local controlTable = {};
			pullDown:BuildEntry( "InstanceOne", controlTable );
			controlTable.Button:SetVoids( playerID, -1 );
			controlTable.Button:LocalizeAndSetText("TXT_KEY_RANDOM_LEADER");
			controlTable.Button:LocalizeAndSetToolTip("TXT_KEY_RANDOM_LEADER_HELP");
	
			for id, civ in ipairs(playableCivs) do
				local controlTable = {};
				pullDown:BuildEntry( "InstanceOne", controlTable );

				controlTable.Button:SetVoids( playerID, id );
				controlTable.Button:LocalizeAndSetText("TXT_KEY_RANDOM_LEADER_CIV", civ.LeaderDescription, civ.ShortDescription);
				controlTable.Button:LocalizeAndSetToolTip(civ.Description);
			end    
		    
			pullDown:CalculateInternals();
			pullDown:RegisterSelectionCallback(function(playerID, id)
				local civID = playableCivs[id] and playableCivs[id].CivID or -1;
			
				PreGame.SetCivilization( playerID, civID);
				PerformPartialSync();
			end);
		end
		
		local playableCivs = GetPlayableCivInfo();
		PopulateCivPulldown(playableCivs, Controls.CivPulldown, 0 );
		for i = 1, GameDefines.MAX_MAJOR_CIVS-1, 1 do
			PopulateCivPulldown(playableCivs, g_SlotInstances[i].CivPulldown, i );
		end
	end,
	
	PartialSync = function()
		RefreshPlayerList();
	end,
	
},
----------------------------------------------------------------

----------------------------------------------------------------
["CustomOptions"] = {
	FullSync = function()
		RefreshDropDownGameOptions();
		RefreshCheckBoxGameOptions();
	end,
	
	PartialSync = function()
		-- Still doing a full sync here..
		RefreshDropDownGameOptions();
		RefreshCheckBoxGameOptions();
	end,
},
----------------------------------------------------------------

----------------------------------------------------------------
["Eras"] = {
	FullSync = function()
		local pullDown = Controls.EraPullDown;
		pullDown:ClearEntries();
		for info in GameInfo.Eras() do

			local instance = {};
			pullDown:BuildEntry( "InstanceOne", instance );
		    
			instance.Button:LocalizeAndSetText(info.Description);
			instance.Button:SetVoid1( info.ID );
		end
		pullDown:CalculateInternals();
		
		pullDown:RegisterSelectionCallback( function(id)
			local era = GameInfo.Eras[id];
			PreGame.SetEra( id );
			pullDown:GetButton():LocalizeAndSetText(era.Description);
			PerformPartialSync();
		end);
	end,
	
	PartialSync = function()
		local info = GameInfo.Eras[ PreGame.GetEra() ];
		Controls.EraPullDown:GetButton():LocalizeAndSetText(info.Description);
		Controls.EraPullDown:GetButton():LocalizeAndSetToolTip(info.Strategy);
	end,
},
----------------------------------------------------------------

----------------------------------------------------------------
["GameSpeeds"] = {
	FullSync = function()
		local pullDown = Controls.GameSpeedPullDown;
		
		pullDown:ClearEntries();
	
		local gameSpeeds = {};
		for row in GameInfo.GameSpeeds() do
			table.insert(gameSpeeds, row);
		end
		table.sort(gameSpeeds, function(a, b) return b.VictoryDelayPercent > a.VictoryDelayPercent end);

		for _, v in ipairs(gameSpeeds) do
			local instance = {};
			pullDown:BuildEntry( "InstanceOne", instance );
		    
			instance.Button:SetText( Locale.ConvertTextKey( v.Description ) );
			instance.Button:SetToolTipString( Locale.ConvertTextKey( v.Help ) );
			instance.Button:SetVoid1( v.ID );
		end
		pullDown:CalculateInternals();
		
		pullDown:RegisterSelectionCallback( function(id)
			PreGame.SetGameSpeed( id );
			local gameSpeed = GameInfo.GameSpeeds[id];
			pullDown:GetButton():LocalizeAndSetText(gameSpeed.Description);
			pullDown:GetButton():SetToolTipString( Locale.ConvertTextKey( gameSpeed.Help ) );
			PerformPartialSync();
		end);
	end,
	
	PartialSync = function()
		local info = GameInfo.GameSpeeds[ PreGame.GetGameSpeed() ];
		Controls.GameSpeedPullDown:GetButton():LocalizeAndSetText(info.Description);
		Controls.GameSpeedPullDown:GetButton():SetToolTipString( Locale.ConvertTextKey( info.Help ) );
	end,
},
----------------------------------------------------------------

----------------------------------------------------------------
["Handicaps"] = {
	FullSync = function()
		local pullDown = Controls.HandicapPullDown;
		pullDown:ClearEntries();
		for info in GameInfo.HandicapInfos() do
			if ( info.Type ~= "HANDICAP_AI_DEFAULT" ) then

				local instance = {};
				pullDown:BuildEntry( "InstanceOne", instance );
			    
				instance.Button:LocalizeAndSetText(info.Description);
				instance.Button:LocalizeAndSetToolTip(info.Help);
				instance.Button:SetVoid1( info.ID );
			
			end
		end
		
		pullDown:CalculateInternals();
		
		pullDown:RegisterSelectionCallback(function(id)
			local handicap = GameInfo.HandicapInfos[id];
			PreGame.SetHandicap( 0, id );
			pullDown:GetButton():LocalizeAndSetText(handicap.Description);
			pullDown:GetButton():LocalizeAndSetToolTip(handicap.Help);
		
			PerformPartialSync();
		end);
	end,
	
	PartialSync = function()
		local info = GameInfo.HandicapInfos[PreGame.GetHandicap(0)];
		Controls.HandicapPullDown:GetButton():LocalizeAndSetText(info.Description);
		Controls.HandicapPullDown:GetButton():LocalizeAndSetToolTip(info.Help);
	end,
},
----------------------------------------------------------------

----------------------------------------------------------------
["MapSizes"] = {
	FullSync = function()
		local pullDown = Controls.MapSizePullDown;
		pullDown:ClearEntries();
		
		
		local mapType;
		if(not PreGame.IsRandomMapScript()) then
			local filename = PreGame.GetMapScript();	
			for row in GameInfo.Map_Sizes() do
				if(Path.GetFileNameWithoutExtension(filename) == Path.GetFileNameWithoutExtension(row.FileName)) then
					mapType = row.MapType;
					break;
				end
			end
		end
		
		if(mapType ~= nil) then
			local numMapSizes = 0;
			local mapSizes = {};
			for row in GameInfo.Map_Sizes{MapType = mapType} do
				mapSizes[row.WorldSizeType] = row.FileName;
				numMapSizes = numMapSizes + 1;
			end
			
			if(numMapSizes > 1) then
				local instance = {};
				pullDown:BuildEntry( "InstanceOne", instance );
				instance.Button:LocalizeAndSetText("TXT_KEY_RANDOM_MAP_SIZE");
				instance.Button:LocalizeAndSetToolTip("TXT_KEY_RANDOM_MAP_SIZE_HELP");
				instance.Button:SetVoid1( -1 );
			end
			
			for info in GameInfo.Worlds("ID >= 0 ORDER BY ID") do
				local sizeEntry = mapSizes[info.Type];
				if(sizeEntry ~= nil) then
					local instance = {};
					pullDown:BuildEntry( "InstanceOne", instance );
					instance.Button:LocalizeAndSetText(info.Description);
					instance.Button:LocalizeAndSetToolTip(info.Help);
					instance.Button:SetVoid1( info.ID );
				end
			end			
		else
			local instance = {};
			pullDown:BuildEntry( "InstanceOne", instance );
			instance.Button:LocalizeAndSetText("TXT_KEY_RANDOM_MAP_SIZE");
			instance.Button:LocalizeAndSetToolTip("TXT_KEY_RANDOM_MAP_SIZE_HELP");
			instance.Button:SetVoid1( -1 );

			for info in GameInfo.Worlds("ID >= 0 ORDER BY ID") do
				local instance = {};
				pullDown:BuildEntry( "InstanceOne", instance );
				instance.Button:LocalizeAndSetText(info.Description);
				instance.Button:LocalizeAndSetToolTip(info.Help);
				instance.Button:SetVoid1( info.ID );
			end
		
		end	

		pullDown:CalculateInternals();
		
		pullDown:RegisterSelectionCallback( function(id)
				
			if( id == -1 ) then
				PreGame.SetRandomWorldSize( true );
				pullDown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_MAP_SIZE");
				pullDown:GetButton():LocalizeAndSetToolTip("TXT_KEY_RANDOM_MAP_SIZE_HELP");
			else
				
				local mapFilter;
				if(PreGame.IsRandomMapScript() == false) then
					 local mapScript = PreGame.GetMapScript();
					 for row in GameInfo.Map_Sizes() do 
						if(Path.GetFileName(mapScript) == Path.GetFileName(row.FileName)) then
							mapFilter = row.MapType;
							break;
						end
					end
				end
		        
				local world = GameInfo.Worlds[id];
				PreGame.SetRandomWorldSize( false );
				PreGame.SetWorldSize( id );
				PreGame.SetNumMinorCivs( world.DefaultMinorCivs );
				
		        
				pullDown:GetButton():LocalizeAndSetText(world.Description);
				pullDown:GetButton():LocalizeAndSetToolTip(world.Help);
				
				if(mapFilter ~= nil) then		
					for row in GameInfo.Map_Sizes{MapType = mapFilter, WorldSizeType = world.Type} do
						PreGame.SetMapScript(row.FileName);
						
						local wb = UI.GetMapPreview(row.FileName);
						if(wb ~= nil) then
							
							PreGame.SetEra(wb.StartEra);
							PreGame.SetGameSpeed(wb.DefaultSpeed);
							PreGame.SetMaxTurns(wb.MaxTurns);
							PreGame.SetNumMinorCivs(wb.CityStateCount);
							PreGame.SetRandomWorldSize(false);
							PreGame.SetWorldSize(wb.MapSize);
							PreGame.SetRandomWorldSize(false);
							PreGame.SetNumMinorCivs(-1);
							
							local victories = {};
							for _, v in ipairs(wb.VictoryTypes) do
								victories[v] = true;
							end
							
							for row in GameInfo.Victories() do
								PreGame.SetVictory(row.ID, victories[row.Type]);
							end;
							
							local numPlayers = wb.PlayerCount;
							if(numPlayers == 0) then
								numPlayers = GameInfo.Worlds[wb.MapSize].DefaultPlayers	
							end
							
							for i = numPlayers, GameDefines.MAX_MAJOR_CIVS - 1 do
								if( PreGame.GetSlotStatus(i) == SlotStatus.SS_COMPUTER) then
									PreGame.SetSlotStatus(i, SlotStatus.SS_OPEN);
								end
							end
						end		
					end
				end
			end
			
			ScreenOptions["Teams"].FullSync();
			PerformPartialSync();
		end);
	end,
	
	PartialSync = function()
	
		--Determine if map type is a WB map or script.
		local bDisableMapSize = not PreGame.IsRandomMapScript();
		local filename = PreGame.GetMapScript();
		for row in GameInfo.MapScripts{FileName = filename} do
			ScreenOptions["MapSizes"].FullSync();
			bDisableMapSize = false;
			break;
		end	
		
		local mapType;
		for row in GameInfo.Map_Sizes{FileName = filename} do
			ScreenOptions["MapSizes"].FullSync();			
			mapType = row.MapType;
			break;
		end
		
		if(mapType ~= nil) then
			local mapSizeCount = 0;
			for row in GameInfo.Map_Sizes{MapType = mapType} do
				mapSizeCount = mapSizeCount + 1
			end
			if(mapSizeCount > 1) then
				bDisableMapSize = false;
			end
		end
		
		Controls.MapSizePullDown:GetButton():SetDisabled(bDisableMapSize);
		if( not PreGame.IsRandomWorldSize() ) then
			local info = GameInfo.Worlds[ PreGame.GetWorldSize() ];
			Controls.MapSizePullDown:GetButton():LocalizeAndSetText(info.Description);
			Controls.MapSizePullDown:GetButton():LocalizeAndSetToolTip(info.Help);
		else
			Controls.MapSizePullDown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_MAP_SIZE");
			Controls.MapSizePullDown:GetButton():LocalizeAndSetToolTip("TXT_KEY_RANDOM_MAP_SIZE_HELP");
		end   
	end,
},
----------------------------------------------------------------

----------------------------------------------------------------
["MapTypes"] = {
	FullSync = function()
		local pullDown = Controls.MapTypePullDown;
		pullDown:ClearEntries();
	
		local mapScripts = {
			[0] = {
				Name = Locale.ConvertTextKey( "TXT_KEY_RANDOM_MAP_SCRIPT" ),
				Description = Locale.ConvertTextKey( "TXT_KEY_RANDOM_MAP_SCRIPT_HELP" ),
			},
		};

		for row in GameInfo.MapScripts{SupportsSinglePlayer = 1, Hidden = 0} do
			local script = {};
			script.FileName = row.FileName;
			script.Name = Locale.ConvertTextKey(row.Name);
			script.Description = row.Description and Locale.ConvertTextKey(row.Description) or "";
			script.DefaultCityStates = row.DefaultCityStates;
			
			table.insert(mapScripts, script);	
		end	
		
		for row in GameInfo.Maps() do	
			local script = {
				Name = Locale.Lookup(row.Name),
				Description = Locale.Lookup(row.Description),			
				MapType = row.Type,
			};		
			table.insert(mapScripts, script);	
		end
		
		
		-- Filter out map files that are part of size groups.
		local worldBuilderMapsToFilter = {};
		for row in GameInfo.Map_Sizes() do
			table.insert(worldBuilderMapsToFilter, row.FileName);
		end
		
		for _, map in ipairs(Modding.GetMapFiles()) do
			
			local bExclude = false;
			for i,v in ipairs(worldBuilderMapsToFilter) do
				if(v == map.File) then
					bExclude = true;
					break;
				end
			end 
		
			if(not bExclude) then
				local mapData = UI.GetMapPreview(map.File);
				
				local name, description;
				local isError = nil;
				
				if(mapData) then
					
					if(not Locale.IsNilOrWhitespace(map.Name)) then
						name = map.Name;
					
					elseif(not Locale.IsNilOrWhitespace(mapData.Name)) then
						name = Locale.Lookup(mapData.Name);
					
					else
						name = Path.GetFileNameWithoutExtension(map.File);
					end
					
					if(map.Description and #map.Description > 0) then
						description = map.Description;
					else
						description = Locale.ConvertTextKey(mapData.Description);
					end
				else
					local _;
					_, _, name = string.find(map.File, "\\.+\\(.+)%.");
					
					local nameTranslation = Locale.ConvertTextKey("TXT_KEY_INVALID_MAP_TITLE", name);
					if(nameTranslation and nameTranslation ~= "TXT_KEY_INVALID_MAP_TITLE") then
						name = nameTranslation;
					else
						name = "[COLOR_RED]" .. name .. "[ENDCOLOR]";
					end
					
					local descTranslation = Locale.ConvertTextKey("TXT_KEY_INVALID_MAP_DESC");
					if(descTranslation and descTranslation ~= "TXT_KEY_INVALID_MAP_DESC") then
						description = descTranslation;
					end
					
					isError = true;
				end
				
				local entry = {
					Name = name,
					Description = description,
					FileName = map.File,
					WBMapData = mapData,
					Error = isError,
				};
				
				table.insert(mapScripts, entry);
			end
		end
		
		table.sort(mapScripts, function(a, b) return Locale.Compare(a.Name, b.Name) == -1; end);
		
		for i, script in ipairs(mapScripts) do
		
			controlTable = {};
			pullDown:BuildEntry( "InstanceOne", controlTable );
		    
			controlTable.Button:SetText(script.Name);
			controlTable.Button:SetToolTipString(script.Description);
			controlTable.Button:SetVoid1(i);
		end
		pullDown:CalculateInternals();

		pullDown:RegisterSelectionCallback( function(id)
			local mapScript = mapScripts[id];
			
			-- If this is an "error" entry (invalid WB file for example), do nothing.
			if(mapScript.Error) then
				return;
			end
			
			PreGame.SetLoadWBScenario(false);
			
			if( id == 0 or mapScript == nil) then
				PreGame.SetRandomMapScript(true);
				
			elseif(mapScript.MapType ~= nil) then
			
				local mapType = mapScript.MapType;
				PreGame.SetRandomMapScript(false);
		
				local mapSizes = {};
				
				for row in GameInfo.Map_Sizes{MapType = mapType} do
					local world = GameInfo.Worlds[row.WorldSizeType];
					
					mapSizes[world.ID] = row.FileName; 
				end
				
				local worldSize = PreGame.GetWorldSize();
				
				
				local mapScriptFileName = mapSizes[worldSize];
				if(mapScriptFileName == nil) then
					-- Not Found, pick random size and set filename to smallest world size.
					for row in GameInfo.Worlds("ID >= 0 ORDER BY ID") do
						local file = mapSizes[row.ID];
						if(file ~= nil) then
							mapScriptFileName = file;
							break;
						end
					end
				end
				
				local wb = UI.GetMapPreview(mapScriptFileName);
				if(wb ~= nil) then
					
					PreGame.SetMapScript(mapScriptFileName);
					PreGame.SetRandomWorldSize(false);
					PreGame.SetWorldSize(wb.MapSize);
					
					if(wb.CityStateCount > 0) then
						PreGame.SetNumMinorCivs(wb.CityStateCount);	
					else
						local world = GameInfo.Worlds[wb.MapSize];
						PreGame.SetNumMinorCivs(world.DefaultMinorCivs);
					end
				end			
			else
				PreGame.SetRandomMapScript(false);
				PreGame.SetMapScript(mapScript.FileName);
				
				if(mapScript.DefaultCityStates ~= nil) then
					PreGame.SetNumMinorCivs(mapScript.DefaultCityStates);
				else
					-- the world size's usual number of city-states (12 on Small, 4 on Duel...)
					local worldNow = GameInfo.Worlds[PreGame.GetWorldSize()];
					if(worldNow ~= nil) then
						PreGame.SetNumMinorCivs(worldNow.DefaultMinorCivs);
					end
				end
				
				-- If it's a WB Map, we have more to do.
				if(mapScript.WBMapData ~= nil) then
					local wb = mapScript.WBMapData;
						
					PreGame.SetEra(wb.StartEra);
					PreGame.SetGameSpeed(wb.DefaultSpeed);
					PreGame.SetMaxTurns(wb.MaxTurns);
					PreGame.SetNumMinorCivs(wb.CityStateCount);
					PreGame.SetRandomWorldSize(false);
					PreGame.SetWorldSize(wb.MapSize);
					PreGame.SetRandomWorldSize(false);
					PreGame.SetNumMinorCivs(-1);
					
					local victories = {};
					for _, v in ipairs(wb.VictoryTypes) do
						victories[v] = true;
					end
					
					for row in GameInfo.Victories() do
						PreGame.SetVictory(row.ID, victories[row.Type]);
					end
					
					local numPlayers = wb.PlayerCount;
					if(numPlayers == 0) then
						numPlayers = GameInfo.Worlds[wb.MapSize].DefaultPlayers	
					end
					
					for i = numPlayers, GameDefines.MAX_MAJOR_CIVS - 1 do
						if( PreGame.GetSlotStatus(i) == SlotStatus.SS_COMPUTER) then
							PreGame.SetSlotStatus(i, SlotStatus.SS_OPEN);
						end
					end
					
					ScreenOptions["Teams"].FullSync();
				end				
			end
			
			PerformPartialSync();
		end);	
	end,
	
	PartialSync = function()
		local pullDown = Controls.MapTypePullDown;
		if( not PreGame.IsRandomMapScript() ) then 
			
			local bFound = false;
			local mapScriptFileName = PreGame.GetMapScript();
			for row in GameInfo.MapScripts{FileName = mapScriptFileName} do
				pullDown:GetButton():LocalizeAndSetText(row.Name);
				pullDown:GetButton():LocalizeAndSetToolTip(row.Description or "");
				bFound = true;
				break;
			end
			
			if(not bFound) then
				for row in GameInfo.Map_Sizes{FileName = mapScriptFileName} do
					local mapEntry = GameInfo.Maps[row.MapType];
					if(mapEntry ~= nil) then
						local pullDownButton = pullDown:GetButton();
						pullDownButton:LocalizeAndSetText(mapEntry.Name);
						pullDownButton:LocalizeAndSetToolTip(mapEntry.Description or "");
						bFound = true;
					end
				end
			end
			
			if(not bFound) then
				for _, map in ipairs(Modding.GetMapFiles()) do
					if(map.File == mapScriptFileName) then
						
						local mapData = UI.GetMapPreview(map.File);
						local name = "";
						local description = "";
					
						if(not Locale.IsNilOrWhitespace(map.Name)) then
							name = map.Name;
						elseif(not Locale.IsNilOrWhitespace(mapData.Name)) then
							name = mapData.Name;
						else
							name = Path.GetFileNameWithoutExtension(map.File);
						end
						
						if(not Locale.IsNilOrWhitespace(map.Description)) then
							description = map.Description;
						elseif(mapData.Description and #mapData.Description > 0) then
							description = mapData.Description;
						end
					
						pullDown:GetButton():LocalizeAndSetText(name);
						pullDown:GetButton():LocalizeAndSetToolTip(description);
						bFound = true;
						break;
					end
				end
			end
	        
			if(not bFound) then
				PreGame.SetRandomMapScript(true);
			end
		end
	    
		if(PreGame.IsRandomMapScript()) then
			pullDown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_MAP_SCRIPT");
			pullDown:GetButton():LocalizeAndSetToolTip("TXT_KEY_RANDOM_MAP_SCRIPT_HELP");
		end
	end,
},
----------------------------------------------------------------

----------------------------------------------------------------
["MaxTurns"] = {
	FullSync = function()
		Controls.MaxTurnsCheck:RegisterCallback( Mouse.eLClick, function()
			if(Controls.MaxTurnsCheck:IsChecked()) then
				Controls.MaxTurnsEditbox:SetHide(false);
			else
				Controls.MaxTurnsEditbox:SetHide(true);
				PreGame.SetMaxTurns(0);
			end
			
			PerformPartialSync();
		end);

		Controls.MaxTurnsEdit:RegisterCallback(function()
			PreGame.SetMaxTurns(Controls.MaxTurnsEdit:GetText());
		end);
		
		ScreenOptions["MaxTurns"].PartialSync();
	end,
	
	PartialSync = function()
		local maxTurns = PreGame.GetMaxTurns();
		if(maxTurns > 0) then
			Controls.MaxTurnsCheck:SetCheck(true);
		end
		Controls.MaxTurnsEditbox:SetHide(not Controls.MaxTurnsCheck:IsChecked());
		Controls.MaxTurnsEdit:SetText(maxTurns);
	end,
},
----------------------------------------------------------------

----------------------------------------------------------------
["MinorCivs"] = {
	FullSync = function()
		function OnMinorCivsSlider( fValue )
		
			local maxMinorCivs = math.min(41, #GameInfo.MinorCivilizations);
			PreGame.SetNumMinorCivs( math.floor( fValue * maxMinorCivs ) );
			Controls.MinorCivsLabel:LocalizeAndSetText("TXT_KEY_AD_SETUP_CITY_STATES", math.floor(fValue * maxMinorCivs));
		end
		Controls.MinorCivsSlider:RegisterSliderCallback( OnMinorCivsSlider );
	end,
	
	PartialSync = function()
		local maxMinorCivs = math.min(41, #GameInfo.MinorCivilizations);
	
		if ( not PreGame.IsRandomWorldSize() ) then
			Controls.MinorCivsSlider:SetHide(false);
			Controls.MinorCivsLabel:SetHide(false);
			
			if (PreGame.GetNumMinorCivs() == -1) then
				PreGame.SetNumMinorCivs(GameInfo.Worlds[ PreGame.GetWorldSize() ].DefaultMinorCivs);
			end
			Controls.MinorCivsSlider:SetValue( PreGame.GetNumMinorCivs() / maxMinorCivs );
			Controls.MinorCivsLabel:LocalizeAndSetText("TXT_KEY_AD_SETUP_CITY_STATES", PreGame.GetNumMinorCivs());
		else
			Controls.MinorCivsSlider:SetHide(true);
			Controls.MinorCivsLabel:SetHide(true);
			
			if (PreGame.GetNumMinorCivs() == -1) then
				Controls.MinorCivsSlider:SetValue(0 / maxMinorCivs);
				Controls.MinorCivsLabel:LocalizeAndSetText("TXT_KEY_AD_SETUP_CITY_STATES", 0);
			else
				Controls.MinorCivsSlider:SetValue(PreGame.GetNumMinorCivs() / maxMinorCivs);
				Controls.MinorCivsLabel:LocalizeAndSetText("TXT_KEY_AD_SETUP_CITY_STATES", PreGame.GetNumMinorCivs());
			end
		end
	end,	
},
----------------------------------------------------------------

----------------------------------------------------------------
["Teams"] = {
	FullSync = function()
		function PopulateTeamPulldown( pullDown, playerID )
			local count = 0;

			pullDown:ClearEntries();
			
			-- Display Each Player
			local controlTable = {};
			pullDown:BuildEntry( "InstanceOne", controlTable );
			
			controlTable.Button:LocalizeAndSetText("TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", 1);
			controlTable.Button:SetVoids( playerID, 0 );
			
			for i = 1, GameDefines.MAX_MAJOR_CIVS-1, 1 do
				if( PreGame.GetSlotStatus( i ) == SlotStatus.SS_COMPUTER ) then
					local controlTable = {};
					pullDown:BuildEntry( "InstanceOne", controlTable );
					
					controlTable.Button:LocalizeAndSetText("TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", i + 1);
					controlTable.Button:SetVoids( playerID, i );
				end
			end    

			pullDown:CalculateInternals();
			pullDown:RegisterSelectionCallback(function(playerID, playerChoiceID)
				
				PreGame.SetTeam(playerID, playerChoiceID);
				local slotInstance = g_SlotInstances[playerID];
				
				if( slotInstance ~= nil ) then
					slotInstance.TeamLabel:LocalizeAndSetText( "TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", PreGame.GetTeam(playerID) + 1 );
				else
					Controls.TeamLabel:LocalizeAndSetText( "TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", PreGame.GetTeam(playerID) + 1 );
				end
				
				PerformValidation();
			end);
			
			
			local team = PreGame.GetTeam(playerID);
			local slotInstance = g_SlotInstances[playerID];
			
			if( slotInstance ~= nil ) then
				slotInstance.TeamLabel:LocalizeAndSetText( "TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", team + 1 );
			else
				Controls.TeamLabel:LocalizeAndSetText( "TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", team + 1 );
			end

		end
	
		PopulateTeamPulldown( Controls.TeamPullDown, 0);
		for i = 1, GameDefines.MAX_MAJOR_CIVS-1, 1 do
			PopulateTeamPulldown( g_SlotInstances[i].TeamPullDown, i);
		end		
	end,
	
	PartialSync = function()
		Controls.TeamLabel:LocalizeAndSetText( "TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", PreGame.GetTeam(0) + 1 );
		for i = 1, GameDefines.MAX_MAJOR_CIVS-1, 1 do
			local team = PreGame.GetTeam(i);
			g_SlotInstances[i].TeamLabel:LocalizeAndSetText( "TXT_KEY_MULTIPLAYER_DEFAULT_TEAM_NAME", team + 1 );
		end		
	end,
	
	Validate = function(args)
		local playerTeam = PreGame.GetTeam(0);
	    
		for i = 1, GameDefines.MAX_MAJOR_CIVS-1 do
			if( PreGame.GetSlotStatus(i) == SlotStatus.SS_COMPUTER ) then
        		if( PreGame.GetTeam(i) ~= playerTeam ) then
        			return;
        		end
    		end
		end
		
		args.Valid = false;
		args.Reason = "TXT_KEY_BAD_TEAMS";
	end,
},
-------------------------------------------------

----------------------------------------------------------------
["VictoryConditions"] =	{
	FullSync = function()
		g_VictoryCondtionsManager:ResetInstances();
	
		for row in GameInfo.Victories() do
			if(row.Type ~= "VICTORY_SCRAP" and row.Type ~= "VICTORY_REMAP") then
				local victoryCondition = g_VictoryCondtionsManager:GetInstance();
				
				local victoryConditionTextButton = victoryCondition.GameOptionRoot:GetTextButton();
				victoryConditionTextButton:LocalizeAndSetText(row.Description);
				
				victoryCondition.GameOptionRoot:SetCheck(PreGame.IsVictory(row.ID));
				
				victoryCondition.GameOptionRoot:RegisterCheckHandler( function(bCheck)
				PreGame.SetVictory(row.ID, bCheck);
				end);
			end
		end
		Controls.VictoryConditionsStack:CalculateSize();
		Controls.VictoryConditionsStack:ReprocessAnchoring();
	end,
	
	PartialSync = function()
		ScreenOptions["VictoryConditions"].FullSync();
	end,
},
----------------------------------------------------------------

}
-------------------------------------------------

----------------------------------------------------------------
-- ScreenOptions methods
-- Used to manage the entries in ScreenOptions
----------------------------------------------------------------
function ForEachScreenOption(func, ...)
	for _,v in pairs(ScreenOptions) do
		if(v[func]) then
			v[func](...);
		end
	end
end
------------------------------------------------------------------
function PerformFullSync()
	ForEachScreenOption("FullSync");
end
------------------------------------------------------------------
function PerformPartialSync()
	ForEachScreenOption("PartialSync");
end
------------------------------------------------------------------
function PerformValidation()
	local args = {Valid = true};
	ForEachScreenOption("Validate", args);
	
	Controls.StartButton:SetDisabled(not args.Valid);
	
	if(not args.Valid) then
		Controls.StartButton:LocalizeAndSetToolTip(args.Reason);
	else
		Controls.StartButton:SetToolTipString(nil);
	end
end
------------------------------------------------------------------

------------------------------------------------------------------
-- Edit Player Details
------------------------------------------------------------------
Controls.EditButton:RegisterCallback( Mouse.eLClick, function()
	UIManager:PushModal(Controls.SetCivNames);
end);
-------------------------------------------------
function OnCancelEditPlayerDetails()
	Controls.RemoveButton:SetHide(true);
	Controls.CivName:SetHide(true);
	Controls.CivPulldown:SetHide(false);

	PreGame.SetLeaderName( 0, "");
	PreGame.SetCivilizationDescription( 0, "");
	PreGame.SetCivilizationShortDescription( 0, "");
	PreGame.SetCivilizationAdjective( 0, "");
	
	local civIndex = PreGame.GetCivilization( 0 );
    if( civIndex ~= -1 ) then
		local civ = GameInfo.Civilizations[ civIndex ];

        -- Use the Civilization_Leaders table to cross reference from this civ to the Leaders table
        local leader = GameInfo.Leaders[GameInfo.Civilization_Leaders( "CivilizationType = '" .. civ.Type .. "'" )().LeaderheadType];
		local leaderDescription = leader.Description;
		
		Controls.CivPulldown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_LEADER_CIV", Locale.ConvertTextKey(leaderDescription), Locale.ConvertTextKey(civ.ShortDescription));
	else
		Controls.CivPulldown:GetButton():LocalizeAndSetText("TXT_KEY_RANDOM_CIV");
	end
end
Controls.RemoveButton:RegisterCallback( Mouse.eLClick, OnCancelEditPlayerDetails );
------------------------------------------------------------------



   
---------------------------------------------------------------- 
-- Add AI Button Handler
---------------------------------------------------------------- 
function OnAdAIClicked()
    -- skip player 0 
    for i = 1, GameDefines.MAX_MAJOR_CIVS-1, 1 do
        if( PreGame.GetSlotStatus(i) ~= SlotStatus.SS_COMPUTER) then
            PreGame.SetSlotStatus(i, SlotStatus.SS_COMPUTER);
            PreGame.SetCivilization(i, -1);
            break;
        end
    end
	ScreenOptions["Teams"].FullSync();
    PerformPartialSync();
    PerformValidation();
end
Controls.AddAIButton:RegisterCallback(Mouse.eLClick, OnAdAIClicked);


---------------------------------------------------------------- 
-- Back Button Handler
---------------------------------------------------------------- 
function OnBackClicked()
    UIManager:DequeuePopup( ContextPtr );
end
Controls.BackButton:RegisterCallback(Mouse.eLClick, OnBackClicked);

---------------------------------------------------------------- 
-- Start Button Handler
---------------------------------------------------------------- 
function OnStartClicked()
	PreGame.SetPersistSettings(true);
	
	Events.SerialEventStartGame();
	UIManager:SetUICursor( 1 );
end
Controls.StartButton:RegisterCallback(Mouse.eLClick, OnStartClicked);


---------------------------------------------------------------- 
-- Defaults Button Handler
---------------------------------------------------------------- 
function OnDefaultsClicked()
	Controls.RemoveButton:SetHide(true);

	-- Default custom civ names
	PreGame.SetLeaderName( 0, "");
	PreGame.SetCivilizationDescription( 0, "");
	PreGame.SetCivilizationShortDescription( 0, "");
	PreGame.SetCivilizationAdjective( 0, "");
	
	for i = 0, GameDefines.MAX_MAJOR_CIVS - 1, 1 do
		PreGame.SetCivilization(i, -1);
		PreGame.SetTeam(i, i);
	end
	
	-- Default Map Size
	local worldSize = GameInfo.Worlds["WORLDSIZE_SMALL"];
	if(worldSize == nil) then
		worldSize = GameInfo.Worlds()(); -- Get first world size found.
	end
	PreGame.SetRandomWorldSize( false );
	PreGame.SetWorldSize( worldSize.ID );
	PreGame.SetNumMinorCivs( worldSize.DefaultMinorCivs );
	
	-- Default Map Type
	PreGame.SetLoadWBScenario(false);
	
	local mapScript = GameInfo.MapScripts{FileName = "Assets\\Maps\\Continents.lua"}();
	if(mapScript ~= nil) then
		PreGame.SetRandomMapScript(false);
		PreGame.SetMapScript(mapScript.FileName);
	else
		PreGame.SetRandomMapScript(true);
	end
	
	-- Default Game Pace
	local gameSpeed = GameInfo.GameSpeeds["GAMESPEED_STANDARD"];
	if(gameSpeed == nil) then
		gameSpeed = GameInfo.GameSpeeds()();
	end
	PreGame.SetGameSpeed(gameSpeed.ID);
	
	-- Default Start Era
	local era = GameInfo.Eras["ERA_ANCIENT"];
	if(era == nil) then
		era = GameInfo.Eras()();
	end
	PreGame.SetEra(era.ID);
	
	--Default Difficulty to Chieftain
	local handicap = GameInfo.HandicapInfos["HANDICAP_PRINCE"];
	if(handicap == nil) then
		handicap = GameInfo.HandicapInfos()(); --Get first handicap info found.
	end
	PreGame.SetHandicap( 0, handicap.ID );
	

	for row in GameInfo.Victories() do
		PreGame.SetVictory(row.ID, true);
	end
		
	-- Reset Max Turns
	PreGame.SetMaxTurns(0);
	
	PreGame.ResetGameOptions();
	PreGame.ResetMapOptions();
	
	-- Update elements
	PerformPartialSync();
	PerformValidation();
end
Controls.DefaultButton:RegisterCallback( Mouse.eLClick, OnDefaultsClicked );
---------------------------------------------------------------- 

----------------------------------------------------------------
-- Input Handler
----------------------------------------------------------------
function InputHandler( uiMsg, wParam, lParam )
	if uiMsg == KeyEvents.KeyDown then
		if wParam == Keys.VK_ESCAPE then
		    OnBackClicked();
        	return true;
		end
	end
	
end
ContextPtr:SetInputHandler( InputHandler );
----------------------------------------------------------------     

----------------------------------------------------------------
-- Visibility Handler
---------------------------------------------------------------- 
-- TeamerBalance: the first time this screen opens after the main menu, the game mode starts as Normal KEK
-- (later visits keep what the player picked). The front end reloads this file after every game.
g_TeamerModeResetDone = false;
function ShowHideHandler( bIsHide, bInit )
	if (not bIsHide) then
		if(not g_TeamerModeResetDone) then
			g_TeamerModeResetDone = true;
			SetTeamerMode(0);
		end
		PerformPartialSync();
		PerformValidation();
	end
end
ContextPtr:SetShowHideHandler( ShowHideHandler );
---------------------------------------------------------------- 

-----------------------------------------------------------------
-- Adjust for resolution
-----------------------------------------------------------------
function AdjustScreenSize()
    local _, screenY = UIManager:GetScreenSizeVal();

    local TOP_COMPENSATION = 52 + ((screenY - 768) * 0.3 );
    local BOTTOM_COMPENSATION = 190;
    local LOCAL_SLOT_COMPENSATION = 74;

    Controls.MainGrid:SetSizeY( screenY - TOP_COMPENSATION );
    Controls.ListingScrollPanel:SetSizeY( screenY - TOP_COMPENSATION - BOTTOM_COMPENSATION - LOCAL_SLOT_COMPENSATION );
    Controls.OptionsScrollPanel:SetSizeY( screenY - TOP_COMPENSATION - BOTTOM_COMPENSATION );
    
    Controls.ListingScrollPanel:CalculateInternalSize();
    Controls.OptionsScrollPanel:CalculateInternalSize();
end

-------------------------------------------------
-------------------------------------------------
function OnUpdateUI( type )
    if( type == SystemUpdateUIType.ScreenResize ) then
        AdjustScreenSize();
    end
end
Events.SystemUpdateUI.Add( OnUpdateUI );


-- When mods affect game state, perform a full sync.
Events.AfterModsActivate.Add(function()
	PerformFullSync();
end);

Events.AfterModsDeactivate.Add(function()
	PerformFullSync();
end);

-- This is a one time initialization function for UI elements.
-- It should NEVER be called more than once.
function CreateSlotInstances()
	for i = 1, GameDefines.MAX_MAJOR_CIVS-1, 1 do
		g_SlotInstances[i] = {};
		ContextPtr:BuildInstanceForControl( "PlayerSlot", g_SlotInstances[i], Controls.SlotStack );
		g_SlotInstances[i].Root:SetHide( true );	
	end
end

AdjustScreenSize();
CreateSlotInstances();
PerformFullSync();
