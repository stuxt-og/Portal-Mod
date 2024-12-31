//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $NoKeywords: $
//=============================================================================//
#if !defined( C_BASEHLPLAYER_H )
#define C_BASEHLPLAYER_H
#ifdef _WIN32
#pragma once
#endif


#include "c_baseplayer.h"
#include <portal/c_portal_playerlocaldata.h>

#if !defined( HL2MP ) && defined ( MAPBASE )
#include "mapbase/singleplayer_animstate.h"
#endif

class C_BasePortalPlayer : public C_BasePlayer
{
public:
	DECLARE_CLASS( C_BasePortalPlayer, C_BasePlayer );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

						C_BasePortalPlayer();

	virtual void		OnDataChanged( DataUpdateType_t updateType );

	void				Weapon_DropPrimary( void );
		
	float				GetFOV();
	void				Zoom( float FOVOffset, float time );
	float				GetZoom( void );
	bool				IsZoomed( void )	{ return m_PortalLocal.m_bZooming; }

	//Tony; minor cosmetic really, fix confusion by simply renaming this one; everything calls IsSprinting(), and this isn't really even used.
	bool				IsSprintActive( void ) { return m_PortalLocal.m_bitsActiveDevices & bits_SUIT_DEVICE_SPRINT; }
	bool				IsFlashlightActive( void ) { return m_PortalLocal.m_bitsActiveDevices & bits_SUIT_DEVICE_FLASHLIGHT; }
	bool				IsBreatherActive( void ) { return m_PortalLocal.m_bitsActiveDevices & bits_SUIT_DEVICE_BREATHER; }

	virtual int			DrawModel( int flags );
	virtual	void		BuildTransformations( CStudioHdr *hdr, Vector *pos, Quaternion q[], const matrix3x4_t& cameraTransform, int boneMask, CBoneBitList &boneComputed );

	LadderMove_t		*GetLadderMove() { return &m_PortalLocal.m_LadderMove; }
	virtual void		ExitLadder();
	bool				IsSprinting() const { return m_fIsSprinting; }
	
	// Input handling
	virtual bool	CreateMove( float flInputSampleTime, CUserCmd *pCmd );
	void			PerformClientSideObstacleAvoidance( float flFrameTime, CUserCmd *pCmd );
	void			PerformClientSideNPCSpeedModifiers( float flFrameTime, CUserCmd *pCmd );

#ifdef SP_ANIM_STATE
	virtual const QAngle&	GetRenderAngles( void );
#endif

	bool IsSuppressingCrosshair(void) { return m_bSuppressingCrosshair; }

public:

	C_PortaPlayerLocalData		m_PortalLocal;
	EHANDLE				m_hClosestNPC;
	float				m_flSpeedModTime;
	bool				m_fIsSprinting;

private:
	C_BasePortalPlayer( const C_BasePortalPlayer & ); // not defined, not accessible
	
	bool				TestMove( const Vector &pos, float fVertDist, float radius, const Vector &objPos, const Vector &objDir );

	float				m_flZoomStart;
	float				m_flZoomEnd;
	float				m_flZoomRate;
	float				m_flZoomStartTime;

	bool				m_bPlayUseDenySound;		// Signaled by PlayerUse, but can be unset by Portal ladder code...
	float				m_flSpeedMod;
	float				m_flExitSpeedMod;
	
	bool m_bSuppressingCrosshair;

#ifdef SP_ANIM_STATE
	// At the moment, we network the render angles since almost none of the player anim stuff is done on the client in SP.
	// If any of this is ever adapted for MP, this method should be replaced with replicating/moving the anim state to the client.
	float				m_flAnimRenderYaw;
	QAngle				m_angAnimRender;
#endif

friend class CHL2GameMovement;
};


#endif
