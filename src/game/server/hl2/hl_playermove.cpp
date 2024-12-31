//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "player_command.h"
#include "player.h"
#include "igamemovement.h"
#include "hl_movedata.h"
#include "ipredictionsystem.h"
#include "iservervehicle.h"
#include <portal/portal_player.h>
#include "gamestats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CHLPlayerMove : public CPlayerMove
{
	DECLARE_CLASS( CHLPlayerMove, CPlayerMove );
public:
	CHLPlayerMove() :
		m_bWasInVehicle( false ),
		m_bVehicleFlipped( false ),
		m_bInGodMode( false ),
		m_bInNoClip( false )
	{
		m_vecSaveOrigin.Init();
	}

	void SetupMove( CBasePlayer *player, CUserCmd *ucmd, IMoveHelper *pHelper, CMoveData *move );
	void FinishMove( CBasePlayer *player, CUserCmd *ucmd, CMoveData *move );

private:
	Vector m_vecSaveOrigin;
	bool m_bWasInVehicle;
	bool m_bVehicleFlipped;
	bool m_bInGodMode;
	bool m_bInNoClip;
};

//
//
// PlayerMove Interface
static CHLPlayerMove g_PlayerMove;

//-----------------------------------------------------------------------------
// Singleton accessor
//-----------------------------------------------------------------------------
CPlayerMove *PlayerMove()
{
	return &g_PlayerMove;
}

//

static CHLMoveData g_HLMoveData;
CMoveData *g_pMoveData = &g_HLMoveData;

IPredictionSystem *IPredictionSystem::g_pPredictionSystems = NULL;

void CHLPlayerMove::SetupMove( CBasePlayer *player, CUserCmd *ucmd, IMoveHelper *pHelper, CMoveData *move )
{
	// Call the default SetupMove code.
	BaseClass::SetupMove( player, ucmd, pHelper, move );

	// Convert to HL2 data.
	CPortal_Player *pHLPlayer = static_cast<CPortal_Player*>( player );
	Assert( pHLPlayer );

	CHLMoveData *pHLMove = static_cast<CHLMoveData*>( move );
	Assert( pHLMove );

	player->m_flForwardMove = ucmd->forwardmove;
	player->m_flSideMove = ucmd->sidemove;

	pHLMove->m_bIsSprinting = pHLPlayer->IsSprinting();

	if ( gpGlobals->frametime != 0 )
	{
		IServerVehicle *pVehicle = player->GetVehicle();

		if ( pVehicle )
		{
			pVehicle->SetupMove( player, ucmd, pHelper, move ); 

			if ( !m_bWasInVehicle )
			{
				m_bWasInVehicle = true;
				m_vecSaveOrigin.Init();
			}
		}
		else
		{
			m_vecSaveOrigin = player->GetAbsOrigin();
			if ( m_bWasInVehicle )
			{
				m_bWasInVehicle = false;
			}
		}
	}
}


void CHLPlayerMove::FinishMove( CBasePlayer *player, CUserCmd *ucmd, CMoveData *move )
{
	// Call the default FinishMove code.
	BaseClass::FinishMove( player, ucmd, move );
	if ( gpGlobals->frametime != 0 )
	{		
		float distance = VectorLength(player->GetAbsOrigin() - m_vecSaveOrigin);

		m_bVehicleFlipped = false;

		if ( distance > 0 )
		{
			gamestats->Event_PlayerTraveled( player, distance, false, true && static_cast< CPortal_Player * >( player )->IsSprinting() );
		}
	}

	bool bGodMode = ( player->GetFlags() & FL_GODMODE ) ? true : false;
	if ( m_bInGodMode != bGodMode )
	{
		m_bInGodMode = bGodMode;
		if ( bGodMode )
		{
			gamestats->Event_PlayerEnteredGodMode( player );
		}
	}
	bool bNoClip = ( player->GetMoveType() == MOVETYPE_NOCLIP );
	if ( m_bInNoClip != bNoClip )
	{
		m_bInNoClip = bNoClip;
		if ( bNoClip )
		{
			gamestats->Event_PlayerEnteredNoClip( player );
		}
	}
}
