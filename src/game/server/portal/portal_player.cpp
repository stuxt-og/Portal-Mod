//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for HL2.
//
//=============================================================================//

#include "cbase.h"
#include <portal/portal_player.h>
#include "globalstate.h"
#include "game.h"
#include "gamerules.h"
#include "trains.h"
#include "basehlcombatweapon_shared.h"
#include "vcollide_parse.h"
#include "in_buttons.h"
#include "ai_interactions.h"
#include "ai_squad.h"
#include "igamemovement.h"
#include "ai_hull.h"
#include "hl2_shareddefs.h"
#include "info_camera_link.h"
#include "point_camera.h"
#include "engine/IEngineSound.h"
#include "ndebugoverlay.h"
#include "iservervehicle.h"
#include "IVehicle.h"
#include "globals.h"
#include "collisionutils.h"
#include "coordsize.h"
#include "effect_color_tables.h"
#include "vphysics/player_controller.h"
#include "player_pickup.h"
#include "weapon_physcannon.h"
#include "script_intro.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h" 
#include "ai_basenpc.h"
#include "AI_Criteria.h"
#include "entitylist.h"
#include "env_zoom.h"
#include <portal/portal_gamerules.h>
#include "datacache/imdlcache.h"
#include "eventqueue.h"
#include "gamestats.h"
#include "filters.h"
#include "tier0/icommandline.h"

#ifdef MAPBASE
#include "triggers.h"
#include "mapbase/variant_tools.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar weapon_showproficiency;
extern ConVar autoaim_max_dist;

// Do not touch with without seeing me, please! (sjb)
// For consistency's sake, enemy gunfire is traced against a scaled down
// version of the player's hull, not the hitboxes for the player's model
// because the player isn't aware of his model, and can't do anything about
// preventing headshots and other such things. Also, game difficulty will
// not change if the model changes. This is the value by which to scale
// the X/Y of the player's hull to get the volume to trace bullets against.
#define PLAYER_HULL_REDUCTION	0.70

// This switches between the single primary weapon, and multiple weapons with buckets approach (jdw)
#define	HL2_SINGLE_PRIMARY_WEAPON_MODE	0

#define TIME_IGNORE_FALL_DAMAGE 10.0

extern int gEvilImpulse101;

ConVar sv_autojump( "sv_autojump", "0" );

ConVar hl2_walkspeed( "hl2_walkspeed", "150" );
ConVar hl2_normspeed( "hl2_normspeed", "190" );
ConVar hl2_sprintspeed( "hl2_sprintspeed", "320" );

ConVar hl2_darkness_flashlight_factor ( "hl2_darkness_flashlight_factor", "1" );

#ifdef HL2MP
	#define	HL2_WALK_SPEED 150
	#define	HL2_NORM_SPEED 190
	#define	HL2_SPRINT_SPEED 320
#else
	#define	HL2_WALK_SPEED hl2_walkspeed.GetFloat()
	#define	HL2_NORM_SPEED hl2_normspeed.GetFloat()
	#define	HL2_SPRINT_SPEED hl2_sprintspeed.GetFloat()
#endif

ConVar player_showpredictedposition( "player_showpredictedposition", "0" );
ConVar player_showpredictedposition_timestep( "player_showpredictedposition_timestep", "1.0" );

ConVar player_squad_transient_commands( "player_squad_transient_commands", "1", FCVAR_REPLICATED );
ConVar player_squad_double_tap_time( "player_squad_double_tap_time", "0.25" );

ConVar sv_infinite_aux_power( "sv_infinite_aux_power", "0", FCVAR_CHEAT );

ConVar autoaim_unlock_target( "autoaim_unlock_target", "0.8666" );

ConVar sv_stickysprint("sv_stickysprint", "0", FCVAR_ARCHIVE | FCVAR_ARCHIVE_XBOX);

#ifdef MAPBASE
ConVar player_autoswitch_enabled( "player_autoswitch_enabled", "1", FCVAR_NONE, "This convar was added by Mapbase to toggle whether players automatically switch to their ''best'' weapon upon picking up ammo for it after it was dry." );

#ifdef SP_ANIM_STATE
ConVar hl2_use_sp_animstate( "hl2_use_sp_animstate", "1", FCVAR_NONE, "Allows SP HL2 players to use HL2:DM animations for custom player models. (changes may not apply until model is reloaded)" );
#endif

#endif

#define	FLASH_DRAIN_TIME	 1.1111	// 100 units / 90 secs
#define	FLASH_CHARGE_TIME	 50.0f	// 100 units / 2 secs


//==============================================================================================
// CAPPED PLAYER PHYSICS DAMAGE TABLE
//==============================================================================================
static impactentry_t cappedPlayerLinearTable[] =
{
	{ 150*150, 5 },
	{ 250*250, 10 },
	{ 450*450, 20 },
	{ 550*550, 30 },
	//{ 700*700, 100 },
	//{ 1000*1000, 500 },
};

static impactentry_t cappedPlayerAngularTable[] =
{
	{ 100*100, 10 },
	{ 150*150, 20 },
	{ 200*200, 30 },
	//{ 300*300, 500 },
};

static impactdamagetable_t gCappedPlayerImpactDamageTable =
{
	cappedPlayerLinearTable,
	cappedPlayerAngularTable,

	ARRAYSIZE(cappedPlayerLinearTable),
	ARRAYSIZE(cappedPlayerAngularTable),

	24*24.0f,	// minimum linear speed
	360*360.0f,	// minimum angular speed
	2.0f,		// can't take damage from anything under 2kg

	5.0f,		// anything less than 5kg is "small"
	5.0f,		// never take more than 5 pts of damage from anything under 5kg
	36*36.0f,	// <5kg objects must go faster than 36 in/s to do damage

	0.0f,		// large mass in kg (no large mass effects)
	1.0f,		// large mass scale
	2.0f,		// large mass falling scale
	320.0f,		// min velocity for player speed to cause damage

};

// Flashlight utility
bool g_bCacheLegacyFlashlightStatus = true;
#ifdef MAPBASE
extern ThreeState_t Flashlight_GetLegacyVersionKey();
#endif
bool g_bUseLegacyFlashlight;
bool Flashlight_UseLegacyVersion( void )
{
	// If this is the first run through, cache off what the answer should be (cannot change during a session)
	if ( g_bCacheLegacyFlashlightStatus )
	{
#ifdef MAPBASE
		// Check if there's a gameinfo setting.
		ThreeState_t iGameKey = Flashlight_GetLegacyVersionKey();
		if (iGameKey != TRS_NONE)
		{
			g_bUseLegacyFlashlight = (iGameKey == TRS_TRUE);
			g_bCacheLegacyFlashlightStatus = false;
			return g_bUseLegacyFlashlight;
		}
#endif

		char modDir[MAX_PATH];
		if ( UTIL_GetModDir( modDir, sizeof(modDir) ) == false )
			return false;

		g_bUseLegacyFlashlight = ( !Q_strcmp( modDir, "hl2" ) ||
					   !Q_strcmp( modDir, "episodic" ) ||
					   !Q_strcmp( modDir, "lostcoast" ) || !Q_strcmp( modDir, "hl1" ));

		g_bCacheLegacyFlashlightStatus = false;
	}

	// Return the results
	return g_bUseLegacyFlashlight;
}

//-----------------------------------------------------------------------------
// Purpose: Used to relay outputs/inputs from the player to the world and viceversa
//-----------------------------------------------------------------------------
class CLogicPlayerProxy : public CLogicalEntity
{
	DECLARE_CLASS( CLogicPlayerProxy, CLogicalEntity );

private:

	DECLARE_DATADESC();

public:

	COutputEvent m_OnFlashlightOn;
	COutputEvent m_OnFlashlightOff;
	COutputEvent m_PlayerDied;

	COutputInt m_RequestedPlayerHealth;

#ifdef MAPBASE
	COutputEvent m_PlayerDamaged;
	COutputEvent m_OnPlayerSpawn;
#endif

	void InputRequestPlayerHealth( inputdata_t &inputdata );
	void InputSetPlayerHealth( inputdata_t &inputdata );
	void InputEnableCappedPhysicsDamage( inputdata_t &inputdata );
	void InputDisableCappedPhysicsDamage( inputdata_t &inputdata );

	void InputSuppressCrosshair( inputdata_t &inputdata );

#ifdef MAPBASE
	void InputSetHandModel( inputdata_t &inputdata );
	void InputSetHandModelSkin( inputdata_t &inputdata );
	void InputSetHandModelBodyGroup( inputdata_t &inputdata );

	void InputSetPlayerModel( inputdata_t &inputdata );
	void InputSetPlayerDrawExternally( inputdata_t &inputdata );
#endif

	void Activate ( void );

#ifdef MAPBASE
	bool KeyValue( const char *szKeyName, const char *szValue );

	bool AcceptInput( const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID );

	void NotifyPlayerHasProxy();

	// This is here because the player might not be available when we spawn.
	// Hope there wouldn't be enough time for this to need to be saved...
	CUtlDict<string_t, int> m_QueuedKV;

	int m_SuitZoomFOV = 25;
#endif

	bool PassesDamageFilter( const CTakeDamageInfo &info );

	EHANDLE m_hPlayer;
};

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CC_ToggleZoom( void )
{
	CBasePlayer* pPlayer = UTIL_GetCommandClient();

	if( pPlayer )
	{
		CPortal_Player *pHL2Player = dynamic_cast<CPortal_Player*>(pPlayer);

		if( pHL2Player )
		{
			pHL2Player->ToggleZoom();
		}
	}
}

static ConCommand toggle_zoom("toggle_zoom", CC_ToggleZoom, "Toggles zoom display" );

// ConVar cl_forwardspeed( "cl_forwardspeed", "400", FCVAR_CHEAT ); // Links us to the client's version
ConVar xc_crouch_range( "xc_crouch_range", "0.85", FCVAR_ARCHIVE, "Percentarge [1..0] of joystick range to allow ducking within" );	// Only 1/2 of the range is used
ConVar xc_use_crouch_limiter( "xc_use_crouch_limiter", "0", FCVAR_ARCHIVE, "Use the crouch limiting logic on the controller" );

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void CC_ToggleDuck( void )
{
	CBasePlayer* pPlayer = UTIL_GetCommandClient();
	if ( pPlayer == NULL )
		return;

	// Cannot be frozen
	if ( pPlayer->GetFlags() & FL_FROZEN )
		return;

	static bool		bChecked = false;
	static ConVar *pCVcl_forwardspeed = NULL;
	if ( !bChecked )
	{
		bChecked = true;
		pCVcl_forwardspeed = ( ConVar * )cvar->FindVar( "cl_forwardspeed" );
	}


	// If we're not ducked, do extra checking
	if ( xc_use_crouch_limiter.GetBool() )
	{
		if ( pPlayer->GetToggledDuckState() == false )
		{
			float flForwardSpeed = 400.0f;
			if ( pCVcl_forwardspeed )
			{
				flForwardSpeed = pCVcl_forwardspeed->GetFloat();
			}

			flForwardSpeed = MAX( 1.0f, flForwardSpeed );

			// Make sure we're not in the blindspot on the crouch detection
			float flStickDistPerc = ( pPlayer->GetStickDist() / flForwardSpeed ); // Speed is the magnitude
			if ( flStickDistPerc > xc_crouch_range.GetFloat() )
				return;
		}
	}

	// Toggle the duck
	pPlayer->ToggleDuck();
}

static ConCommand toggle_duck("toggle_duck", CC_ToggleDuck, "Toggles duck" );

#ifndef HL2MP
#ifndef PORTAL
LINK_ENTITY_TO_CLASS( player, CPortal_Player );
#endif
#endif

PRECACHE_REGISTER(player);

CBaseEntity *FindEntityForward( CBasePlayer *pMe, bool fHull );

BEGIN_SIMPLE_DATADESC( LadderMove_t )
	DEFINE_FIELD( m_bForceLadderMove, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bForceMount, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_flArrivalTime, FIELD_TIME ),
	DEFINE_FIELD( m_vecGoalPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecStartPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_hForceLadder, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hReservedSpot, FIELD_EHANDLE ),
END_DATADESC()

// Global Savedata for HL2 player
BEGIN_DATADESC( CPortal_Player )

	DEFINE_FIELD( m_nControlClass, FIELD_INTEGER ),
	DEFINE_EMBEDDED( m_PortalLocal ),

	DEFINE_FIELD( m_bSprintEnabled, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flTimeAllSuitDevicesOff, FIELD_TIME ),
	DEFINE_FIELD( m_fIsSprinting, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fIsWalking, FIELD_BOOLEAN ),

	/*
	// These are initialized every time the player calls Activate()
	DEFINE_FIELD( m_bIsAutoSprinting, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fAutoSprintMinTime, FIELD_TIME ),
	*/

	// 	Field is used within a single tick, no need to save restore
	// DEFINE_FIELD( m_bPlayUseDenySound, FIELD_BOOLEAN ),  
	//							m_pPlayerAISquad reacquired on load

	DEFINE_AUTO_ARRAY( m_vecMissPositions, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_nNumMissPositions, FIELD_INTEGER ),

	// Suit power fields
	DEFINE_FIELD( m_flSuitPowerLoad, FIELD_FLOAT ),

	DEFINE_FIELD( m_flIdleTime, FIELD_TIME ),
	DEFINE_FIELD( m_flMoveTime, FIELD_TIME ),
	DEFINE_FIELD( m_flLastDamageTime, FIELD_TIME ),
	DEFINE_FIELD( m_flTargetFindTime, FIELD_TIME ),

	DEFINE_FIELD( m_bFlashlightDisabled, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_bUseCappedPhysicsDamageTable, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_hLockedAutoAimEntity, FIELD_EHANDLE ),

	DEFINE_EMBEDDED( m_AutoaimTimer ),

	DEFINE_INPUTFUNC( FIELD_VOID, "DisableFlashlight", InputDisableFlashlight ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableFlashlight", InputEnableFlashlight ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ForceDropPhysObjects", InputForceDropPhysObjects ),
#ifdef MAPBASE
	DEFINE_INPUTFUNC( FIELD_VOID, "TurnFlashlightOn", InputTurnFlashlightOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TurnFlashlightOff", InputTurnFlashlightOff ),
#endif

	DEFINE_SOUNDPATCH( m_sndLeeches ),
	DEFINE_SOUNDPATCH( m_sndWaterSplashes ),

	DEFINE_FIELD( m_flTimeUseSuspended, FIELD_TIME ),

	DEFINE_FIELD( m_flTimeNextLadderHint, FIELD_TIME ),

	//DEFINE_FIELD( m_hPlayerProxy, FIELD_EHANDLE ), //Shut up class check!

END_DATADESC()

#ifdef MAPBASE_VSCRIPT
BEGIN_ENT_SCRIPTDESC( CPortal_Player, CBasePlayer, "The HL2 player entity." )

#ifdef SP_ANIM_STATE
	DEFINE_SCRIPTFUNC( AddAnimStateLayer, "Adds a custom sequence index as a misc. layer for the singleplayer anim state, wtih parameters for blending in/out, setting the playback rate, holding the animation at the end, and only playing when the player is still." )
#endif

END_SCRIPTDESC();
#endif

CPortal_Player::CPortal_Player()
{
	m_nNumMissPositions	= 0;
	m_bSprintEnabled = true;
}

//
// SUIT POWER DEVICES
//
#define SUITPOWER_CHARGE_RATE	12.5											// 100 units in 8 seconds

#ifdef HL2MP
	CSuitPowerDevice SuitDeviceSprint( bits_SUIT_DEVICE_SPRINT, 25.0f );				// 100 units in 4 seconds
#else
	CSuitPowerDevice SuitDeviceSprint( bits_SUIT_DEVICE_SPRINT, 12.5f );				// 100 units in 8 seconds
#endif

#ifdef HL2_EPISODIC
	CSuitPowerDevice SuitDeviceFlashlight( bits_SUIT_DEVICE_FLASHLIGHT, 1.111 );	// 100 units in 90 second
#else
	CSuitPowerDevice SuitDeviceFlashlight( bits_SUIT_DEVICE_FLASHLIGHT, 2.222 );	// 100 units in 45 second
#endif
CSuitPowerDevice SuitDeviceBreather( bits_SUIT_DEVICE_BREATHER, 6.7f );		// 100 units in 15 seconds (plus three padded seconds)

IMPLEMENT_SERVERCLASS_ST(CPortal_Player, DT_Portal_Player)
	SendPropDataTable(SENDINFO_DT(m_PortalLocal), &REFERENCE_SEND_TABLE(DT_PortalLocal), SendProxy_SendLocalDataTable),
	SendPropBool( SENDINFO(m_fIsSprinting) ),
#ifdef SP_ANIM_STATE
	SendPropFloat( SENDINFO(m_flAnimRenderYaw), 0, SPROP_NOSCALE ),
#endif
END_SEND_TABLE()


void CPortal_Player::Precache( void )
{
	BaseClass::Precache();

	PrecacheScriptSound( "HL2Player.SprintNoPower" );
	PrecacheScriptSound( "HL2Player.SprintStart" );
	PrecacheScriptSound( "HL2Player.UseDeny" );
	PrecacheScriptSound( "HL2Player.FlashLightOn" );
	PrecacheScriptSound( "HL2Player.FlashLightOff" );
	PrecacheScriptSound( "HL2Player.PickupWeapon" );
	PrecacheScriptSound( "HL2Player.TrainUse" );
	PrecacheScriptSound( "HL2Player.Use" );
	PrecacheScriptSound( "HL2Player.BurnPain" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::CheckSuitZoom( void )
{
	if ( m_afButtonReleased & IN_ZOOM )
		StopZooming();
	else if ( m_afButtonPressed & IN_ZOOM )
		StartZooming();
}

static ConVar sv_sprint("sv_sprint", "0", FCVAR_CHEAT);

void CPortal_Player::HandleSpeedChanges( void )
{
	if (!sv_sprint.GetBool())
		return;

	int buttonsChanged = m_afButtonPressed | m_afButtonReleased;

	bool bCanSprint = CanSprint();
	bool bIsSprinting = IsSprinting();
	bool bWantSprint = ( bCanSprint && (m_nButtons & IN_SPEED) );
	if ( bIsSprinting != bWantSprint && (buttonsChanged & IN_SPEED) )
	{
		// If someone wants to sprint, make sure they've pressed the button to do so. We want to prevent the
		// case where a player can hold down the sprint key and burn tiny bursts of sprint as the suit recharges
		// We want a full debounce of the key to resume sprinting after the suit is completely drained
		if ( bWantSprint )
		{
			if ( sv_stickysprint.GetBool() )
			{
				StartAutoSprint();
			}
			else
			{
				StartSprinting();
			}
		}
		else
		{
			if ( !sv_stickysprint.GetBool() )
			{
				StopSprinting();
			}
			// Reset key, so it will be activated post whatever is suppressing it.
			m_nButtons &= ~IN_SPEED;
		}
	}

	bool bIsWalking = IsWalking();
	// have suit, pressing button, not sprinting or ducking
	bool bWantWalking = (m_nButtons & IN_WALK) && !IsSprinting() && !(m_nButtons & IN_DUCK);
	
	if( bIsWalking != bWantWalking )
	{
		if ( bWantWalking )
		{
			StartWalking();
		}
		else
		{
			StopWalking();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Allow pre-frame adjustments on the player
//-----------------------------------------------------------------------------
void CPortal_Player::PreThink(void)
{
	if ( player_showpredictedposition.GetBool() )
	{
		Vector	predPos;

		UTIL_PredictedPosition( this, player_showpredictedposition_timestep.GetFloat(), &predPos );

		NDebugOverlay::Box( predPos, NAI_Hull::Mins( GetHullType() ), NAI_Hull::Maxs( GetHullType() ), 0, 255, 0, 0, 0.01f );
		NDebugOverlay::Line( GetAbsOrigin(), predPos, 0, 255, 0, 0, 0.01f );
	}

	// This is an experiment of mine- autojumping! 
	// only affects you if sv_autojump is nonzero.
	if( (GetFlags() & FL_ONGROUND) && sv_autojump.GetFloat() != 0 )
	{
		VPROF( "CPortal_Player::PreThink-Autojump" );
		// check autojump
		Vector vecCheckDir;

		vecCheckDir = GetAbsVelocity();

		float flVelocity = VectorNormalize( vecCheckDir );

		if( flVelocity > 200 )
		{
			// Going fast enough to autojump
			vecCheckDir = WorldSpaceCenter() + vecCheckDir * 34 - Vector( 0, 0, 16 );

			trace_t tr;

			UTIL_TraceHull( WorldSpaceCenter() - Vector( 0, 0, 16 ), vecCheckDir, NAI_Hull::Mins(HULL_TINY_CENTERED),NAI_Hull::Maxs(HULL_TINY_CENTERED), MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER, &tr );
			
			//NDebugOverlay::Line( tr.startpos, tr.endpos, 0,255,0, true, 10 );

			if( tr.fraction == 1.0 && !tr.startsolid )
			{
				// Now trace down!
				UTIL_TraceLine( vecCheckDir, vecCheckDir - Vector( 0, 0, 64 ), MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &tr );

				//NDebugOverlay::Line( tr.startpos, tr.endpos, 0,255,0, true, 10 );

				if( tr.fraction == 1.0 && !tr.startsolid )
				{
					// !!!HACKHACK
					// I KNOW, I KNOW, this is definitely not the right way to do this,
					// but I'm prototyping! (sjb)
					Vector vecNewVelocity = GetAbsVelocity();
					vecNewVelocity.z += 250;
					SetAbsVelocity( vecNewVelocity );
				}
			}
		}
	}

	VPROF_SCOPE_BEGIN( "CPortal_Player::PreThink-Speed" );
	HandleSpeedChanges();

	if( sv_stickysprint.GetBool() && m_bIsAutoSprinting )
	{
		// If we're ducked and not in the air
		if( IsDucked() && GetGroundEntity() != NULL )
		{
			StopSprinting();
		}
		// Stop sprinting if the player lets off the stick for a moment.
		else if( GetStickDist() == 0.0f )
		{
			if( gpGlobals->curtime > m_fAutoSprintMinTime )
			{
				StopSprinting();
			}
		}
		else
		{
			// Stop sprinting one half second after the player stops inputting with the move stick.
			m_fAutoSprintMinTime = gpGlobals->curtime + 0.5f;
		}
	}
	else if ( IsSprinting() )
	{
		// Disable sprint while ducked unless we're in the air (jumping)
		if ( IsDucked() && ( GetGroundEntity() != NULL ) )
		{
			StopSprinting();
		}
	}

	VPROF_SCOPE_END();

	if ( g_fGameOver || IsPlayerLockedInPlace() )
		return;         // finale

	VPROF_SCOPE_BEGIN( "CPortal_Player::PreThink-ItemPreFrame" );
	ItemPreFrame( );
	VPROF_SCOPE_END();

	VPROF_SCOPE_BEGIN( "CPortal_Player::PreThink-WaterMove" );
	WaterMove();
	VPROF_SCOPE_END();

	if ( g_pGameRules && g_pGameRules->FAllowFlashlight() )
		m_Local.m_iHideHUD &= ~HIDEHUD_FLASHLIGHT;
	else
		m_Local.m_iHideHUD |= HIDEHUD_FLASHLIGHT;

	// checks if new client data (for HUD and view control) needs to be sent to the client
	VPROF_SCOPE_BEGIN( "CPortal_Player::PreThink-UpdateClientData" );
	UpdateClientData();
	VPROF_SCOPE_END();
	
	VPROF_SCOPE_BEGIN( "CPortal_Player::PreThink-CheckTimeBasedDamage" );
	CheckTimeBasedDamage();
	VPROF_SCOPE_END();

	VPROF_SCOPE_BEGIN( "CPortal_Player::PreThink-CheckSuitUpdate" );
	CheckSuitUpdate();
	VPROF_SCOPE_END();

	VPROF_SCOPE_BEGIN( "CPortal_Player::PreThink-CheckSuitZoom" );
	CheckSuitZoom();
	VPROF_SCOPE_END();

	if (m_lifeState >= LIFE_DYING)
	{
		PlayerDeathThink();
		return;
	}

	// So the correct flags get sent to client asap.
	//
	if ( m_afPhysicsFlags & PFLAG_DIROVERRIDE )
		AddFlag( FL_ONTRAIN );
	else 
		RemoveFlag( FL_ONTRAIN );

	// Train speed control
	if ( m_afPhysicsFlags & PFLAG_DIROVERRIDE )
	{
		CBaseEntity *pTrain = GetGroundEntity();
		float vel;

		if ( pTrain )
		{
			if ( !(pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE) )
				pTrain = NULL;
		}
		
		if ( !pTrain )
		{
			if ( GetActiveWeapon() && (GetActiveWeapon()->ObjectCaps() & FCAP_DIRECTIONAL_USE) )
			{
				m_iTrain = TRAIN_ACTIVE | TRAIN_NEW;

				if ( m_nButtons & IN_FORWARD )
				{
					m_iTrain |= TRAIN_FAST;
				}
				else if ( m_nButtons & IN_BACK )
				{
					m_iTrain |= TRAIN_BACK;
				}
				else
				{
					m_iTrain |= TRAIN_NEUTRAL;
				}
				return;
			}
			else
			{
				trace_t trainTrace;
				// Maybe this is on the other side of a level transition
				UTIL_TraceLine( GetAbsOrigin(), GetAbsOrigin() + Vector(0,0,-38), 
					MASK_PLAYERSOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &trainTrace );

				if ( trainTrace.fraction != 1.0 && trainTrace.m_pEnt )
					pTrain = trainTrace.m_pEnt;


				if ( !pTrain || !(pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE) || !pTrain->OnControls(this) )
				{
//					Warning( "In train mode with no train!\n" );
					m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
					m_iTrain = TRAIN_NEW|TRAIN_OFF;
					return;
				}
			}
		}
		else if ( !( GetFlags() & FL_ONGROUND ) || pTrain->HasSpawnFlags( SF_TRACKTRAIN_NOCONTROL ) || (m_nButtons & (IN_MOVELEFT|IN_MOVERIGHT) ) )
		{
			// Turn off the train if you jump, strafe, or the train controls go dead
			m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
			m_iTrain = TRAIN_NEW|TRAIN_OFF;
			return;
		}

		SetAbsVelocity( vec3_origin );
		vel = 0;
		if ( m_afButtonPressed & IN_FORWARD )
		{
			vel = 1;
			pTrain->Use( this, this, USE_SET, (float)vel );
		}
		else if ( m_afButtonPressed & IN_BACK )
		{
			vel = -1;
			pTrain->Use( this, this, USE_SET, (float)vel );
		}

		if (vel)
		{
			m_iTrain = TrainSpeed(pTrain->m_flSpeed, ((CFuncTrackTrain*)pTrain)->GetMaxSpeed());
			m_iTrain |= TRAIN_ACTIVE|TRAIN_NEW;
		}
	} 
	else if (m_iTrain & TRAIN_ACTIVE)
	{
		m_iTrain = TRAIN_NEW; // turn off train
	}


	//
	// If we're not on the ground, we're falling. Update our falling velocity.
	//
	if ( !( GetFlags() & FL_ONGROUND ) )
	{
		m_Local.m_flFallVelocity = -GetAbsVelocity().z;
	}

	// StudioFrameAdvance( );//!!!HACKHACK!!! Can't be hit by traceline when not animating?

	// Update weapon's ready status
	UpdateWeaponPosture();

	// Disallow shooting while zooming
	if ( IsX360() )
	{
		if ( IsZooming() )
		{
			if( GetActiveWeapon() && !GetActiveWeapon()->IsWeaponZoomed() )
			{
				// If not zoomed because of the weapon itself, do not attack.
				m_nButtons &= ~(IN_ATTACK|IN_ATTACK2);
			}
		}
	}
	else
	{
		if ( m_nButtons & IN_ZOOM )
		{
			//FIXME: Held weapons like the grenade get sad when this happens
	#ifdef HL2_EPISODIC
			// Episodic allows players to zoom while using a func_tank
			CBaseCombatWeapon* pWep = GetActiveWeapon();
			if ( !m_hUseEntity || ( pWep && pWep->IsWeaponVisible() ) )
	#endif
			m_nButtons &= ~(IN_ATTACK|IN_ATTACK2);
		}
	}
}

void CPortal_Player::PostThink( void )
{
	BaseClass::PostThink();

#ifdef SP_ANIM_STATE
	if (m_pPlayerAnimState)
	{
		QAngle angEyeAngles = EyeAngles();
		m_pPlayerAnimState->Update( angEyeAngles.y, angEyeAngles.x );

		m_flAnimRenderYaw.Set( m_pPlayerAnimState->GetRenderAngles().y );
	}
#endif
}

#define HL2PLAYER_RELOADGAME_ATTACK_DELAY 1.0f

void CPortal_Player::Activate( void )
{
	BaseClass::Activate();
	InitSprinting();

#ifdef HL2_EPISODIC

	// Delay attacks by 1 second after loading a game.
	if ( GetActiveWeapon() )
	{
		float flRemaining = GetActiveWeapon()->m_flNextPrimaryAttack - gpGlobals->curtime;

		if ( flRemaining < HL2PLAYER_RELOADGAME_ATTACK_DELAY )
		{
			GetActiveWeapon()->m_flNextPrimaryAttack = gpGlobals->curtime + HL2PLAYER_RELOADGAME_ATTACK_DELAY;
		}

		flRemaining = GetActiveWeapon()->m_flNextSecondaryAttack - gpGlobals->curtime;

		if ( flRemaining < HL2PLAYER_RELOADGAME_ATTACK_DELAY )
		{
			GetActiveWeapon()->m_flNextSecondaryAttack = gpGlobals->curtime + HL2PLAYER_RELOADGAME_ATTACK_DELAY;
		}
	}

#endif

	GetPlayerProxy();
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
Class_T  CPortal_Player::Classify ( void )
{
	// If player controlling another entity?  If so, return this class
	if (m_nControlClass != CLASS_NONE)
	{
		return m_nControlClass;
	}
	else
	{
		if(IsInAVehicle())
		{
			IServerVehicle *pVehicle = GetVehicle();
#ifdef MAPBASE
			if (!pVehicle)
				return CLASS_PLAYER;
#endif
			return pVehicle->ClassifyPassenger( this, CLASS_PLAYER );
		}
		else
		{
			return CLASS_PLAYER;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:  This is a generic function (to be implemented by sub-classes) to
//			 handle specific interactions between different types of characters
//			 (For example the barnacle grabbing an NPC)
// Input  :  Constant for the type of interaction
// Output :	 true  - if sub-class has a response for the interaction
//			 false - if sub-class has no response
//-----------------------------------------------------------------------------
bool CPortal_Player::HandleInteraction(int interactionType, void *data, CBaseCombatCharacter* sourceEnt)
{
	return false;
}


void CPortal_Player::PlayerRunCommand(CUserCmd *ucmd, IMoveHelper *moveHelper)
{
	// Can't use stuff while dead
	if ( IsDead() )
	{
		ucmd->buttons &= ~IN_USE;
	}

	//Update our movement information
	if ( ( ucmd->forwardmove != 0 ) || ( ucmd->sidemove != 0 ) || ( ucmd->upmove != 0 ) )
	{
		m_flIdleTime -= TICK_INTERVAL * 2.0f;
		
		if ( m_flIdleTime < 0.0f )
		{
			m_flIdleTime = 0.0f;
		}

		m_flMoveTime += TICK_INTERVAL;

		if ( m_flMoveTime > 4.0f )
		{
			m_flMoveTime = 4.0f;
		}
	}
	else
	{
		m_flIdleTime += TICK_INTERVAL;
		
		if ( m_flIdleTime > 4.0f )
		{
			m_flIdleTime = 4.0f;
		}

		m_flMoveTime -= TICK_INTERVAL * 2.0f;
		
		if ( m_flMoveTime < 0.0f )
		{
			m_flMoveTime = 0.0f;
		}
	}

	//Msg("Player time: [ACTIVE: %f]\t[IDLE: %f]\n", m_flMoveTime, m_flIdleTime );

	BaseClass::PlayerRunCommand( ucmd, moveHelper );
}

#ifdef MAPBASE
void CPortal_Player::SpawnedAtPoint( CBaseEntity *pSpawnPoint )
{
	FirePlayerProxyOutput( "OnPlayerSpawn", variant_t(), this, pSpawnPoint );
}

//-----------------------------------------------------------------------------

ConVar player_use_anim_enabled( "player_use_anim_enabled", "1" );
ConVar player_use_anim_heavy_mass( "player_use_anim_heavy_mass", "20.0" );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
Activity CPortal_Player::Weapon_TranslateActivity( Activity baseAct, bool *pRequired )
{
	Activity weaponTranslation = BaseClass::Weapon_TranslateActivity( baseAct, pRequired );
	
#if EXPANDED_HL2DM_ACTIVITIES
	// +USE activities
	// HACKHACK: Make sure m_hUseEntity is a pickup controller first
	if ( m_hUseEntity && m_hUseEntity->ClassMatches("player_pickup") && player_use_anim_enabled.GetBool())
	{
		CBaseEntity* pHeldEnt = GetPlayerHeldEntity( this );
		float flMass = pHeldEnt ?
			(pHeldEnt->VPhysicsGetObject() ? PlayerPickupGetHeldObjectMass( m_hUseEntity, pHeldEnt->VPhysicsGetObject() ) : player_use_anim_heavy_mass.GetFloat()) :
			(m_hUseEntity->VPhysicsGetObject() ? m_hUseEntity->GetMass() : player_use_anim_heavy_mass.GetFloat());
		if ( flMass >= player_use_anim_heavy_mass.GetFloat() )
		{
			// Heavy versions
			switch (baseAct)
			{
				case ACT_HL2MP_IDLE:			weaponTranslation = ACT_HL2MP_IDLE_USE_HEAVY; break;
				case ACT_HL2MP_RUN:				weaponTranslation = ACT_HL2MP_RUN_USE_HEAVY; break;
				case ACT_HL2MP_WALK:			weaponTranslation = ACT_HL2MP_WALK_USE_HEAVY; break;
				case ACT_HL2MP_IDLE_CROUCH:		weaponTranslation = ACT_HL2MP_IDLE_CROUCH_USE_HEAVY; break;
				case ACT_HL2MP_WALK_CROUCH:		weaponTranslation = ACT_HL2MP_WALK_CROUCH_USE_HEAVY; break;
				case ACT_HL2MP_JUMP:			weaponTranslation = ACT_HL2MP_JUMP_USE_HEAVY; break;
			}
		}
		else
		{
			switch (baseAct)
			{
				case ACT_HL2MP_IDLE:			weaponTranslation = ACT_HL2MP_IDLE_USE; break;
				case ACT_HL2MP_RUN:				weaponTranslation = ACT_HL2MP_RUN_USE; break;
				case ACT_HL2MP_WALK:			weaponTranslation = ACT_HL2MP_WALK_USE; break;
				case ACT_HL2MP_IDLE_CROUCH:		weaponTranslation = ACT_HL2MP_IDLE_CROUCH_USE; break;
				case ACT_HL2MP_WALK_CROUCH:		weaponTranslation = ACT_HL2MP_WALK_CROUCH_USE; break;
				case ACT_HL2MP_JUMP:			weaponTranslation = ACT_HL2MP_JUMP_USE; break;
			}
		}
	}
#endif

	return weaponTranslation;
}

#ifdef SP_ANIM_STATE
// Set the activity based on an event or current state
void CPortal_Player::SetAnimation( PLAYER_ANIM playerAnim )
{
	if (!m_pPlayerAnimState)
	{
		BaseClass::SetAnimation( playerAnim );
		return;
	}

	m_pPlayerAnimState->SetPlayerAnimation( playerAnim );
}

void CPortal_Player::AddAnimStateLayer( int iSequence, float flBlendIn, float flBlendOut, float flPlaybackRate, bool bHoldAtEnd, bool bOnlyWhenStill )
{
	if (!m_pPlayerAnimState)
		return;

	m_pPlayerAnimState->AddMiscSequence( iSequence, flBlendIn, flBlendOut, flPlaybackRate, bHoldAtEnd, bOnlyWhenStill );
}
#endif

//-----------------------------------------------------------------------------
// Purpose: model-change notification. Fires on dynamic load completion as well
//-----------------------------------------------------------------------------
CStudioHdr *CPortal_Player::OnNewModel()
{
	CStudioHdr *hdr = BaseClass::OnNewModel();

#ifdef SP_ANIM_STATE
	// Clears the animation state if we already have one.
	if ( m_pPlayerAnimState != NULL )
	{
		m_pPlayerAnimState->Release();
		m_pPlayerAnimState = NULL;
	}

	if ( hdr && hdr->HaveSequenceForActivity(ACT_HL2MP_IDLE) && hl2_use_sp_animstate.GetBool() )
	{
		// Here we create and init the player animation state.
		m_pPlayerAnimState = CreatePlayerAnimationState(this);
	}
	else
	{
		m_flAnimRenderYaw = FLT_MAX;
	}
#endif

	return hdr;
}

extern char g_szDefaultPlayerModel[MAX_PATH];
extern bool g_bDefaultPlayerDrawExternally;
#endif

//-----------------------------------------------------------------------------
// Purpose: Sets HL2 specific defaults.
//-----------------------------------------------------------------------------
void CPortal_Player::Spawn(void)
{
//#ifdef MAPBASE
//	if ( GetModelName() == NULL_STRING )
//		SetModel( g_szDefaultPlayerModel );
//#else
	SetModel( "models/player.mdl" );
//#endif

	BaseClass::Spawn();

#ifdef MAPBASE
	// Ported from CHL2MP_Player. Fixes issues with respawning players in SP
	if ( !IsObserver() )
	{
		pl.deadflag = false;
		RemoveSolidFlags( FSOLID_NOT_SOLID );

		RemoveEffects( EF_NODRAW );
	}

	SetDrawPlayerModelExternally( g_bDefaultPlayerDrawExternally );
#endif

	//
	// Our player movement speed is set once here. This will override the cl_xxxx
	// cvars unless they are set to be lower than this.
	//
	//m_flMaxspeed = 320;

	StartWalking();

	m_Local.m_iHideHUD |= HIDEHUD_CHAT;

	InitSprinting();

	GetPlayerProxy();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::InitSprinting( void )
{
	StopSprinting();
}


//-----------------------------------------------------------------------------
// Purpose: Returns whether or not we are allowed to sprint now.
//-----------------------------------------------------------------------------
bool CPortal_Player::CanSprint()
{
	return ( m_bSprintEnabled &&										// Only if sprint is enabled 
			!IsWalking() &&												// Not if we're walking
			!( m_Local.m_bDucked && !m_Local.m_bDucking ) &&			// Nor if we're ducking
			(GetWaterLevel() != 3) &&									// Certainly not underwater
			(GlobalEntity_GetState("suit_no_sprint") != GLOBAL_ON) );	// Out of the question without the sprint module
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::StartAutoSprint() 
{
	if( IsSprinting() )
	{
		StopSprinting();
	}
	else
	{
		StartSprinting();
		m_bIsAutoSprinting = true;
		m_fAutoSprintMinTime = gpGlobals->curtime + 1.5f;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::StartSprinting( void )
{
	if( !SuitPower_AddDevice( SuitDeviceSprint ) )
		return;

	CPASAttenuationFilter filter( this );
	filter.UsePredictionRules();
	EmitSound( filter, entindex(), "HL2Player.SprintStart" );

	SetMaxSpeed( HL2_SPRINT_SPEED );
	m_fIsSprinting = true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::StopSprinting( void )
{
	if ( m_PortalLocal.m_bitsActiveDevices & SuitDeviceSprint.GetDeviceID() )
	{
		SuitPower_RemoveDevice( SuitDeviceSprint );
	}

	SetMaxSpeed( HL2_WALK_SPEED );

	m_fIsSprinting = false;

	if ( sv_stickysprint.GetBool() )
	{
		m_bIsAutoSprinting = false;
		m_fAutoSprintMinTime = 0.0f;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Called to disable and enable sprint due to temporary circumstances:
//			- Carrying a heavy object with the physcannon
//-----------------------------------------------------------------------------
void CPortal_Player::EnableSprint( bool bEnable )
{
	if ( !bEnable && IsSprinting() )
	{
		StopSprinting();
	}

	m_bSprintEnabled = bEnable;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::StartWalking( void )
{
	SetMaxSpeed( HL2_WALK_SPEED );
	m_fIsWalking = true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::StopWalking( void )
{
	SetMaxSpeed( HL2_NORM_SPEED );
	m_fIsWalking = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CPortal_Player::CanZoom( CBaseEntity *pRequester )
{
	if ( IsZooming() )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::ToggleZoom(void)
{
	if( IsZooming() )
	{
		StopZooming();
	}
	else
	{
		StartZooming();
	}
}

//-----------------------------------------------------------------------------
// Purpose: +zoom suit zoom
//-----------------------------------------------------------------------------
void CPortal_Player::StartZooming( void )
{
#ifdef MAPBASE
	int iFOV = GetPlayerProxy() ? GetPlayerProxy()->m_SuitZoomFOV : 25;
#else
	int iFOV = 25;
#endif
	if ( SetFOV( this, iFOV, 0.4f ) )
	{
		m_PortalLocal.m_bZooming = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::StopZooming( void )
{
	int iFOV = GetZoomOwnerDesiredFOV( m_hZoomOwner );

	if ( SetFOV( this, iFOV, 0.2f ) )
	{
		m_PortalLocal.m_bZooming = false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CPortal_Player::IsZooming( void )
{
	if ( m_hZoomOwner != NULL )
		return true;

	return false;
}

class CPhysicsPlayerCallback : public IPhysicsPlayerControllerEvent
{
public:
	int ShouldMoveTo( IPhysicsObject *pObject, const Vector &position )
	{
		CPortal_Player *pPlayer = (CPortal_Player *)pObject->GetGameData();
		if ( pPlayer )
		{
			if ( pPlayer->TouchedPhysics() )
			{
				return 0;
			}
		}
		return 1;
	}
};

static CPhysicsPlayerCallback playerCallback;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::InitVCollision( const Vector &vecAbsOrigin, const Vector &vecAbsVelocity )
{
	BaseClass::InitVCollision( vecAbsOrigin, vecAbsVelocity );

	// Setup the HL2 specific callback.
	IPhysicsPlayerController *pPlayerController = GetPhysicsController();
	if ( pPlayerController )
	{
		pPlayerController->SetEventHandler( &playerCallback );
	}
}


CPortal_Player::~CPortal_Player( void )
{
#ifdef SP_ANIM_STATE
	// Clears the animation state.
	if ( m_pPlayerAnimState != NULL )
	{
		m_pPlayerAnimState->Release();
		m_pPlayerAnimState = NULL;
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iImpulse - 
//-----------------------------------------------------------------------------
void CPortal_Player::CheatImpulseCommands( int iImpulse )
{
	BaseClass::CheatImpulseCommands(iImpulse);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::SetupVisibility( CBaseEntity *pViewEntity, unsigned char *pvs, int pvssize )
{
	BaseClass::SetupVisibility( pViewEntity, pvs, pvssize );

	int area = pViewEntity ? pViewEntity->NetworkProp()->AreaNum() : NetworkProp()->AreaNum();
	PointCameraSetupVisibility( this, area, pvs, pvssize );

	// If the intro script is playing, we want to get it's visibility points
	if ( g_hIntroScript )
	{
		Vector vecOrigin;
		CBaseEntity *pCamera;
		if ( g_hIntroScript->GetIncludedPVSOrigin( &vecOrigin, &pCamera ) )
		{
			// If it's a point camera, turn it on
			CPointCamera *pPointCamera = dynamic_cast< CPointCamera* >(pCamera); 
			if ( pPointCamera )
			{
				pPointCamera->SetActive( true );
			}
			engine->AddOriginToPVS( vecOrigin );
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPortal_Player::SuitPower_IsDeviceActive( const CSuitPowerDevice &device )
{
	return (m_PortalLocal.m_bitsActiveDevices & device.GetDeviceID()) != 0;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPortal_Player::SuitPower_AddDevice( const CSuitPowerDevice &device )
{
	// Make sure this device is NOT active!!
	if (m_PortalLocal.m_bitsActiveDevices & device.GetDeviceID())
		return false;

	m_PortalLocal.m_bitsActiveDevices |= device.GetDeviceID();
	m_flSuitPowerLoad += device.GetDeviceDrainRate();
	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPortal_Player::SuitPower_RemoveDevice( const CSuitPowerDevice &device )
{
	// Make sure this device is active!!
	if (!(m_PortalLocal.m_bitsActiveDevices & device.GetDeviceID()))
		return false;

	m_PortalLocal.m_bitsActiveDevices &= ~device.GetDeviceID();
	m_flSuitPowerLoad -= device.GetDeviceDrainRate();

	if (m_PortalLocal.m_bitsActiveDevices == 0x00000000)
	{
		// With this device turned off, we can set this timer which tells us when the
		// suit power system entered a no-load state.
		m_flTimeAllSuitDevicesOff = gpGlobals->curtime;
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CPortal_Player::FlashlightIsOn( void )
{
	return IsEffectActive( EF_DIMLIGHT );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::FlashlightTurnOn( void )
{
	if( m_bFlashlightDisabled )
		return;

	if ( Flashlight_UseLegacyVersion() )
	{
		if( !SuitPower_AddDevice( SuitDeviceFlashlight ) )
			return;
	}

	AddEffects( EF_DIMLIGHT );
	EmitSound( "HL2Player.FlashLightOn" );

	variant_t flashlighton;
	flashlighton.SetFloat( m_PortalLocal.m_flSuitPower / 100.0f );
	FirePlayerProxyOutput( "OnFlashlightOn", flashlighton, this, this );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::FlashlightTurnOff( void )
{
	if ( Flashlight_UseLegacyVersion() )
	{
		if( !SuitPower_RemoveDevice( SuitDeviceFlashlight ) )
			return;
	}

	RemoveEffects( EF_DIMLIGHT );
	EmitSound( "HL2Player.FlashLightOff" );

	variant_t flashlightoff;
	flashlightoff.SetFloat( m_PortalLocal.m_flSuitPower / 100.0f );
	FirePlayerProxyOutput( "OnFlashlightOff", flashlightoff, this, this );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#define FLASHLIGHT_RANGE	Square(600)
bool CPortal_Player::IsIlluminatedByFlashlight( CBaseEntity *pEntity, float *flReturnDot )
{
	if( !FlashlightIsOn() )
		return false;

	// Within 50 feet?
 	float flDistSqr = GetAbsOrigin().DistToSqr(pEntity->GetAbsOrigin());
	if( flDistSqr > FLASHLIGHT_RANGE )
		return false;

	// Within 45 degrees?
	Vector vecSpot = pEntity->WorldSpaceCenter();
	Vector los;

	// If the eyeposition is too close, move it back. Solves problems
	// caused by the player being too close the target.
	if ( flDistSqr < (128 * 128) )
	{
		Vector vecForward;
		EyeVectors( &vecForward );
		Vector vecMovedEyePos = EyePosition() - (vecForward * 128);
		los = ( vecSpot - vecMovedEyePos );
	}
	else
	{
		los = ( vecSpot - EyePosition() );
	}

	VectorNormalize( los );
	Vector facingDir = EyeDirection3D( );
	float flDot = DotProduct( los, facingDir );

	if ( flReturnDot )
	{
		 *flReturnDot = flDot;
	}

	if ( flDot < 0.92387f )
		return false;

	if( !FVisible(pEntity) )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Let NPCs know when the flashlight is trained on them
//-----------------------------------------------------------------------------
void CPortal_Player::CheckFlashlight( void )
{
	if ( !FlashlightIsOn() )
		return;

	// Loop through NPCs looking for illuminated ones
	for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
	{
		CAI_BaseNPC *pNPC = g_AI_Manager.AccessAIs()[i];

		float flDot;

		if ( IsIlluminatedByFlashlight( pNPC, &flDot ) )
		{
			pNPC->PlayerHasIlluminatedNPC( this, flDot );
		}
	}
}

//-----------------------------------------------------------------------------
bool CPortal_Player::PassesDamageFilter( const CTakeDamageInfo &info )
{
	CBaseEntity *pAttacker = info.GetAttacker();
	if( pAttacker && pAttacker->MyNPCPointer() && pAttacker->MyNPCPointer()->IsPlayerAlly() )
	{
		return false;
	}

	if( m_hPlayerProxy && !m_hPlayerProxy->PassesDamageFilter( info ) )
	{
		return false;
	}

	return BaseClass::PassesDamageFilter( info );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::SetFlashlightEnabled( bool bState )
{
	m_bFlashlightDisabled = !bState;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::InputDisableFlashlight( inputdata_t &inputdata )
{
	if( FlashlightIsOn() )
		FlashlightTurnOff();

	SetFlashlightEnabled( false );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::InputEnableFlashlight( inputdata_t &inputdata )
{
	SetFlashlightEnabled( true );
}


//-----------------------------------------------------------------------------
// Purpose: Prevent the player from taking fall damage for [n] seconds, but
// reset back to taking fall damage after the first impact (so players will be
// hurt if they bounce off what they hit). This is the original behavior.
//-----------------------------------------------------------------------------
void CPortal_Player::InputIgnoreFallDamage( inputdata_t &inputdata )
{
}


//-----------------------------------------------------------------------------
// Purpose: Absolutely prevent the player from taking fall damage for [n] seconds. 
//-----------------------------------------------------------------------------
void CPortal_Player::InputIgnoreFallDamageWithoutReset( inputdata_t &inputdata )
{
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::InputTurnFlashlightOn( inputdata_t &inputdata )
{
	FlashlightTurnOn();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::InputTurnFlashlightOff( inputdata_t &inputdata )
{
	FlashlightTurnOff();
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ConVar test_massive_dmg("test_massive_dmg", "30" );
ConVar test_massive_dmg_clip("test_massive_dmg_clip", "0.5" );
int	CPortal_Player::OnTakeDamage( const CTakeDamageInfo &info )
{
	if ( GlobalEntity_GetState( "gordon_invulnerable" ) == GLOBAL_ON )
		return 0;

	// ignore fall damage if instructed to do so by input
	if ( ( info.GetDamageType() & DMG_FALL ) )
	{
		return 0;
	}

	if( info.GetDamageType() & DMG_BLAST_SURFACE )
	{
		if( GetWaterLevel() > 2 )
		{
			// Don't take blast damage from anything above the surface.
			if( info.GetInflictor()->GetWaterLevel() == 0 )
			{
				return 0;
			}
		}
	}

	if ( info.GetDamage() > 0.0f )
	{
		m_flLastDamageTime = gpGlobals->curtime;
	}
	
	// Modify the amount of damage the player takes, based on skill.
	CTakeDamageInfo playerDamage = info;

	// Should we run this damage through the skill level adjustment?
	bool bAdjustForSkillLevel = true;

	if( info.GetDamageType() == DMG_GENERIC && info.GetAttacker() == this && info.GetInflictor() == this )
	{
		// Only do a skill level adjustment if the player isn't his own attacker AND inflictor.
		// This prevents damage from SetHealth() inputs from being adjusted for skill level.
		bAdjustForSkillLevel = false;
	}

	if ( GetVehicleEntity() != NULL && GlobalEntity_GetState("gordon_protect_driver") == GLOBAL_ON )
	{
		if( playerDamage.GetDamage() > test_massive_dmg.GetFloat() && playerDamage.GetInflictor() == GetVehicleEntity() && (playerDamage.GetDamageType() & DMG_CRUSH) )
		{
			playerDamage.ScaleDamage( test_massive_dmg_clip.GetFloat() / playerDamage.GetDamage() );
		}
	}

	if( bAdjustForSkillLevel )
	{
		playerDamage.AdjustPlayerDamageTakenForSkillLevel();
	}

	gamestats->Event_PlayerDamage( this, info );

#ifdef MAPBASE
	FirePlayerProxyOutput("PlayerDamaged", variant_t(), info.GetAttacker(), this);
#endif

	return BaseClass::OnTakeDamage( playerDamage );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &info - 
//-----------------------------------------------------------------------------
int CPortal_Player::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	// Drown
	if( info.GetDamageType() & DMG_DROWN )
	{
		if( m_idrowndmg == m_idrownrestored )
		{
			EmitSound( "Player.DrownStart" );
		}
		else
		{
			EmitSound( "Player.DrownContinue" );
		}
	}

	// Burnt
	if ( info.GetDamageType() & DMG_BURN )
	{
		EmitSound( "HL2Player.BurnPain" );
	}


	if( (info.GetDamageType() & DMG_SLASH) && hl2_episodic.GetBool() )
	{
		if( m_afPhysicsFlags & PFLAG_USING )
		{
			// Stop the player using a rotating button for a short time if hit by a creature's melee attack.
			// This is for the antlion burrow-corking training in EP1 (sjb).
			SuspendUse( 0.5f );
		}
	}


	// Call the base class implementation
	return BaseClass::OnTakeDamage_Alive( info );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::OnDamagedByExplosion( const CTakeDamageInfo &info )
{
	if ( info.GetInflictor() && info.GetInflictor()->ClassMatches( "mortarshell" ) )
	{
		// No ear ringing for mortar
		UTIL_ScreenShake( info.GetInflictor()->GetAbsOrigin(), 4.0, 1.0, 0.5, 1000, SHAKE_START, false );
		return;
	}
	BaseClass::OnDamagedByExplosion( info );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPortal_Player::ShouldShootMissTarget( CBaseCombatCharacter *pAttacker )
{
	if( gpGlobals->curtime > m_flTargetFindTime )
	{
		// Put this off into the future again.
		m_flTargetFindTime = gpGlobals->curtime + random->RandomFloat( 3, 5 );
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::Event_KilledOther( CBaseEntity *pVictim, const CTakeDamageInfo &info )
{
	BaseClass::Event_KilledOther( pVictim, info );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );

#ifdef MAPBASE
	FirePlayerProxyOutput( "PlayerDied", variant_t(), info.GetAttacker(), this );
#else
	FirePlayerProxyOutput( "PlayerDied", variant_t(), this, this );
#endif
	NotifyScriptsOfDeath();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::NotifyScriptsOfDeath( void )
{
	CBaseEntity *pEnt =	gEntList.FindEntityByClassname( NULL, "scripted_sequence" );

	while( pEnt )
	{
		variant_t emptyVariant;
		pEnt->AcceptInput( "ScriptPlayerDeath", NULL, NULL, emptyVariant, 0 );

		pEnt = gEntList.FindEntityByClassname( pEnt, "scripted_sequence" );
	}

	pEnt =	gEntList.FindEntityByClassname( NULL, "logic_choreographed_scene" );

	while( pEnt )
	{
		variant_t emptyVariant;
		pEnt->AcceptInput( "ScriptPlayerDeath", NULL, NULL, emptyVariant, 0 );

		pEnt = gEntList.FindEntityByClassname( pEnt, "logic_choreographed_scene" );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CPortal_Player::GetAutoaimVector( autoaim_params_t &params )
{
	BaseClass::GetAutoaimVector( params );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPortal_Player::ShouldKeepLockedAutoaimTarget( EHANDLE hLockedTarget )
{
	Vector vecLooking;
	Vector vecToTarget;

	vecToTarget = hLockedTarget->WorldSpaceCenter()	- EyePosition();
	float flDist = vecToTarget.Length2D();
	VectorNormalize( vecToTarget );

	if( flDist > autoaim_max_dist.GetFloat() )
		return false;

	float flDot;

	vecLooking = EyeDirection3D();
	flDot = DotProduct( vecLooking, vecToTarget );

	if( flDot < autoaim_unlock_target.GetFloat() ) 
		return false;

	return true;
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPortal_Player::CanAutoSwitchToNextBestWeapon( CBaseCombatWeapon *pWeapon )
{
	return player_autoswitch_enabled.GetBool();
}
#endif

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iCount - 
//			iAmmoIndex - 
//			bSuppressSound - 
// Output : int
//-----------------------------------------------------------------------------
int CPortal_Player::GiveAmmo( int nCount, int nAmmoIndex, bool bSuppressSound)
{
	// Don't try to give the player invalid ammo indices.
	if (nAmmoIndex < 0)
		return 0;

	bool bCheckAutoSwitch = false;
	if (!HasAnyAmmoOfType(nAmmoIndex))
	{
		bCheckAutoSwitch = true;
	}

	int nAdd = BaseClass::GiveAmmo(nCount, nAmmoIndex, bSuppressSound);

	if ( nCount > 0 && nAdd == 0 )
	{
		// we've been denied the pickup, display a hud icon to show that
		CSingleUserRecipientFilter user( this );
		user.MakeReliable();
		UserMessageBegin( user, "AmmoDenied" );
			WRITE_SHORT( nAmmoIndex );
		MessageEnd();
	}

	//
	// If I was dry on ammo for my best weapon and justed picked up ammo for it,
	// autoswitch to my best weapon now.
	//
	if (bCheckAutoSwitch)
	{
		CBaseCombatWeapon *pWeapon = g_pGameRules->GetNextBestWeapon(this, GetActiveWeapon());

		if ( pWeapon && pWeapon->GetPrimaryAmmoType() == nAmmoIndex )
		{
#ifdef MAPBASE
			if (CanAutoSwitchToNextBestWeapon(pWeapon))
#endif
			SwitchToNextBestWeapon(GetActiveWeapon());
		}
	}

	return nAdd;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CPortal_Player::Weapon_CanUse( CBaseCombatWeapon *pWeapon )
{
	return BaseClass::Weapon_CanUse( pWeapon );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pWeapon - 
//-----------------------------------------------------------------------------
void CPortal_Player::Weapon_Equip( CBaseCombatWeapon *pWeapon )
{
#if	HL2_SINGLE_PRIMARY_WEAPON_MODE

	if ( pWeapon->GetSlot() == WEAPON_PRIMARY_SLOT )
	{
		Weapon_DropSlot( WEAPON_PRIMARY_SLOT );
	}

#endif

	if( GetActiveWeapon() == NULL )
	{
		m_PortalLocal.m_bWeaponLowered = false;
	}

	BaseClass::Weapon_Equip( pWeapon );
}

//-----------------------------------------------------------------------------
// Purpose: Player reacts to bumping a weapon. 
// Input  : pWeapon - the weapon that the player bumped into.
// Output : Returns true if player picked up the weapon
//-----------------------------------------------------------------------------
bool CPortal_Player::BumpWeapon( CBaseCombatWeapon *pWeapon )
{

#if	HL2_SINGLE_PRIMARY_WEAPON_MODE

	CBaseCombatCharacter *pOwner = pWeapon->GetOwner();

	// Can I have this weapon type?
	if ( pOwner || !Weapon_CanUse( pWeapon ) || !g_pGameRules->CanHavePlayerItem( this, pWeapon ) )
	{
		if ( gEvilImpulse101 )
		{
			UTIL_Remove( pWeapon );
		}
		return false;
	}

	// ----------------------------------------
	// If I already have it just take the ammo
	// ----------------------------------------
	if (Weapon_OwnsThisType( pWeapon->GetClassname(), pWeapon->GetSubType())) 
	{
		//Only remove the weapon if we attained ammo from it
		if ( Weapon_EquipAmmoOnly( pWeapon ) == false )
			return false;

		// Only remove me if I have no ammo left
		// Can't just check HasAnyAmmo because if I don't use clips, I want to be removed, 
		if ( pWeapon->UsesClipsForAmmo1() && pWeapon->HasPrimaryAmmo() )
			return false;

		UTIL_Remove( pWeapon );
		return false;
	}
	// -------------------------
	// Otherwise take the weapon
	// -------------------------
	else 
	{
		//Make sure we're not trying to take a new weapon type we already have
		if ( Weapon_SlotOccupied( pWeapon ) )
		{
			CBaseCombatWeapon *pActiveWeapon = Weapon_GetSlot( WEAPON_PRIMARY_SLOT );

			if ( pActiveWeapon != NULL && pActiveWeapon->HasAnyAmmo() == false && Weapon_CanSwitchTo( pWeapon ) )
			{
				Weapon_Equip( pWeapon );
				return true;
			}

			//Attempt to take ammo if this is the gun we're holding already
			if ( Weapon_OwnsThisType( pWeapon->GetClassname(), pWeapon->GetSubType() ) )
			{
				Weapon_EquipAmmoOnly( pWeapon );
			}

			return false;
		}

		pWeapon->CheckRespawn();

		pWeapon->AddSolidFlags( FSOLID_NOT_SOLID );
		pWeapon->AddEffects( EF_NODRAW );

		Weapon_Equip( pWeapon );

		EmitSound( "HL2Player.PickupWeapon" );
		
		return true;
	}
#else

	return BaseClass::BumpWeapon( pWeapon );

#endif

}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *cmd - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CPortal_Player::ClientCommand( const CCommand &args )
{
#if	HL2_SINGLE_PRIMARY_WEAPON_MODE

	//Drop primary weapon
	if ( !Q_stricmp( args[0], "DropPrimary" ) )
	{
		Weapon_DropSlot( WEAPON_PRIMARY_SLOT );
		return true;
	}

#endif

	if ( !Q_stricmp( args[0], "emit" ) )
	{
		CSingleUserRecipientFilter filter( this );
		if ( args.ArgC() > 1 )
		{
			EmitSound( filter, entindex(), args[ 1 ] );
		}
		else
		{
			EmitSound( filter, entindex(), "Test.Sound" );
		}
		return true;
	}

	return BaseClass::ClientCommand( args );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : void CBasePlayer::PlayerUse
//-----------------------------------------------------------------------------
void CPortal_Player::PlayerUse ( void )
{
	// Was use pressed or released?
	if ( ! ((m_nButtons | m_afButtonPressed | m_afButtonReleased) & IN_USE) )
		return;

	if ( m_afButtonPressed & IN_USE )
	{
		// Currently using a latched entity?
		if ( ClearUseEntity() )
		{
			return;
		}
		else
		{
			if ( m_afPhysicsFlags & PFLAG_DIROVERRIDE )
			{
				m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
				m_iTrain = TRAIN_NEW|TRAIN_OFF;
				return;
			}
			else
			{	// Start controlling the train!
				CBaseEntity *pTrain = GetGroundEntity();
				if ( pTrain && !(m_nButtons & IN_JUMP) && (GetFlags() & FL_ONGROUND) && (pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE) && pTrain->OnControls(this) )
				{
					m_afPhysicsFlags |= PFLAG_DIROVERRIDE;
					m_iTrain = TrainSpeed(pTrain->m_flSpeed, ((CFuncTrackTrain*)pTrain)->GetMaxSpeed());
					m_iTrain |= TRAIN_NEW;
					EmitSound( "HL2Player.TrainUse" );
					return;
				}
			}
		}

		// Tracker 3926:  We can't +USE something if we're climbing a ladder
		if ( GetMoveType() == MOVETYPE_LADDER )
		{
			return;
		}
	}

	if( m_flTimeUseSuspended > gpGlobals->curtime )
	{
		// Something has temporarily stopped us being able to USE things.
		// Obviously, this should be used very carefully.(sjb)
		return;
	}

	CBaseEntity *pUseEntity = FindUseEntity();

	bool usedSomething = false;

	// Found an object
	if ( pUseEntity )
	{
		//!!!UNDONE: traceline here to prevent +USEing buttons through walls			
		int caps = pUseEntity->ObjectCaps();
		variant_t emptyVariant;

		if ( m_afButtonPressed & IN_USE )
		{
			// Robin: Don't play sounds for NPCs, because NPCs will allow respond with speech.
			if ( !pUseEntity->MyNPCPointer() )
			{
				EmitSound( "HL2Player.Use" );
			}
		}

		if ( ( (m_nButtons & IN_USE) && (caps & FCAP_CONTINUOUS_USE) ) ||
			 ( (m_afButtonPressed & IN_USE) && (caps & (FCAP_IMPULSE_USE|FCAP_ONOFF_USE)) ) )
		{
			if ( caps & FCAP_CONTINUOUS_USE )
				m_afPhysicsFlags |= PFLAG_USING;

			pUseEntity->AcceptInput( "Use", this, this, emptyVariant, USE_TOGGLE );

			usedSomething = true;
		}
		// UNDONE: Send different USE codes for ON/OFF.  Cache last ONOFF_USE object to send 'off' if you turn away
		else if ( (m_afButtonReleased & IN_USE) && (pUseEntity->ObjectCaps() & FCAP_ONOFF_USE) )	// BUGBUG This is an "off" use
		{
			pUseEntity->AcceptInput( "Use", this, this, emptyVariant, USE_TOGGLE );

			usedSomething = true;
		}

#if	HL2_SINGLE_PRIMARY_WEAPON_MODE

		//Check for weapon pick-up
		if ( m_afButtonPressed & IN_USE )
		{
			CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon *>(pUseEntity);

			if ( ( pWeapon != NULL ) && ( Weapon_CanSwitchTo( pWeapon ) ) )
			{
				//Try to take ammo or swap the weapon
				if ( Weapon_OwnsThisType( pWeapon->GetClassname(), pWeapon->GetSubType() ) )
				{
					Weapon_EquipAmmoOnly( pWeapon );
				}
				else
				{
					Weapon_DropSlot( pWeapon->GetSlot() );
					Weapon_Equip( pWeapon );
				}

				usedSomething = true;
			}
		}
#endif
	}
	else if ( m_afButtonPressed & IN_USE )
	{
		// Signal that we want to play the deny sound, unless the user is +USEing on a ladder!
		// The sound is emitted in ItemPostFrame, since that occurs after GameMovement::ProcessMove which
		// lets the ladder code unset this flag.
		m_bPlayUseDenySound = true;
	}

	// Debounce the use key
	if ( usedSomething && pUseEntity )
	{
		m_Local.m_nOldButtons |= IN_USE;
		m_afButtonPressed &= ~IN_USE;
	}
}

ConVar	sv_show_crosshair_target( "sv_show_crosshair_target", "0" );

//-----------------------------------------------------------------------------
// Purpose: Updates the posture of the weapon from lowered to ready
//-----------------------------------------------------------------------------
void CPortal_Player::UpdateWeaponPosture( void )
{
	if( g_pGameRules->GetAutoAimMode() != AUTOAIM_NONE )
	{
		if( !m_AutoaimTimer.Expired() )
			return;

		m_AutoaimTimer.Set( .1 );

		VPROF( "hl2_x360_aiming" );

		// Call the autoaim code to update the local player data, which allows the client to update.
		autoaim_params_t params;
		params.m_vecAutoAimPoint.Init();
		params.m_vecAutoAimDir.Init();
		params.m_fScale = AUTOAIM_SCALE_DEFAULT;
		params.m_fMaxDist = autoaim_max_dist.GetFloat();
		GetAutoaimVector( params );
		m_PortalLocal.m_hAutoAimTarget.Set( params.m_hAutoAimEntity );
		m_PortalLocal.m_vecAutoAimPoint.Set( params.m_vecAutoAimPoint );
		m_PortalLocal.m_bAutoAimTarget = ( params.m_bAutoAimAssisting || params.m_bOnTargetNatural );
		return;
	}
	else
	{
		// Make sure there's no residual autoaim target if the user changes the xbox_aiming convar on the fly.
		m_PortalLocal.m_hAutoAimTarget.Set(NULL);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns whether or not we can switch to the given weapon.
// Input  : pWeapon - 
//-----------------------------------------------------------------------------
bool CPortal_Player::Weapon_CanSwitchTo( CBaseCombatWeapon *pWeapon )
{
	CBasePlayer *pPlayer = (CBasePlayer *)this;
#if !defined( CLIENT_DLL )
	IServerVehicle *pVehicle = pPlayer->GetVehicle();
#else
	IClientVehicle *pVehicle = pPlayer->GetVehicle();
#endif
	if (pVehicle && !pPlayer->UsingStandardWeaponsInVehicle())
		return false;

	if ( !pWeapon->HasAnyAmmo() && !GetAmmoCount( pWeapon->m_iPrimaryAmmoType ) )
		return false;

	if ( !pWeapon->CanDeploy() )
		return false;

	if ( GetActiveWeapon() )
	{
		if ( PhysCannonGetHeldEntity( GetActiveWeapon() ) == pWeapon && 
			Weapon_OwnsThisType( pWeapon->GetClassname(), pWeapon->GetSubType()) )
		{
			return true;
		}

		if ( !GetActiveWeapon()->CanHolster() )
			return false;
	}

	return true;
}

void CPortal_Player::PickupObject( CBaseEntity *pObject, bool bLimitMassAndSize )
{
	// can't pick up what you're standing on
	if ( GetGroundEntity() == pObject )
		return;
	
	if ( bLimitMassAndSize == true )
	{
		if ( CBasePlayer::CanPickupObject( pObject, 35, 128 ) == false )
			 return;
	}

	// Can't be picked up if NPCs are on me
	if ( pObject->HasNPCsOnIt() )
		return;

	PlayerPickupObject( this, pObject );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : CBaseEntity
//-----------------------------------------------------------------------------
bool CPortal_Player::IsHoldingEntity( CBaseEntity *pEnt )
{
	return PlayerPickupControllerIsHoldingEntity( m_hUseEntity, pEnt );
}

float CPortal_Player::GetHeldObjectMass( IPhysicsObject *pHeldObject )
{
	float mass = PlayerPickupGetHeldObjectMass( m_hUseEntity, pHeldObject );
	if ( mass == 0.0f )
	{
		mass = PhysCannonGetHeldObjectMass( GetActiveWeapon(), pHeldObject );
	}
	return mass;
}

CBaseEntity	*CPortal_Player::GetHeldObject( void )
{
	return PhysCannonGetHeldEntity( GetActiveWeapon() );
}

//-----------------------------------------------------------------------------
// Purpose: Force the player to drop any physics objects he's carrying
//-----------------------------------------------------------------------------
void CPortal_Player::ForceDropOfCarriedPhysObjects( CBaseEntity *pOnlyIfHoldingThis )
{
	if ( PhysIsInCallback() )
	{
		variant_t value;
		g_EventQueue.AddEvent( this, "ForceDropPhysObjects", value, 0.01f, pOnlyIfHoldingThis, this );
		return;
	}

	// Drop any objects being handheld.
	ClearUseEntity();

	// Then force the physcannon to drop anything it's holding, if it's our active weapon
	PhysCannonForceDrop( GetActiveWeapon(), NULL );
}

void CPortal_Player::InputForceDropPhysObjects( inputdata_t &data )
{
	ForceDropOfCarriedPhysObjects( data.pActivator );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortal_Player::UpdateClientData( void )
{
	if (m_DmgTake || m_DmgSave || m_bitsHUDDamage != m_bitsDamageType)
	{
		// Comes from inside me if not set
		Vector damageOrigin = GetLocalOrigin();
		// send "damage" message
		// causes screen to flash, and pain compass to show direction of damage
		damageOrigin = m_DmgOrigin;

		// only send down damage type that have hud art
		int iShowHudDamage = g_pGameRules->Damage_GetShowOnHud();
		int visibleDamageBits = m_bitsDamageType & iShowHudDamage;

		m_DmgTake = clamp( m_DmgTake, 0, 255 );
		m_DmgSave = clamp( m_DmgSave, 0, 255 );

		// If we're poisoned, but it wasn't this frame, don't send the indicator
		// Without this check, any damage that occured to the player while they were
		// recovering from a poison bite would register as poisonous as well and flash
		// the whole screen! -- jdw
		if ( visibleDamageBits & DMG_POISON )
		{
			float flLastPoisonedDelta = gpGlobals->curtime - m_tbdPrev;
			if ( flLastPoisonedDelta > 0.1f )
			{
				visibleDamageBits &= ~DMG_POISON;
			}
		}

		CSingleUserRecipientFilter user( this );
		user.MakeReliable();
		UserMessageBegin( user, "Damage" );
			WRITE_BYTE( m_DmgSave );
			WRITE_BYTE( m_DmgTake );
			WRITE_LONG( visibleDamageBits );
			WRITE_FLOAT( damageOrigin.x );	//BUG: Should be fixed point (to hud) not floats
			WRITE_FLOAT( damageOrigin.y );	//BUG: However, the HUD does _not_ implement bitfield messages (yet)
			WRITE_FLOAT( damageOrigin.z );	//BUG: We use WRITE_VEC3COORD for everything else
		MessageEnd();
	
		m_DmgTake = 0;
		m_DmgSave = 0;
		m_bitsHUDDamage = m_bitsDamageType;
		
		// Clear off non-time-based damage indicators
		int iTimeBasedDamage = g_pGameRules->Damage_GetTimeBased();
		m_bitsDamageType &= iTimeBasedDamage;
	}

	BaseClass::UpdateClientData();
}

//---------------------------------------------------------
//---------------------------------------------------------
void CPortal_Player::OnRestore()
{
	BaseClass::OnRestore();
}

//---------------------------------------------------------
//---------------------------------------------------------
Vector CPortal_Player::EyeDirection2D( void )
{
	Vector vecReturn = EyeDirection3D();
	vecReturn.z = 0;
	vecReturn.AsVector2D().NormalizeInPlace();

	return vecReturn;
}

//---------------------------------------------------------
//---------------------------------------------------------
Vector CPortal_Player::EyeDirection3D( void )
{
	Vector vecForward;
#ifdef MAPBASE
	EyeVectors( &vecForward );
	return vecForward;
#else
	// Return the vehicle angles if we request them
	if ( GetVehicle() != NULL )
	{
		CacheVehicleView();
		EyeVectors( &vecForward );
		return vecForward;
	}
	
	AngleVectors( EyeAngles(), &vecForward );
	return vecForward;
#endif
}


//---------------------------------------------------------
//---------------------------------------------------------
bool CPortal_Player::Weapon_Switch( CBaseCombatWeapon *pWeapon, int viewmodelindex )
{
	MDLCACHE_CRITICAL_SECTION();

	// Recalculate proficiency!
	SetCurrentWeaponProficiency( CalcWeaponProficiency( pWeapon ) );

	// Come out of suit zoom mode
	if ( IsZooming() )
	{
		StopZooming();
	}

	return BaseClass::Weapon_Switch( pWeapon, viewmodelindex );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
WeaponProficiency_t CPortal_Player::CalcWeaponProficiency( CBaseCombatWeapon *pWeapon )
{
	WeaponProficiency_t proficiency;

	proficiency = WEAPON_PROFICIENCY_PERFECT;

	if( weapon_showproficiency.GetBool() != 0 )
	{
		Msg("Player switched to %s, proficiency is %s\n", pWeapon->GetClassname(), GetWeaponProficiencyName( proficiency ) );
	}

	return proficiency;
}

//-----------------------------------------------------------------------------
// Purpose: override how single player rays hit the player
//-----------------------------------------------------------------------------

bool LineCircleIntersection(
	const Vector2D &center,
	const float radius,
	const Vector2D &vLinePt,
	const Vector2D &vLineDir,
	float *fIntersection1,
	float *fIntersection2)
{
	// Line = P + Vt
	// Sphere = r (assume we've translated to origin)
	// (P + Vt)^2 = r^2
	// VVt^2 + 2PVt + (PP - r^2)
	// Solve as quadratic:  (-b  +/-  sqrt(b^2 - 4ac)) / 2a
	// If (b^2 - 4ac) is < 0 there is no solution.
	// If (b^2 - 4ac) is = 0 there is one solution (a case this function doesn't support).
	// If (b^2 - 4ac) is > 0 there are two solutions.
	Vector2D P;
	float a, b, c, sqr, insideSqr;


	// Translate circle to origin.
	P[0] = vLinePt[0] - center[0];
	P[1] = vLinePt[1] - center[1];
	
	a = vLineDir.Dot(vLineDir);
	b = 2.0f * P.Dot(vLineDir);
	c = P.Dot(P) - (radius * radius);

	insideSqr = b*b - 4*a*c;
	if(insideSqr <= 0.000001f)
		return false;

	// Ok, two solutions.
	sqr = (float)FastSqrt(insideSqr);

	float denom = 1.0 / (2.0f * a);
	
	*fIntersection1 = (-b - sqr) * denom;
	*fIntersection2 = (-b + sqr) * denom;

	return true;
}

static void Collision_ClearTrace( const Vector &vecRayStart, const Vector &vecRayDelta, CBaseTrace *pTrace )
{
	pTrace->startpos = vecRayStart;
	pTrace->endpos = vecRayStart;
	pTrace->endpos += vecRayDelta;
	pTrace->startsolid = false;
	pTrace->allsolid = false;
	pTrace->fraction = 1.0f;
	pTrace->contents = 0;
}


bool IntersectRayWithAACylinder( const Ray_t &ray, 
	const Vector &center, float radius, float height, CBaseTrace *pTrace )
{
	Assert( ray.m_IsRay );
	Collision_ClearTrace( ray.m_Start, ray.m_Delta, pTrace );

	// First intersect the ray with the top + bottom planes
	float halfHeight = height * 0.5;

	// Handle parallel case
	Vector vStart = ray.m_Start - center;
	Vector vEnd = vStart + ray.m_Delta;

	float flEnterFrac, flLeaveFrac;
	if (FloatMakePositive(ray.m_Delta.z) < 1e-8)
	{
		if ( (vStart.z < -halfHeight) || (vStart.z > halfHeight) )
		{
			return false; // no hit
		}
		flEnterFrac = 0.0f; flLeaveFrac = 1.0f;
	}
	else
	{
		// Clip the ray to the top and bottom of box
		flEnterFrac = IntersectRayWithAAPlane( vStart, vEnd, 2, 1, halfHeight);
		flLeaveFrac = IntersectRayWithAAPlane( vStart, vEnd, 2, 1, -halfHeight);

		if ( flLeaveFrac < flEnterFrac )
		{
			float temp = flLeaveFrac;
			flLeaveFrac = flEnterFrac;
			flEnterFrac = temp;
		}

		if ( flLeaveFrac < 0 || flEnterFrac > 1)
		{
			return false;
		}
	}

	// Intersect with circle
	float flCircleEnterFrac, flCircleLeaveFrac;
	if ( !LineCircleIntersection( vec3_origin.AsVector2D(), radius,
		vStart.AsVector2D(), ray.m_Delta.AsVector2D(), &flCircleEnterFrac, &flCircleLeaveFrac ) )
	{
		return false; // no hit
	}

	Assert( flCircleEnterFrac <= flCircleLeaveFrac );
	if ( flCircleLeaveFrac < 0 || flCircleEnterFrac > 1)
	{
		return false;
	}

	if ( flEnterFrac < flCircleEnterFrac )
		flEnterFrac = flCircleEnterFrac;
	if ( flLeaveFrac > flCircleLeaveFrac )
		flLeaveFrac = flCircleLeaveFrac;

	if ( flLeaveFrac < flEnterFrac )
		return false;

	VectorMA( ray.m_Start, flEnterFrac , ray.m_Delta, pTrace->endpos );
	pTrace->fraction = flEnterFrac;
	pTrace->contents = CONTENTS_SOLID;

	// Calculate the point on our center line where we're nearest the intersection point
	Vector collisionCenter;
	CalcClosestPointOnLineSegment( pTrace->endpos, center + Vector( 0, 0, halfHeight ), center - Vector( 0, 0, halfHeight ), collisionCenter );
	
	// Our normal is the direction from that center point to the intersection point
	pTrace->plane.normal = pTrace->endpos - collisionCenter;
	VectorNormalize( pTrace->plane.normal );

	return true;
}


bool CPortal_Player::TestHitboxes( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	if( g_pGameRules->IsMultiplayer() )
	{
		return BaseClass::TestHitboxes( ray, fContentsMask, tr );
	}
	else
	{
		Assert( ray.m_IsRay );

		Vector mins, maxs;

		mins = WorldAlignMins();
		maxs = WorldAlignMaxs();

		if ( IntersectRayWithAACylinder( ray, WorldSpaceCenter(), maxs.x * PLAYER_HULL_REDUCTION, maxs.z - mins.z, &tr ) )
		{
			tr.hitbox = 0;
			CStudioHdr *pStudioHdr = GetModelPtr( );
			if (!pStudioHdr)
				return false;

			mstudiohitboxset_t *set = pStudioHdr->pHitboxSet( m_nHitboxSet );
			if ( !set || !set->numhitboxes )
				return false;

			mstudiobbox_t *pbox = set->pHitbox( tr.hitbox );
			mstudiobone_t *pBone = pStudioHdr->pBone(pbox->bone);
			tr.surface.name = "**studio**";
			tr.surface.flags = SURF_HITBOX;
			tr.surface.surfaceProps = physprops->GetSurfaceIndex( pBone->pszSurfaceProp() );
		}
		
		return true;
	}
}

//---------------------------------------------------------
// Show the player's scaled down bbox that we use for
// bullet impacts.
//---------------------------------------------------------
void CPortal_Player::DrawDebugGeometryOverlays(void) 
{
	BaseClass::DrawDebugGeometryOverlays();

	if (m_debugOverlays & OVERLAY_BBOX_BIT) 
	{	
		Vector mins, maxs;

		mins = WorldAlignMins();
		maxs = WorldAlignMaxs();

		mins.x *= PLAYER_HULL_REDUCTION;
		mins.y *= PLAYER_HULL_REDUCTION;

		maxs.x *= PLAYER_HULL_REDUCTION;
		maxs.y *= PLAYER_HULL_REDUCTION;

		NDebugOverlay::Box( GetAbsOrigin(), mins, maxs, 255, 0, 0, 100, 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Helper to remove from ladder
//-----------------------------------------------------------------------------
void CPortal_Player::ExitLadder()
{
	if ( MOVETYPE_LADDER != GetMoveType() )
		return;
	
	SetMoveType( MOVETYPE_WALK );
	SetMoveCollide( MOVECOLLIDE_DEFAULT );
	// Remove from ladder
	m_PortalLocal.m_hLadder.Set( NULL );
}


surfacedata_t *CPortal_Player::GetLadderSurface( const Vector &origin )
{
	extern const char *FuncLadder_GetSurfaceprops(CBaseEntity *pLadderEntity);

	CBaseEntity *pLadder = m_PortalLocal.m_hLadder.Get();
	if ( pLadder )
	{
		const char *pSurfaceprops = FuncLadder_GetSurfaceprops(pLadder);
		// get ladder material from func_ladder
		return physprops->GetSurfaceData( physprops->GetSurfaceIndex( pSurfaceprops ) );

	}
	return BaseClass::GetLadderSurface(origin);
}

//-----------------------------------------------------------------------------
// Purpose: Queues up a use deny sound, played in ItemPostFrame.
//-----------------------------------------------------------------------------
void CPortal_Player::PlayUseDenySound()
{
	m_bPlayUseDenySound = true;
}


void CPortal_Player::ItemPostFrame()
{
	BaseClass::ItemPostFrame();

	if ( m_bPlayUseDenySound )
	{
		m_bPlayUseDenySound = false;
		EmitSound( "HL2Player.UseDeny" );
	}
}


void CPortal_Player::StartWaterDeathSounds( void )
{
	CPASAttenuationFilter filter( this );

	if ( m_sndLeeches == NULL )
	{
		m_sndLeeches = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "coast.leech_bites_loop" , ATTN_NORM );
	}

	if ( m_sndLeeches )
	{
		(CSoundEnvelopeController::GetController()).Play( m_sndLeeches, 1.0f, 100 );
	}

	if ( m_sndWaterSplashes == NULL )
	{
		m_sndWaterSplashes = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "coast.leech_water_churn_loop" , ATTN_NORM );
	}

	if ( m_sndWaterSplashes )
	{
		(CSoundEnvelopeController::GetController()).Play( m_sndWaterSplashes, 1.0f, 100 );
	}
}

void CPortal_Player::StopWaterDeathSounds( void )
{
	if ( m_sndLeeches )
	{
		(CSoundEnvelopeController::GetController()).SoundFadeOut( m_sndLeeches, 0.5f, true );
		m_sndLeeches = NULL;
	}

	if ( m_sndWaterSplashes )
	{
		(CSoundEnvelopeController::GetController()).SoundFadeOut( m_sndWaterSplashes, 0.5f, true );
		m_sndWaterSplashes = NULL;
	}
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CPortal_Player::DisplayLadderHudHint()
{
#if !defined( CLIENT_DLL )
	if( gpGlobals->curtime > m_flTimeNextLadderHint )
	{
		m_flTimeNextLadderHint = gpGlobals->curtime + 60.0f;

		CFmtStr hint;
		hint.sprintf( "#Valve_Hint_Ladder" );
		UTIL_HudHintText( this, hint.Access() );
	}
#endif//CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Shuts down sounds
//-----------------------------------------------------------------------------
void CPortal_Player::StopLoopingSounds( void )
{
	if ( m_sndLeeches != NULL )
	{
		 (CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLeeches );
		 m_sndLeeches = NULL;
	}

	if ( m_sndWaterSplashes != NULL )
	{
		 (CSoundEnvelopeController::GetController()).SoundDestroy( m_sndWaterSplashes );
		 m_sndWaterSplashes = NULL;
	}

	BaseClass::StopLoopingSounds();
}

//-----------------------------------------------------------------------------
void CPortal_Player::ModifyOrAppendPlayerCriteria( AI_CriteriaSet& set )
{
	BaseClass::ModifyOrAppendPlayerCriteria( set );

	if ( GlobalEntity_GetIndex( "gordon_precriminal" ) == -1 )
	{
		set.AppendCriteria( "gordon_precriminal", "0" );
	}
}

#ifdef MAPBASE
const char *CPortal_Player::GetOverrideStepSound( const char *pszBaseStepSoundName )
{
	int idx = FindContextByName("footsteps");
	if (idx != -1)
	{
		const char *szSound = GetContextValue(idx);
		if (szSound[0] != '\0')
		{
			return szSound;
		}
	}
	return pszBaseStepSoundName;
}
#endif


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const impactdamagetable_t &CPortal_Player::GetPhysicsImpactDamageTable()
{
	if ( m_bUseCappedPhysicsDamageTable )
		return gCappedPlayerImpactDamageTable;
	
	return BaseClass::GetPhysicsImpactDamageTable();
}


//-----------------------------------------------------------------------------
// Purpose: Makes a splash when the player transitions between water states
//-----------------------------------------------------------------------------
void CPortal_Player::Splash( void )
{
	CEffectData data;
	data.m_fFlags = 0;
	data.m_vOrigin = GetAbsOrigin();
	data.m_vNormal = Vector(0,0,1);
	data.m_vAngles = QAngle( 0, 0, 0 );
	
	if ( GetWaterType() & CONTENTS_SLIME )
	{
		data.m_fFlags |= FX_WATER_IN_SLIME;
	}

	float flSpeed = GetAbsVelocity().Length();
	if ( flSpeed < 300 )
	{
		data.m_flScale = random->RandomFloat( 10, 12 );
		DispatchEffect( "waterripple", data );
	}
	else
	{
		data.m_flScale = random->RandomFloat( 6, 8 );
		DispatchEffect( "watersplash", data );
	}
}

CLogicPlayerProxy *CPortal_Player::GetPlayerProxy( void )
{
	CLogicPlayerProxy *pProxy = dynamic_cast< CLogicPlayerProxy* > ( m_hPlayerProxy.Get() );

	if ( pProxy == NULL )
	{
		pProxy = (CLogicPlayerProxy*)gEntList.FindEntityByClassname(NULL, "logic_playerproxy" );

		if ( pProxy == NULL )
			return NULL;

		pProxy->m_hPlayer = this;
		m_hPlayerProxy = pProxy;
#ifdef MAPBASE
		pProxy->NotifyPlayerHasProxy();
#endif
	}

	return pProxy;
}

void CPortal_Player::FirePlayerProxyOutput( const char *pszOutputName, variant_t variant, CBaseEntity *pActivator, CBaseEntity *pCaller )
{
	if ( GetPlayerProxy() == NULL )
		return;

	GetPlayerProxy()->FireNamedOutput( pszOutputName, variant, pActivator, pCaller );
}

LINK_ENTITY_TO_CLASS( logic_playerproxy, CLogicPlayerProxy );

BEGIN_DATADESC( CLogicPlayerProxy )
	DEFINE_OUTPUT( m_OnFlashlightOn, "OnFlashlightOn" ),
	DEFINE_OUTPUT( m_OnFlashlightOff, "OnFlashlightOff" ),
	DEFINE_OUTPUT( m_RequestedPlayerHealth, "PlayerHealth" ),
	DEFINE_OUTPUT( m_PlayerDied,	"PlayerDied" ),
#ifdef MAPBASE
	DEFINE_OUTPUT( m_PlayerDamaged, "PlayerDamaged" ),
	DEFINE_OUTPUT( m_OnPlayerSpawn, "OnPlayerSpawn" ),
#endif
	DEFINE_INPUTFUNC( FIELD_VOID,	"RequestPlayerHealth",	InputRequestPlayerHealth ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetPlayerHealth",	InputSetPlayerHealth ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"EnableCappedPhysicsDamage", InputEnableCappedPhysicsDamage ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"DisableCappedPhysicsDamage", InputDisableCappedPhysicsDamage ),
#ifdef PORTAL
	DEFINE_INPUTFUNC( FIELD_VOID,	"SuppressCrosshair", InputSuppressCrosshair ),
#endif // PORTAL
#ifdef MAPBASE
	DEFINE_INPUTFUNC( FIELD_STRING,	"SetHandModel", InputSetHandModel ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetHandModelSkin", InputSetHandModelSkin ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetHandModelBodyGroup", InputSetHandModelBodyGroup ),
	DEFINE_INPUTFUNC( FIELD_STRING,	"SetPlayerModel", InputSetPlayerModel ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetPlayerDrawExternally", InputSetPlayerDrawExternally ),
	DEFINE_INPUT( m_SuitZoomFOV, FIELD_INTEGER, "SetSuitZoomFOV" ),
#endif
	DEFINE_FIELD( m_hPlayer, FIELD_EHANDLE ),
END_DATADESC()

void CLogicPlayerProxy::Activate( void )
{
	BaseClass::Activate();

	if ( m_hPlayer == NULL )
	{
		m_hPlayer = AI_GetSinglePlayer();
	}
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Purpose: Cache user entity field values until spawn is called.
// Input  : szKeyName - Key to handle.
//			szValue - Value for key.
// Output : Returns true if the key was handled, false if not.
//-----------------------------------------------------------------------------
bool CLogicPlayerProxy::KeyValue( const char *szKeyName, const char *szValue )
{
	bool bPlayerKV = false;

	if (Q_strnicmp(szKeyName, "HandsVM", 7) == 0)
	{
		if (m_hPlayer)
		{
			szKeyName += 7;
			CBasePlayer *pPlayer = static_cast<CBasePlayer*>( m_hPlayer.Get() );
			CBaseViewModel *vm = pPlayer->GetViewModel(1);
			if (vm)
			{
				if (*szKeyName == NULL && PrecacheModel(szValue)) // HandsVM
					vm->SetModel(szValue);
				else if (FStrEq(szKeyName, "Skin")) // HandsVMSkin
					vm->m_nSkin = atoi(szValue);
				else if (FStrEq(szKeyName, "Body")) // HandsVMBody
					vm->m_nBody = atoi(szValue);
			}
			return true;
		}
	}
	else if (FStrEq(szKeyName, "ResponseContext"))
	{
		bPlayerKV = true;
		if (m_hPlayer)
			return m_hPlayer->KeyValue(szKeyName, szValue);
	}
	else if (FStrEq(szKeyName, "HideSquadHUD"))
	{
		if (m_hPlayer)
		{
			if (szValue[0] != '0')
				m_hPlayer->AddSpawnFlags(SF_PLAYER_HIDE_SQUAD_HUD);
			else
				m_hPlayer->RemoveSpawnFlags(SF_PLAYER_HIDE_SQUAD_HUD);
			return true;
		}
	}
	else if (FStrEq(szKeyName, "PlayerModel"))
	{
		if (m_hPlayer)
		{
			if (PrecacheModel( szValue ))
			{
				m_hPlayer->SetModel( szValue );
			}
			return true;
		}
	}
	else
	{
		if (BaseClass::KeyValue( szKeyName, szValue ))
			return true;

		if (m_hPlayer)
		{
			DevMsg("logic_playerproxy: Passing unhandled keyvalue \"%s, %s\" to player\n", szKeyName, szValue);
			return m_hPlayer->KeyValue(szKeyName, szValue);
		}
	}

	// If we reach this point, player is not available to test unidentified/special KV
	// Queue it up
	DevMsg("logic_playerproxy: Queueing %s, %s\n", szKeyName, szValue);
	m_QueuedKV.Insert(bPlayerKV ? UTIL_VarArgs("&&%s", szKeyName) : szKeyName, AllocPooledString(szValue));

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: calls the appropriate message mapped function in the entity according
//			to the fired action.
// Input  : char *szInputName - input destination
//			*pActivator - entity which initiated this sequence of actions
//			*pCaller - entity from which this event is sent
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CLogicPlayerProxy::AcceptInput( const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID )
{
	bool base = BaseClass::AcceptInput( szInputName, pActivator, pCaller, Value, outputID );

	if (!base)
	{
		if (m_hPlayer)
		{
			DevMsg("logic_playerproxy: Passing unhandled input \"%s\" to player\n", szInputName);
			return m_hPlayer->AcceptInput( szInputName, pActivator, pCaller, Value, outputID );
		}
		else
		{
			DevMsg("logic_playerproxy: Player not found!\n");

			// Need to allocate the string here in case szInputName is freed before the input fires
			g_EventQueue.AddEvent("!player", STRING( AllocPooledString(szInputName) ), Value, 0.01f, pActivator, pCaller);
		}
	}

	return base;
}

//-----------------------------------------------------------------------------
// Purpose: Notifies logic_playerproxy when player is valid
//-----------------------------------------------------------------------------
void CLogicPlayerProxy::NotifyPlayerHasProxy()
{
	Assert( m_hPlayer != NULL );

	// Handle any queued keyvalues
	int iQueueCount = m_QueuedKV.Count();
	for (int i = 0; i < iQueueCount; i++)
	{
		const char *name = m_QueuedKV.GetElementName(i);
		const char *value = STRING(m_QueuedKV[i]);
		DevMsg("logic_playerproxy: Handing over %s, %s from dict\n", name, value);

		if (name[0] == '&' && name[1] == '&')
		{
			// We're supposed to send this to the player
			m_hPlayer->KeyValue(name + 2, value);
		}

		KeyValue(name, value);
	}

	m_QueuedKV.RemoveAll();
}
#endif

bool CLogicPlayerProxy::PassesDamageFilter( const CTakeDamageInfo &info )
{
	if (m_hDamageFilter)
	{
		CBaseFilter *pFilter = (CBaseFilter *)(m_hDamageFilter.Get());
#ifdef MAPBASE
		return pFilter->PassesDamageFilter(m_hPlayer.Get(), info);
#else
		return pFilter->PassesDamageFilter(info);
#endif
	}

	return true;
}

void CLogicPlayerProxy::InputSetPlayerHealth( inputdata_t &inputdata )
{
	if ( m_hPlayer == NULL )
		return;

	m_hPlayer->SetHealth( inputdata.value.Int() );

}

void CLogicPlayerProxy::InputRequestPlayerHealth( inputdata_t &inputdata )
{
	if ( m_hPlayer == NULL )
		return;

	m_RequestedPlayerHealth.Set( m_hPlayer->GetHealth(), inputdata.pActivator, inputdata.pCaller );
}

void CLogicPlayerProxy::InputEnableCappedPhysicsDamage( inputdata_t &inputdata )
{
	if( m_hPlayer == NULL )
		return;

	CPortal_Player *pPlayer = dynamic_cast<CPortal_Player*>(m_hPlayer.Get());
	pPlayer->EnableCappedPhysicsDamage();
}

void CLogicPlayerProxy::InputDisableCappedPhysicsDamage( inputdata_t &inputdata )
{
	if( m_hPlayer == NULL )
		return;

	CPortal_Player *pPlayer = dynamic_cast<CPortal_Player*>(m_hPlayer.Get());
	pPlayer->DisableCappedPhysicsDamage();
}

void CLogicPlayerProxy::InputSuppressCrosshair( inputdata_t &inputdata )
{
	if( m_hPlayer == NULL )
		return;


	CPortal_Player* pPlayer = static_cast<CPortal_Player*>(m_hPlayer.Get());;
	pPlayer->SuppressCrosshair( true );
}

#ifdef MAPBASE
void CLogicPlayerProxy::InputSetHandModel( inputdata_t &inputdata )
{
	if (!m_hPlayer)
		return;

	string_t iszModel = inputdata.value.StringID();

	if (iszModel != NULL_STRING)
		PrecacheModel(STRING(iszModel));

	CBasePlayer *pPlayer = static_cast<CBasePlayer*>( m_hPlayer.Get() );
	CBaseViewModel *vm = pPlayer->GetViewModel(1);
	if (vm)
		vm->SetModel(STRING(iszModel));
}

void CLogicPlayerProxy::InputSetHandModelSkin( inputdata_t &inputdata )
{
	if (!m_hPlayer)
		return;

	CBasePlayer *pPlayer = static_cast<CBasePlayer*>( m_hPlayer.Get() );
	CBaseViewModel *vm = pPlayer->GetViewModel(1);
	if (vm)
		vm->m_nSkin = inputdata.value.Int();
}

void CLogicPlayerProxy::InputSetHandModelBodyGroup( inputdata_t &inputdata )
{
	if (!m_hPlayer)
		return;

	CBasePlayer *pPlayer = static_cast<CBasePlayer*>( m_hPlayer.Get() );
	CBaseViewModel *vm = pPlayer->GetViewModel(1);
	if (vm)
		vm->m_nBody = inputdata.value.Int();
}

void CLogicPlayerProxy::InputSetPlayerModel( inputdata_t &inputdata )
{
	if (!m_hPlayer)
		return;

	string_t iszModel = inputdata.value.StringID();

	if (iszModel != NULL_STRING)
		PrecacheModel( STRING( iszModel ) );
	else
	{
		// We're resetting the model. The original model should've been cached to our own model name.
		iszModel = GetModelName();
	}

	// Cache the original model as our own model name.
	SetModelName( m_hPlayer->GetModelName() );

	m_hPlayer->SetModel( STRING(iszModel) );
}

void CLogicPlayerProxy::InputSetPlayerDrawExternally( inputdata_t &inputdata )
{
	if (!m_hPlayer)
		return;

	CBasePlayer *pPlayer = static_cast<CBasePlayer*>(m_hPlayer.Get());
	pPlayer->SetDrawPlayerModelExternally( inputdata.value.Bool() );
}
#endif
