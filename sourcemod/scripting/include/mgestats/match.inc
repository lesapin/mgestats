/**
 * =============================================================================
 * MGE Stats - Match
 * Start, keep track of and end match stats gathering for both players.
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

#if defined _mgestats_match_included
    #endinput
#endif
#define _mgestats_match_included

#include <tf2_stocks>
#include <mgestats/common>

/* 	Match struct for recording player performance
 * 	and other general information about the match.
 *
 * 	A file-global index points to the most recently
 *	started match in the Matches array.
 */
enum struct MatchStats
{
	bool IsLocked; // defaults to false
	
	char PlayerID1[STEAMID_LEN];
	char PlayerID2[STEAMID_LEN];
	
	TFClassType Class[2];

	int PlayerClient[2];
	int Score[2];
	int TimeStart;
} 

MatchStats Matches[MAX_MATCHES];

/* 	Track which match is active for each player. 
 * 	Two players should share the same index.
 */
int MatchIndex[MAX_PLAYERS + 1] = {-1, ...};

/*	A new match starts when a player deals damage to the other.
 *      
 *	Find an unlocked Match struct and initialize it for clients.
 *	If all the matches are locked, start a repeating timer and
 *	attempt to initialize the match again after MATCH_RETRY_SECONDS.
 */
void MatchStart(int player1, int player2)
{
#if defined DEBUG
	LogMessage("MatchStart %d %d", player1, player2);
#endif
	if (!MatchStart_Loop(player1, player2))
	{
		LogMessage("MatchStart no open Match structs");
	}
}

bool MatchStart_Loop(int player1, int player2)
{
	int idx = 0;

	while (idx < MAX_MATCHES)
	{
		if (!Matches[idx].IsLocked)
		{
#if defined DEBUG
			LogMessage("MatchStart_Loop success %d", idx);
#endif
			if (IsClientInGame(player1) && IsClientInGame(player2))
			{
				GetClientAuthId(player1, AuthId_Steam2, Matches[idx].PlayerID1, STEAMID_LEN);
				GetClientAuthId(player2, AuthId_Steam2, Matches[idx].PlayerID2, STEAMID_LEN);

				Matches[idx].Class[0] = TF2_GetPlayerClass(player1);
				Matches[idx].Class[1] = TF2_GetPlayerClass(player2);

				Matches[idx].PlayerClient[0] = player1;
				Matches[idx].PlayerClient[1] = player2;

				Matches[idx].TimeStart = GetTime();
				Matches[idx].IsLocked = true;

				MatchIndex[player1] = idx;
				MatchIndex[player2] = idx;
			
				return true;
			}
#if defined DEBUG
            LogMessage("MatchStart_Loop player not in game");
#endif
			return false;
		}

		idx++;
	}

	return false;
}

Action MatchStart_Timer(Handle timer, DataPack pack)
{
#if defined DEBUG
	LogMessage("MatchStart_Timer");
#endif
	pack.Reset();

	int player1 = GetClientOfUserId(pack.ReadCell());
	int player2 = GetClientOfUserId(pack.ReadCell());

	if 
	(	
		player1 > 0 && player2 > 0 &&
		//IsClientConnected(player1) && IsClientConnected(player2) &&
	    	MatchIndex[player1] < 0 && MatchIndex[player2] < 0
	)
	{
		// If both clients are still in-game and not in a
		// match, attempt to find an open match again. 
		if (!MatchStart_Loop(player1, player2))
		{
			return Plugin_Continue;
		}
	}

	return Plugin_Stop;
}

/*	Matches end when a player disconnects, changes class or
 *	a death occurs in a duel against a new opponent.
 *
 *	Statistics are sent to a database when a match ends. The
 *	Match struct should stay locked during this period. Call
 *	MatchEnd once we are sure that the insertion was successful
 *	so that all arrays get cleared and structs unlocked.
 */
void MatchUnlock(int matchIndex)
{
	if (Matches[matchIndex].IsLocked)
	{
#if defined DEBUG
		LogMessage("MatchUnlock %d", matchIndex);
#endif
		Matches[matchIndex].PlayerClient[0] = 0;
		Matches[matchIndex].PlayerClient[1] = 0;
		Matches[matchIndex].PlayerID1[0] = '\0';
		Matches[matchIndex].PlayerID2[0] = '\0';
		Matches[matchIndex].Score[0] = 0;
		Matches[matchIndex].Score[1] = 0;
		Matches[matchIndex].TimeStart = -1;
		Matches[matchIndex].IsLocked = false;
	}
}

void GetClassName(TFClassType class_t, char[] dest, int dest_sz)
{
	switch (class_t)
	{
		case TFClass_Scout: Format(dest, dest_sz, "Scout");
		case TFClass_Soldier: Format(dest, dest_sz, "Soldier");
		case TFClass_DemoMan: Format(dest, dest_sz, "Demoman");
		case TFClass_Sniper: Format(dest, dest_sz, "Sniper");
		case TFClass_Spy: Format(dest, dest_sz, "Spy");
		case TFClass_Pyro: Format(dest, dest_sz, "Pyro");
		case TFClass_Engineer: Format(dest, dest_sz, "Engineer");
		case TFClass_Medic: Format(dest, dest_sz, "Medic");
		case TFClass_Heavy: Format(dest, dest_sz, "Heavy");
		case TFClass_Unknown: Format(dest, dest_sz, "N/A");
	}
}

char GetClassId(TFClassType class_t)
{
	return view_as<char>(_:class_t);
}
