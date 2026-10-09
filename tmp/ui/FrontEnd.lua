-------------------------------------------------
-- FrontEnd
-------------------------------------------------

-- KEK Mod: pixel size of KekModBackground.dds
local BACKGROUND_W, BACKGROUND_H = 1920, 1200;

-- KEK Mod: fill the screen with the background, keeping its aspect ratio.
-- The control stays exactly screen-sized and the texture is cropped instead
-- (centred), because a control bigger than the screen makes the FrontEnd
-- context grow with it and pushes every centred menu off-centre.
-- Stock draws it at a fixed 1920x1200, leaving black bars on bigger screens.
function ResizeBackground()
	local x, y = UIManager:GetScreenSizeVal();
	if x == nil or y == nil or x <= 0 or y <= 0 then
		return;
	end
	local w, h = BACKGROUND_W, BACKGROUND_H;
	if x / y > w / h then
		h = math.floor( w * y / x );
	else
		w = math.floor( h * x / y );
	end
	Controls.AtlasLogo:SetSizeVal( x, y );
	Controls.AtlasLogo:SetTextureOffsetVal( math.floor( ( BACKGROUND_W - w ) / 2 ), math.floor( ( BACKGROUND_H - h ) / 2 ) );
	Controls.AtlasLogo:SetTextureSizeVal( w, h );
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
