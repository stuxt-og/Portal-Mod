//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The Half-Life 2 game rules, such as the relationship tables and ammo
//			damage cvars.
//
//=============================================================================

#include "cbase.h"
#include <portal/portal_gamerules.h>
#include "ammodef.h"
#include "hl2_shareddefs.h"

#ifndef CLIENT_DLL
	#include "player.h"
	#include "game.h"
	#include "gamerules.h"
	#include "teamplay_gamerules.h"
	#include <portal/portal_player.h>
	#include "voice_gamemgr.h"
	#include "globalstate.h"
	#include "ai_basenpc.h"
	#include "weapon_physcannon.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

REGISTER_GAMERULES_CLASS( CPortal );

BEGIN_NETWORK_TABLE_NOBASE( CPortal, DT_PortalGameRules )
	#ifdef CLIENT_DLL
		RecvPropBool( RECVINFO( m_bMegaPhysgun ) ),
	#else
		SendPropBool( SENDINFO( m_bMegaPhysgun ) ),
	#endif
END_NETWORK_TABLE()

#if MAPBASE && GAME_DLL
extern bool g_bUseLegacyFlashlight;
extern bool g_bCacheLegacyFlashlightStatus;

BEGIN_DATADESC( CPortalProxy )

	// These get the gamerules values on save and write to them on restore
	DEFINE_FIELD( m_save_DefaultCitizenType, FIELD_INTEGER ),
	DEFINE_FIELD( m_save_LegacyFlashlight, FIELD_CHARACTER ),
	DEFINE_FIELD( m_save_AllowSPRespawn, FIELD_BOOLEAN ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_VOID, "EpisodicOn", InputEpisodicOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EpisodicOff", InputEpisodicOff ),

	// These are FIELD_STRING because they call KeyValue() directly
	DEFINE_INPUTFUNC( FIELD_STRING, "SetFriendlyFire", InputSetFriendlyFire ),
	DEFINE_INPUTFUNC( FIELD_STRING, "DefaultCitizenType", InputSetDefaultCitizenType ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetLegacyFlashlight", InputSetLegacyFlashlight ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetAllowSPRespawn", InputSetAllowSPRespawn ),

END_DATADESC()
#endif

LINK_ENTITY_TO_CLASS( portal_gamerules, CPortalProxy );
IMPLEMENT_NETWORKCLASS_ALIASED( PortalProxy, DT_PortalProxy )

#if defined(MAPBASE) && defined(GAME_DLL)
void CPortalProxy::InputEpisodicOn( inputdata_t &inputdata ) { KeyValue("SetEpisodic", "1"); }
void CPortalProxy::InputEpisodicOff( inputdata_t &inputdata ) { KeyValue("SetEpisodic", "0"); }
void CPortalProxy::InputSetFriendlyFire( inputdata_t &inputdata ) { KeyValue("GlobalFriendlyFire", inputdata.value.String()); }
void CPortalProxy::InputSetDefaultCitizenType( inputdata_t &inputdata ) { KeyValue("DefaultCitizenType", inputdata.value.String()); }
void CPortalProxy::InputSetLegacyFlashlight( inputdata_t &inputdata ) { KeyValue("SetLegacyFlashlight", inputdata.value.String()); }
void CPortalProxy::InputSetAllowSPRespawn( inputdata_t &inputdata ) { KeyValue( "SetAllowSPRespawn", inputdata.value.String() ); }

//-----------------------------------------------------------------------------
// Purpose: Cache user entity field values until spawn is called.
// Input  : szKeyName - Key to handle.
//			szValue - Value for key.
// Output : Returns true if the key was handled, false if not.
//-----------------------------------------------------------------------------
bool CPortalProxy::KeyValue( const char *szKeyName, const char *szValue )
{
	if (FStrEq(szKeyName, "DefaultCitizenType"))
	{
		PortalGameRules()->SetDefaultCitizenType(atoi(szValue));
	}
	else if (FStrEq(szKeyName, "GlobalFriendlyFire"))
	{
		PortalGameRules()->SetGlobalFriendlyFire(TO_THREESTATE(atoi(szValue)));
	}
	else if (FStrEq(szKeyName, "SetEpisodic") && !FStrEq(szValue, "2"))
	{
		hl2_episodic.SetValue(!FStrEq(szValue, "0"));
	}
	else if (FStrEq(szKeyName, "SetLegacyFlashlight"))
	{
		// Turn off flashlights first
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );

			if ( pPlayer )
			{
				if (pPlayer->FlashlightIsOn())
					pPlayer->FlashlightTurnOff();
			}
		}

		g_bUseLegacyFlashlight = !FStrEq(szValue, "0");

		// We have overridden it, don't test directory
		g_bCacheLegacyFlashlightStatus = false;

		// Tell our save/load we've modified it
		// 1 = modified, 2 = legacy enabled
		m_save_LegacyFlashlight |= 1;
		if (g_bUseLegacyFlashlight)
			m_save_LegacyFlashlight |= 2;
	}
	else if (FStrEq(szKeyName, "SetAllowSPRespawn"))
	{
		PortalGameRules()->SetAllowSPRespawn(!FStrEq(szValue, "0"));
	}
	else
	{
		return BaseClass::KeyValue( szKeyName, szValue );
	}

	return true;
}

bool CPortalProxy::GetKeyValue( const char *szKeyName, char *szValue, int iMaxLen )
{
	if (FStrEq(szKeyName, "DefaultCitizenType"))
	{
		Q_snprintf( szValue, iMaxLen, "%i", PortalGameRules()->GetDefaultCitizenType() );
	}
	else if (FStrEq(szKeyName, "GlobalFriendlyFire"))
	{
		Q_snprintf( szValue, iMaxLen, "%i", PortalGameRules()->GlobalFriendlyFire() );
	}
	else if (FStrEq(szKeyName, "SetEpisodic") && !FStrEq(szValue, "2"))
	{
		Q_snprintf( szValue, iMaxLen, "%s", hl2_episodic.GetString() );
	}
	else if (FStrEq(szKeyName, "SetLegacyFlashlight"))
	{
		Q_snprintf( szValue, iMaxLen, "%d", g_bUseLegacyFlashlight );
	}
	else if (FStrEq(szKeyName, "SetAllowSPRespawn"))
	{
		Q_snprintf( szValue, iMaxLen, "%d", PortalGameRules()->AllowSPRespawn() );
	}
	else
	{
		return BaseClass::GetKeyValue( szKeyName, szValue, iMaxLen );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Saves the current object out to disk, by iterating through the objects
//			data description hierarchy
// Input  : &save - save buffer which the class data is written to
// Output : int	- 0 if the save failed, 1 on success
//-----------------------------------------------------------------------------
int CPortalProxy::Save( ISave &save )
{
	m_save_DefaultCitizenType = PortalGameRules()->GetDefaultCitizenType();

	// As a static variable, this is actually kept across save games, but lost when the game exits.
	// NOTE: Now set in KeyValue() directly
	//m_save_LegacyFlashlight = (g_bUseLegacyFlashlight);

	m_save_AllowSPRespawn = PortalGameRules()->AllowSPRespawn();

	return BaseClass::Save(save);
}

//-----------------------------------------------------------------------------
// Purpose: Restores the current object from disk, by iterating through the objects
//			data description hierarchy
// Input  : &restore - restore buffer which the class data is read from
// Output : int	- 0 if the restore failed, 1 on success
//-----------------------------------------------------------------------------
int CPortalProxy::Restore( IRestore &restore )
{
	int base = BaseClass::Restore(restore);

	PortalGameRules()->SetDefaultCitizenType(m_save_DefaultCitizenType);

	// Are we modding the legacy flashlight?
	if (m_save_LegacyFlashlight & 1)
	{
		g_bUseLegacyFlashlight = (m_save_LegacyFlashlight & 2) != 0;

		// If we've got the desired legacy flashlight state saved, don't bother caching.
		g_bCacheLegacyFlashlightStatus = false;
	}

	PortalGameRules()->SetAllowSPRespawn(m_save_AllowSPRespawn);

	return base;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPortalProxy::UpdateOnRemove()
{
	// Were we modding the legacy flashlight?
	if (m_save_LegacyFlashlight & 1)
	{
		// Restore the default state.
		g_bCacheLegacyFlashlightStatus = true;
	}

	BaseClass::UpdateOnRemove();
}
#endif


#ifdef CLIENT_DLL
	void RecvProxy_PortalGameRules(const RecvProp* pProp, void** pOut, void* pData, int objectID)
	{
		CPortal *pRules = PortalGameRules();
		Assert( pRules );
		*pOut = pRules;
	}

	BEGIN_RECV_TABLE( CPortalProxy, DT_PortalProxy )
		RecvPropDataTable( "portal_gamerules_data", 0, 0, &REFERENCE_RECV_TABLE( DT_PortalGameRules ), RecvProxy_PortalGameRules )
	END_RECV_TABLE()
#else
	void* SendProxy_PortalGameRules( const SendProp *pProp, const void *pStructBase, const void *pData, CSendProxyRecipients *pRecipients, int objectID )
	{
		CPortal *pRules = PortalGameRules();
		Assert( pRules );
		pRecipients->SetAllRecipients();
		return pRules;
	}
	// portal_gamerules
	BEGIN_SEND_TABLE( CPortalProxy, DT_PortalProxy )
		SendPropDataTable( "portal_gamerules_data", 0, &REFERENCE_SEND_TABLE( DT_PortalGameRules ), SendProxy_PortalGameRules )
	END_SEND_TABLE()
#endif

ConVar  physcannon_mega_enabled( "physcannon_mega_enabled", "0", FCVAR_CHEAT | FCVAR_REPLICATED );

// Controls the application of the robus radius damage model.
ConVar	sv_robust_explosions( "sv_robust_explosions","1", FCVAR_REPLICATED );

// Damage scale for damage inflicted by the player on each skill level.
ConVar	sk_dmg_inflict_scale1( "sk_dmg_inflict_scale1", "1.50", FCVAR_REPLICATED );
ConVar	sk_dmg_inflict_scale2( "sk_dmg_inflict_scale2", "1.00", FCVAR_REPLICATED );
ConVar	sk_dmg_inflict_scale3( "sk_dmg_inflict_scale3", "0.75", FCVAR_REPLICATED );

// Damage scale for damage taken by the player on each skill level.
ConVar	sk_dmg_take_scale1( "sk_dmg_take_scale1", "0.50", FCVAR_REPLICATED );
ConVar	sk_dmg_take_scale2( "sk_dmg_take_scale2", "1.00", FCVAR_REPLICATED );
#ifdef HL2_EPISODIC
	ConVar	sk_dmg_take_scale3( "sk_dmg_take_scale3", "2.0", FCVAR_REPLICATED );
#else
	ConVar	sk_dmg_take_scale3( "sk_dmg_take_scale3", "1.50", FCVAR_REPLICATED );
#endif//HL2_EPISODIC

ConVar	sk_allow_autoaim( "sk_allow_autoaim", "1", FCVAR_REPLICATED | FCVAR_ARCHIVE_XBOX );

// Autoaim scale
ConVar	sk_autoaim_scale1( "sk_autoaim_scale1", "1.0", FCVAR_REPLICATED );
ConVar	sk_autoaim_scale2( "sk_autoaim_scale2", "1.0", FCVAR_REPLICATED );
//ConVar	sk_autoaim_scale3( "sk_autoaim_scale3", "0.0", FCVAR_REPLICATED ); NOT CURRENTLY OFFERED ON SKILL 3

// Quantity scale for ammo received by the player.
ConVar	sk_ammo_qty_scale1 ( "sk_ammo_qty_scale1", "1.20", FCVAR_REPLICATED );
ConVar	sk_ammo_qty_scale2 ( "sk_ammo_qty_scale2", "1.00", FCVAR_REPLICATED );
ConVar	sk_ammo_qty_scale3 ( "sk_ammo_qty_scale3", "0.60", FCVAR_REPLICATED );

ConVar	sk_plr_dmg_pistol("sk_plr_dmg_pistol", "0", FCVAR_REPLICATED);
ConVar	sk_npc_dmg_pistol("sk_npc_dmg_pistol", "0", FCVAR_REPLICATED);
ConVar	sk_max_pistol("sk_max_pistol", "0", FCVAR_REPLICATED);

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iDmgType - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
int CPortal::Damage_GetTimeBased( void )
{
#ifdef HL2_EPISODIC
	int iDamage = ( DMG_PARALYZE | DMG_NERVEGAS | DMG_POISON | DMG_RADIATION | DMG_DROWNRECOVER | DMG_ACID | DMG_SLOWBURN );
	return iDamage;
#else
	return BaseClass::Damage_GetTimeBased();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iDmgType - 
// Output :		bool
//-----------------------------------------------------------------------------
bool CPortal::Damage_IsTimeBased( int iDmgType )
{
	// Damage types that are time-based.
#ifdef HL2_EPISODIC
	// This makes me think EP2 should have its own rules, but they are #ifdef all over in here.
	return ( ( iDmgType & ( DMG_PARALYZE | DMG_NERVEGAS | DMG_POISON | DMG_RADIATION | DMG_DROWNRECOVER | DMG_ACID | DMG_SLOWBURN ) ) != 0 );
#else
	return BaseClass::Damage_IsTimeBased( iDmgType );
#endif
}

#ifdef CLIENT_DLL //{


#else //}{

	extern bool		g_fGameOver;

#if !(defined( HL2MP ) || defined( PORTAL_MP ))
	class CVoiceGameMgrHelper : public IVoiceGameMgrHelper
	{
	public:
		virtual bool		CanPlayerHearPlayer( CBasePlayer *pListener, CBasePlayer *pTalker, bool &bProximity )
		{
			return true;
		}
	};
	CVoiceGameMgrHelper g_VoiceGameMgrHelper;
	IVoiceGameMgrHelper *g_pVoiceGameMgrHelper = &g_VoiceGameMgrHelper;
#endif
	
	//-----------------------------------------------------------------------------
	// Purpose:
	// Input  :
	// Output :
	//-----------------------------------------------------------------------------
	CPortal::CPortal()
	{
		m_bMegaPhysgun = false;

#ifdef MAPBASE
		m_DefaultCitizenType = 0;
		m_bAllowSPRespawn = false;
#endif
	}

	//-----------------------------------------------------------------------------
	// Purpose: called each time a player uses a "cmd" command
	// Input  : *pEdict - the player who issued the command
	//			Use engine.Cmd_Argv,  engine.Cmd_Argv, and engine.Cmd_Argc to get 
	//			pointers the character string command.
	//-----------------------------------------------------------------------------
	bool CPortal::ClientCommand( CBaseEntity *pEdict, const CCommand &args )
	{
		if( BaseClass::ClientCommand( pEdict, args ) )
			return true;

		CPortal_Player *pPlayer = (CPortal_Player *) pEdict;

		if ( pPlayer->ClientCommand( args ) )
			return true;

		return false;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Player has just spawned. Equip them.
	//-----------------------------------------------------------------------------
	void CPortal::PlayerSpawn( CBasePlayer *pPlayer )
	{
	}

	//-----------------------------------------------------------------------------
	// Purpose: MULTIPLAYER BODY QUE HANDLING
	//-----------------------------------------------------------------------------
	class CCorpse : public CBaseAnimating
	{
	public:
		DECLARE_CLASS( CCorpse, CBaseAnimating );
		DECLARE_SERVERCLASS();

		virtual int ObjectCaps( void ) { return FCAP_DONT_SAVE; }	

	public:
		CNetworkVar( int, m_nReferencePlayer );
	};

	IMPLEMENT_SERVERCLASS_ST(CCorpse, DT_Corpse)
		SendPropInt( SENDINFO(m_nReferencePlayer), 10, SPROP_UNSIGNED )
	END_SEND_TABLE()

	LINK_ENTITY_TO_CLASS( bodyque, CCorpse );


	CCorpse		*g_pBodyQueueHead;

	void InitBodyQue(void)
	{
		CCorpse *pEntity = ( CCorpse * )CreateEntityByName( "bodyque" );
		pEntity->AddEFlags( EFL_KEEP_ON_RECREATE_ENTITIES );
		g_pBodyQueueHead = pEntity;
		CCorpse *p = g_pBodyQueueHead;
		
		// Reserve 3 more slots for dead bodies
		for ( int i = 0; i < 3; i++ )
		{
			CCorpse *next = ( CCorpse * )CreateEntityByName( "bodyque" );
			next->AddEFlags( EFL_KEEP_ON_RECREATE_ENTITIES );
			p->SetOwnerEntity( next );
			p = next;
		}
		
		p->SetOwnerEntity( g_pBodyQueueHead );
	}

	//-----------------------------------------------------------------------------
	// Purpose: make a body que entry for the given ent so the ent can be respawned elsewhere
	// GLOBALS ASSUMED SET:  g_eoBodyQueueHead
	//-----------------------------------------------------------------------------
	void CopyToBodyQue( CBaseAnimating *pCorpse ) 
	{
		if ( pCorpse->IsEffectActive( EF_NODRAW ) )
			return;

		CCorpse *pHead	= g_pBodyQueueHead;

		pHead->CopyAnimationDataFrom( pCorpse );

		pHead->SetMoveType( MOVETYPE_FLYGRAVITY );
		pHead->SetAbsVelocity( pCorpse->GetAbsVelocity() );
		pHead->ClearFlags();
		pHead->m_nReferencePlayer	= ENTINDEX( pCorpse );

		pHead->SetLocalAngles( pCorpse->GetAbsAngles() );
		UTIL_SetOrigin(pHead, pCorpse->GetAbsOrigin());

		UTIL_SetSize(pHead, pCorpse->WorldAlignMins(), pCorpse->WorldAlignMaxs());
		g_pBodyQueueHead = (CCorpse *)pHead->GetOwnerEntity();
	}

	//------------------------------------------------------------------------------
	// Purpose : Initialize all default class relationships
	// Input   :
	// Output  :
	//------------------------------------------------------------------------------
	void CPortal::InitDefaultAIRelationships( void )
	{
		int i, j;

		//  Allocate memory for default relationships
		CBaseCombatCharacter::AllocateDefaultRelationships();

		// --------------------------------------------------------------
		// First initialize table so we can report missing relationships
		// --------------------------------------------------------------
		for (i=0;i<NUM_AI_CLASSES;i++)
		{
			for (j=0;j<NUM_AI_CLASSES;j++)
			{
				// By default all relationships are neutral of priority zero
				CBaseCombatCharacter::SetDefaultRelationship( (Class_T)i, (Class_T)j, D_NU, 0 );
			}
		}

		// ------------------------------------------------------------
		//	> CLASS_BULLSEYE
		// ------------------------------------------------------------
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_BULLSEYE,			CLASS_NONE,				D_NU, 0);			
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_BULLSEYE,			CLASS_PLAYER,			D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_BULLSEYE,			CLASS_BULLSEYE,			D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_BULLSEYE,			CLASS_PLAYER_ALLY,		D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_BULLSEYE,			CLASS_PLAYER_ALLY_VITAL,D_NU, 0);

		// ------------------------------------------------------------
		//	> CLASS_NONE
		// ------------------------------------------------------------
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_NONE,				CLASS_NONE,				D_NU, 0);			
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_NONE,				CLASS_PLAYER,			D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_NONE,				CLASS_BULLSEYE,			D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_NONE,				CLASS_PLAYER_ALLY,		D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_NONE,				CLASS_PLAYER_ALLY_VITAL,D_NU, 0);

		// ------------------------------------------------------------
		//	> CLASS_PLAYER
		// ------------------------------------------------------------
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER,			CLASS_NONE,				D_NU, 0);			
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER,			CLASS_PLAYER,			D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER,			CLASS_BULLSEYE,			D_HT, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER,			CLASS_PLAYER_ALLY,		D_LI, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER,			CLASS_PLAYER_ALLY_VITAL,D_LI, 0);

		// ------------------------------------------------------------
		//	> CLASS_PLAYER_ALLY
		// ------------------------------------------------------------
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER_ALLY,			CLASS_NONE,				D_NU, 0);			
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER_ALLY,			CLASS_PLAYER,			D_LI, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER_ALLY,			CLASS_BULLSEYE,			D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER_ALLY,			CLASS_PLAYER_ALLY,		D_LI, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_PLAYER_ALLY,			CLASS_PLAYER_ALLY_VITAL,D_LI, 0);

		// ------------------------------------------------------------
		//	> CLASS_COMBINE
		// ------------------------------------------------------------
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_COMBINE, CLASS_NONE, D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_COMBINE, CLASS_PLAYER, D_HT, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_COMBINE, CLASS_BULLSEYE, D_NU, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_COMBINE, CLASS_COMBINE, D_LI, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_COMBINE, CLASS_PLAYER_ALLY, D_HT, 0);
		CBaseCombatCharacter::SetDefaultRelationship(CLASS_COMBINE, CLASS_PLAYER_ALLY_VITAL, D_HT, 0);
	}


	//------------------------------------------------------------------------------
	// Purpose : Return classify text for classify type
	// Input   :
	// Output  :
	//------------------------------------------------------------------------------
	const char* CPortal::AIClassText(int classType)
	{
		switch (classType)
		{
			case CLASS_NONE:			return "CLASS_NONE";
			case CLASS_PLAYER:			return "CLASS_PLAYER";

			case CLASS_PLAYER_ALLY:		return "CLASS_PLAYER_ALLY";
			case CLASS_PLAYER_ALLY_VITAL:	return "CLASS_PLAYER_ALLY_VITAL";
			case CLASS_COMBINE:			return "CLASS_COMBINE";
			default:					return "MISSING CLASS in ClassifyText()";
		}
	}

	void CPortal::PlayerThink( CBasePlayer *pPlayer )
	{
	}

	void CPortal::Think( void )
	{
		BaseClass::Think();

		if( physcannon_mega_enabled.GetBool() == true )
		{
			m_bMegaPhysgun = true;
		}
		else
		{
			// FIXME: Is there a better place for this?
			m_bMegaPhysgun = ( GlobalEntity_GetState("super_phys_gun") == GLOBAL_ON );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: Returns how much damage the given ammo type should do to the victim
	//			when fired by the attacker.
	// Input  : pAttacker - Dude what shot the gun.
	//			pVictim - Dude what done got shot.
	//			nAmmoType - What been shot out.
	// Output : How much hurt to put on dude what done got shot (pVictim).
	//-----------------------------------------------------------------------------
	float CPortal::GetAmmoDamage( CBaseEntity *pAttacker, CBaseEntity *pVictim, int nAmmoType )
	{
		float flDamage = 0.0f;
		CAmmoDef *pAmmoDef = GetAmmoDef();

		if ( pAmmoDef->DamageType( nAmmoType ) & DMG_SNIPER )
		{
			// If this damage is from a SNIPER, we do damage based on what the bullet
			// HITS, not who fired it. All other bullets have their damage values
			// arranged according to the owner of the bullet, not the recipient.
			if ( pVictim->IsPlayer() )
			{
				// Player
				flDamage = pAmmoDef->PlrDamage( nAmmoType );
			}
			else
			{
				// NPC or breakable
				flDamage = pAmmoDef->NPCDamage( nAmmoType );
			}
		}
		else
		{
			flDamage = BaseClass::GetAmmoDamage( pAttacker, pVictim, nAmmoType );
		}

		if( pAttacker->IsPlayer() && pVictim->IsNPC() )
		{
			if( pVictim->MyCombatCharacterPointer() )
			{
				// Player is shooting an NPC. Adjust the damage! This protects breakables
				// and other 'non-living' entities from being easier/harder to break
				// in different skill levels.
				flDamage = pAmmoDef->PlrDamage( nAmmoType );
				flDamage = AdjustPlayerDamageInflicted( flDamage );
			}
		}

		return flDamage;
	}

   	//-----------------------------------------------------------------------------
  	//-----------------------------------------------------------------------------
 	bool CPortal::AllowDamage( CBaseEntity *pVictim, const CTakeDamageInfo &info )
  	{
#ifndef CLIENT_DLL
	if( (info.GetDamageType() & DMG_CRUSH) && info.GetInflictor() && pVictim->MyNPCPointer() )
	{
		if( pVictim->MyNPCPointer()->IsPlayerAlly() )
		{
			// A physics object has struck a player ally. Don't allow damage if it
			// came from the player's physcannon. 
			CBasePlayer *pPlayer = UTIL_PlayerByIndex(1);

#ifdef MAPBASE
			// Friendly fire needs to be handled here.
			if ( pPlayer && !pVictim->MyNPCPointer()->FriendlyFireEnabled() )
#else
			if( pPlayer )
#endif
			{
				CBaseEntity *pWeapon = pPlayer->HasNamedPlayerItem("weapon_physcannon");

				if( pWeapon )
				{
					CBaseCombatWeapon *pCannon = assert_cast <CBaseCombatWeapon*>(pWeapon);

					if( pCannon )
					{
						if( PhysCannonAccountableForObject(pCannon, info.GetInflictor() ) )
							return false;
					}
				}
			}
		}
	}
#endif
  		return true;
  	}

#endif //} !CLIENT_DLL


// ------------------------------------------------------------------------------------ //
// Shared CPortal implementation.
// ------------------------------------------------------------------------------------ //
bool CPortal::ShouldCollide( int collisionGroup0, int collisionGroup1 )
{
	// The smaller number is always first
	if ( collisionGroup0 > collisionGroup1 )
	{
		// swap so that lowest is always first
		int tmp = collisionGroup0;
		collisionGroup0 = collisionGroup1;
		collisionGroup1 = tmp;
	}
	
	// Prevent the player movement from colliding with spit globs (caused the player to jump on top of globs while in water)
	if ( collisionGroup0 == COLLISION_GROUP_PLAYER_MOVEMENT && collisionGroup1 == HL2COLLISION_GROUP_SPIT )
		return false;

	// HL2 treats movement and tracing against players the same, so just remap here
	if ( collisionGroup0 == COLLISION_GROUP_PLAYER_MOVEMENT )
	{
		collisionGroup0 = COLLISION_GROUP_PLAYER;
	}

	if( collisionGroup1 == COLLISION_GROUP_PLAYER_MOVEMENT )
	{
		collisionGroup1 = COLLISION_GROUP_PLAYER;
	}

	//If collisionGroup0 is not a player then NPC_ACTOR behaves just like an NPC.
	if ( collisionGroup1 == COLLISION_GROUP_NPC_ACTOR && collisionGroup0 != COLLISION_GROUP_PLAYER )
	{
		collisionGroup1 = COLLISION_GROUP_NPC;
	}

	// This is only for the super physcannon
	if ( m_bMegaPhysgun )
	{
		if ( collisionGroup0 == COLLISION_GROUP_INTERACTIVE_DEBRIS && collisionGroup1 == COLLISION_GROUP_PLAYER )
			return false;
	}

	//players don't collide against NPC Actors.
	//I could've done this up where I check if collisionGroup0 is NOT a player but I decided to just
	//do what the other checks are doing in this function for consistency sake.
	if ( collisionGroup1 == COLLISION_GROUP_NPC_ACTOR && collisionGroup0 == COLLISION_GROUP_PLAYER )
		return false;
		
	// In cases where NPCs are playing a script which causes them to interpenetrate while riding on another entity,
	// such as a train or elevator, you need to disable collisions between the actors so the mover can move them.
	if ( collisionGroup0 == COLLISION_GROUP_NPC_SCRIPTED && collisionGroup1 == COLLISION_GROUP_NPC_SCRIPTED )
		return false;

	return BaseClass::ShouldCollide( collisionGroup0, collisionGroup1 ); 
}

#ifndef CLIENT_DLL
//---------------------------------------------------------
//---------------------------------------------------------
void CPortal::AdjustPlayerDamageTaken( CTakeDamageInfo *pInfo )
{
	if( pInfo->GetDamageType() & (DMG_DROWN|DMG_CRUSH|DMG_FALL|DMG_POISON|DMG_SNIPER) )
	{
		// Skill level doesn't affect these types of damage.
		return;
	}

	switch( GetSkillLevel() )
	{
	case SKILL_EASY:
		pInfo->ScaleDamage( sk_dmg_take_scale1.GetFloat() );
		break;

	case SKILL_MEDIUM:
		pInfo->ScaleDamage( sk_dmg_take_scale2.GetFloat() );
		break;

	case SKILL_HARD:
		pInfo->ScaleDamage( sk_dmg_take_scale3.GetFloat() );
		break;
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
float CPortal::AdjustPlayerDamageInflicted( float damage )
{
	switch( GetSkillLevel() ) 
	{
	case SKILL_EASY:
		return damage * sk_dmg_inflict_scale1.GetFloat();
		break;

	case SKILL_MEDIUM:
		return damage * sk_dmg_inflict_scale2.GetFloat();
		break;

	case SKILL_HARD:
		return damage * sk_dmg_inflict_scale3.GetFloat();
		break;

	default:
		return damage;
		break;
	}
}
#endif//CLIENT_DLL

//---------------------------------------------------------
//---------------------------------------------------------
bool CPortal::ShouldUseRobustRadiusDamage(CBaseEntity *pEntity)
{
#ifdef CLIENT_DLL
	return false;
#endif

	if( !sv_robust_explosions.GetBool() )
		return false;

	if( !pEntity->IsNPC() )
	{
		// Only NPC's
		return false;
	}

#ifndef CLIENT_DLL
	CAI_BaseNPC *pNPC = pEntity->MyNPCPointer();
	if( pNPC->CapabilitiesGet() & bits_CAP_SIMPLE_RADIUS_DAMAGE )
	{
		// This NPC only eligible for simple radius damage.
		return false;
	}
#endif//CLIENT_DLL

	return true;
}

#ifndef CLIENT_DLL
//---------------------------------------------------------
//---------------------------------------------------------
bool CPortal::ShouldAutoAim( CBasePlayer *pPlayer, edict_t *target )
{
	return sk_allow_autoaim.GetBool() != 0;
}

//---------------------------------------------------------
//---------------------------------------------------------
float CPortal::GetAutoAimScale( CBasePlayer *pPlayer )
{
#ifdef _X360
	return 1.0f;
#else
	switch( GetSkillLevel() )
	{
	case SKILL_EASY:
		return sk_autoaim_scale1.GetFloat();

	case SKILL_MEDIUM:
		return sk_autoaim_scale2.GetFloat();

	default:
		return 0.0f;
	}
#endif
}

//---------------------------------------------------------
//---------------------------------------------------------
float CPortal::GetAmmoQuantityScale( int iAmmoIndex )
{
	switch( GetSkillLevel() )
	{
	case SKILL_EASY:
		return sk_ammo_qty_scale1.GetFloat();

	case SKILL_MEDIUM:
		return sk_ammo_qty_scale2.GetFloat();

	case SKILL_HARD:
		return sk_ammo_qty_scale3.GetFloat();

	default:
		return 0.0f;
	}
}

void CPortal::LevelInitPreEntity()
{
	// Remove this if you fix the bug in ep1 where the striders need to touch
	// triggers using their absbox instead of their bbox
#ifdef HL2_EPISODIC
	if ( !Q_strnicmp( gpGlobals->mapname.ToCStr(), "ep1_", 4 ) )
	{
		// episode 1 maps use the surrounding box trigger behavior
		CBaseEntity::sm_bAccurateTriggerBboxChecks = false;
	}
#endif
	BaseClass::LevelInitPreEntity();
}

//-----------------------------------------------------------------------------
// This takes the long way around to see if a prop should emit a DLIGHT when it
// ignites, to avoid having Alyx-related code in props.cpp.
//-----------------------------------------------------------------------------
bool CPortal::ShouldBurningPropsEmitLight()
{
	return true;
}

#ifdef MAPBASE
//-----------------------------------------------------------------------------
// Gets the default citizen type.
//-----------------------------------------------------------------------------
int CPortal::GetDefaultCitizenType()
{
	return m_DefaultCitizenType;
}

//-----------------------------------------------------------------------------
// Sets the default citizen type.
//-----------------------------------------------------------------------------
void CPortal::SetDefaultCitizenType(int val)
{
	m_DefaultCitizenType = val;
}

//-----------------------------------------------------------------------------
// Gets our global friendly fire override.
//-----------------------------------------------------------------------------
ThreeState_t CPortal::GlobalFriendlyFire()
{
	return GlobalEntity_IsInTable(FRIENDLY_FIRE_GLOBALNAME) ? TO_THREESTATE(GlobalEntity_GetState(FRIENDLY_FIRE_GLOBALNAME)) : TRS_NONE;
}

//-----------------------------------------------------------------------------
// Sets our global friendly fire override.
//-----------------------------------------------------------------------------
void CPortal::SetGlobalFriendlyFire(ThreeState_t val)
{
	GlobalEntity_Add(MAKE_STRING(FRIENDLY_FIRE_GLOBALNAME), gpGlobals->mapname, (GLOBALESTATE)val);
}

//-----------------------------------------------------------------------------
// Gets our SP respawn setting.
//-----------------------------------------------------------------------------
bool CPortal::AllowSPRespawn()
{
	return m_bAllowSPRespawn;
}

//-----------------------------------------------------------------------------
// Sets our SP respawn setting.
//-----------------------------------------------------------------------------
void CPortal::SetAllowSPRespawn( bool toggle )
{
	m_bAllowSPRespawn = toggle;
}

//BEGIN_SIMPLE_DATADESC( CPortal )
//
//	DEFINE_FIELD( m_DefaultCitizenType, FIELD_INTEGER ),
//	DEFINE_FIELD( m_bPlayerSquadAutosummonDisabled, FIELD_BOOLEAN ),
//
//END_DATADESC()
#endif


#endif//CLIENT_DLL

// ------------------------------------------------------------------------------------ //
// Global functions.
// ------------------------------------------------------------------------------------ //

#ifndef HL2MP
#ifndef PORTAL

// shared ammo definition
// JAY: Trying to make a more physical bullet response
#define BULLET_MASS_GRAINS_TO_LB(grains)	(0.002285*(grains)/16.0f)
#define BULLET_MASS_GRAINS_TO_KG(grains)	lbs2kg(BULLET_MASS_GRAINS_TO_LB(grains))

// exaggerate all of the forces, but use real numbers to keep them consistent
#define BULLET_IMPULSE_EXAGGERATION			3.5
// convert a velocity in ft/sec and a mass in grains to an impulse in kg in/s
#define BULLET_IMPULSE(grains, ftpersec)	((ftpersec)*12*BULLET_MASS_GRAINS_TO_KG(grains)*BULLET_IMPULSE_EXAGGERATION)


CAmmoDef *GetAmmoDef()
{
	static CAmmoDef def;
	static bool bInitted = false;
	
	if ( !bInitted )
	{
		bInitted = true;

		def.AddAmmoType("Gravity",			DMG_CLUB,					TRACER_NONE,			0,	0, 8, 0, 0 );
		def.AddAmmoType("Pistol", DMG_BULLET, TRACER_LINE_AND_WHIZ, "sk_plr_dmg_pistol", "sk_npc_dmg_pistol", "sk_max_pistol", BULLET_IMPULSE(200, 1225), 0);
	}

	return &def;
}

#endif
#endif
