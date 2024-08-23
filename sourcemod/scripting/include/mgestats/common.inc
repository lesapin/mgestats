#if defined _mgestats_common_included
    #endinput
#endif
#define _mgestats_common_included

#define PL_VERSION "0.9.9"

#define MIN_MATCH_LENGTH 60
#define MAX_UPDATE_ERRORS 5
#define UPDATE_RETRY_SECONDS 10.0

#define MAX_PLAYERS 24
#define MAX_MATCHES 24 // CEIL(MAX_PLAYERS/2) <= X <= MAX_PLAYERS
#define STEAMID_LEN 32
#define MATCH_RETRY_SECONDS 10.0

#define MAX_WEAPONS_TRACKED 5 // 3 + 2
#define WEAPON_FIRED "CTFWeaponBaseGun::FireProjectile"
#define LOADOUT_CHANGED "CTFPlayer::GiveNamedItem"
