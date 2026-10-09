-------------------------------------------------
-- FrontEnd
-------------------------------------------------

-- KEK Mod: pixel size of KekModBackground.dds
local BACKGROUND_W, BACKGROUND_H = 1920, 1200;

-- KEK Mod: scale the background to cover the screen, keeping its aspect ratio.
-- The overflow is cut evenly off both sides (AtlasLogo is anchored C,C).
-- Stock draws it at a fixed 1920x1200, leaving black bars on bigger screens.
function ResizeBackground()
	local x, y = UIManager:GetScreenSizeVal();
	if x == nil or y == nil or x <= 0 or y <= 0 then
		return;
	end
	local scale = math.max( x / BACKGROUND_W, y / BACKGROUND_H );
	Controls.AtlasLogo:SetSizeVal( math.ceil( BACKGROUND_W * scale ), math.ceil( BACKGROUND_H * scale ) );
	-- Sample the whole texture, not a screen-sized window of it
	Controls.AtlasLogo:SetTextureSizeVal( BACKGROUND_W, BACKGROUND_H );
	Controls.AtlasLogo:ReprocessAnchoring();
end

function ShowHideHandler( bIsHide, bIsInit )

		-- Check for game invites first.  If we have a game invite, we will have flipped 
		-- the Civ5App::eHasShownLegal and not show the legal/touch screens.
		UI:CheckForCommandLineInvitation();
		
    if( not UI:HasShownLegal() ) then
        UIManager:QueuePopup( Controls.LegalScreen, PopupPriority.LegalScreen );
    end

    if( not bIsHide ) then
        -- KEK Mod: own background, scaled to cover the screen
        Controls.AtlasLogo:SetTexture( "KekModBackground.dds" );
        ResizeBackground();
    	UIManager:SetUICursor( 0 );
        UIManager:QueuePopup( Controls.MainMenu, PopupPriority.MainMenu );
    else
        Controls.AtlasLogo:UnloadTexture();
    end
end
ContextPtr:SetShowHideHandler( ShowHideHandler );

-- KEK Mod: rescale when the window size changes
Events.SystemUpdateUI.Add( function( type )
	if type == SystemUpdateUIType.ScreenResize then
		ResizeBackground();
	end
end );
