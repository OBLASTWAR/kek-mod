-------------------------------------------------
-- Main Menu
-------------------------------------------------
include( "MPGameDefaults" );

-------------------------------------------------
-- Script Body
-------------------------------------------------
local bHideUITest = true;
local bHideGridExamples = true;
local bHideLoadGame = true;
local bHidePreGame = true;
local fTime = 0;
local i1, i2 = string.find( UI.GetVersionInfo(), " " );
versionNumber = string.sub(UI.GetVersionInfo(), 1, i2-1);
Controls.VersionNumber:SetText(versionNumber);

-------------------------------------------------
-- kekmod: installed version + update check, shown above the game build number.
-- The version is stamped at package/deploy time from KEKMOD_MOD_VERSION in
-- CvHttpUtils.cpp. The check reads the GitHub releases Atom feed (plain URL, no
-- User-Agent needed, unlike the API) through the undocumented vanilla
-- Network.HttpRequest. Stable installs compare against stable tags only, betas
-- against every tag, so stable players aren't nagged about betas.
-------------------------------------------------
local KEKMOD_VERSION = "@KEKMOD_VERSION@";
local KEK_RELEASES_FEED = "https://github.com/OBLASTWAR/kek-mod/releases.atom";
local KEK_RELEASE_PAGE = "http://github.com/OBLASTWAR/kek-mod/releases/tag/"; -- http: Steam overlay is flaky with https
local KEK_CHECK_TIMEOUT = 10; -- seconds

local g_KekLatest = nil;      -- newer tag than ours ("v2.4"), once the check found one
local g_KekCheckState = nil;  -- nil (not started), "running", "done", "failed"

-- "v2.3-beta2" -> { 2, 3, 0, 2 }; a stable sorts after its betas (4th part huge).
local function KekVersionKey( s )
	local major, minor, rest = string.match( s, "^v?(%d+)%.(%d+)(.*)$" );
	if major == nil then
		return nil;
	end
	local patch = tonumber( string.match( rest, "^%.(%d+)" ) ) or 0;
	local beta = tonumber( string.match( rest, "%-beta(%d+)" ) ) or 1e9;
	return { tonumber( major ), tonumber( minor ), patch, beta };
end

local function KekVersionNewer( a, b )
	for i = 1, 4 do
		if a[i] ~= b[i] then
			return a[i] > b[i];
		end
	end
	return false;
end

local function KekText( key, fallback, ... )
	local text = Locale.ConvertTextKey( key, ... );
	if text == nil or text == "" or text == key then
		return fallback;
	end
	return text;
end

local function KekRenderVersion()
	local v = KEKMOD_VERSION;
	local label, tip;
	if string.sub( v, 1, 1 ) == "@" then
		label = "KEK Mod (unstamped dev build)";
	elseif g_KekCheckState == "done" and g_KekLatest ~= nil then
		local latest = string.sub( g_KekLatest, 2 );
		label = KekText( "TXT_KEY_KEK_MENU_VERSION_OUTDATED",
			"KEK Mod v" .. v .. " - [COLOR_NEGATIVE_TEXT]v" .. latest .. " is out, update with the KEK Mod installer[ENDCOLOR]", v, latest );
		tip = KekText( "TXT_KEY_KEK_MENU_VERSION_OUTDATED_TT", "Click to open the v" .. latest .. " release page.", latest );
	elseif g_KekCheckState == "done" then
		label = KekText( "TXT_KEY_KEK_MENU_VERSION_UPTODATE", "KEK Mod v" .. v .. " - [COLOR_POSITIVE_TEXT]up to date[ENDCOLOR]", v );
	elseif g_KekCheckState == "failed" then
		label = KekText( "TXT_KEY_KEK_MENU_VERSION_UNREACHABLE", "KEK Mod v" .. v .. " - could not check for updates", v );
	else
		label = KekText( "TXT_KEY_KEK_MENU_VERSION_CHECKING", "KEK Mod v" .. v .. " - checking for updates...", v );
	end
	Controls.KekVersionLabel:SetText( label );
	Controls.KekVersionButton:SetToolTipString( tip or "" );
end

-- Newest tag in the feed that is newer than ours, or nil if we're current.
local function KekNewerTag( body )
	local mine = KekVersionKey( KEKMOD_VERSION );
	local mineIsBeta = mine[4] < 1e9;
	local best, bestKey = nil, mine;
	for tag in string.gmatch( body, "/releases/tag/([^\"<]+)" ) do
		local key = KekVersionKey( tag );
		if key ~= nil and ( mineIsBeta or key[4] == 1e9 ) and KekVersionNewer( key, bestKey ) then
			best, bestKey = tag, key;
		end
	end
	return best;
end

local function KekFinishCheck( state, latest )
	ContextPtr:ClearUpdate();
	g_KekCheckState = state;
	g_KekLatest = latest;
	KekRenderVersion();
end

local function KekStartVersionCheck()
	if g_KekCheckState ~= nil or KekVersionKey( KEKMOD_VERSION ) == nil then
		return;
	end
	if Network == nil or type( Network.HttpRequest ) ~= "function" then
		g_KekCheckState = "failed";
		return;
	end
	local ok, req = pcall( Network.HttpRequest, KEK_RELEASES_FEED );
	if not ok or req == nil then
		g_KekCheckState = "failed";
		return;
	end
	g_KekCheckState = "running";
	local tStart = os.time();
	ContextPtr:SetUpdate( function()
		local okF, finished = pcall( function() return req.Finished(); end );
		if okF and finished then
			local body = nil;
			pcall( function() body = req.PopReceivedData(); end );
			if body ~= nil and string.find( body, "/releases/tag/", 1, true ) then
				KekFinishCheck( "done", KekNewerTag( body ) );
			else
				KekFinishCheck( "failed" );
			end
		elseif not okF or os.time() - tStart > KEK_CHECK_TIMEOUT then
			KekFinishCheck( "failed" );
		end
	end );
end

Controls.KekVersionButton:RegisterCallback( Mouse.eLClick, function()
	if g_KekLatest ~= nil then
		pcall( function() Steam.ActivateGameOverlayToWebPage( KEK_RELEASE_PAGE .. g_KekLatest ); end );
	end
end );


function ShowHideHandler( bIsHide, bIsInit )
    if( not bIsHide ) then
        Controls.Civ5Logo:SetTexture( "CivilzationV_Logo.dds" );
        KekStartVersionCheck();
        KekRenderVersion();
        
        -- This is a catch all to ensure that mods are not activated at this point in the UI.
        -- Also, since certain maps and settings will only be available in either the modding or multiplayer
        -- screen, we want to ensure that "safe" settings are loaded that can be used for either SP, MP or Mods.
        -- Activating the DLC (there doesn't have to be any) will make sure no mods are active and all the user's
        -- purchased content is available
        if (not ContextPtr:IsHotLoad()) then
			UIManager:SetUICursor( 1 );
			Modding.ActivateDLC();
			PreGame.LoadPreGameSettings();
			UIManager:SetUICursor( 0 );
			
			-- Send out an event to continue on, as the ActivateDLC may have swapped out the UI	
			Events.SystemUpdateUI( SystemUpdateUIType.RestoreUI, "MainMenu" );
		end
    else
        Controls.Civ5Logo:UnloadTexture();
    end
end
ContextPtr:SetShowHideHandler( ShowHideHandler );

-------------------------------------------------
-- Event Handler: ConnectedToNetworkHost
-------------------------------------------------

-------------------------------------------------
-- StartGame Button Handler
-------------------------------------------------
function SinglePlayerClick()
	UIManager:QueuePopup( Controls.SinglePlayerScreen, PopupPriority.SinglePlayerScreen );
end
Controls.SinglePlayerButton:RegisterCallback( Mouse.eLClick, SinglePlayerClick );

-------------------------------------------------
-- Multiplayer Button Handler
-------------------------------------------------
function MultiplayerClick()
    UIManager:QueuePopup( Controls.MultiplayerSelectScreen, PopupPriority.MultiplayerSelectScreen );
end
Controls.MultiplayerButton:RegisterCallback( Mouse.eLClick, MultiplayerClick );


-------------------------------------------------
-- Mods button handler
-------------------------------------------------
function ModsButtonClick()
    UIManager:QueuePopup( Controls.ModsEULAScreen, PopupPriority.ModsEULAScreen );
end
Controls.ModsButton:RegisterCallback( Mouse.eLClick, ModsButtonClick );


-------------------------------------------------
-- UITest Button Handler
-------------------------------------------------
--[[
function UITestRClick()
    bHideUITest = not bHideUITest;
    Controls.UITestScreen:SetHide( bHideUITest );
end
Controls.OptionsButton:RegisterCallback( Mouse.eRClick, UITestRClick );
--]]


-------------------------------------------------
-- Options Button Handler
-------------------------------------------------
function OptionsClick()
    UIManager:QueuePopup( Controls.OptionsMenu_FrontEnd, PopupPriority.OptionsMenu );
end
Controls.OptionsButton:RegisterCallback( Mouse.eLClick, OptionsClick );


-------------------------------------------------
-- Hall Of Fame Button Handler
-------------------------------------------------
function OtherClick()
    UIManager:QueuePopup( Controls.Other, PopupPriority.OtherMenu );
end
Controls.OtherButton:RegisterCallback( Mouse.eLClick, OtherClick );


-------------------------------------------------
-- Exit Button Handler
-------------------------------------------------
function OnExitGame()
	Events.UserRequestClose();
end
Controls.ExitButton:RegisterCallback( Mouse.eLClick, OnExitGame );


----------------------------------------------------------------        
----------------------------------------------------------------
Steam.SetOverlayNotificationPosition( "bottom_left" );

-------------------------------------------------
-- Event Handler: MultiplayerGameLaunched
-------------------------------------------------
function OnGameLaunched()

	UIManager:DequeuePopup( ContextPtr );

end
Events.MultiplayerGameLaunched.Add( OnGameLaunched );


-- Returns -1 if time1 < time2, 0 if equal, 1 if time1 > time 2
function CompareTime(time1, time2)
	
	--First, convert the table into a single numerical value
	-- YYYYMMDDHH
	function convert(t)
		local r = 0;
		if(t.year ~= nil) then
			r = r + t.year * 1000000
		end
		
		if(t.month ~= nil) then
			r = r + t.month * 10000
		end
		
		if(t.day ~= nil) then
			r = r + t.day * 100
		end
		
		if(t.hour ~= nil) then
			r = r + t.hour;
		end
		
		return r;
	end
	
	local ct1 = convert(time1);
	local ct2 = convert(time2);
	
	if(ct1 < ct2) then
		return -1;
	elseif(ct1 > ct2) then
		return 1;
	else
		return 0;
	end
end

function DisplayDLCButtons()
	local ButtonsDisplayUntil = {};
	
	if (Controls.MapPack2PromoButton ~= nil) then
		ButtonsDisplayUntil[Controls.MapPack2PromoButton] = {
			start = {
				month = 10,
				day = 2,
				year = 2013,
				hour = 12,
			},
			
			stop = {
				year = 2013,
				month = 11, 
				day = 4,
				hour = 12,
			},		
			
			customurl = "http://store.steampowered.com/app/235584/",
		}
	end
	
	if (Controls.MapPack3PromoButton ~= nil) then
		ButtonsDisplayUntil[Controls.MapPack3PromoButton] = {
			start = {
				month = 11,
				day = 12,
				year = 2013,
				hour = 17,
			},
			
			stop = {
				year = 2013,
				month = 11, 
				day = 25,
				hour = 12,
			},
			
			customurl = "http://store.steampowered.com/app/235585/",
		}
	end
	
	if (Controls.AcePatrolPromoButton ~= nil) then
		ButtonsDisplayUntil[Controls.AcePatrolPromoButton] = {
			start = {
				month = 11,
				day = 5,
				year = 2013,
				hour = 12,
			},
			
			stop = {
				year = 2013,
				month = 11, 
				day = 11,
				hour = 12,
			},
			
			customurl = "http://store.steampowered.com/app/244090/",
		}
	end	
	
	local currentDate = os.date("!*t");

	for k,v in pairs(ButtonsDisplayUntil) do
		local bShow = false;
		
		if(CompareTime(currentDate, v.start) >= 0 and CompareTime(v.stop, currentDate) >= 0) then
			bShow = true;
		end
		
		
		k:SetHide(not bShow);
		
		k:RegisterCallback(Mouse.eLClick, function()
			if(v.customurl == nil) then
				Steam.ActivateGameOverlayToStore();
			else
				Steam.ActivateGameOverlayToWebPage(v.customurl);
			end
		end);
	end
end

DisplayDLCButtons();

----------------------------------------------------------------        
function OnExpansionRulesSwitch()
	UIManager:QueuePopup( Controls.PremiumContentScreen, PopupPriority.OtherMenu );
end		
Controls.ExpansionRulesSwitch:RegisterCallback(Mouse.eLClick, OnExpansionRulesSwitch);

-------------------------------------------------------------------------------
function OnSystemUpdateUI( type, tag  )
    if( type == SystemUpdateUIType.RestoreUI) then
		if (tag == "MainMenu") then
			-- Look for any cached invite
			UI:CheckForCommandLineInvitation();    		
			
			if (Network.IsDedicatedServer()) then
					ResetMultiplayerOptions(); 
			    UIManager:QueuePopup( ContextPtr:LookUpControl( "DedicatedServerScreen" ), PopupPriority.LobbyScreen );
			end
		elseif (tag == "StagingRoom") then
			if (UIManager:GetVisibleNamedContext("StagingRoom") == nil) then
				UIManager:QueuePopup( Controls.StagingRoomScreen, PopupPriority.StagingScreen );
			end
		elseif (tag == "ScenariosMenuReset") then			
			local pScenarioScreen = ContextPtr:LookUpControl( "SinglePlayerScreen/ScenariosScreen" );
			if (pScenarioScreen ~= nil) then
				if (pScenarioScreen:IsHidden()) then						
					UIManager:QueuePopup( pScenarioScreen, PopupPriority.GameSetupScreen );
				end
			end
		elseif (tag == "ModsBrowserReset") then
			local pModsMenu = ContextPtr:LookUpControl("ModsEULAScreen/ModsBrowser" );
			if(pModsMenu ~= nil) then
				if(pModsMenu:IsHidden()) then
					UIManager:QueuePopup(pModsMenu, PopupPriority.ModsBrowserScreen);
				end
			end 
		elseif (tag == "ModsMenu" ) then
			local pModsMenu = ContextPtr:LookUpControl("ModsEULAScreen/ModsBrowser/ModsMenu" );
			if(pModsMenu ~= nil) then
				if(pModsMenu:IsHidden()) then
					UIManager:QueuePopup(pModsMenu, PopupPriority.ModsMenuScreen);
				end
			end 
	    end
	end
end

Events.SystemUpdateUI.Add( OnSystemUpdateUI );

-------------------------------------------------------------------------------
if(UI.IsTouchScreenEnabled()) then
	function OnTouchHelpButton()
		Controls.TouchControlsMenu:SetHide( false );
	end		
	Controls.TouchHelpButton:RegisterCallback( Mouse.eLClick, OnTouchHelpButton );
	Controls.TouchHelpButton:SetHide(false);
	OnTouchHelpButton();
else
	Controls.TouchHelpButton:SetHide(true);
	
end

