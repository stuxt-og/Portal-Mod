//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

//==================================================
// Definition for all AI interactions
//==================================================

#ifndef	AI_INTERACTIONS_H
#define	AI_INTERACTIONS_H

#ifdef _WIN32
#pragma once
#endif

//ScriptedTarget
extern int  g_interactionScriptedTarget;

//Floor turret
extern int	g_interactionTurretStillStanding;

// AI Interaction for being hit by a physics object
extern int  g_interactionHitByPlayerThrownPhysObj;

// Alerts vital allies when the player punts a large object (car)
extern int	g_interactionPlayerPuntedHeavyObject;

#endif	//AI_INTERACTIONS_H