//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game rules for Half-Life 2.
//
//=============================================================================//

#ifndef HL2_GAMERULES_H
#define HL2_GAMERULES_H
#ifdef _WIN32
#pragma once
#endif

#include "gamerules.h"
#include "singleplay_gamerules.h"
#include "hl2_shareddefs.h"

#ifdef CLIENT_DLL
	#define CPortal C_Portal
	#define CPortalProxy C_PortalProxy
#endif

#if MAPBASE && GAME_DLL
#define FRIENDLY_FIRE_GLOBALNAME "friendly_fire_override"
#endif


class CPortalProxy : public CGameRulesProxy
{
public:
	DECLARE_CLASS( CPortalProxy, CGameRulesProxy );
	DECLARE_NETWORKCLASS();

#if defined(MAPBASE) && defined(GAME_DLL)
	bool KeyValue( const char *szKeyName, const char *szValue );
	bool GetKeyValue( const char *szKeyName, char *szValue, int iMaxLen );

	virtual int	Save( ISave &save );
	virtual int	Restore( IRestore &restore );
	virtual void UpdateOnRemove();

	// Inputs
	void InputEpisodicOn( inputdata_t &inputdata );
	void InputEpisodicOff( inputdata_t &inputdata );
	void InputSetFriendlyFire( inputdata_t &inputdata );
	void InputSetDefaultCitizenType( inputdata_t &inputdata );
	void InputSetLegacyFlashlight( inputdata_t &inputdata );
	void InputSetAllowSPRespawn( inputdata_t &inputdata );

	// Gamerules classes don't seem to support datadescs, so the portal_gamerules entity takes the current values
	// from the actual gamerules and saves them in the entity itself, where they're saved via the entity's own datadesc.
	// When the save is loaded, the entity sets the main gamerules values according to what was saved.
	int m_save_DefaultCitizenType;
	char m_save_LegacyFlashlight;
	bool m_save_AllowSPRespawn;

	DECLARE_DATADESC();
#endif
};


class CPortal : public CSingleplayRules
{
public:
	DECLARE_CLASS( CPortal, CSingleplayRules );

	// Damage Query Overrides.
	virtual bool			Damage_IsTimeBased( int iDmgType );
	// TEMP:
	virtual int				Damage_GetTimeBased( void );
	
	virtual bool			ShouldCollide( int collisionGroup0, int collisionGroup1 );
	virtual bool			ShouldUseRobustRadiusDamage(CBaseEntity *pEntity);
#ifndef CLIENT_DLL
	virtual bool			ShouldAutoAim( CBasePlayer *pPlayer, edict_t *target );
	virtual float			GetAutoAimScale( CBasePlayer *pPlayer );
	virtual float			GetAmmoQuantityScale( int iAmmoIndex );
	virtual void			LevelInitPreEntity();
#endif

#ifdef MAPBASE_VSCRIPT
	virtual void			RegisterScriptFunctions( void );
#endif

private:
	// Rules change for the mega physgun
	CNetworkVar( bool, m_bMegaPhysgun );

#ifdef CLIENT_DLL

	DECLARE_CLIENTCLASS_NOBASE(); // This makes datatables able to access our private vars.

#else

	DECLARE_SERVERCLASS_NOBASE(); // This makes datatables able to access our private vars.

	CPortal();
	virtual ~CPortal() {}

	virtual void			Think( void );

	virtual bool			ClientCommand( CBaseEntity *pEdict, const CCommand &args );
	virtual void			PlayerSpawn( CBasePlayer *pPlayer );

	virtual void			InitDefaultAIRelationships( void );
	virtual const char*		AIClassText(int classType);
	virtual const char *GetGameDescription( void ) { return "Portal"; }

	// Ammo
	virtual void			PlayerThink( CBasePlayer *pPlayer );
	virtual float			GetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType );

	virtual bool			ShouldBurningPropsEmitLight();
public:

	bool AllowDamage( CBaseEntity *pVictim, const CTakeDamageInfo &info );

	bool	MegaPhyscannonActive( void ) { return m_bMegaPhysgun; }

#ifdef MAPBASE
	int				GetDefaultCitizenType();
	void			SetDefaultCitizenType(int val);

	ThreeState_t	GlobalFriendlyFire();
	void			SetGlobalFriendlyFire(ThreeState_t val);

	virtual bool	AllowSPRespawn();
	void			SetAllowSPRespawn( bool toggle );
#endif

private:
#ifdef MAPBASE
	int		m_DefaultCitizenType;
	bool	m_bAllowSPRespawn;
#endif

	void AdjustPlayerDamageTaken( CTakeDamageInfo *pInfo );
	float AdjustPlayerDamageInflicted( float damage );

	int						DefaultFOV( void ) { return 75; }
#endif
};


//-----------------------------------------------------------------------------
// Gets us at the Portal game rules
//-----------------------------------------------------------------------------
inline CPortal* PortalGameRules()
{
#if ( !defined( HL2_DLL ) && !defined( HL2_CLIENT_DLL ) ) || defined( HL2MP )
	Assert( 0 );	// g_pGameRules is NOT an instance of CPortal and bad things happen
#endif

	return static_cast<CPortal*>(g_pGameRules);
}



#endif // HL2_GAMERULES_H
