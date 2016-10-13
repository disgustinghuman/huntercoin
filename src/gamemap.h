#ifndef GAMEMAP_H
#define GAMEMAP_H

#ifdef GUI
//#define TRADE_OUTPUT_HTML
#define RPG_OUTFIT_NPCS
#ifdef RPG_OUTFIT_NPCS
#define RPG_OUTFIT_ITEMS
//#define RPG_OUTFIT_DEBUG
#define ZHUNT_MAPOBJECTS
#include <string>
#endif
#endif

namespace Game
{

static const int MAP_WIDTH = 502;
static const int MAP_HEIGHT = 502;

static const int SPAWN_AREA_LENGTH = 15;
static const int NUM_HARVEST_AREAS = 18;
static const int NUM_CROWN_LOCATIONS = 416;

static const int CROWN_START_X = 250;
static const int CROWN_START_Y = 248;

#ifdef GUI
// Visual elements of the map
static const int MAP_LAYERS = 3;          // Map is layered for visual purposes

// better GUI -- more map tiles
static const int NUM_TILE_IDS = 503; // 235;      // Total number of different tile textures

// extern const short GameMap[MAP_LAYERS][MAP_HEIGHT][MAP_WIDTH];
#endif

// for FORK_TIMESAVE
extern unsigned char SpawnMap[MAP_HEIGHT][MAP_WIDTH];
#define SPAWNMAPFLAG_BANK 1
#define SPAWNMAPFLAG_PLAYER 2
#define CHARACTER_MODE_NORMAL 6
// difference of 2 means we can walk over (and along) the player spawn strip without logout
#define CHARACTER_MODE_LOGOUT 8
#define CHARACTER_MODE_SPECTATOR_BEGIN 9
#define CHARACTER_HAS_SPAWN_PROTECTION(S) (S<CHARACTER_MODE_NORMAL)
#define CHARACTER_IS_PROTECTED(S) ((S<CHARACTER_MODE_NORMAL)||(S>CHARACTER_MODE_LOGOUT))
#define CHARACTER_SPAWN_PROTECTION_ALMOST_FINISHED(S) (S==CHARACTER_MODE_NORMAL-1)
#define CHARACTER_IN_SPECTATOR_MODE(S) (S>CHARACTER_MODE_LOGOUT)
#define CHARACTER_NO_LOGOUT(S) ((S!=CHARACTER_MODE_LOGOUT)&&(S<CHARACTER_MODE_SPECTATOR_BEGIN+15))


extern const unsigned char ObstacleMap[MAP_HEIGHT][MAP_WIDTH];

// HarvestAreas[i] has size 2*HarvestAreaSizes[i] and contains alternating x,y coordinates
extern const int *HarvestAreas[NUM_HARVEST_AREAS];
extern const int HarvestAreaSizes[NUM_HARVEST_AREAS];

// Harvest amounts are subject to block reward halving
extern const int HarvestPortions[NUM_HARVEST_AREAS];  // Harvest amounts in cents
static const int TOTAL_HARVEST = 900;                 // Total harvest in cents (includes CROWN_BONUS)
static const int CROWN_BONUS = 25;                    // Bonus for holding Crown of the Fortune in cents

// Locations where the crown can spawn when the crown holder enters spawn area (x,y pairs)
extern const int CrownSpawn[NUM_CROWN_LOCATIONS * 2];

inline bool IsInsideMap(int x, int y)
{
    return x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT;
}

inline bool IsWalkable(int x, int y)
{
    return ObstacleMap[y][x] == 0;
}

inline bool IsOriginalSpawnArea(int x, int y)
{
    return ((x == 0 || x == MAP_WIDTH - 1) && (y < SPAWN_AREA_LENGTH || y >= MAP_HEIGHT - SPAWN_AREA_LENGTH))
        || ((y == 0 || y == MAP_HEIGHT - 1) && (x < SPAWN_AREA_LENGTH || x >= MAP_WIDTH - SPAWN_AREA_LENGTH));
}

}


#ifdef GUI
#define SORTED_ORDER_BOOK_LINES 100
extern char Displaycache_book[SORTED_ORDER_BOOK_LINES][200];
extern long long Displaycache_book_sort1[SORTED_ORDER_BOOK_LINES];
extern long long Displaycache_book_sort2[SORTED_ORDER_BOOK_LINES];
extern long long Displaycache_book_sort3[SORTED_ORDER_BOOK_LINES];
extern bool Displaycache_book_done[SORTED_ORDER_BOOK_LINES];

// better GUI -- variables declaration
#define RPG_MAP_HEIGHT 512
#define RPG_MAP_WIDTH 542

// grabbing coins
//extern int AI_playermap[Game::MAP_HEIGHT][Game::MAP_WIDTH][Game::NUM_TEAM_COLORS];
extern long long AI_coinmap[RPG_MAP_HEIGHT][RPG_MAP_WIDTH];
extern long long AI_coinmap_copy[RPG_MAP_HEIGHT][RPG_MAP_WIDTH];

extern char AsciiArtMap[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];
extern int AsciiArtTileCount[RPG_MAP_HEIGHT + 4][RPG_MAP_WIDTH + 4];

#define SHADOW_LAYERS 3
#define SHADOW_EXTRALAYERS 1
#define SHADOW_SHAPES 17
extern int Displaycache_gamemapgood[RPG_MAP_HEIGHT][RPG_MAP_WIDTH];
extern int Displaycache_gamemap[RPG_MAP_HEIGHT][RPG_MAP_WIDTH][Game::MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS];
//extern bool Display_dbg_obstacle_marker;

#define RPG_EXTRA_TEAM_COLORS 33

#define RPG_ICON_EMPTY 276
#define RPG_ICON_HUC_BANDIT 411
#define RPG_ICON_HUC_BANDIT400 272

#define RPG_ICON_FIRE 269
#define RPG_ICON_WORD_RECALL 273

//#define RPG_TILE_GRASS_GREEN_DARK 263
//#define RPG_TILE_GRASS_GREEN_LITE 266
//#define RPG_TILE_GRASS_RED_DARK 259
//#define RPG_TILE_GRASS_RED_LITE 262

#define RPG_TILE_TPGLOW 277
#define RPG_TILE_TPGLOW_TINY 304
#define RPG_TILE_TPGLOW_SMALL 305

//#define TILE_IS_TERRAIN(T) (T<=68 ? 1 : ((T==92)||(T==93)||(T==177)||(T==178)||((T>=200)&&(T!=203)&&(T<=208))||((T>=213)&&(T==215))) ? 2 : 0)
#define TILE_IS_GRASS(T) ((T==259) || ((T>=262) && (T<=268)))

#define ASCIIART_IS_TREE(T) ((T=='B') || (T=='b') || (T=='C') || (T=='c'))
#define ASCIIART_IS_ROCK(T) ((T=='G') || (T=='g') || (T=='H') || (T=='h'))
#define ASCIIART_IS_CLIFFBASE(T) ((T=='[') || (T==']') || (T=='!') || (T=='|'))
#define ASCIIART_IS_BASETERRAIN(T) ((T=='0') || (T=='1') || (T=='.'))
#define ASCIIART_IS_WALKABLETERRAIN(T) ((T=='0') || (T=='.'))
#define ASCIIART_IS_CLIFFSIDE(T) ((T=='(') || (T=='{') || (T=='<') || (T==')') || (T=='}') || (T=='>'))
#define ASCIIART_IS_CLIFFSIDE_NEW(T) ((T=='(') || (T=='{') || (T=='<') || (T==')') || (T=='}') || (T=='>') || (T=='i') || (T=='I') || (T=='j') || (T=='J'))
#define ASCIIART_IS_CLIFFSAND(T) ((T==',') || (T==';') || (T==':'))
#define ASCIIART_IS_CLIFFTOP(T) ((T=='?') || (T=='_'))
#define ASCIIART_IS_COBBLESTONE(T) ((T=='o') || (T=='O') || (T=='q') || (T=='Q') || (T=='8'))
#endif

#ifdef RPG_OUTFIT_NPCS
// for the actual items
#define RPG_MINHEIGHT_OUTFIT(T) (T?319000:1129000)
#define RPG_NUM_OUTFITS 3
//extern std::string outfit_cache_name[RPG_NUM_OUTFITS];
extern bool outfit_cache[RPG_NUM_OUTFITS];
extern int rpg_spawnpoint_x[RPG_NUM_OUTFITS];
extern int rpg_spawnpoint_y[RPG_NUM_OUTFITS];
// for NPCs
#define RPG_NUM_NPCS 6
#define RPG_PATH_LEN 12
extern std::string rpg_npc_name[RPG_NUM_NPCS];
extern int rpg_interval[RPG_NUM_NPCS];
extern int rpg_interval_tnet[RPG_NUM_NPCS];
extern int rpg_timeshift[RPG_NUM_NPCS];
extern int rpg_timeshift_tnet[RPG_NUM_NPCS];
extern int rpg_finished[RPG_NUM_NPCS];
extern int rpg_finished_tnet[RPG_NUM_NPCS];
extern int rpg_sprite[RPG_NUM_NPCS];
extern int rpg_path_x[RPG_NUM_NPCS][RPG_PATH_LEN];
extern int rpg_path_y[RPG_NUM_NPCS][RPG_PATH_LEN];
extern int rpg_path_d[RPG_NUM_NPCS][RPG_PATH_LEN];
#endif

#ifdef ZHUNT_MAPOBJECTS
#define ZHUNT_GEM_SPOINT_X 433
#define ZHUNT_GEM_SPOINT_Y 228
#define ZHUNT_GEM_SPOINT2_X 436
#define ZHUNT_GEM_SPOINT2_Y 262
#define ZHUNT_NUM_SPAWN 3
#define ZHUNT_NUM_TP 6
extern int zhunt_spawn_x[ZHUNT_NUM_SPAWN];
extern int zhunt_spawn_y[ZHUNT_NUM_SPAWN];
extern int zhunt_tp_x[ZHUNT_NUM_TP];
extern int zhunt_tp_y[ZHUNT_NUM_TP];
extern int zhunt_tp_exit_x[ZHUNT_NUM_TP];
extern int zhunt_tp_exit_y[ZHUNT_NUM_TP];
extern int zhunt_distancemap[Game::MAP_HEIGHT][Game::MAP_WIDTH];
extern int zhunt_playermap[Game::MAP_HEIGHT][Game::MAP_WIDTH];

#define ZHUNT_MAX_ATTACK_RANGE 9
#define ZHUNT_MAX_SCOUT_RANGE 9
#define ZHUNT_TELEPORTER_RANGE 12
#define ZHUNT_DIBS_RANGE 5
#define CREATURE_PREDATOR 4
#define CREATURE_SCOUT 3
#define ZHUNT_STATE2_OUTOFTIME 128
#define ZHUNT_STATE2_NOWCOLD 64
#define ZHUNT_STATE2_TOOHOT 32
#define ZHUNT_STATE_ZAP 64
#define ZHUNT_STATE_HOT 32
#define ZHUNT_STATE_WRONGPLACE 16
#define ZHUNT_STATE_DIBS 8
#define ZHUNT_STATE_FIREBALL 4
#define ZHUNT_STATE_BLINK 2
#define ZHUNT_STATE_WAIT 1
#define ZHUNT_MAX_LIFETIME 10000
// right border is at (Game::MAP_WIDTH - 1)
#define ZHUNT_MAPBORDER_EAST 405
#define ZHUNT_MAPBORDER_NORTH 150
#define ZHUNT_MAPBORDER_SOUTH 350
#define ZHUNT_BASE_FEE 4000000
//#define ZHUNT_GEM_RESET(T,H) (((T)&&(H%100==0)) || ((!T)&&(H%300==0)))
#define ZHUNT_GEM_RESET(T,H) (((T)&&(H%100==0)) || ((!T)&&(H%1242==0)))
#endif

#endif
