#ifndef GAMESTATE_H
#define GAMESTATE_H

#ifndef Q_MOC_RUN
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#endif
#include "json/json_spirit_value.h"
#include "uint256.h"
#include "serialize.h"

#include <map>
#include <string>

// gems and storage
// uncomment this line if you dare
//#define PERMANENT_LUGGAGE

#ifdef PERMANENT_LUGGAGE
#define PERMANENT_LUGGAGE_OR_GUI
#define PERMANENT_LUGGAGE_AUCTION
#define AUX_STORAGE_VOTING
#define AUX_STORAGE_VERSION2
#define AUX_STORAGE_VERSION3
#define AUX_AUCTION_BOT
// CRD test
#define AUX_TICKFIX_DELETEME
//#define PERMANENT_LUGGAGE_LOG_PAYMENTS
//#define PERMANENT_LUGGAGE_TALLY
#endif
#ifdef GUI
#define PERMANENT_LUGGAGE_OR_GUI

// windows stability bug workaround
#ifdef WIN32
#define PMON_DEBUG_WIN32_GUI
#define PMON_DBGWIN_T012SEPARATE 4
#define PMON_DBGWIN_MORESLEEP 8
#define PMON_DBGWIN_LOG 16
void ThreadSocketHandler(void* parg);
#endif
#endif

namespace Game
{

static const int NUM_TEAM_COLORS = 4;
static const int MAX_WAYPOINTS = 100;                      // Maximum number of waypoints per character
static const int MAX_CHARACTERS_PER_PLAYER = 20;           // Maximum number of characters per player at the same time
static const int MAX_CHARACTERS_PER_PLAYER_TOTAL = 1000;   // Maximum number of characters per player in the lifetime

// Unique player name
typedef std::string PlayerID;

// Player name + character index
struct CharacterID
{
    PlayerID player;
    int index;
    
    CharacterID() : index(-1) { }
    CharacterID(const PlayerID &player_, int index_)
        : player(player_), index(index_)
    {
        if (index_ < 0)
            throw std::runtime_error("Bad character index");
    }

    std::string ToString() const;

    static CharacterID Parse(const std::string &s)
    {
        size_t pos = s.find('.');
        if (pos == std::string::npos)
            return CharacterID(s, 0);
        return CharacterID(s.substr(0, pos), atoi(s.substr(pos + 1).c_str()));
    }

    bool operator==(const CharacterID &that) const { return player == that.player && index == that.index; }
    bool operator!=(const CharacterID &that) const { return !(*this == that); }
    // Lexicographical comparison
    bool operator<(const CharacterID &that) const { return player < that.player || (player == that.player && index < that.index); }
    bool operator>(const CharacterID &that) const { return that < *this; }
    bool operator<=(const CharacterID &that) const { return !(*this > that); }
    bool operator>=(const CharacterID &that) const { return !(*this < that); }
};

class GameState;
class KilledByInfo;
class PlayerState;
class RandomGenerator;
class StepResult;

// Define STL types used for killed player identification later on.
typedef std::set<PlayerID> PlayerSet;
typedef std::multimap<PlayerID, KilledByInfo> KilledByMap;
typedef std::map<PlayerID, PlayerState> PlayerStateMap;

struct Coord
{
    int x, y;

    Coord() : x(0), y(0) { }
    Coord(int x_, int y_) : x(x_), y(y_) { }

    unsigned int GetSerializeSize(int = 0, int = VERSION) const
    {
        return sizeof(int) * 2;
    }

    template<typename Stream>
    void Serialize(Stream& s, int = 0, int = VERSION) const
    {
        WRITEDATA(s, x);
        WRITEDATA(s, y);
    }

    template<typename Stream>
    void Unserialize(Stream& s, int = 0, int = VERSION)
    {
        READDATA(s, x);
        READDATA(s, y);
    }

    bool operator==(const Coord &that) const { return x == that.x && y == that.y; }
    bool operator!=(const Coord &that) const { return !(*this == that); }
    // Lexicographical comparison
    bool operator<(const Coord &that) const { return y < that.y || (y == that.y && x < that.x); }
    bool operator>(const Coord &that) const { return that < *this; }
    bool operator<=(const Coord &that) const { return !(*this > that); }
    bool operator>=(const Coord &that) const { return !(*this < that); }
};

typedef std::vector<Coord> WaypointVector;

struct Move
{
    PlayerID player;

    // New amount of locked coins (equals name output of move tx).
    int64_t newLocked;

    // Updates to the player state
    boost::optional<std::string> message;
    boost::optional<std::string> address;
    boost::optional<std::string> addressLock;

#ifdef PERMANENT_LUGGAGE
    // gems and storage
    boost::optional<std::string> playernameaddress;
#endif

    /* For spawning moves.  */
    unsigned char color;

    std::map<int, WaypointVector> waypoints;
    std::set<int> destruct;

    Move ()
      : newLocked(-1), color(0xFF)
    {}

    std::string AddressOperationPermission(const GameState &state) const;

    bool IsSpawn() const { return color != 0xFF; }
    bool IsValid(const GameState &state) const;
    void ApplyCommon(GameState &state) const;
    void ApplySpawn(GameState &state, RandomGenerator &rnd) const;
    void ApplyWaypoints(GameState &state) const;
    bool IsAttack(const GameState &state, int character_index) const;
 
    // Move must be empty before Parse and cannot be reused after Parse
    bool Parse(const PlayerID &player, const std::string &json);

    // Returns true if move is initialized (i.e. was parsed successfully)
    operator bool() { return !player.empty(); }

    /**
     * Return the minimum required "game fee" for this move.  The block height
     * must be passed because it is used to decide about hardfork states.
     * @param nHeight Block height at which this move is.
     * @return Minimum required game fee payment.
     */
    int64_t MinimumGameFee (unsigned nHeight) const;
};

/**
 * A character on the map that stores information while processing attacks.
 * Keep track of all attackers, so that we can both construct the killing gametx
 * and also handle life-stealing.
 */
struct AttackableCharacter
{

  /** The character this represents.  */
  CharacterID chid;

  /** The character's colour.  */
  unsigned char color;

  /**
   * Amount of coins already drawn from the attacked character's life.
   * This is the value that can be redistributed to the attackers.
   */
  int64_t drawnLife;

  /** All attackers that hit it.  */
  std::set<CharacterID> attackers;

  /**
   * Perform an attack by the given character.  Its ID and state must
   * correspond to the same attacker.
   */
  void AttackBy (const CharacterID& attackChid, const PlayerState& pl);

  /**
   * Handle self-effect of destruct.  The game state's height is used
   * to determine whether or not this has an effect (before the life-steal
   * fork).
   */
  void AttackSelf (const GameState& state);

};

/**
 * Hold the map from tiles to attackable characters.  This is built lazily
 * when attacks are done, so that we can save the processing time if not.
 */
struct CharactersOnTiles
{

  /** The map type used.  */
  typedef std::multimap<Coord, AttackableCharacter> Map;

  /** The actual map.  */
  Map tiles;

  /** Whether it is already built.  */
  bool built;

  /**
   * Construct an empty object.
   */
  inline CharactersOnTiles ()
    : tiles(), built(false)
  {}

  /**
   * Build it from the game state if not yet built.
   * @param state The game state from which to extract characters.
   */
  void EnsureIsBuilt (const GameState& state);

  /**
   * Perform all attacks in the moves.
   * @param state The current game state to build it if necessary.
   * @param moves All moves in the step.
   */
  void ApplyAttacks (const GameState& state, const std::vector<Move>& moves);

  /**
   * Deduct life from attached characters.  This also handles killing
   * of those with too many attackers, including pre-life-steal.
   * @param state The game state, will be modified.
   * @param result The step result object to fill in.
   */
  void DrawLife (GameState& state, StepResult& result);

  /**
   * Remove mutual attacks from the attacker arrays.
   * @param state The state to look up players.
   */
  void DefendMutualAttacks (const GameState& state);

  /**
   * Give drawn life back to attackers.  If there are more attackers than
   * available coins, distribute randomly.
   * @param rnd The RNG to use.
   * @param state The state to update.
   */
  void DistributeDrawnLife (RandomGenerator& rnd, GameState& state) const;

};

// Do not use for user-provided coordinates, as abs can overflow on INT_MIN.
// Use for algorithmically-computed coordinates that guaranteedly lie within the game map.
inline int distLInf(const Coord &c1, const Coord &c2)
{
    return std::max(abs(c1.x - c2.x), abs(c1.y - c2.y));
}

#ifdef PERMANENT_LUGGAGE
// gems and storage
struct StorageVault
{
    int64_t nGems;
    int64_t nGemsLocked;
    int gemlockfinished;
    int vaultflags;
    int64_t feed_price;
    int64_t auction_ask_size;
    int64_t auction_ask_price;
    int64_t vote_raw_amount;
    int64_t vote_txid60bit;
    int64_t ex_vote_mm_limits;
    int feed_chronon;
    int auction_ask_chronon;
    unsigned char item_outfit;
    unsigned char gem_reserve10;

    // strings
    std::string huntername;
#ifdef AUX_STORAGE_VERSION2
    std::string vote_comment;
    std::string str_reserve2;

    // exchange
    unsigned char ex_order_flags;
    unsigned char ex_position_type;
    int64 ex_order_price_bid;
    int64 ex_order_price_ask;
    int64 ex_order_size_bid;
    int64 ex_order_size_ask;
    int64 ex_position_price;
    int64 ex_position_size;
    int64 ex_position_price2;
    int64 ex_position_size2;
    int64 ex_rollover_price; // not used
    int64 ex_trade_profitloss;
    int64 ex_trade_aux;
    int ex_order_chronon_bid;
    int ex_order_chronon_ask;
    int ex_reserve1;
    int ex_reserve2;

    // playground/rpg
    Coord ai_coord;
    Coord ai_from;
    unsigned char ai_dir;
    unsigned char ai_npc_role;
    unsigned char ai_poi;
    unsigned char ai_state;
    unsigned char ai_state2;
    unsigned char ai_chat;
    unsigned char ai_idle_time;
    unsigned char ai_mapitem_count;
    unsigned char ai_foe_count;
    unsigned char ai_foe_dist;
    unsigned char ai_fav_harvest_poi;
    unsigned char ai_reason;
    unsigned char rpg_slot_spell;
    unsigned char rpg_slot_cooldown;
    unsigned char rpg_slot_amulet;
    unsigned char rpg_slot_ring;
    unsigned char rpg_slot_armor;
    unsigned char rpg_reserve1;
    unsigned char rpg_reserve2;

    // multi purpose/reserve
    int64 aux_storage_s1;
    int64 aux_storage_s2;
    uint64 aux_storage_u1;
    uint64 aux_storage_u2;
#endif

    StorageVault()
        : nGems(0), nGemsLocked(0), gemlockfinished(0), vaultflags(0),
          feed_price(0),
          auction_ask_size(0),
          auction_ask_price(0),
          vote_raw_amount(0),
          vote_txid60bit(0),
          ex_vote_mm_limits(0),
          feed_chronon(0),
          auction_ask_chronon(0),
          item_outfit(0),
          gem_reserve10(0)
#ifdef AUX_STORAGE_VERSION2
        , ex_order_flags(0),
          ex_position_type(0),
          ex_order_price_bid(0),
          ex_order_price_ask(0),
          ex_order_size_bid(0),
          ex_order_size_ask(0),
          ex_position_price(0),
          ex_position_size(0),
          ex_position_price2(0),
          ex_position_size2(0),
          ex_rollover_price(0),
          ex_trade_profitloss(0),
          ex_trade_aux(0),
          ex_order_chronon_bid(0),
          ex_order_chronon_ask(0),
          ex_reserve1(0),
          ex_reserve2(0),

          ai_coord(0, 0),
          ai_from(0, 0),
          ai_dir(0),
          ai_npc_role(0),
          ai_poi(0),
          ai_state(0),
          ai_state2(0),
          ai_chat(0),
          ai_idle_time(0),
          ai_mapitem_count(0),
          ai_foe_count(0),
          ai_foe_dist(0),
          ai_fav_harvest_poi(0),
          ai_reason(0),
          rpg_slot_spell(0),
          rpg_slot_cooldown(0),
          rpg_slot_amulet(0),
          rpg_slot_ring(0),
          rpg_slot_armor(0),
          rpg_reserve1(0),
          rpg_reserve2(0),

          aux_storage_s1(0),
          aux_storage_s2(0),
          aux_storage_u1(0),
          aux_storage_u2(0)
#endif
    { }
    StorageVault(int64_t nGems_)
        : nGems(nGems_), nGemsLocked(0), gemlockfinished(0), vaultflags(0),
          feed_price(0),
          auction_ask_size(0),
          auction_ask_price(0),
          vote_raw_amount(0),
          vote_txid60bit(0),
          ex_vote_mm_limits(0),
          feed_chronon(0),
          auction_ask_chronon(0),
          item_outfit(0),
          gem_reserve10(0)
#ifdef AUX_STORAGE_VERSION2
        , ex_order_flags(0),
          ex_position_type(0),
          ex_order_price_bid(0),
          ex_order_price_ask(0),
          ex_order_size_bid(0),
          ex_order_size_ask(0),
          ex_position_price(0),
          ex_position_size(0),
          ex_position_price2(0),
          ex_position_size2(0),
          ex_rollover_price(0),
          ex_trade_profitloss(0),
          ex_trade_aux(0),
          ex_order_chronon_bid(0),
          ex_order_chronon_ask(0),
          ex_reserve1(0),
          ex_reserve2(0),

          ai_coord(0, 0),
          ai_from(0, 0),
          ai_dir(0),
          ai_npc_role(0),
          ai_poi(0),
          ai_state(0),
          ai_state2(0),
          ai_chat(0),
          ai_idle_time(0),
          ai_mapitem_count(0),
          ai_foe_count(0),
          ai_foe_dist(0),
          ai_fav_harvest_poi(0),
          ai_reason(0),
          rpg_slot_spell(0),
          rpg_slot_cooldown(0),
          rpg_slot_amulet(0),
          rpg_slot_ring(0),
          rpg_slot_armor(0),
          rpg_reserve1(0),
          rpg_reserve2(0),

          aux_storage_s1(0),
          aux_storage_s2(0),
          aux_storage_u1(0),
          aux_storage_u2(0)
#endif
    { }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(nGems);
        READWRITE(nGemsLocked);
        READWRITE(gemlockfinished);
        READWRITE(vaultflags);
        READWRITE(feed_price);
        READWRITE(auction_ask_size);
        READWRITE(auction_ask_price);
        READWRITE(vote_raw_amount);
        READWRITE(vote_txid60bit);
        READWRITE(ex_vote_mm_limits);
        READWRITE(feed_chronon);
        READWRITE(auction_ask_chronon);
        READWRITE(item_outfit);
        READWRITE(gem_reserve10);
        READWRITE(huntername);
#ifdef AUX_STORAGE_VERSION2
        READWRITE(vote_comment);
        READWRITE(str_reserve2);

        READWRITE(ex_order_flags);
        READWRITE(ex_position_type);
        READWRITE(ex_order_price_bid);
        READWRITE(ex_order_price_ask);
        READWRITE(ex_order_size_bid);
        READWRITE(ex_order_size_ask);
        READWRITE(ex_position_price);
        READWRITE(ex_position_size);
        READWRITE(ex_position_price2);
        READWRITE(ex_position_size2);
        READWRITE(ex_rollover_price);
        READWRITE(ex_trade_profitloss);
        READWRITE(ex_trade_aux);
        READWRITE(ex_order_chronon_bid);
        READWRITE(ex_order_chronon_ask);
        READWRITE(ex_reserve1);
        READWRITE(ex_reserve2);

        READWRITE(ai_coord);
        READWRITE(ai_from);
        READWRITE(ai_dir);
        READWRITE(ai_npc_role);
        READWRITE(ai_poi);
        READWRITE(ai_state);
        READWRITE(ai_state2);
        READWRITE(ai_chat);
        READWRITE(ai_idle_time);
        READWRITE(ai_mapitem_count);
        READWRITE(ai_foe_count);
        READWRITE(ai_foe_dist);
        READWRITE(ai_fav_harvest_poi);
        READWRITE(ai_reason);
        READWRITE(rpg_slot_spell);
        READWRITE(rpg_slot_cooldown);
        READWRITE(rpg_slot_amulet);
        READWRITE(rpg_slot_ring);
        READWRITE(rpg_slot_armor);
        READWRITE(rpg_reserve1);
        READWRITE(rpg_reserve2);

        READWRITE(aux_storage_s1);
        READWRITE(aux_storage_s2);
        READWRITE(aux_storage_u1);
        READWRITE(aux_storage_u2);
#endif
    )
};
#endif

struct LootInfo
{
    int64_t nAmount;
    // Time span over the which this loot accumulated
    // This is merely for informative purposes, plus to make
    // hash of the loot tx unique
    int firstBlock, lastBlock;

    LootInfo() : nAmount(0), firstBlock(-1), lastBlock(-1) { }
    LootInfo(int64_t nAmount_, int nHeight) : nAmount(nAmount_), firstBlock(nHeight), lastBlock(nHeight) { }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(nAmount);
        READWRITE(firstBlock);
        READWRITE(lastBlock);
    )
};

struct CollectedLootInfo : public LootInfo
{
    /* Time span over which the loot was collected.  If this is a
       player refund bounty, collectedFirstBlock = -1 and collectedLastBlock
       is set to the refunding block height.  */
    int collectedFirstBlock, collectedLastBlock;
    
    CollectedLootInfo() : LootInfo(), collectedFirstBlock(-1), collectedLastBlock(-1) { }

    void Collect(const LootInfo &loot, int nHeight)
    {
        assert (!IsRefund ());

        if (loot.nAmount <= 0)
            return;

        nAmount += loot.nAmount;

        if (firstBlock < 0 || loot.firstBlock < firstBlock)
            firstBlock = loot.firstBlock;
        if (loot.lastBlock > lastBlock)
            lastBlock = loot.lastBlock;

        if (collectedFirstBlock < 0)
            collectedFirstBlock = nHeight;
        collectedLastBlock = nHeight;
    }

    /* Set the loot info to a state that means "this is a player refunding tx".
       They are used to give back coins if a player is killed for staying in
       the spawn area, and encoded differently in the game transactions.
       The block height is present to make the resulting tx unique.  */
    inline void
    SetRefund (int64_t refundAmount, int nHeight)
    {
      assert (nAmount == 0);
      assert (collectedFirstBlock == -1 && collectedLastBlock == -1);
      nAmount = refundAmount;
      collectedLastBlock = nHeight;
    }

    /* Check if this is a player refund tx.  */
    inline bool
    IsRefund () const
    {
      return (nAmount > 0 && collectedFirstBlock == -1);
    }

    /* When this is a refund, return the refund block height.  */
    inline int
    GetRefundHeight () const
    {
      assert (IsRefund ());
      return collectedLastBlock;
    }

    IMPLEMENT_SERIALIZE
    (
        READWRITE(*(LootInfo*)this);
        READWRITE(collectedFirstBlock);
        READWRITE(collectedLastBlock);
        assert (!IsRefund ());
    )
};

struct CharacterState
{
    Coord coord;                        // Current coordinate
    unsigned char dir;                  // Direction of last move (for nice sprite orientation). Encoding: as on numeric keypad.
    Coord from;                         // Straight-line pathfinding for current waypoint
    WaypointVector waypoints;           // Waypoints (stored in reverse so removal of the first waypoint is fast)
    CollectedLootInfo loot;             // Loot collected by player but not banked yet
    unsigned char stay_in_spawn_area;   // Auto-kill players who stay in the spawn area too long
#ifdef PERMANENT_LUGGAGE
    int64 rpg_gems_in_purse;            // gems and storage
#ifdef AUX_STORAGE_VERSION2
    int64_t cs_reserve1;
    int64_t cs_reserve2;
    int cs_reserve3;
    int cs_reserve4;
    unsigned char cs_reserve5;
    unsigned char cs_reserve6;
#endif
#endif

    CharacterState ()
      : coord(0, 0), dir(0), from(0, 0),
        stay_in_spawn_area(0)
#ifdef PERMANENT_LUGGAGE
      , rpg_gems_in_purse(0)
#ifdef AUX_STORAGE_VERSION2
      , cs_reserve1(0),
      cs_reserve2(0),
      cs_reserve3(0),
      cs_reserve4(0),
      cs_reserve5(0),
      cs_reserve6(0)
#endif
#endif
    {}

    IMPLEMENT_SERIALIZE
    (
        /* Last version change is beyond the last version where the game db
           is fully reconstructed.  */
        assert (nVersion >= 1000900);

        READWRITE(coord);
        READWRITE(dir);
        READWRITE(from);
        READWRITE(waypoints);
        READWRITE(loot);
        READWRITE(stay_in_spawn_area);
#ifdef PERMANENT_LUGGAGE
        READWRITE(rpg_gems_in_purse);
#ifdef AUX_STORAGE_VERSION2
        READWRITE(cs_reserve1);
        READWRITE(cs_reserve2);
        READWRITE(cs_reserve3);
        READWRITE(cs_reserve4);
        READWRITE(cs_reserve5);
        READWRITE(cs_reserve6);
#endif
#endif
    )

    void Spawn(unsigned nHeight, int color, RandomGenerator &rnd);

    void StopMoving()
    {
        from = coord;
        waypoints.clear();
    }

    void MoveTowardsWaypoint();
    WaypointVector DumpPath(const WaypointVector *alternative_waypoints = NULL) const;

    /**
     * Calculate total length (in the same L-infinity sense that gives the
     * actual movement time) of the outstanding path.
     * @param altWP Optionally provide alternative waypoints (for queued moves).
     * @return Time necessary to finish current path in blocks.
     */
    unsigned TimeToDestination(const WaypointVector *altWP = NULL) const;

    /* Collect loot by this character.  This takes the carrying capacity
       into account and only collects until this limit is reached.  All
       loot amount that *remains* will be returned.  */
    int64_t CollectLoot (LootInfo newLoot, int nHeight, int64_t carryCap);

    json_spirit::Value ToJsonValue(bool has_crown) const;
};

struct PlayerState
{
    /* Colour represents player team.  */
    unsigned char color;

    /* Value locked in the general's name on the blockchain.  This is the
       initial cost plus all "game fees" paid in the mean time.  It is compared
       to the new output value given by a move tx in order to compute
       the game fee as difference.  In that sense, it is a "cache" for
       the prevout.  */
    int64_t lockedCoins;
    /* Actual value of the general in the game state.  */
    int64_t value;

    std::map<int, CharacterState> characters;   // Characters owned by the player (0 is the main character)
    int next_character_index;                   // Index of the next spawned character

    /* Number of blocks the player still lives if poisoned.  If it is 1,
       the player will be killed during the next game step.  -1 means
       that there is no poisoning yet.  It should never be 0.  */
    int remainingLife;

    std::string message;      // Last message, can be shown as speech bubble
    int message_block;        // Block number. Game visualizer can hide messages that are too old
    std::string address;      // Address for receiving rewards. Empty means receive to the name address
    std::string addressLock;  // "Admin" address for player - reward address field can only be changed, if player is transferred to addressLock

#ifdef PERMANENT_LUGGAGE
    // gems and storage
    std::string playernameaddress;
    int playerflags;
#ifdef AUX_STORAGE_VERSION2
    int64_t pl_reserve1;
    int64_t pl_reserve2;
    int pl_reserve3;
    int pl_reserve4;
    unsigned char pl_reserve5;
    unsigned char pl_reserve6;
    std::string pl_str_reserve1;
    std::string pl_str_reserve2;
#endif
#endif

    IMPLEMENT_SERIALIZE
    (
        /* Last version change is beyond the last version where the game db
           is fully reconstructed.  */
        assert (nVersion >= 1001100);

        READWRITE(color);
        READWRITE(characters);
        READWRITE(next_character_index);
        READWRITE(remainingLife);

        READWRITE(message);
        READWRITE(message_block);
        READWRITE(address);
        READWRITE(addressLock);

#ifdef PERMANENT_LUGGAGE
        READWRITE(playernameaddress);
        READWRITE(playerflags);
#ifdef AUX_STORAGE_VERSION2
        READWRITE(pl_reserve1);
        READWRITE(pl_reserve2);
        READWRITE(pl_reserve3);
        READWRITE(pl_reserve4);
        READWRITE(pl_reserve5);
        READWRITE(pl_reserve6);
        READWRITE(pl_str_reserve1);
        READWRITE(pl_str_reserve2);
#endif
#endif

        READWRITE(lockedCoins);
        if (nVersion < 1030000)
          {
            assert (fRead);
            const_cast<PlayerState*> (this)->value = lockedCoins;
          }
        else
          READWRITE(value);
    )

    PlayerState ()
      : color(0xFF), lockedCoins(0), value(-1),
        next_character_index(0), remainingLife(-1), message_block(0)
#ifdef PERMANENT_LUGGAGE
      , playerflags(0)
#ifdef AUX_STORAGE_VERSION2
      , pl_reserve1(0),
      pl_reserve2(0),
      pl_reserve3(0),
      pl_reserve4(0),
      pl_reserve5(0),
      pl_reserve6(0)
#endif
#endif
    {}

    void SpawnCharacter(unsigned nHeight, RandomGenerator &rnd);
    bool CanSpawnCharacter()
    {
        return characters.size() < MAX_CHARACTERS_PER_PLAYER && next_character_index < MAX_CHARACTERS_PER_PLAYER_TOTAL;
    }
    json_spirit::Value ToJsonValue(int crown_index, bool dead = false) const;
};

struct GameState
{
    GameState();

    // Player states
    PlayerStateMap players;

    // Last chat messages of dead players (only in the current block)
    // Minimum info is stored: color, message, message_block.
    // When converting to JSON, this array is concatenated with normal players.
    std::map<PlayerID, PlayerState> dead_players_chat;

#ifdef PERMANENT_LUGGAGE
    // gems and storage
    std::map<std::string, StorageVault> vault;
    Coord gemSpawnPos;
    int gemSpawnState;

    int64_t feed_nextexp_price;
    int64_t feed_prevexp_price;

    int64_t feed_reward_dividend;
    int64_t feed_reward_divisor;
    int64_t feed_reward_remaining;
    int64_t upgrade_test;
    int64_t liquidity_reward_remaining;
    int64_t auction_settle_price;
    int64_t auction_last_price;
    int64_t auction_last_chronon;
#ifdef AUX_STORAGE_VERSION2
#ifdef AUX_STORAGE_VERSION3
    int64_t gs_reserve31;
    int64_t gs_reserve32;
    int64_t gs_reserve33;
    int64_t gs_reserve34;
    int64_t crd_nextexp_price; // not used
#endif
    int64_t crd_last_price;
    int64_t crd_last_size;
    int64_t crd_prevexp_price;
    int64_t crd_mm_orderlimits;
    int crd_last_chronon;
    int gs_reserve6;
    unsigned char gs_reserve7;
    unsigned char gs_reserve8;
    uint64_t auction_settle_conservative; // value will not be correct before storage version 3
    uint64_t gs_reserve10;
    std::string gs_str_reserve1;
    std::string gs_str_reserve2;
#endif
#endif

    std::map<Coord, LootInfo> loot;
    std::set<Coord> hearts;

    /* Store banks together with their remaining life time.  */
    std::map<Coord, unsigned> banks;

    Coord crownPos;
    CharacterID crownHolder;

    /* Amount of coins in the "game fund" pool.  */
    int64_t gameFund;

    // Number of steps since the game start.
    // State with nHeight==i includes moves from i-th block
    // -1 = initial game state (before genesis block)
    // 0  = game state immediately after the genesis block
    int nHeight;

    /* Block height (as per nHeight) of the last state that had a disaster.
       I. e., for a game state where disaster has just happened,
       nHeight == nDisasterHeight.  It is -1 before the first disaster
       happens.  */
    int nDisasterHeight;

    // Hash of the last block, moves from which were included
    // into this game state. This is meta-information (i.e. used
    // mainly for managing game states rather than as part of game
    // state, though it can be used as a random seed)
    uint256 hashBlock;
    
    IMPLEMENT_SERIALIZE
    (
      /* Should be only ever written to disk.  */
      assert (nType & SER_DISK);

      /* This is the version at which we last do a full reconstruction
         of the game DB.  No need to support older versions here.  */
      assert (nVersion >= 1001100);

      READWRITE(players);
      READWRITE(dead_players_chat);
      READWRITE(loot);

#ifdef PERMANENT_LUGGAGE
      // gems and storage
      READWRITE(vault);
      READWRITE(gemSpawnPos);
      READWRITE(gemSpawnState);

      READWRITE(feed_nextexp_price);
      READWRITE(feed_prevexp_price);

      READWRITE(feed_reward_dividend);
      READWRITE(feed_reward_divisor);
      READWRITE(feed_reward_remaining);
      READWRITE(upgrade_test);
      READWRITE(liquidity_reward_remaining);
      READWRITE(auction_settle_price);
      READWRITE(auction_last_price);
      READWRITE(auction_last_chronon);
#ifdef AUX_STORAGE_VERSION2
#ifdef AUX_STORAGE_VERSION3
      READWRITE(gs_reserve31);
      READWRITE(gs_reserve32);
      READWRITE(gs_reserve33);
      READWRITE(gs_reserve34);
      READWRITE(crd_nextexp_price);
#endif
      READWRITE(crd_last_price);
      READWRITE(crd_last_size);
      READWRITE(crd_prevexp_price);
      READWRITE(crd_mm_orderlimits);
      READWRITE(crd_last_chronon);
      READWRITE(gs_reserve6);
      READWRITE(gs_reserve7);
      READWRITE(gs_reserve8);
      READWRITE(auction_settle_conservative);
      READWRITE(gs_reserve10);
      READWRITE(gs_str_reserve1);
      READWRITE(gs_str_reserve2);
#endif
#endif

      READWRITE(hearts);
      if (nVersion >= 1030000)
        READWRITE(banks);
      else
        {
          /* Simply clear the banks here.  UpdateVersion takes care of
             setting them to the correct values for old states.  */
          assert (fRead);
          const_cast<GameState*> (this)->banks.clear ();
        }
      READWRITE(crownPos);
      READWRITE(crownHolder.player);
      if (!crownHolder.player.empty())
        READWRITE(crownHolder.index);
      READWRITE(gameFund);

      READWRITE(nHeight);
      READWRITE(nDisasterHeight);
      READWRITE(hashBlock);
    )

    void UpdateVersion(int oldVersion);

    json_spirit::Value ToJsonValue() const;

    // Helper functions
    void AddLoot(Coord coord, int64_t nAmount);
    void DivideLootAmongPlayers();
    void CollectHearts(RandomGenerator &rnd);
    void UpdateCrownState(bool &respawn_crown);
    void CollectCrown(RandomGenerator &rnd, bool respawn_crown);
    void CrownBonus(int64_t nAmount);

    /**
     * Get the number of initial characters for players created in this
     * game state.  This was initially 3, and is changed in a hardfork
     * depending on the block height.
     * @return Number of initial characters to create (including general).
     */
    unsigned GetNumInitialCharacters () const;

    /**
     * Check if a given location is a banking spot.
     * @param c The coordinate to check.
     * @return True iff it is a banking spot.
     */
    bool IsBank (const Coord& c) const;

    /* Handle loot of a killed character.  Depending on the circumstances,
       it may be dropped (with or without miner tax), refunded in a bounty
       transaction or added to the game fund.  */
    void HandleKilledLoot (const PlayerID& pId, int chInd,
                           const KilledByInfo& info, StepResult& step);

    /* For a given list of killed players, kill all their characters
       and collect the tax amount.  The killed players are removed from
       the state's list of players.  */
    void FinaliseKills (StepResult& step);

    /* Check if a disaster should happen at the current state given
       the random numbers.  */
    bool CheckForDisaster (RandomGenerator& rng) const;

    /* Perform spawn deaths.  */
    void KillSpawnArea (StepResult& step);

    /* Apply poison disaster to the state.  */
    void ApplyDisaster (RandomGenerator& rng);
    /* Decrement poison life expectation and kill players whose has
       dropped to zero.  */
    void DecrementLife (StepResult& step);

    /* Special action at the life-steal fork height:  Remove all hearts
       on the map and kill all hearted players.  */
    void RemoveHeartedCharacters (StepResult& step);

    /* Update the banks randomly (eventually).  */
    void UpdateBanks (RandomGenerator& rng);

    /* Return total amount of coins on the map (in loot and hold by players,
       including also general values).  */
    int64_t GetCoinsOnMap () const;

};

struct StepData : boost::noncopyable
{
    int64_t nTreasureAmount;
    uint256 newHash;
    std::vector<Move> vMoves;
};

/* Encode data for a banked bounty.  This includes also the payment address
   as per the player state (may be empty if no explicit address is set), so
   that the reward-paying game tx can be constructed even if the player
   is no longer alive (e. g., killed by a disaster).  */
struct CollectedBounty
{

  CharacterID character;
  CollectedLootInfo loot;
  std::string address;

  inline CollectedBounty (const PlayerID& p, int cInd,
                          const CollectedLootInfo& l,
                          const std::string& addr)
    : character(p, cInd), loot(l), address(addr)
  {}

  /* Look up the player in the given game state and if it is still
     there, update the address from the game state.  */
  void UpdateAddress (const GameState& state);

};

/* Encode data about why or by whom a player was killed.  Possibilities
   are a player (also self-destruct), staying too long in spawn area and
   due to poisoning after a disaster.  The information is used to
   construct the game transactions.  */
struct KilledByInfo
{

  /* Actual reason for death.  Since this is also used for ordering of
     the killed-by infos, the order here is crucial and determines
     how the killed-by info will be represented in the constructed game tx.  */
  enum Reason
  {
    KILLED_DESTRUCT = 1, /* Killed by destruct / some player.  */
    KILLED_SPAWN,        /* Staying too long in spawn area.  */
    KILLED_POISON        /* Killed by poisoning.  */
  } reason;

  /* The killing character, if killed by destruct.  */
  CharacterID killer;

  inline KilledByInfo (Reason why)
    : reason(why)
  {
    assert (why != KILLED_DESTRUCT);
  }

  inline KilledByInfo (const CharacterID& ch)
    : reason(KILLED_DESTRUCT), killer(ch)
  {}

  /* See if this killing reason pays out miner tax or not.  */
  bool HasDeathTax () const;

  /* See if this killing should drop the coins.  Otherwise (e. g., for poison)
     the coins are added to the game fund.  */
  bool DropCoins (unsigned nHeight, const PlayerState& victim) const;

  /* See if this killing allows a refund of the general cost to the player.
     This depends on the height, since poison death refunds only after
     the life-steal fork.  */
  bool CanRefund (unsigned nHeight, const PlayerState& victim) const;

  /* Comparison necessary for STL containers.  */

  friend inline bool
  operator== (const KilledByInfo& a, const KilledByInfo& b)
  {
    if (a.reason != b.reason)
      return false;

    switch (a.reason)
      {
      case KILLED_DESTRUCT:
        return a.killer == b.killer;
      default:
        return true;
      }
  }

  friend inline bool
  operator< (const KilledByInfo& a, const KilledByInfo& b)
  {
    if (a.reason != b.reason)
      return (a.reason < b.reason);

    switch (a.reason)
      {
      case KILLED_DESTRUCT:
        return a.killer < b.killer;
      default:
        return false;
      }
  }

};

class StepResult
{

private:

    // The following arrays only contain killed players
    // (i.e. the main character)
    PlayerSet killedPlayers;
    KilledByMap killedBy;

public:

    std::vector<CollectedBounty> bounties;

    int64_t nTaxAmount;

    StepResult() : nTaxAmount(0) { }

    /* Insert information about a killed player.  */
    inline void
    KillPlayer (const PlayerID& victim, const KilledByInfo& killer)
    {
      killedBy.insert (std::make_pair (victim, killer));
      killedPlayers.insert (victim);
    }

    /* Read-only access to the killed player maps.  */

    inline const PlayerSet&
    GetKilledPlayers () const
    {
      return killedPlayers;
    }

    inline const KilledByMap&
    GetKilledBy () const
    {
      return killedBy;
    }

};

// All moves happen simultaneously, so this function must work identically
// for any ordering of the moves, except non-critical cases (e.g. finding
// an empty cell to spawn new player)
bool PerformStep(const GameState &inState, const StepData &stepData, GameState &outState, StepResult &stepResult);

}


#ifdef GUI
// pending tx monitor -- variables
#define PMONSTATE_STOPPED 1
#define PMONSTATE_SHUTDOWN 2
#define PMONSTATE_START 3
#define PMONSTATE_CONSOLE 0
#define PMONSTATE_RUN 4
extern bool pmon_noisy;
extern int pmon_out_of_wp_idx;
extern bool pmon_new_data;
extern int pmon_state;
extern int pmon_go;
extern int pmon_block_age;
extern int64 pmon_tick;
#define PMON_TX_MAX 1000
extern std::string pmon_tx_names[PMON_TX_MAX];
extern std::string pmon_tx_values[PMON_TX_MAX];
extern int pmon_tx_age[PMON_TX_MAX];
extern int pmon_tx_count;
extern std::string pmon_oldtick_tx_names[PMON_TX_MAX];
extern std::string pmon_oldtick_tx_values[PMON_TX_MAX];
extern int pmon_oldtick_tx_age[PMON_TX_MAX];
extern int pmon_oldtick_tx_count;
#define PMON_ALL_MAX 2000
extern std::string pmon_all_names[PMON_ALL_MAX];
extern int pmon_all_x[PMON_ALL_MAX];
extern int pmon_all_y[PMON_ALL_MAX];
extern int pmon_all_next_x[PMON_ALL_MAX];
extern int pmon_all_next_y[PMON_ALL_MAX];
extern int pmon_all_color[PMON_ALL_MAX];
extern int pmon_all_color[PMON_ALL_MAX];
extern int pmon_all_tx_age[PMON_ALL_MAX];
extern bool pmon_all_cache_isinmylist[PMON_ALL_MAX];
extern int pmon_all_count;
#define PMON_MY_MAX 30
extern std::string pmon_my_names[PMON_MY_MAX];
extern int pmon_my_alarm_dist[PMON_MY_MAX];
extern int pmon_my_foe_dist[PMON_MY_MAX];
extern int pmon_my_idx[PMON_MY_MAX];
extern int pmon_my_alarm_state[PMON_MY_MAX];
extern int pmon_my_foecontact_age[PMON_MY_MAX];
extern int pmon_my_idlecount[PMON_MY_MAX];
extern bool pmon_name_pending_start();
extern bool pmon_name_pending();
extern bool pmon_name_update(int my_idx, int x, int y);
#ifdef AUX_AUCTION_BOT
extern bool pmon_sendtoaddress(const std::string& strAddress, const int64 nAmount);
extern int pmon_config_auction_auto_stateicon; // auction bot
extern int64 pmon_config_auction_auto_price;
extern int64 pmon_config_auction_auto_size;
extern int64 pmon_config_auction_auto_coinmax;
extern int64 auction_auto_actual_amount;
extern int64 auction_auto_actual_price;
extern int64 auction_auto_actual_totalcoins;
extern std::string pmon_config_auction_auto_name;
#define RPG_ICON_ABSTATE_UNKNOWN 248 // never displayed, unknown if alive ("alive" is changed to this before client-side check of all hunters in gamemapview.cpp)
#define RPG_ICON_ABSTATE_WAITING_BLUE 455 // still alive, waiting for sell order that would match ours
#define RPG_ICON_ABSTATE_STOPPED_WHITE 458 // stopped, blockchain problem, or error
#define RPG_ICON_ABSTATE_STOPPED_RED 459 // stopped, hunter dead, or enemy nearby
#define RPG_ICON_ABSTATE_STOPPED_BLUE 460 // stopped, session limit reached
#define RPG_ICON_ABSTATE_GO_YELLOW 457 // go!
#define RPG_ICON_ABSTATE_WAIT2_GREEN 456 // chat msg sent
#define RPG_ICON_ABSTATE_DONE_GREEN 454 // done, coins sent
#endif

#define WHYVALIDATE_UNKNOWN 0
#define WHYVALIDATE_CONNECTBLOCK 1
extern int pmon_why_validate;

extern int pmon_need_bank_idx;
extern int pmon_my_bankstate[PMON_MY_MAX];
extern int pmon_my_bankdist[PMON_MY_MAX];
extern int pmon_my_bank_x[PMON_MY_MAX];
extern int pmon_my_bank_y[PMON_MY_MAX];

#define PMON_CONFIG_MAX 15
extern int pmon_config_loot_notice;
extern int pmon_config_bank_notice;
extern int pmon_config_zoom;
extern int pmon_config_warn_stalled;
extern int pmon_config_warn_disaster;
extern int pmon_config_afk_leave;
extern int pmon_config_defence;
extern int pmon_config_hold;
extern int pmon_config_confirm;
extern int pmon_config_vote_tally;

// windows stability bug workaround
#ifdef PMON_DEBUG_WIN32_GUI
extern int pmon_config_dbg_loops;
extern int pmon_config_dbg_sleep;
extern volatile int pmon_dbg_which_thread;
extern int pmon_dbg_waitcount_t0;
extern int pmon_dbg_waitcount_t1;
extern int pmon_dbg_waitcount_t2;
#endif
#endif

#ifdef PERMANENT_LUGGAGE_OR_GUI
// gems and storage
#define GEM_SPAWNED 1
#define GEM_HARVESTING 2
#define GEM_ININVENTORY 3
#define GEM_UNKNOWN_HUNTER 4
#define GEM_ALLOW_SPAWN(T,H) (((T)&&(H>315500)) || (H>1030000))
#define GEM_RESET_INTERVAL(T) (T?100:1242)
#define GEM_RESET(T,H) (((T)&&(H%100==0)) || ((!T)&&(H%1242==0)))
#define GEM_RESET_HOTFIX(T,H) ((!T) && ((H==1031000)||(H==1032330)||(H==1033660))) // match behavior of old bugged version
extern int gem_visualonly_state;
extern int gem_visualonly_x;
extern int gem_visualonly_y;
#define GEM_NUM_SPAWNPOINTS 2
extern int gem_spawnpoint_x[GEM_NUM_SPAWNPOINTS];
extern int gem_spawnpoint_y[GEM_NUM_SPAWNPOINTS];
extern std::string gem_cache_winner_name; // visualonly unless state was set to GEM_HARVESTING
extern int gem_log_height;
#endif

#ifdef PERMANENT_LUGGAGE
#define GEM_NORMAL_VALUE 102000000
#define GEM_ONETIME_STORAGE_FEE 2000000

extern std::string Huntermsg_cache_address;

#define PLAYER_SET_REWARDADDRESS 1
#define PLAYER_TRANSFERRED 2
#define PLAYER_FOUND_ITEM 4
#define PLAYER_BOUGHT_ITEM 8
#define PLAYER_DO_PURSE 15
//define PLAYER_RESERVED 16
#define PLAYER_TRANSFERRED1 32
#define PLAYER_TRANSFERRED2 64
#define PLAYER_TRANSFERRED3 128
#define PLAYER_SUSPEND (2|32|64|128)

#ifdef PERMANENT_LUGGAGE_AUCTION
#define AUCTION_BID_PRIORITY_TIMEOUT 15
#define AUCTION_MIN_SIZE 10000000
#define AUCTION_DUTCHAUCTION_INTERVAL 100
extern int64 auctioncache_bid_price;
extern int64 auctioncache_bid_size;
extern int auctioncache_bid_chronon;
extern std::string auctioncache_bid_name;
extern int64 auctioncache_bestask_price;
extern int64 auctioncache_bestask_size;
extern int auctioncache_bestask_chronon;
extern std::string auctioncache_bestask_key;

#define PAYMENTCACHE_MAX 1000
extern int paymentcache_idx;
extern uint256 paymentcache_instate_blockhash;
extern int64 paymentcache_amount[PAYMENTCACHE_MAX];
extern std::string paymentcache_vault_addr[PAYMENTCACHE_MAX];

#define AUX_MINHEIGHT_FEED(T) (T?317500:1090000)
#define AUX_EXPIRY_INTERVAL(T) (T?100:10000)
#define VAULTFLAG_FEED_REWARD 1
#define FEEDCACHE_NORMAL 1
#define FEEDCACHE_EXPIRY 2
extern int64 feedcache_volume_total;
extern int64 feedcache_volume_participation;
extern int64 feedcache_volume_bull;
extern int64 feedcache_volume_bear;
extern int64 feedcache_volume_neutral;
extern int64 feedcache_volume_reward;
extern int feedcache_status;
#endif

#ifdef AUX_STORAGE_VERSION2
// CRD test
#define AUX_COIN ((int64)100000000)
extern int64 tradecache_pricetick_up(int64 old);
extern int64 tradecache_pricetick_down(int64 old);

// market maker
#define AUX_MINHEIGHT_TRADE(T) (T?322700:1220000)
#define AUX_MINHEIGHT_MM_AI_UPGRADE(T) (T?323000:1240000)
#define AUX_MINHEIGHT_SETTLE(T) (T?325000:1260000)
// possible values for "B"id and "A"sk are 100000 (==COIN/1000), 101000, 102000, ..., 100000000000 (1000*COIN)
#define MM_ORDERLIMIT_PACK(I64,B,A) {I64=(B/1000)+(A*1000000);}
#define MM_ORDERLIMIT_UNPACK(I64,B,A) {B=(I64%1000000000)*1000;A=(I64/1000000000)*1000;}
#define MM_AI_TICK_INTERVAL 10
extern int64 mmlimitcache_volume_total;
extern int64 mmlimitcache_volume_participation;
extern int64 mmmaxbidcache_volume_bull;
extern int64 mmmaxbidcache_volume_bear;
extern int64 mmmaxbidcache_volume_neutral;
extern int64 mmminaskcache_volume_bull;
extern int64 mmminaskcache_volume_bear;
extern int64 mmminaskcache_volume_neutral;

#define TRADE_CRD_MIN_SIZE 100000000
#define ORDERFLAG_BID_ACTIVE 1
#define ORDERFLAG_ASK_ACTIVE 2
#define ORDERFLAG_BID_INVALID 4
#define ORDERFLAG_ASK_INVALID 8
#define ORDERFLAG_BID_EXECUTING 16
#define ORDERFLAG_ASK_EXECUTING 32
extern int64 tradecache_bestbid_price;
extern int64 tradecache_bestask_price;
extern int64 tradecache_bestbid_size;
extern int64 tradecache_bestbid_fullsize;
extern int64 tradecache_bestask_size;
extern int64 tradecache_bestask_fullsize;
extern int64 tradecache_crd_nextexp_mm_adjusted;
extern int tradecache_bestbid_chronon;
extern int tradecache_bestask_chronon;
extern bool tradecache_is_print;
#endif

#ifdef AUX_STORAGE_VOTING
#define AUX_MINHEIGHT_VOTING(T) (T?320000:1180000)
#define AUX_VOTING_INTERVAL(T) (T?1000:10000)
//#define AUX_VOTING_CLOSE(T) (T?975:9975)
#define AUX_VOTING_CLEANUP_PERIOD(T) (T?144:1440)
#define VOTINGCACHE_MAX 1000
extern int votingcache_idx;
extern uint256 votingcache_instate_blockhash;
extern int64 votingcache_amount[PAYMENTCACHE_MAX];
extern int64 votingcache_txid60bit[PAYMENTCACHE_MAX];
extern std::string votingcache_vault_addr[PAYMENTCACHE_MAX];
extern bool votingcache_vault_exists[PAYMENTCACHE_MAX];
#endif
#endif


#endif
