/**
 * =============================================================================
 * MGE Stats - Weapons
 * Track weapon stats like damage dealth and accuracy for each player weapon.
 *
 * (C) 2024 MGE.ME.  All rights reserved
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#if defined _mgestats_weapons_included
    #endinput
#endif
#define _mgestats_weapons_included

#include <dhooks>
#include <mgestats/common>

enum struct PlayerLoadout
{
	int Weapon[MAX_WEAPONS_TRACKED];
	int ItemDefIndex[MAX_WEAPONS_TRACKED];

	int Shots[MAX_WEAPONS_TRACKED];
	int Hits[MAX_WEAPONS_TRACKED];
	int Special[MAX_WEAPONS_TRACKED];

	float Damage[MAX_WEAPONS_TRACKED];

	int ActiveWeaponSlot;
}

PlayerLoadout Loadout[MAX_PLAYERS + 1];

void ClearPlayerLoadout(int client)
{
	for (int i = 0; i < MAX_WEAPONS_TRACKED; i++)
	{
		Loadout[client].Weapon[i] = 0;
		Loadout[client].ItemDefIndex[i] = 0;
		Loadout[client].Damage[i] = 0.0;
		Loadout[client].Shots[i] = 0;
		Loadout[client].Hits[i] = 0;
		Loadout[client].Special[i] = 0;
		Loadout[client].ActiveWeaponSlot = 0;
	}

	for (int i = 0; i < 3; i++)
	{
		int weapon = GetPlayerWeaponSlot(client, i);
		Loadout[client].Weapon[i] = weapon;
		Loadout[client].ItemDefIndex[i] = weapon > 0 ? 
			GetEntProp(weapon, Prop_Send, "m_iItemDefinitionIndex") : 0;
	}
}

void DHookWeapons()
{
	Handle hGameData = LoadGameConfigFile("mgestats.plugin");
	Handle hWeaponFired;
	Handle hWeaponChanged;

	// Detour Weapon Fired ==============

	if (!hGameData)
	{
		SetFailState("Could not load gamedata mgestats.plugin.txt");
	}

	hWeaponFired = DHookCreateDetour(Address_Null, CallConv_THISCALL,
		       ReturnType_CBaseEntity, ThisPointer_Address);

	if (!hWeaponFired) SetFailState("DHookCreateDetour failed");

	if (!DHookSetFromConf(hWeaponFired, hGameData, SDKConf_Signature, WEAPON_FIRED))
	{
		SetFailState("Could not load signature");
	}
	else
	{
		DHookAddParam(hWeaponFired, HookParamType_CBaseEntity);

		if (!DHookEnableDetour(hWeaponFired, false, WeaponFired_Detour))
		{
			SetFailState("Could not detour %s", WEAPON_FIRED);
		}
		else
		{
			LogMessage("%s --> detoured", WEAPON_FIRED);
		}
	}

	// Detour Weapon Changed =========

	hWeaponChanged = DHookCreateDetour(Address_Null, CallConv_THISCALL,
			 ReturnType_CBaseEntity, ThisPointer_CBaseEntity);

	if (!hWeaponChanged) SetFailState("hWeaponChanged detour failed");

	if (!DHookSetFromConf(hWeaponChanged, hGameData, SDKConf_Signature, LOADOUT_CHANGED))
	{
		SetFailState("Could not load hWeaponChanged signature");
	}

	DHookAddParam(hWeaponChanged, HookParamType_CharPtr);
	DHookAddParam(hWeaponChanged, HookParamType_Int);
	DHookAddParam(hWeaponChanged, HookParamType_ObjectPtr);

	if (!DHookEnableDetour(hWeaponChanged, false, LoadoutChanged_Detour))
	{
		SetFailState("Could not detour %s", LOADOUT_CHANGED);
	}
	else
	{
		LogMessage("%s --> detoured", LOADOUT_CHANGED);
	}

	delete hGameData;
	delete hWeaponFired;
	delete hWeaponChanged;
}

MRESReturn WeaponFired_Detour(Address pThis, Handle hRet, Handle hParams)
{
	int client = DHookGetParam(hParams, 1);
	Loadout[client].Shots[Loadout[client].ActiveWeaponSlot]++;

	return MRES_Ignored;
}

/*	Player changed their loadout through the inventory menu.
 *	This detour is good for telling when a player changed
 * 	something in their loadout, but does not clearly 
 *      distinguish if the player changed a cosmetic item or a weapon.
 */
MRESReturn LoadoutChanged_Detour(int pThis, Handle hRet, Handle hParams)
{
	// Process player weapons in the next frame because
	// item entities are not created at this point yet.
	RequestFrame(WeaponsChanged, pThis);

	return MRES_Ignored;
}

/*
 *	Check if the new weapon is already in the weapon store.
 *	Then swap the new weapon into the active slot and move
 *	the current weapon into the store. If there are no empty
 *	slots in the store, then overwrite the last slot.
 */
void WeaponsChanged(int client)
{
	Loadout[client].ActiveWeaponSlot = 0;

	for (int SlotFrom = 0; SlotFrom <= 2; SlotFrom++)
	{
		int weapon = GetPlayerWeaponSlot(client, SlotFrom);
		int itemDefIdx = weapon > 0 ? 
			GetEntProp(weapon, Prop_Send, "m_iItemDefinitionIndex") : 0;

		if (weapon != Loadout[client].Weapon[SlotFrom])
		{
			int SlotTo = MAX_WEAPONS_TRACKED - 1;

			// Check if the weapon exists in store already
			for (int i = 3; i < MAX_WEAPONS_TRACKED - 1; ++i)
			{
				if (Loadout[client].ItemDefIndex[i] == itemDefIdx)
				{
					SlotTo = i;
					break;
				}
				else if (Loadout[client].Weapon[i] == 0)
				{
					SlotTo = i;
				}
			}	

			WeaponsChanged_Swap(client, SlotFrom, SlotTo, weapon);
		}
	}
}

/*
 *	Swap the stats from loadout slot slotFrom to slotTo.
 */
void WeaponsChanged_Swap(int client, int slotFrom, int slotTo, int weapon)
{
	float Damage = Loadout[client].Damage[slotFrom];
	int Shots = Loadout[client].Shots[slotFrom];
	int Hits = Loadout[client].Hits[slotFrom];
	int Special = Loadout[client].Special[slotFrom];

	Loadout[client].Damage[slotFrom] = Loadout[client].Damage[slotTo];
	Loadout[client].Shots[slotFrom] = Loadout[client].Shots[slotTo];
	Loadout[client].Hits[slotFrom] = Loadout[client].Hits[slotTo];
	Loadout[client].Special[slotFrom] = Loadout[client].Special[slotTo];

	Loadout[client].Weapon[slotTo] = Loadout[client].Weapon[slotFrom];
	Loadout[client].ItemDefIndex[slotTo] = Loadout[client].ItemDefIndex[slotFrom];
	Loadout[client].Damage[slotTo] = Damage;
	Loadout[client].Shots[slotTo] = Shots;
	Loadout[client].Hits[slotTo] = Hits;
	Loadout[client].Special[slotTo] = Special;

	Loadout[client].Weapon[slotFrom] = weapon;
	Loadout[client].ItemDefIndex[slotFrom] = weapon > 0 ? 
				GetEntProp(weapon, Prop_Send, "m_iItemDefinitionIndex") : 0;
}
