//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef HL2_PLAYER_SHARED_H
#define HL2_PLAYER_SHARED_H
#ifdef _WIN32
#pragma once
#endif

// Shared header file for players
#if defined( CLIENT_DLL )
#define CPortal_Player C_BasePortalPlayer	//FIXME: Lovely naming job between server and client here...
#include <portal/c_base_portal_player.h>
#else
#include <portal/portal_player.h>
#endif

#endif // HL2_PLAYER_SHARED_H
