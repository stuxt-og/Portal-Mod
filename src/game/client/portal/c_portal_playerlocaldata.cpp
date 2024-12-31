//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include <portal/c_portal_playerlocaldata.h>
#include "dt_recv.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_RECV_TABLE_NOBASE( C_PortaPlayerLocalData, DT_PortalLocal )
	RecvPropFloat( RECVINFO(m_flSuitPower) ),
	RecvPropInt( RECVINFO(m_bZooming) ),
	RecvPropInt( RECVINFO(m_bitsActiveDevices) ),
	RecvPropInt( RECVINFO(m_iSquadMemberCount) ),
	RecvPropInt( RECVINFO(m_iSquadMedicCount) ),
	RecvPropBool( RECVINFO(m_fSquadInFollowMode) ),
	RecvPropBool( RECVINFO(m_bWeaponLowered) ),
	RecvPropEHandle( RECVINFO(m_hAutoAimTarget) ),
	RecvPropVector( RECVINFO(m_vecAutoAimPoint) ),
	RecvPropEHandle( RECVINFO(m_hLadder) ),
	RecvPropBool( RECVINFO(m_bDisplayReticle) ),
	RecvPropBool( RECVINFO(m_bStickyAutoAim) ),
	RecvPropBool( RECVINFO(m_bAutoAimTarget) ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA_NO_BASE( C_PortaPlayerLocalData )
	DEFINE_PRED_FIELD( m_hLadder, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()

C_PortaPlayerLocalData::C_PortaPlayerLocalData()
{
	m_flSuitPower = 0.0;
	m_bZooming = false;
	m_iSquadMemberCount = 0;
	m_iSquadMedicCount = 0;
	m_fSquadInFollowMode = false;
	m_bWeaponLowered = false;
	m_hLadder = NULL;
}

