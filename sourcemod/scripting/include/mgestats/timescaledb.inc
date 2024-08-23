/**
 * =============================================================================
 * MGE Stats - TimescaleDB
 *
 * TimescaleDB is an extension for PostgreSQL databases. Match data inserted in
 * the database is treated as time series data that gets split into chunks.
 * Each chunk (table) should take about 25% of total system RAM, so adjust the
 * chunk_time_interval in Timescale accordingly.
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

#if defined _mgestats_timescaledb_included
    #endinput
#endif
#define _mgestats_timescaledb_included

#include <dbi>
#include <mgestats/common>

Database hDatabase = null;

void StartPGSQL()
{
    Database.Connect(DB_Connected, "mgestats");
}

public void DB_Connected(Database db, const char[] err, any data)
{
    if (db == null)
    {
        SetFailState("Database failure: %s", err);
    }
    else
    {
        LogMessage("Connection to DB was successful");
        hDatabase = db;

        char query[1024];

        FormatEx
        (
            /*  Players table holds information about each player. Read
             *  often but updated and inserted to seldomly. Additionally, 
             *  player_id is a quick reference to a particular steam_id.
             */
            query, sizeof(query),
            "CREATE TABLE IF NOT EXISTS players(                    \
                steam_id    CHAR(20)                            ,   \
                player_id   SERIAL                              ,   \
                name        CHAR(32)    NOT NULL                ,   \
                CONSTRAINT  unique_id   UNIQUE(player_id)       ,   \
                CONSTRAINT  player_pk   PRIMARY KEY(steam_id)       \
            )"      
        );

        db.Query(T_ErrorCheck, query);
        
        FormatEx
        (
            /*  The main table, matches, will list all unique matches that
             *  have taken place and the time interval in which they 
             *  happened. Multiple reads and inserts but no updates.
             */
            query, sizeof(query),
            "CREATE TABLE IF NOT EXISTS matches(                    \
                match_id    SERIAL                              ,   \
                start       TIMESTAMPTZ NOT NULL                ,   \
                stop        TIMESTAMPTZ NOT NULL                ,   \
                CONSTRAINT  time_order  CHECK (stop > start)    ,   \
                CONSTRAINT  match_pk    PRIMARY KEY(match_id)       \
            )"  
        );

        db.Query(T_ErrorCheck, query);
        
        FormatEx
        (
            /*  match_participants Stores information about the players
             *  of a match with match_id. Create one row in the table 
             *  for each player_id who played in the match. No updates.
             */
            query, sizeof(query),
            "CREATE TABLE IF NOT EXISTS match_participants(                     \
                match_id    INTEGER     REFERENCES matches(match_id)        ,   \
                player_id   INTEGER     REFERENCES players(player_id)       ,   \
                class_id    CHAR        NOT NULL                            ,   \
                score       SMALLINT    NOT NULL                            ,   \
                CONSTRAINT  participant_pk  PRIMARY KEY(match_id,player_id)     \
            )"  
        );

        db.Query(T_ErrorCheck, query);
        
        // Create a Timescale hypertable

        FormatEx
        (
            /*  Weapon index can be stored as a SMALLINT, since 1 < idx < 30759.
             *  Hits, special and shots are small enough to store with SMALLINT.
             *  A lot of inserts, but fewer reads and no updates.
             */
            query, sizeof(query),
            "CREATE TABLE IF NOT EXISTS weapons(                            \
                match_id    INTEGER     REFERENCES matches(match_id)    ,   \
                player_id   INTEGER     REFERENCES players(player_id)   ,   \
                weapon_id   SMALLINT    NOT NULL                        ,   \
                shots       SMALLINT                                    ,   \
                hits        SMALLINT                                    ,   \
                damage      INTEGER                                     ,   \
                special     SMALLINT                                        \
            )"  
        );

        db.Query(T_ErrorCheck, query);
    }
}

void T_ErrorCheck(Database db, DBResultSet res, const char[] err, any data)
{
    if (db == null || res == null || err[0] != '\0')
    {
        LogError("Query failed: %s", err);
    }
}
