/**
 * =============================================================================
 * MGE Stats
 * 
 * Players begin a session by joining the server. Duels against other players
 * get recorded in a Match struct and player weapon statistics are tracked in
 * hash tables, one for each player. Statistics are inserted into a database
 * after the match is over, but only if its duration exceeds a set limit. 
 *
 * A match is determined to have ended when a player switches class, disconnects 
 * or a death occurs in a duel against a new opponent. 
 
 * (C) 2024 MGE.ME.  All rights reserved.
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

#pragma semicolon 1

#include <sourcemod>
#include <sdkhooks>
#include <mgestats/common>
#include <mgestats/match>
#include <mgestats/timescaledb>
#include <mgestats/weapons>

#pragma newdecls required

int ClientToPlayerID[MAX_PLAYERS + 1] = {-1, ...};

#define PL_VERSION "1.0.3"

public Plugin myinfo =
{
    name = "MGE Stats",
    author = "bzdmn",
    description = "Gather player performance metrics and store them in PostgreSQL",
    version = PL_VERSION,
    url = "https://stats.mge.me"
};

public void OnPluginStart()
{
    StartPGSQL();

    DHookWeapons();

    HookEvent("player_death", Event_PlayerDeath, EventHookMode_Pre);
    HookEvent("player_team", Event_PlayerTeam, EventHookMode_Pre);

    RegConsoleCmd("joinclass", JoinClass_Command);
#if defined DEBUG
    RegConsoleCmd("weapons", Weapons_Command);
    RegConsoleCmd("clearweapons", WeaponsClear_Command);
#endif

    //TODO: on plugin reload, query all connected players for player_id
}

public void OnMapStart()
{
    for (int i = 0; i < MAX_MATCHES; i++) 
    {
        MatchEnd(i); 
    }

    for (int i = 0; i <= MAX_PLAYERS; i++) 
    {
        MatchIndex[i] = -1;
    }
}

public void OnClientPostAdminCheck(int client)
{
    char name[32], esc_name[2*32+1]; // escaping every character
    char auth[STEAMID_LEN];
    char query[256];

    GetClientAuthId(client, AuthId_Steam2, auth, sizeof(auth));
    GetClientName(client, name, sizeof(name));
    
    if (SQL_EscapeString(hDatabase, name, esc_name, sizeof(esc_name)))
    {
        // UPSERT_PLAYER is defined in timescaledb.inc
        FormatEx(query, sizeof(query), "SELECT UPSERT_PLAYER('%s', '%s')", auth, esc_name);
        hDatabase.Query(T_PlayerAuth, query, client);
    }
    else
    {
        LogError("SQL_EscapeString failed for %s", name);
    }
}

public void OnClientPutInServer(int client)
{
    SDKHook(client, SDKHook_WeaponSwitch, WeaponSwitch_Hook);
    SDKHook(client, SDKHook_OnTakeDamage, OnTakeDamage_Hook);
}

public void OnClientDisconnect(int client)
{
    SDKUnhook(client, SDKHook_WeaponSwitch, WeaponSwitch_Hook);
    SDKUnhook(client, SDKHook_OnTakeDamage, OnTakeDamage_Hook);

    if (MatchIndex[client] >= 0)
    {
        MatchEnd(MatchIndex[client]);
    }

    // Player sessions start with no active matches.
    MatchIndex[client] = -1;

    //TODO: Race condition in T_InsertMatch
    ClientToPlayerID[client] = -1;
}

/*  
 *  Track player score by deaths. Deaths to world (fall damage etc.)
 *  count towards the opponents score like in MGE mod. 
 *
 *  If the two players don't have the same match index, or if neither
 *  of them are in an active match (matchindex = -1), then consider 
 *  the first death of either one as the beginning of a new match.
 */
Action Event_PlayerDeath(Event ev, const char[] name, bool dontBroadcast)
{
    int killed = GetClientOfUserId(ev.GetInt("userid"));
    int killer = GetClientOfUserId(ev.GetInt("attacker"));

    if 
    (
        (killer != 0 && killer != killed) && 
        (ClientToPlayerID[killer] != -1 && ClientToPlayerID[killed] != -1)
    )
    {
        if 
        (
            (MatchIndex[killed] == -1 && MatchIndex[killer] == -1) ||
            (MatchIndex[killed] != MatchIndex[killer]) // redundant?
        )
        {
            MatchIndex[killed] = -2;
            MatchIndex[killer] = -2;

            MatchStart(killed, killer);

            ClearPlayerLoadout(killer);
            ClearPlayerLoadout(killed);
        }
    }

    int idx = MatchIndex[killed];

    if (idx >= 0)
    {
        if (Matches[idx].PlayerClient[0] == killed)
        {
            Matches[idx].Score[1] += 1;
        }
        else
        {
            Matches[idx].Score[0] += 1;
        }
    }

    return Plugin_Continue;
}

/*  
 *  Whenever a player types !remove, !add, !first, disconnects or
 *  they get removed from an arena because someone else joined it,
 *  they get moved to spectator/red/blue. This is a good opportunity
 *  to end a match that was ongoing before the player changed teams.
 */
Action Event_PlayerTeam(Event ev, const char[] name, bool dontBroadcast)
{
    int client = GetClientOfUserId(ev.GetInt("userid"));

    //TODO: changing teams might be causing players to time out
    // ex: when joining the server, unassigned->spectator
    // or when changing arenas with !add or !remove
    if (MatchIndex[client] >= 0)
    {
        MatchEnd(MatchIndex[client]);
    }
    
    return Plugin_Continue;
}

/*  
 *  When a player changes class, his active match should end. This
 *  is because we want to track matches between particular classes.
 */
Action JoinClass_Command(int client, int args)
{
    int idx = MatchIndex[client];

    if (idx >= 0)
    {
        MatchEnd(idx);  
    }
    
    return Plugin_Continue;
}

#if defined DEBUG
Action Weapons_Command(int client, int args)
{
    for (int i = 0; i < MAX_WEAPONS_TRACKED; i++)
    {
        ReplyToCommand(client, "Weapon: %d ItemDefIndex: %d Hits: %d Shots: %d Damage: %.2f", 
        Loadout[client].Weapon[i], Loadout[client].ItemDefIndex[i], Loadout[client].Hits[i],
        Loadout[client].Shots[i], Loadout[client].Damage[i]);
    }

    ReplyToCommand(client, "ActiveWeaponSlot: %d", Loadout[client].ActiveWeaponSlot);

    return Plugin_Continue;
}

Action WeaponsClear_Command(int client, int args)
{
    ClearPlayerLoadout(client);

    return Plugin_Continue;
}
#endif

void MatchEnd(int matchIdx)
{
    int TimeStart, TimeStop, client1, client2;
    
    if (matchIdx < 0 || !Matches[matchIdx].IsLocked)
    {
        return;
    }

    if (Matches[matchIdx].TimeStart > 0) 
    {
        TimeStart = Matches[matchIdx].TimeStart;
        TimeStop = GetTime();

        // These clients might not be valid if they left from a match
        // by disconnecting so do not use these for any SM functions
        client1 = Matches[matchIdx].PlayerClient[0];
        client2 = Matches[matchIdx].PlayerClient[1];

        // When match data is being processed and sent to the database,
        // the players should no longer be associated with this match.
        if (matchIdx == MatchIndex[client1])
        {
            MatchIndex[client1] = -1;
        }
        if (matchIdx == MatchIndex[client2])
        {
            MatchIndex[client2] = -1;
        }
    }
    else 
    {
        LogError("Match struct TimeStart was malformed");
        return;
    }

    if (TimeStop - TimeStart > MIN_MATCH_LENGTH)
    {
#if defined DEBUG
        LogMessage("MatchEnd %d", matchIdx);
#endif
        DataPack pack = new DataPack();
        char match_query[256];

        FormatEx
        (
            match_query, sizeof(match_query), 
            "INSERT INTO matches (match_id, start, stop)         \
            VALUES (DEFAULT, to_timestamp(%d), to_timestamp(%d)) \
            RETURNING match_id",
            TimeStart, TimeStop
        );

        pack.WriteCell(matchIdx); 

        // store weapon stats in case they get reset before
        // sourcemod has time to process them in the db query
         
        pack.WriteCell(ClientToPlayerID[client1]);
        for (int i = 0; i < MAX_WEAPONS_TRACKED; i++) 
        {
            pack.WriteCell(Loadout[client1].ItemDefIndex[i]);
            pack.WriteCell(Loadout[client1].Shots[i]);
            pack.WriteCell(Loadout[client1].Hits[i]);
            pack.WriteCell(Loadout[client1].Special[i]);
            pack.WriteFloat(Loadout[client1].Damage[i]);
        }

        pack.WriteCell(ClientToPlayerID[client2]);
        for (int i = 0; i < MAX_WEAPONS_TRACKED; i++) 
        {
            pack.WriteCell(Loadout[client2].ItemDefIndex[i]);
            pack.WriteCell(Loadout[client2].Shots[i]);
            pack.WriteCell(Loadout[client2].Hits[i]);
            pack.WriteCell(Loadout[client2].Special[i]);
            pack.WriteFloat(Loadout[client2].Damage[i]);
        }

        hDatabase.Query(T_InsertMatch, match_query, pack);
    }
    else
    {
        MatchUnlock(matchIdx);
    }
}

Action MatchEnd_Timer(Handle timer, any data)
{
    MatchEnd(data);
    return Plugin_Continue;
}

void T_InsertMatch(Database db, DBResultSet result, const char[] err, any data)
{
    DataPack pack = view_as<DataPack>(data);
    pack.Reset();

    int matchIdx = pack.ReadCell();
#if defined DEBUG
    LogMessage("T_InsertMatch matchIdx: %d", matchIdx);
#endif

    if (db == null || result == null || err[0] != '\0')
    {
        static int err_counter = 0;

        LogError("T_InsertMatch failure: %s\n err_counter: %d", err, err_counter);

        if (err_counter < MAX_UPDATE_ERRORS)
        {
            err_counter++;
            CreateTimer(UPDATE_RETRY_SECONDS, MatchEnd_Timer, matchIdx);
        }
        else
        {
            err_counter = 0;
            MatchUnlock(matchIdx);
        }
    }
    else if (result.FetchRow())
    {
        int match_id = result.FetchInt(0);
        int itemDefIdx, hits, shots, special;
        float damage;

        char participant_values[64];
        char participant_query[256] = 
            "INSERT INTO match_participants(match_id, player_id, class_id, score)   \
            VALUES";

        char weapon_values[128];
        char weapon_query[1024] = 
            "INSERT INTO weapons(match_id, player_id, weapon_id, \
            shots, hits, damage, special) VALUES";

        // Build the query.

        for (int i = 0; i <= 1; i++)
        {
            int client = Matches[matchIdx].PlayerClient[i];
            int player_id = pack.ReadCell();//ClientToPlayerID[client];
            int score = Matches[matchIdx].Score[i];
            char class_id = GetClassId(Matches[matchIdx].Class[i]);
            
            FormatEx
            (
                participant_values, sizeof(participant_values), 
                "(%d, %d, '%c', %d),",
                match_id, player_id, class_id, score
            );

            StrCat(participant_query, sizeof(participant_query), participant_values);

            for (int j = 0; j < MAX_WEAPONS_TRACKED; j++)
            {
                itemDefIdx = pack.ReadCell();//Loadout[client].ItemDefIndex[j];
                shots = pack.ReadCell();//Loadout[client].Shots[j]; // avoids melee weapons
                hits = pack.ReadCell();//Loadout[client].Hits[j];
                special = pack.ReadCell();//Loadout[client].Special[j];
                damage = pack.ReadFloat();//Loadout[client].Damage[j];

                if (shots > 0)
                {
                    FormatEx
                    (
                        weapon_values, sizeof(weapon_values),
                        "(%d, %d, %d, %d, %d, %.0f, %d),",
                        match_id, player_id, itemDefIdx,
                        shots, hits, damage, special
                    );
                
                    StrCat(weapon_query, sizeof(weapon_query), weapon_values);
                }
            }
        }

        // Remove trailing commas and insert all data in one transaction.
        
        participant_query[strlen(participant_query) - 1] = '\0';
        weapon_query[strlen(weapon_query) - 1] = '\0';

        Transaction ParticipantsWeapons = SQL_CreateTransaction();

        ParticipantsWeapons.AddQuery(participant_query);
        ParticipantsWeapons.AddQuery(weapon_query);

        hDatabase.Execute
        (
            ParticipantsWeapons, 
            T_ParticipantsWeapons_Success, 
            T_ParticipantsWeapons_Failure,
            matchIdx
        );
    }
    else
    {
        LogError("T_InsertMatch failed to fetch row");
    }
    
    delete pack;
}

void T_ParticipantsWeapons_Success
(
    Database db, any data, int numQueries, Handle[] results, any[] queryData
)
{
#if defined DEBUG
    LogMessage("T_ParticipantsWeapons_Success");
#endif
    int matchIdx = data;
    MatchUnlock(matchIdx);
}

void T_ParticipantsWeapons_Failure
(
    Database db, any data, int numQueries, const char[] err, int failIndex, any[] queryData
)
{
    LogError("T_ParticipantsWeapons_Failure query %d: %s", failIndex, err);

    // remove the match from matches table
    int matchIdx = data;
    MatchUnlock(matchIdx);
}

void T_PlayerAuth(Database db, DBResultSet result, const char[] err, any data)
{
    int client = view_as<int>(data);

    if (db == null || result == null || err[0] != '\0')
    {
        LogError("T_PlayerAuth: %s", err);
    }
    else
    {
        if (result.FetchRow())
        {
            ClientToPlayerID[client] = result.FetchInt(0);
#if defined DEBUG
            LogMessage("ClientAuth: player_id: %d", ClientToPlayerID[client]);
#endif
        }
        else
        {
            LogError("T_PlayerAuth could not FetchRow");
        }
    }
}

Action WeaponSwitch_Hook(int client, int weapon)
{
    if (Loadout[client].Weapon[0] == weapon)
    {
        Loadout[client].ActiveWeaponSlot = 0;
    }
    else if (Loadout[client].Weapon[1] == weapon)
    {
        Loadout[client].ActiveWeaponSlot = 1;
    }
    else
    {
        Loadout[client].ActiveWeaponSlot = 2;
    }

    return Plugin_Continue;
}

Action OnTakeDamage_Hook
(
    int victim, int& attacker, int& inflictor, float& damage, int& damagetype,
    int& weapon, float damageForce[3], float damagePosition[3]
)
{
    if (weapon > 0 && victim != attacker)
    {
        if (Loadout[attacker].Weapon[0] == weapon)
        {
            Loadout[attacker].Damage[0] += damage;
            Loadout[attacker].Hits[0]++;
        }
        else if (Loadout[attacker].Weapon[1] == weapon)
        {
            Loadout[attacker].Damage[1] += damage;
            Loadout[attacker].Hits[1]++;
        }
    }

    return Plugin_Continue;
}
