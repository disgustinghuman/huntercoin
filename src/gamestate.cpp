#include "gamestate.h"
#include "gamemap.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"
#include <boost/xpressive/xpressive_dynamic.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "headers.h"
#include "huntercoin.h"

#ifdef GUI
// pending tx monitor -- acoustic alarm
#include <QUrl>
#include <QDesktopServices>
#include <boost/filesystem.hpp>
#endif


using namespace Game;

json_spirit::Value ValueFromAmount(int64 amount);

/* Parameters that determine when a poison-disaster will happen.  The
   probability is 1/x at each block between min and max time.  */
static const unsigned PDISASTER_MIN_TIME = 1440;
static const unsigned PDISASTER_MAX_TIME = 12 * 1440;
static const unsigned PDISASTER_PROBABILITY = 10000;

/* Parameters about how long a poisoned player may still live.  */
static const unsigned POISON_MIN_LIFE = 1;
static const unsigned POISON_MAX_LIFE = 50;

/* Parameters for dynamic banks after the life-steal fork.  */
static const unsigned DYNBANKS_NUM_BANKS = 75;
static const unsigned DYNBANKS_MIN_LIFE = 25;
static const unsigned DYNBANKS_MAX_LIFE = 100;

namespace Game
{

inline bool IsOriginalSpawnArea(const Coord &c)
{
    return IsOriginalSpawnArea(c.x, c.y);
}

inline bool IsWalkable(const Coord &c)
{
    return IsWalkable(c.x, c.y);
}

/**
 * Keep a set of walkable tiles.  This is used for random selection of
 * one of them for spawning / dynamic bank purposes.  Note that it is
 * important how they are ordered (according to Coord::operator<) in order
 * to reach consensus on the game state.
 *
 * This is filled in from IsWalkable() whenever it is empty (on startup).  It
 * does not ever change.
 */
static std::vector<Coord> walkableTiles;

/* Calculate carrying capacity.  This is where it is basically defined.
   It depends on the block height (taking forks changing it into account)
   and possibly properties of the player.  Returns -1 if the capacity
   is unlimited.  */
static int64_t
GetCarryingCapacity (int nHeight, bool isGeneral, bool isCrownHolder)
{
  if (!ForkInEffect (FORK_CARRYINGCAP, nHeight) || isCrownHolder)
    return -1;

  if (ForkInEffect (FORK_LIFESTEAL, nHeight))
    return 100 * COIN;

  if (ForkInEffect (FORK_LESSHEARTS, nHeight))
    return 2000 * COIN;

  return (isGeneral ? 50 : 25) * COIN;
}

/* Return the minimum necessary amount of locked coins.  This replaces the
   old NAME_COIN_AMOUNT constant and makes it more dynamic, so that we can
   change it with hard forks.  */
static int64_t
GetNameCoinAmount (unsigned nHeight)
{
  if (ForkInEffect (FORK_LESSHEARTS, nHeight))
    return 200 * COIN;
  if (ForkInEffect (FORK_POISON, nHeight))
    return 10 * COIN;
  return COIN;
}

/* Get the destruct radius a hunter has at a certain block height.  This
   may depend on whether or not it is a general.  */
static int
GetDestructRadius (int nHeight, bool isGeneral)
{
  if (ForkInEffect (FORK_LESSHEARTS, nHeight))
    return 1;

  return isGeneral ? 2 : 1;
}

/* Get maximum allowed stay on a bank.  */
static int
MaxStayOnBank (int nHeight)
{
  if (ForkInEffect (FORK_LIFESTEAL, nHeight))
    return 2;

  /* Between those two forks, spawn death was disabled.  */
  if (ForkInEffect (FORK_CARRYINGCAP, nHeight)
        && !ForkInEffect (FORK_LESSHEARTS, nHeight))
    return -1;

  /* Return original value.  */
  return 30;
}

/* Check whether or not a heart should be dropped at the current height.  */
static bool
DropHeart (int nHeight)
{
  if (ForkInEffect (FORK_LIFESTEAL, nHeight))
    return false;

  const int heartEvery = (ForkInEffect (FORK_LESSHEARTS, nHeight) ? 500 : 10);
  return nHeight % heartEvery == 0;
} 

/* Ensure that walkableTiles is filled.  */
static void
FillWalkableTiles ()
{
  if (!walkableTiles.empty ())
    return;

  for (int x = 0; x < MAP_WIDTH; ++x)
    for (int y = 0; y < MAP_HEIGHT; ++y)
      if (IsWalkable (x, y))
        walkableTiles.push_back (Coord (x, y));

  /* Do not forget to sort in the order defined by operator<!  */
  std::sort (walkableTiles.begin (), walkableTiles.end ());

  assert (!walkableTiles.empty ());
}

} // namespace Game


// Random generator seeded with block hash
class Game::RandomGenerator
{
public:
    RandomGenerator(uint256 hashBlock)
        : state0(SerializeHash(hashBlock, SER_GETHASH, 0))
    {
        state = state0;
    }

    int GetIntRnd(int modulo)
    {
        // Advance generator state, if most bits of the current state were used
        if (state < MIN_STATE)
        {
            state0.setuint256(SerializeHash(state0, SER_GETHASH, 0));
            state = state0;
        }
        return state.DivideGetRemainder(modulo).getint();
    }

    /* Get an integer number in [a, b].  */
    int GetIntRnd (int a, int b)
    {
      assert (a <= b);
      const int mod = (b - a + 1);
      const int res = GetIntRnd (mod) + a;
      assert (res >= a && res <= b);
      return res;
    }

private:
    CBigNum state, state0;
    static const CBigNum MIN_STATE;
};

const CBigNum RandomGenerator::MIN_STATE = CBigNum().SetCompact(0x097FFFFFu);

/* ************************************************************************** */
/* KilledByInfo.  */

bool
KilledByInfo::HasDeathTax () const
{
  return reason != KILLED_SPAWN;
}

bool
KilledByInfo::DropCoins (unsigned nHeight, const PlayerState& victim) const
{
  if (!ForkInEffect (FORK_LESSHEARTS, nHeight))
    return true;

  /* If the player is poisoned, no dropping of coins.  Note that we have
     to allow ==0 here (despite what gamestate.h says), since that is the
     case precisely when we are killing the player right now due to poison.  */
  if (victim.remainingLife >= 0)
    return false;

  assert (victim.remainingLife == -1);
  return true;
}

bool
KilledByInfo::CanRefund (unsigned nHeight, const PlayerState& victim) const
{
  if (!ForkInEffect (FORK_LESSHEARTS, nHeight))
    return false;

  switch (reason)
    {
    case KILLED_SPAWN:

      /* Before life-steal fork, poisoned players were not refunded.  */
      if (!ForkInEffect (FORK_LIFESTEAL, nHeight) && victim.remainingLife >= 0)
        return false;

      return true;

    case KILLED_POISON:
      return ForkInEffect (FORK_LIFESTEAL, nHeight);

    default:
      return false;
    }

  assert (false);
}

/* ************************************************************************** */
/* Move.  */

static bool
ExtractField (json_spirit::Object& obj, const std::string field,
              json_spirit::Value& v)
{
    for (std::vector<json_spirit::Pair>::iterator i = obj.begin(); i != obj.end(); ++i)
    {
        if (i->name_ == field)
        {
            v = i->value_;
            obj.erase(i);
            return true;
        }
    }
    return false;
}

bool Move::IsValid(const GameState &state) const
{
  PlayerStateMap::const_iterator mi = state.players.find (player);

  /* Before the life-steal fork, check that the move does not contain
     destruct and waypoints together.  This needs the height for its
     decision, thus it is not done in Parse (as before).  */
  /* FIXME: Remove check once the fork is passed.  */
  if (!ForkInEffect (FORK_LIFESTEAL, state.nHeight + 1))
    for (std::map<int, WaypointVector>::const_iterator i = waypoints.begin ();
         i != waypoints.end (); ++i)
      if (destruct.count (i->first) > 0)
        return error ("%s: destruct and waypoints together", __func__);

  int64_t oldLocked;
  if (mi == state.players.end ())
    {
      if (!IsSpawn ())
        return false;
      oldLocked = 0;
    }
  else
    {
      if (IsSpawn ())
        return false;
      oldLocked = mi->second.lockedCoins;
    }

  assert (oldLocked >= 0 && newLocked >= 0);
  const int64_t gameFee = newLocked - oldLocked;
  const int64_t required = MinimumGameFee (state.nHeight + 1);
  assert (required >= 0);
  if (gameFee < required)
    return error ("%s: too little game fee attached, got %lld, required %lld",
                  __func__, gameFee, required);

  return true;
}

bool ParseWaypoints(json_spirit::Object &obj, std::vector<Coord> &result, bool &bWaypoints)
{
    using namespace json_spirit;

    bWaypoints = false;
    result.clear();
    Value v;
    if (!ExtractField(obj, "wp", v))
        return true;
    if (v.type() != array_type)
        return false;
    Array arr = v.get_array();
    if (arr.size() % 2)
        return false;
    int n = arr.size() / 2;
    if (n > MAX_WAYPOINTS)
        return false;
    result.resize(n);
    for (int i = 0; i < n; i++)
    {
        if (arr[2 * i].type() != int_type || arr[2 * i + 1].type() != int_type)
            return false;
        int x = arr[2 * i].get_int();
        int y = arr[2 * i + 1].get_int();
        if (!IsInsideMap(x, y))
            return false;
        // Waypoints are reversed for easier deletion of current waypoint from the end of the vector
        result[n - 1 - i] = Coord(x, y);
        if (i && result[n - 1 - i] == result[n - i])
            return false; // Forbid duplicates        
    }
    bWaypoints = true;
    return true;
}

bool ParseDestruct(json_spirit::Object &obj, bool &result)
{
    using namespace json_spirit;

    result = false;
    Value v;
    if (!ExtractField(obj, "destruct", v))
        return true;
    if (v.type() != bool_type)
        return false;
    result = v.get_bool();
    return true;
}

bool Move::Parse(const PlayerID &player, const std::string &json)
{
    using namespace json_spirit;

    if (!IsValidPlayerName(player))
        return false;
        
    Value v;
    if (!read_string(json, v) || v.type() != obj_type)
        return false;
    Object obj = v.get_obj();

    if (ExtractField(obj, "msg", v))
    {
        if (v.type() != str_type)
            return false;
        message = v.get_str();
    }
    if (ExtractField(obj, "address", v))
    {
        if (v.type() != str_type)
            return false;
        const std::string &addr = v.get_str();
        if (!addr.empty() && !IsValidBitcoinAddress(addr))
            return false;
        address = addr;
    }
    if (ExtractField(obj, "addressLock", v))
    {
        if (v.type() != str_type)
            return false;
        const std::string &addr = v.get_str();
        if (!addr.empty() && !IsValidBitcoinAddress(addr))
            return false;
        addressLock = addr;
    }

    if (ExtractField(obj, "color", v))
    {
        if (v.type() != int_type)
            return false;
        color = v.get_int();
        if (color >= NUM_TEAM_COLORS)
            return false;
        if (!obj.empty()) // Extra fields are not allowed in JSON string
            return false;
        this->player = player;
        return true;
    }

    std::set<int> character_indices;
    for (std::vector<json_spirit::Pair>::iterator it = obj.begin(); it != obj.end(); ++it)
    {
        int i = atoi(it->name_);
        if (i < 0 || strprintf("%d", i) != it->name_)
            return false;               // Number formatting must be strict
        if (character_indices.count(i))
            return false;               // Cannot contain duplicate character indices
        character_indices.insert(i);
        v = it->value_;
        if (v.type() != obj_type)
            return false;
        Object subobj = v.get_obj();
        bool bWaypoints = false;
        std::vector<Coord> wp;
        if (!ParseWaypoints(subobj, wp, bWaypoints))
            return false;
        bool bDestruct;
        if (!ParseDestruct(subobj, bDestruct))
            return false;

        if (bDestruct)
            destruct.insert(i);
        if (bWaypoints)
            waypoints.insert(std::make_pair(i, wp));

        if (!subobj.empty())      // Extra fields are not allowed in JSON string
            return false;
    }
        
    this->player = player;
    return true;
}

void Move::ApplyCommon(GameState &state) const
{
    std::map<PlayerID, PlayerState>::iterator mi = state.players.find(player);

    if (mi == state.players.end())
    {
        if (message)
        {
            PlayerState &pl = state.dead_players_chat[player];
            pl.message = *message;
            pl.message_block = state.nHeight;
        }
        return;
    }

    PlayerState &pl = mi->second;
    if (message)
    {
        pl.message = *message;
        pl.message_block = state.nHeight;
    }
    if (address)
    {
        pl.address = *address;

#ifdef PERMANENT_LUGGAGE
        // gems and storage -- schedule to process reward address change next block
        if (GEM_ALLOW_SPAWN(fTestNet, state.nHeight))
        {
          pl.playerflags |= PLAYER_SET_REWARDADDRESS;
          printf("luggage test: player %s set reward address to %s\n", mi->first.c_str(), pl.address.c_str());
        }
#endif

    }
    if (addressLock)
        pl.addressLock = *addressLock;

#ifdef PERMANENT_LUGGAGE
    if ((playernameaddress) && (*playernameaddress != pl.playernameaddress))
    {
        pl.playernameaddress = *playernameaddress;
        pl.playerflags |= PLAYER_TRANSFERRED;
        printf("luggage test: player %s transferred to %s\n", mi->first.c_str(), pl.playernameaddress.c_str());
    }
#endif
}

std::string Move::AddressOperationPermission(const GameState &state) const
{
    if (!address && !addressLock)
        return std::string();      // No address operation requested - allow

    std::map<PlayerID, PlayerState>::const_iterator mi = state.players.find(player);
    if (mi == state.players.end())
        return std::string();      // Spawn move - allow any address operation

    return mi->second.addressLock;
}

void
Move::ApplySpawn (GameState &state, RandomGenerator &rnd) const
{
  assert (state.players.count (player) == 0);

  PlayerState pl;
  assert (pl.next_character_index == 0);
  pl.color = color;

  /* This is a fresh player and name.  Set its value to the height's
     name coin amount and put the remainder in the game fee.  This prevents
     people from "overpaying" on purpose in order to get beefed-up players.
     This rule, however, is only active after the life-steal fork.  Before
     that, overpaying did, indeed, allow to set the hunter value
     arbitrarily high.  */
  if (ForkInEffect (FORK_LIFESTEAL, state.nHeight))
    {
      const int64_t coinAmount = GetNameCoinAmount (state.nHeight);
      assert (pl.lockedCoins == 0 && pl.value == -1);
      assert (newLocked >= coinAmount);
      pl.value = coinAmount;
      pl.lockedCoins = newLocked;
      state.gameFund += newLocked - coinAmount;
    }
  else
    {
      pl.value = newLocked;
      pl.lockedCoins = newLocked;
    }

  const unsigned limit = state.GetNumInitialCharacters ();
  for (unsigned i = 0; i < limit; i++)
    pl.SpawnCharacter (state.nHeight, rnd);

  state.players.insert (std::make_pair (player, pl));
}

void Move::ApplyWaypoints(GameState &state) const
{
    std::map<PlayerID, PlayerState>::iterator pl;
    pl = state.players.find (player);
    if (pl == state.players.end ())
      return;

    BOOST_FOREACH(const PAIRTYPE(int, std::vector<Coord>) &p, waypoints)
    {
        std::map<int, CharacterState>::iterator mi;
        mi = pl->second.characters.find(p.first);
        if (mi == pl->second.characters.end())
            continue;
        CharacterState &ch = mi->second;
        const std::vector<Coord> &wp = p.second;

        if (ch.waypoints.empty() || wp.empty() || ch.waypoints.back() != wp.back())
            ch.from = ch.coord;
        ch.waypoints = wp;
    }
}

int64_t
Move::MinimumGameFee (unsigned nHeight) const
{
  if (IsSpawn ())
    {
      const int64_t coinAmount = GetNameCoinAmount (nHeight);

      if (ForkInEffect (FORK_LIFESTEAL, nHeight))
        return coinAmount + 5 * COIN;

      return coinAmount;
    }

  if (!ForkInEffect (FORK_LIFESTEAL, nHeight))
    return 0;

  return 20 * COIN * destruct.size ();
}

std::string CharacterID::ToString() const
{
    if (!index)
        return player;
    return player + strprintf(".%d", int(index));
}

/* ************************************************************************** */
/* AttackableCharacter and CharactersOnTiles.  */

void
AttackableCharacter::AttackBy (const CharacterID& attackChid,
                               const PlayerState& pl)
{
  /* Do not attack same colour.  */
  if (color == pl.color)
    return;

  assert (attackers.count (attackChid) == 0);
  attackers.insert (attackChid);
}

void
AttackableCharacter::AttackSelf (const GameState& state)
{
  if (!ForkInEffect (FORK_LIFESTEAL, state.nHeight))
    {
      assert (attackers.count (chid) == 0);
      attackers.insert (chid);
    }
}

void
CharactersOnTiles::EnsureIsBuilt (const GameState& state)
{
  if (built)
    return;
  assert (tiles.empty ());

  BOOST_FOREACH (const PAIRTYPE(PlayerID, PlayerState)& p, state.players)
    BOOST_FOREACH (const PAIRTYPE(int, CharacterState)& pc, p.second.characters)
      {
        AttackableCharacter a;
        a.chid = CharacterID (p.first, pc.first);
        a.color = p.second.color;
        a.drawnLife = 0;

        tiles.insert (std::make_pair (pc.second.coord, a));
      }
  built = true;
}

void
CharactersOnTiles::ApplyAttacks (const GameState& state,
                                 const std::vector<Move>& moves)
{
  BOOST_FOREACH(const Move& m, moves)
    {
      if (m.destruct.empty ())
        continue;

      const PlayerStateMap::const_iterator miPl = state.players.find (m.player);
      assert (miPl != state.players.end ());
      const PlayerState& pl = miPl->second;
      BOOST_FOREACH(int i, m.destruct)
        {
          const std::map<int, CharacterState>::const_iterator miCh
            = pl.characters.find (i);
          if (miCh == pl.characters.end ())
            continue;
          const CharacterID chid(m.player, i);
          if (state.crownHolder == chid)
            continue;

          EnsureIsBuilt (state);

          const int radius = GetDestructRadius (state.nHeight, i == 0);
          const CharacterState& ch = miCh->second;
          const Coord& c = ch.coord;
          for (int y = c.y - radius; y <= c.y + radius; y++)
            for (int x = c.x - radius; x <= c.x + radius; x++)
              {
                const std::pair<Map::iterator, Map::iterator> iters
                  = tiles.equal_range (Coord (x, y));
                for (Map::iterator it = iters.first; it != iters.second; ++it)
                  {
                    AttackableCharacter& a = it->second;
                    if (a.chid == chid)
                      a.AttackSelf (state);
                    else
                      a.AttackBy (chid, pl);
                  }
              }
        }
    }
}

void
CharactersOnTiles::DrawLife (GameState& state, StepResult& result)
{
  if (!built)
    return;

  /* Find damage amount if we have life steal in effect.  */
  const bool lifeSteal = ForkInEffect (FORK_LIFESTEAL, state.nHeight);
  const int64_t damage = GetNameCoinAmount (state.nHeight);

  BOOST_FOREACH (PAIRTYPE(const Coord, AttackableCharacter)& tile, tiles)
    {
      AttackableCharacter& a = tile.second;
      if (a.attackers.empty ())
        continue;
      assert (a.drawnLife == 0);

      /* Find the player state of the attacked character.  */
      PlayerStateMap::iterator vit = state.players.find (a.chid.player);
      assert (vit != state.players.end ());
      PlayerState& victim = vit->second;

      /* In case of life steal, actually draw life.  The coins are not yet
         added to the attacker, but instead their total amount is saved
         for future redistribution.  */
      if (lifeSteal)
        {
          assert (a.chid.index == 0);

          int64_t fullDamage = damage * a.attackers.size ();
          if (fullDamage > victim.value)
            fullDamage = victim.value;

          victim.value -= fullDamage;
          a.drawnLife += fullDamage;

          /* If less than the minimum amount remains, als that is drawn
             and later added to the game fund.  */
          assert (victim.value >= 0);
          if (victim.value < damage)
            {
              a.drawnLife += victim.value;
              victim.value = 0;
            }
        }
      assert (victim.value >= 0);
      assert (a.drawnLife >= 0);

      /* If we have life steal and there is remaining health, let
         the player survive.  Note that it must have at least the minimum
         value.  If "split coins" are remaining, we still kill it.  */
      if (lifeSteal && victim.value != 0)
        {
          assert (victim.value >= damage);
          continue;
        }

      if (a.chid.index == 0)
        for (std::set<CharacterID>::const_iterator at = a.attackers.begin ();
             at != a.attackers.end (); ++at)
          {
            const KilledByInfo killer(*at);
            result.KillPlayer (a.chid.player, killer);
          }

      if (victim.characters.count (a.chid.index) > 0)
        {
          assert (a.attackers.begin () != a.attackers.end ());
          const KilledByInfo& info(*a.attackers.begin ());
          state.HandleKilledLoot (a.chid.player, a.chid.index, info, result);
          victim.characters.erase (a.chid.index);
        }
    }
}

void
CharactersOnTiles::DefendMutualAttacks (const GameState& state)
{
  if (!built)
    return;

  /* Build up a set of all (directed) attacks happening.  The pairs
     mean an attack (from, to).  This is then later used to determine
     mutual attacks, and remove them accordingly.

     One can probably do this in a more efficient way, but for now this
     is how it is implemented.  */

  typedef std::pair<CharacterID, CharacterID> Attack;
  std::set<Attack> attacks;
  BOOST_FOREACH (const PAIRTYPE(const Coord, AttackableCharacter)& tile, tiles)
    {
      const AttackableCharacter& a = tile.second;
      for (std::set<CharacterID>::const_iterator mi = a.attackers.begin ();
           mi != a.attackers.end (); ++mi)
        attacks.insert (std::make_pair (*mi, a.chid));
    }

  BOOST_FOREACH (PAIRTYPE(const Coord, AttackableCharacter)& tile, tiles)
    {
      AttackableCharacter& a = tile.second;

      std::set<CharacterID> notDefended;
      for (std::set<CharacterID>::const_iterator mi = a.attackers.begin ();
           mi != a.attackers.end (); ++mi)
        {
          const Attack counterAttack(a.chid, *mi);
          if (attacks.count (counterAttack) == 0)
            notDefended.insert (*mi);
        }

      a.attackers.swap (notDefended);
    }
}

void
CharactersOnTiles::DistributeDrawnLife (RandomGenerator& rnd,
                                        GameState& state) const
{
  if (!built)
    return;

  const int64_t damage = GetNameCoinAmount (state.nHeight);

  /* Life is already drawn.  It remains to distribute the drawn balances
     from each attacked character back to its attackers.  For this,
     we first find the still alive players and assemble them in a map.  */
  std::map<CharacterID, PlayerState*> alivePlayers;
  BOOST_FOREACH (const PAIRTYPE(const Coord, AttackableCharacter)& tile, tiles)
    {
      const AttackableCharacter& a = tile.second;
      assert (alivePlayers.count (a.chid) == 0);

      /* Only non-hearted characters should be around if this is called,
         since this means that life-steal is in effect.  */
      assert (a.chid.index == 0);

      const PlayerStateMap::iterator pit = state.players.find (a.chid.player);
      if (pit != state.players.end ())
        {
          PlayerState& pl = pit->second;
          assert (pl.characters.count (a.chid.index) > 0);
          alivePlayers.insert (std::make_pair (a.chid, &pl));
        }
    }

  /* Now go over all attacks and distribute life to the attackers.  */
  BOOST_FOREACH (const PAIRTYPE(const Coord, AttackableCharacter)& tile, tiles)
    {
      const AttackableCharacter& a = tile.second;
      if (a.attackers.empty () || a.drawnLife == 0)
        continue;

      /* Find attackers that are still alive.  We will randomly distribute
         coins to them later on.  */
      std::vector<CharacterID> alive;
      for (std::set<CharacterID>::const_iterator mi = a.attackers.begin ();
           mi != a.attackers.end (); ++mi)
        if (alivePlayers.count (*mi) > 0)
          alive.push_back (*mi);

      /* Distribute the drawn life randomly until either all is spent
         or all alive attackers have gotten some.  */
      int64_t toSpend = a.drawnLife;
      while (!alive.empty () && toSpend >= damage)
        {
          const unsigned ind = rnd.GetIntRnd (alive.size ());
          const std::map<CharacterID, PlayerState*>::iterator plIt
            = alivePlayers.find (alive[ind]);
          assert (plIt != alivePlayers.end ());

          toSpend -= damage;
          plIt->second->value += damage;

          /* Do not use a silly trick like swapping in the last element.
             We want to keep the array ordered at all times.  The order is
             important with respect to consensus, and this makes the consensus
             protocol "clearer" to describe.  */
          alive.erase (alive.begin () + ind);
        }

      /* Distribute the remaining value to the game fund.  */
      assert (toSpend >= 0);
      state.gameFund += toSpend;
    }
}

/* ************************************************************************** */
/* CharacterState and PlayerState.  */

void
CharacterState::Spawn (unsigned nHeight, int color, RandomGenerator &rnd)
{
  /* Pick a random walkable spawn location after the life-steal fork.  */
  if (ForkInEffect (FORK_LIFESTEAL, nHeight))
    {
      FillWalkableTiles ();

      const int pos = rnd.GetIntRnd (walkableTiles.size ());
      coord = walkableTiles[pos];

      dir = rnd.GetIntRnd (1, 8);
      if (dir >= 5)
        ++dir;
      assert (dir >= 1 && dir <= 9 && dir != 5);
    }

  /* Use old logic with fixed spawns in the corners before the fork.  */
  else
    {
      const int pos = rnd.GetIntRnd(2 * SPAWN_AREA_LENGTH - 1);
      const int x = pos < SPAWN_AREA_LENGTH ? pos : 0;
      const int y = pos < SPAWN_AREA_LENGTH ? 0 : pos - SPAWN_AREA_LENGTH;
      switch (color)
        {
        case 0: // Yellow (top-left)
          coord = Coord(x, y);
          break;
        case 1: // Red (top-right)
          coord = Coord(MAP_WIDTH - 1 - x, y);
          break;
        case 2: // Green (bottom-right)
          coord = Coord(MAP_WIDTH - 1 - x, MAP_HEIGHT - 1 - y);
          break;
        case 3: // Blue (bottom-left)
          coord = Coord(x, MAP_HEIGHT - 1 - y);
          break;
        default:
          throw std::runtime_error("CharacterState::Spawn: incorrect color");
        }

      // Set look-direction for the sprite
      if (coord.x == 0)
        {
          if (coord.y == 0)
            dir = 3;
          else if (coord.y == MAP_HEIGHT - 1)
            dir = 9;
          else
            dir = 6;
        }
      else if (coord.x == MAP_WIDTH - 1)
        {
          if (coord.y == 0)
            dir = 1;
          else if (coord.y == MAP_HEIGHT - 1)
            dir = 7;
          else
            dir = 4;
        }
      else if (coord.y == 0)
        dir = 2;
      else if (coord.y == MAP_HEIGHT - 1)
        dir = 8;
    }

  StopMoving();
}

// Returns direction from c1 to c2 as a number from 1 to 9 (as on the numeric keypad)
static unsigned char
GetDirection (const Coord& c1, const Coord& c2)
{
    int dx = c2.x - c1.x;
    int dy = c2.y - c1.y;
    if (dx < -1)
        dx = -1;
    else if (dx > 1)
        dx = 1;
    if (dy < -1)
        dy = -1;
    else if (dy > 1)
        dy = 1;

    return (1 - dy) * 3 + dx + 2;
}


// gems and storage
#ifdef PERMANENT_LUGGAGE
std::string Huntermsg_cache_address;

#ifdef RPG_OUTFIT_NPCS
// for the actual items
std::string outfit_cache_name[RPG_NUM_OUTFITS];
bool outfit_cache[RPG_NUM_OUTFITS];
int rpg_spawnpoint_x[RPG_NUM_OUTFITS] = {-1, -1, -1};
int rpg_spawnpoint_y[RPG_NUM_OUTFITS] = {-1, -1, -1};
// for NPCs
std::string rpg_npc_name[RPG_NUM_NPCS] = {"Caran'zara",
                                          "Na'axilan",
                                          "Zeab'batsu",
                                          "Kas'shii",
                                          "Or'lo",
                                          "Tia'tha"};
int rpg_interval[RPG_NUM_NPCS] =      {1000, 1000, 1000, 1100, 2200, 1242};
int rpg_interval_tnet[RPG_NUM_NPCS] = { 100,  100,  100,  100,  200,  100};
int rpg_timeshift[RPG_NUM_NPCS] =     {-400, -700,    0, -115, -117,    0};
int rpg_timeshift_tnet[RPG_NUM_NPCS] = {-40,  -70,    0,  -15,  -17,    0};
int rpg_finished[RPG_NUM_NPCS] =      { 100,  100,  100,   12,   12,   50};
int rpg_finished_tnet[RPG_NUM_NPCS] = {  25,   25,   25,   12,   12,   50};

int rpg_sprite[RPG_NUM_NPCS] =        {  27,   16,   18,   10,   11,    5};

int rpg_path_x[RPG_NUM_NPCS][RPG_PATH_LEN] = {{143, 143, 143, 143, 142, 141, 140, 139, 115, 115, 115, 115},
                                              {143, 143, 143, 143, 142, 141, 140, 139, 115, 115, 115, 115},
                                              {140, 140, 140, 140, 140, 140, 140, 139, 115, 115, 115, 115},
                                              {365, 229, 230, 231, 232, 233, 234, 234, 233, 232, 231, 230},
                                              {473, 229, 230, 230, 230, 230, 230, 230, 230, 230, 231, 230},
                                              {129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 139}};
int rpg_path_y[RPG_NUM_NPCS][RPG_PATH_LEN] = {{481, 481, 481, 481, 481, 481, 481, 481, 483, 482, 481, 480},
                                              {484, 484, 484, 484, 483, 482, 481, 481, 483, 482, 481, 480},
                                              {485, 485, 485, 485, 484, 483, 482, 481, 483, 482, 481, 480},
                                              {500, 251, 251, 251, 251, 250, 250, 250, 249, 249, 250, 250},
                                              {  7, 251, 251, 251, 251, 251, 252, 252, 252, 252, 251, 250},
                                              {487, 487, 487, 487, 486, 485, 485, 485, 484, 483, 482, 481}};
int rpg_path_d[RPG_NUM_NPCS][RPG_PATH_LEN] = {{  4,   4,   4,   4,   4,   4,   4,   4,   4,   7,   7,   1},
                                              {  1,   1,   1,   1,   7,   7,   7,   4,   4,   7,   7,   1},
                                              {  3,   3,   3,   3,   8,   8,   8,   7,   7,   7,   7,   4},
                                              {  9,   6,   6,   6,   6,   9,   6,   6,   7,   4,   1,   4},
                                              {  6,   6,   6,   6,   2,   2,   2,   2,   2,   2,   9,   7},
                                              {  3,   3,   6,   6,   9,   9,   9,   9,   9,   9,   9,   6}};
#endif

#ifdef PERMANENT_LUGGAGE_AUCTION
int64 auctioncache_bid_price;
int64 auctioncache_bid_size;
int auctioncache_bid_chronon;
std::string auctioncache_bid_name;
int64 auctioncache_bestask_price;
int64 auctioncache_bestask_size;
int auctioncache_bestask_chronon;
std::string auctioncache_bestask_key;

int paymentcache_idx;
uint256 paymentcache_instate_blockhash;
int64 paymentcache_amount[PAYMENTCACHE_MAX];
std::string paymentcache_vault_addr[PAYMENTCACHE_MAX];

int64 feedcache_volume_total;
int64 feedcache_volume_participation;
int64 feedcache_volume_bull;
int64 feedcache_volume_bear;
int64 feedcache_volume_neutral;
int64 feedcache_volume_reward;
int feedcache_status;

#ifdef AUX_STORAGE_VERSION2
// CRD test
int64 tradecache_bestbid_price;
int64 tradecache_bestask_price;
int64 tradecache_bestbid_size;
int64 tradecache_bestbid_fullsize;
int64 tradecache_bestask_size;
int64 tradecache_bestask_fullsize;
int64 tradecache_crd_nextexp_mm_adjusted;
int tradecache_bestbid_chronon;
int tradecache_bestask_chronon;
bool tradecache_is_print;

// market maker -- variables (mm cache)
int64 mmlimitcache_volume_total;
int64 mmlimitcache_volume_participation;
int64 mmmaxbidcache_volume_bull;
int64 mmmaxbidcache_volume_bear;
int64 mmmaxbidcache_volume_neutral;
int64 mmminaskcache_volume_bull;
int64 mmminaskcache_volume_bear;
int64 mmminaskcache_volume_neutral;
#endif

// min and max price for feedcache: 0.0001 and 100 (dollar)
// min and max price for tradecache: 0.001 and 1000 (gems)
// min and max price for auctioncache: 1 and 1000000 (coins)
// (high numbers can crash the node/database, even if stored only as string in the blockchain)
static int64 feedcache_pricetick_up(int64 old)
{
    int64 tick;

    if (old < 10000) return 10000;
    if (old >= 10000000000) return 10000000000;

    if (old < 20000) tick = 100;
    else if (old < 50000) tick = 200;
    else if (old < 100000) tick = 500;
    else if (old < 200000) tick = 1000;
    else if (old < 500000) tick = 2000;
    else if (old < 1000000) tick = 5000;
    else if (old < 2000000) tick = 10000;
    else if (old < 5000000) tick = 20000;
    else if (old < 10000000) tick = 50000;
    else if (old < 20000000) tick = 100000;
    else if (old < 50000000) tick = 200000;
    else if (old < 100000000) tick = 500000;
    else if (old < 200000000) tick = 1000000;
    else if (old < 500000000) tick = 2000000;
    else if (old < 1000000000) tick = 5000000;
//  else tick = 10000000;
#ifdef AUX_TICKFIX_DELETEME
    // CRD test
    else if (old < 2000000000) tick = 10000000;
    else if (old < 5000000000) tick = 20000000;
    else if (old < 10000000000) tick = 50000000;
    else tick = 100000000;
#else
    else tick = 10000000;
#endif

    if ((old % tick) > 0)
        old -= (old % tick);

    return (old + tick);
}
static int64 auctioncache_pricetick_up(int64 old)
{
    return (feedcache_pricetick_up(old / 10000) * 10000);
}
// CRD test
// CRD:GEM
int64 tradecache_pricetick_up(int64 old)
{
    return (feedcache_pricetick_up(old / 10) * 10);
}
static int64 feedcache_pricetick_down(int64 old)
{
    int64 tick;

    if (old <= 10000) return 10000;
    if (old > 10000000000) return 10000000000;

    if (old <= 20000) tick = 100;
    else if (old <= 50000) tick = 200;
    else if (old <= 100000) tick = 500;
    else if (old <= 200000) tick = 1000;
    else if (old <= 500000) tick = 2000;
    else if (old <= 1000000) tick = 5000;
    else if (old <= 2000000) tick = 10000;
    else if (old <= 5000000) tick = 20000;
    else if (old <= 10000000) tick = 50000;
    else if (old <= 20000000) tick = 100000;
    else if (old <= 50000000) tick = 200000;
    else if (old <= 100000000) tick = 500000;
    else if (old <= 200000000) tick = 1000000;
    else if (old <= 500000000) tick = 2000000;
    else if (old <= 1000000000) tick = 5000000;
//  else tick = 10000000;
#ifdef AUX_TICKFIX_DELETEME
    // CRD test
    else if (old <= 2000000000) tick = 10000000;
    else if (old <= 5000000000) tick = 20000000;
    else if (old <= 10000000000) tick = 50000000;
    else tick = 100000000;
#else
    else tick = 10000000;
#endif

    if ((old % tick) > 0)
    {
        old -= (old % tick);
        return old;
    }

    return (old - tick);
}
static int64 auctioncache_pricetick_down(int64 old)
{
    return (feedcache_pricetick_down(old / 10000) * 10000);
}
// CRD test
// CRD:GEM
int64 tradecache_pricetick_down(int64 old)
{
    return (feedcache_pricetick_down(old / 10) * 10);
}
#endif

#ifdef AUX_STORAGE_VOTING
int votingcache_idx;
uint256 votingcache_instate_blockhash;
int64 votingcache_amount[PAYMENTCACHE_MAX];
int64 votingcache_txid60bit[PAYMENTCACHE_MAX];
std::string votingcache_vault_addr[PAYMENTCACHE_MAX];
bool votingcache_vault_exists[PAYMENTCACHE_MAX];
#endif
#endif


// Simple straight-line motion
void CharacterState::MoveTowardsWaypoint()
{
    if (waypoints.empty())
    {
        from = coord;
        return;
    }
    if (coord == waypoints.back())
    {
        from = coord;
        do
        {
            waypoints.pop_back();
            if (waypoints.empty())
                return;
        } while (coord == waypoints.back());
    }

    struct Helper
    {
        static int CoordStep(int x, int target)
        {
            if (x < target)
                return x + 1;
            else if (x > target)
                return x - 1;
            else
                return x;
        }

        // Compute new 'v' coordinate using line slope information applied to the 'u' coordinate
        // 'u' is reference coordinate (largest among dx, dy), 'v' is the coordinate to be updated
        static int CoordUpd(int u, int v, int du, int dv, int from_u, int from_v)
        {
            if (dv != 0)
            {
                int tmp = (u - from_u) * dv;
                int res = (abs(tmp) + abs(du) / 2) / du;
                if (tmp < 0)
                    res = -res;
                return res + from_v;
            }
            else
                return v;
        }
    };

    Coord new_c;
    Coord target = waypoints.back();
    
    int dx = target.x - from.x;
    int dy = target.y - from.y;
    
    if (abs(dx) > abs(dy))
    {
        new_c.x = Helper::CoordStep(coord.x, target.x);
        new_c.y = Helper::CoordUpd(new_c.x, coord.y, dx, dy, from.x, from.y);
    }
    else
    {
        new_c.y = Helper::CoordStep(coord.y, target.y);
        new_c.x = Helper::CoordUpd(new_c.y, coord.x, dy, dx, from.y, from.x);
    }

    if (!IsWalkable(new_c))
        StopMoving();
    else
    {
        unsigned char new_dir = GetDirection(coord, new_c);
        // If not moved (new_dir == 5), retain old direction
        if (new_dir != 5)
            dir = new_dir;
        coord = new_c;

        if (coord == target)
        {
            from = coord;
            do
            {
                waypoints.pop_back();
            } while (!waypoints.empty() && coord == waypoints.back());
        }
    }
}

std::vector<Coord> CharacterState::DumpPath(const std::vector<Coord> *alternative_waypoints /* = NULL */) const
{
    std::vector<Coord> ret;
    CharacterState tmp = *this;

    if (alternative_waypoints)
    {
        tmp.StopMoving();
        tmp.waypoints = *alternative_waypoints;
    }

    if (!tmp.waypoints.empty())
    {
        do
        {
            ret.push_back(tmp.coord);
            tmp.MoveTowardsWaypoint();
        } while (!tmp.waypoints.empty());
        if (ret.empty() || ret.back() != tmp.coord)
            ret.push_back(tmp.coord);
    }
    return ret;
}

/**
 * Calculate total length (in the same L-infinity sense that gives the
 * actual movement time) of the outstanding path.
 * @param altWP Optionally provide alternative waypoints (for queued moves).
 * @return Time necessary to finish current path in blocks.
 */
unsigned
CharacterState::TimeToDestination (const WaypointVector* altWP) const
{
  bool reverse = false;
  if (!altWP)
    {
      altWP = &waypoints;
      reverse = true;
    }

  /* In order to handle both reverse and non-reverse correctly, calculate
     first the length of the path alone and only later take the initial
     piece from coord on into account.  */

  if (altWP->empty ())
    return 0;

  unsigned res = 0;
  WaypointVector::const_iterator i = altWP->begin ();
  Coord last = *i;
  for (++i; i != altWP->end (); ++i)
    {
      res += distLInf (last, *i);
      last = *i;
    }

  if (reverse)
    res += distLInf (coord, altWP->back ());
  else
    res += distLInf (coord, altWP->front ());

  return res;
}

int64_t
CharacterState::CollectLoot (LootInfo newLoot, int nHeight, int64_t carryCap)
{
  const int64_t totalBefore = loot.nAmount + newLoot.nAmount;

  int64_t freeCap = carryCap - loot.nAmount;
  if (freeCap < 0)
    {
      /* This means that the character is carrying more than allowed
         (or carryCap == -1, which is handled later anyway).  This
         may happen during transition periods, handle it gracefully.  */
      freeCap = 0;
    }

  int64_t remaining;
  if (carryCap == -1 || newLoot.nAmount <= freeCap)
    remaining = 0;
  else
    remaining = newLoot.nAmount - freeCap;

  if (remaining > 0)
    newLoot.nAmount -= remaining;
  loot.Collect (newLoot, nHeight);

  assert (remaining >= 0 && newLoot.nAmount >= 0);
  assert (totalBefore == loot.nAmount + remaining);
  assert (carryCap == -1 || newLoot.nAmount <= freeCap);
  assert (newLoot.nAmount == 0 || carryCap == -1 || loot.nAmount <= carryCap);

  return remaining;
}

void
PlayerState::SpawnCharacter (unsigned nHeight, RandomGenerator &rnd)
{
  characters[next_character_index++].Spawn (nHeight, color, rnd);
}

json_spirit::Value PlayerState::ToJsonValue(int crown_index, bool dead /* = false*/) const
{
    using namespace json_spirit;

    Object obj;
    obj.push_back(Pair("color", (int)color));
    obj.push_back(Pair("value", ValueFromAmount(value)));

    /* If the character is poisoned, write that out.  Otherwise just
       leave the field off.  */
    if (remainingLife > 0)
      obj.push_back (Pair("poison", remainingLife));
    else
      assert (remainingLife == -1);

    if (!message.empty())
    {
        obj.push_back(Pair("msg", message));
        obj.push_back(Pair("msg_block", message_block));
    }

    if (!dead)
    {
        if (!address.empty())
            obj.push_back(Pair("address", address));
        if (!addressLock.empty())
            obj.push_back(Pair("addressLock", address));
    }
    else
    {
        // Note: not all dead players are listed - only those who sent chat messages in their last move
        assert(characters.empty());
        obj.push_back(Pair("dead", 1));
    }

    BOOST_FOREACH(const PAIRTYPE(int, CharacterState) &pc, characters)
    {
        int i = pc.first;
        const CharacterState &ch = pc.second;
        obj.push_back(Pair(strprintf("%d", i), ch.ToJsonValue(i == crown_index)));
    }

    return obj;
}

json_spirit::Value CharacterState::ToJsonValue(bool has_crown) const
{
    using namespace json_spirit;

    Object obj;
    obj.push_back(Pair("x", coord.x));
    obj.push_back(Pair("y", coord.y));
    if (!waypoints.empty())
    {
        obj.push_back(Pair("fromX", from.x));
        obj.push_back(Pair("fromY", from.y));
        Array arr;
        for (int i = waypoints.size() - 1; i >= 0; i--)
        {
            arr.push_back(Value(waypoints[i].x));
            arr.push_back(Value(waypoints[i].y));
        }
        obj.push_back(Pair("wp", arr));
    }
    obj.push_back(Pair("dir", (int)dir));
    obj.push_back(Pair("stay_in_spawn_area", stay_in_spawn_area));
    obj.push_back(Pair("loot", ValueFromAmount(loot.nAmount)));
    if (has_crown)
        obj.push_back(Pair("has_crown", true));

    return obj;
}

/* ************************************************************************** */
/* GameState.  */

static void
SetOriginalBanks (std::map<Coord, unsigned>& banks)
{
  assert (banks.empty ());
  for (int d = 0; d < SPAWN_AREA_LENGTH; ++d)
    {
      banks.insert (std::make_pair (Coord (0, d), 0));
      banks.insert (std::make_pair (Coord (d, 0), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - 1, d), 0));
      banks.insert (std::make_pair (Coord (d, MAP_HEIGHT - 1), 0));
      banks.insert (std::make_pair (Coord (0, MAP_HEIGHT - d - 1), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - d - 1, 0), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - 1,
                                           MAP_HEIGHT - d - 1), 0));
      banks.insert (std::make_pair (Coord (MAP_WIDTH - d - 1,
                                           MAP_HEIGHT - 1), 0));
    }

  assert (banks.size () == 4 * (2 * SPAWN_AREA_LENGTH - 1));
  BOOST_FOREACH (const PAIRTYPE(Coord, unsigned)& b, banks)
    {
      assert (IsOriginalSpawnArea (b.first));
      assert (b.second == 0);
    }
}

GameState::GameState()
{
    crownPos.x = CROWN_START_X;
    crownPos.y = CROWN_START_Y;
    gameFund = 0;
    nHeight = -1;
    nDisasterHeight = -1;
    hashBlock = 0;

    // gems and storage
#ifdef PERMANENT_LUGGAGE
    gemSpawnPos.x = 0;
    gemSpawnPos.y = 0;
    gemSpawnState = 0;

    // todo: move to GameState::GameState() with storage version 2
    feed_nextexp_price = 0;
    feed_prevexp_price = 0;
    feed_reward_dividend = 0;
    feed_reward_divisor = 0;
    feed_reward_remaining = 0;
    upgrade_test = 0;
    liquidity_reward_remaining = 0;
    auction_settle_price = 0;
    auction_last_price = 0;
    auction_last_chronon = 0;

#ifdef AUX_STORAGE_VERSION2
#ifdef AUX_STORAGE_VERSION3
    gs_reserve31 = 0;
    gs_reserve32 = 0;
    gs_reserve33 = 0;
    gs_reserve34 = 0;
    crd_nextexp_price = 0;
#endif
    crd_last_price = 0;
    crd_last_size = 0;
    crd_prevexp_price = 0;
    crd_mm_orderlimits = 0;
    crd_last_chronon = 0;
    gs_reserve6 = 0;
    gs_reserve7 = 0;
    gs_reserve8 = 0;
    auction_settle_conservative = 0;
    gs_reserve10 = 0;
//    gs_str_reserve1 = "";
//    gs_str_reserve2 = "";
#endif
#endif

    SetOriginalBanks (banks);
}

void
GameState::UpdateVersion(int oldVersion)
{
  /* Last version change is beyond the last version where the game db
     is fully reconstructed.  */
  assert (oldVersion >= 1001100);

  /* If necessary, initialise the banks array to the original spawn area.
     Make sure that we are not yet at the fork height!  Otherwise this
     is completely wrong.  */
  if (oldVersion < 1030000)
    {
      if (ForkInEffect (FORK_LIFESTEAL, nHeight))
        {
          error ("game DB version upgrade while the life-steal fork is"
                 " already active");
          assert (false);
        }

      SetOriginalBanks (banks);
    }
}

json_spirit::Value GameState::ToJsonValue() const
{
    using namespace json_spirit;

    Object obj;

    Object subobj;
    BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState) &p, players)
    {
        int crown_index = p.first == crownHolder.player ? crownHolder.index : -1;
        subobj.push_back(Pair(p.first, p.second.ToJsonValue(crown_index)));
    }

    // Save chat messages of dead players
    BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState) &p, dead_players_chat)
        subobj.push_back(Pair(p.first, p.second.ToJsonValue(-1, true)));

    obj.push_back(Pair("players", subobj));

    Array arr;
    BOOST_FOREACH(const PAIRTYPE(Coord, LootInfo) &p, loot)
    {
        subobj.clear();
        subobj.push_back(Pair("x", p.first.x));
        subobj.push_back(Pair("y", p.first.y));
        subobj.push_back(Pair("amount", ValueFromAmount(p.second.nAmount)));
        Array blk_rng;
        blk_rng.push_back(p.second.firstBlock);
        blk_rng.push_back(p.second.lastBlock);
        subobj.push_back(Pair("blockRange", blk_rng));
        arr.push_back(subobj);
    }
    obj.push_back(Pair("loot", arr));

    arr.clear ();
    BOOST_FOREACH (const Coord& c, hearts)
      {
        subobj.clear ();
        subobj.push_back (Pair ("x", c.x));
        subobj.push_back (Pair ("y", c.y));
        arr.push_back (subobj);
      }
    obj.push_back (Pair ("hearts", arr));

    arr.clear ();
    BOOST_FOREACH (const PAIRTYPE(Coord, unsigned)& b, banks)
      {
        subobj.clear ();
        subobj.push_back (Pair ("x", b.first.x));
        subobj.push_back (Pair ("y", b.first.y));
        subobj.push_back (Pair ("life", static_cast<int> (b.second)));
        arr.push_back (subobj);
      }
    obj.push_back (Pair ("banks", arr));

    subobj.clear();
    subobj.push_back(Pair("x", crownPos.x));
    subobj.push_back(Pair("y", crownPos.y));
    if (!crownHolder.player.empty())
    {
        subobj.push_back(Pair("holderName", crownHolder.player));
        subobj.push_back(Pair("holderIndex", crownHolder.index));
    }
    obj.push_back(Pair("crown", subobj));

    obj.push_back (Pair("gameFund", ValueFromAmount (gameFund)));
    obj.push_back (Pair("height", nHeight));
    obj.push_back (Pair("disasterHeight", nDisasterHeight));
    obj.push_back (Pair("hashBlock", hashBlock.ToString().c_str()));

    return obj;
}

void GameState::AddLoot(Coord coord, int64_t nAmount)
{
    if (nAmount == 0)
        return;
    std::map<Coord, LootInfo>::iterator mi = loot.find(coord);
    if (mi != loot.end())
    {
        if ((mi->second.nAmount += nAmount) == 0)
            loot.erase(mi);
        else
            mi->second.lastBlock = nHeight;
    }
    else
        loot.insert(std::make_pair(coord, LootInfo(nAmount, nHeight)));
}

/*

We try to split loot equally among players on a loot tile.
If a character hits its carrying capacity, the remaining coins
are split among the others.  To achieve this effect, we sort
the players by increasing (remaining) capacity -- so the ones
with least remaining capacity pick their share first, and if
it fills the capacity, leave extra coins lying around for the
others to pick up.  Since they are then filled up anyway,
it won't matter if others also leave coins, so no "iteration"
is required.

Note that for indivisible amounts the order of players matters.
For equal capacity (which is particularly true before the
hardfork point), we sort by player/character.  This makes
the new logic compatible with the old one.

The class CharacterOnLootTile takes this sorting into account.

*/

class CharacterOnLootTile
{
public:

  PlayerID pid;
  int cid;

  CharacterState* ch;
  int64_t carryCap;

  /* Get remaining carrying capacity.  */
  inline int64_t
  GetRemainingCapacity () const
  {
    if (carryCap == -1)
      return -1;

    /* During periods of change in the carrying capacity, there may be
       players "overloaded".  Take care of them.  */
    if (carryCap < ch->loot.nAmount)
      return 0;

    return carryCap - ch->loot.nAmount;
  }

  friend bool operator< (const CharacterOnLootTile& a,
                         const CharacterOnLootTile& b);

};

bool
operator< (const CharacterOnLootTile& a, const CharacterOnLootTile& b)
{
  const int64_t remA = a.GetRemainingCapacity ();
  const int64_t remB = b.GetRemainingCapacity ();

  if (remA == remB)
    {
      if (a.pid != b.pid)
        return a.pid < b.pid;
      return a.cid < b.cid;
    }

  if (remA == -1)
    {
      assert (remB >= 0);
      return false;
    }
  if (remB == -1)
    {
      assert (remA >= 0);
      return true;
    }

  return remA < remB;
}

void GameState::DivideLootAmongPlayers()
{
    std::map<Coord, int> playersOnLootTile;
    std::vector<CharacterOnLootTile> collectors;
    BOOST_FOREACH (PAIRTYPE(const PlayerID, PlayerState)& p, players)
      BOOST_FOREACH (PAIRTYPE(const int, CharacterState)& pc,
                     p.second.characters)
        {
          CharacterOnLootTile tileChar;

          tileChar.pid = p.first;
          tileChar.cid = pc.first;
          tileChar.ch = &pc.second;

          const bool isCrownHolder = (tileChar.pid == crownHolder.player
                                      && tileChar.cid == crownHolder.index);
          tileChar.carryCap = GetCarryingCapacity (nHeight, tileChar.cid == 0,
                                                   isCrownHolder);

          const Coord& coord = tileChar.ch->coord;
          if (loot.count (coord) > 0)
            {
              std::map<Coord, int>::iterator mi;
              mi = playersOnLootTile.find (coord);

              if (mi != playersOnLootTile.end ())
                mi->second++;
              else
                playersOnLootTile.insert (std::make_pair (coord, 1));

              collectors.push_back (tileChar);
            }
        }

    std::sort (collectors.begin (), collectors.end ());
    for (std::vector<CharacterOnLootTile>::iterator i = collectors.begin ();
         i != collectors.end (); ++i)
      {
        const Coord& coord = i->ch->coord;
        std::map<Coord, int>::iterator mi = playersOnLootTile.find (coord);
        assert (mi != playersOnLootTile.end ());

        LootInfo lootInfo = loot[coord];
        assert (mi->second > 0);
        lootInfo.nAmount /= (mi->second--);

        /* If amount was ~1e-8 and several players moved onto it, then
           some of them will get nothing.  */
        if (lootInfo.nAmount > 0)
          {
            const int64_t rem = i->ch->CollectLoot (lootInfo, nHeight,
                                                    i->carryCap);
            AddLoot (coord, rem - lootInfo.nAmount);
          }
      }
}

void GameState::UpdateCrownState(bool &respawn_crown)
{
    respawn_crown = false;
    if (crownHolder.player.empty())
        return;

    std::map<PlayerID, PlayerState>::const_iterator mi = players.find(crownHolder.player);
    if (mi == players.end())
    {
        // Player is dead, drop the crown
        crownHolder = CharacterID();
        return;
    }

    const PlayerState &pl = mi->second;
    std::map<int, CharacterState>::const_iterator mi2 = pl.characters.find(crownHolder.index);
    if (mi2 == pl.characters.end())
    {
        // Character is dead, drop the crown
        crownHolder = CharacterID();
        return;
    }

    if (IsBank (mi2->second.coord))
    {
        // Character entered spawn area, drop the crown
        crownHolder = CharacterID();
        respawn_crown = true;
    }
    else
    {
        // Update crown position to character position
        crownPos = mi2->second.coord;
    }
}

void
GameState::CrownBonus (int64_t nAmount)
{
  if (!crownHolder.player.empty ())
    {
      PlayerState& p = players[crownHolder.player];
      CharacterState& ch = p.characters[crownHolder.index];

      const LootInfo loot(nAmount, nHeight);
      const int64_t cap = GetCarryingCapacity (nHeight, crownHolder.index == 0,
                                               true);
      const int64_t rem = ch.CollectLoot (loot, nHeight, cap);

      /* We keep to the logic of "crown on the floor -> game fund" and
         don't distribute coins that can not be hold by the crown holder
         due to carrying capacity to the map.  */
      gameFund += rem;
    }
  else
    gameFund += nAmount;
}

unsigned
GameState::GetNumInitialCharacters () const
{
  return (ForkInEffect (FORK_POISON, nHeight) ? 1 : 3);
}

bool
GameState::IsBank (const Coord& c) const
{
  assert (!banks.empty ());
  return banks.count (c) > 0;
}

int64_t
GameState::GetCoinsOnMap () const
{
  int64_t onMap = 0;
  BOOST_FOREACH(const PAIRTYPE(Coord, LootInfo)& l, loot)
    onMap += l.second.nAmount;
  BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState)& p, players)
    {
      onMap += p.second.value;
      BOOST_FOREACH(const PAIRTYPE(int, CharacterState)& pc,
                    p.second.characters)
        onMap += pc.second.loot.nAmount;
    }

  return onMap;
}

void GameState::CollectHearts(RandomGenerator &rnd)
{
    std::map<Coord, std::vector<PlayerState*> > playersOnHeartTile;
    for (std::map<PlayerID, PlayerState>::iterator mi = players.begin(); mi != players.end(); mi++)
    {
        PlayerState *pl = &mi->second;
        if (!pl->CanSpawnCharacter())
            continue;
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, pl->characters)
        {
            const CharacterState &ch = pc.second;

            if (hearts.count(ch.coord))
                playersOnHeartTile[ch.coord].push_back(pl);
        }
    }
    for (std::map<Coord, std::vector<PlayerState*> >::iterator mi = playersOnHeartTile.begin(); mi != playersOnHeartTile.end(); mi++)
    {
        const Coord &c = mi->first;
        std::vector<PlayerState*> &v = mi->second;
        int n = v.size();
        int i;
        for (;;)
        {
            if (!n)
            {
                i = -1;
                break;
            }
            i = n == 1 ? 0 : rnd.GetIntRnd(n);
            if (v[i]->CanSpawnCharacter())
                break;
            v.erase(v.begin() + i);
            n--;
        }
        if (i >= 0)
        {
            v[i]->SpawnCharacter(nHeight, rnd);
            hearts.erase(c);
        }
    }
}

void GameState::CollectCrown(RandomGenerator &rnd, bool respawn_crown)
{
    if (!crownHolder.player.empty())
    {
        assert(!respawn_crown);
        return;
    }

    if (respawn_crown)
    {   
        int a = rnd.GetIntRnd(NUM_CROWN_LOCATIONS);
        crownPos.x = CrownSpawn[2 * a];
        crownPos.y = CrownSpawn[2 * a + 1];
    }

    std::vector<CharacterID> charactersOnCrownTile;
    BOOST_FOREACH(const PAIRTYPE(PlayerID, PlayerState) &pl, players)
    {
        BOOST_FOREACH(const PAIRTYPE(int, CharacterState) &pc, pl.second.characters)
        {
            if (pc.second.coord == crownPos)
                charactersOnCrownTile.push_back(CharacterID(pl.first, pc.first));
        }
    }
    int n = charactersOnCrownTile.size();
    if (!n)
        return;
    int i = n == 1 ? 0 : rnd.GetIntRnd(n);
    crownHolder = charactersOnCrownTile[i];
}

// Loot is pushed out from the spawn area to avoid some ambiguities with banking rules (as spawn areas are also banks)
// Note: the map must be constructed in such a way that there are no obstacles near spawn areas
static Coord
PushCoordOutOfSpawnArea(const Coord &c)
{
    if (!IsOriginalSpawnArea(c))
        return c;
    if (c.x == 0)
    {
        if (c.y == 0)
            return Coord(c.x + 1, c.y + 1);
        else if (c.y == MAP_HEIGHT - 1)
            return Coord(c.x + 1, c.y - 1);
        else
            return Coord(c.x + 1, c.y);
    }
    else if (c.x == MAP_WIDTH - 1)
    {
        if (c.y == 0)
            return Coord(c.x - 1, c.y + 1);
        else if (c.y == MAP_HEIGHT - 1)
            return Coord(c.x - 1, c.y - 1);
        else
            return Coord(c.x - 1, c.y);
    }
    else if (c.y == 0)
        return Coord(c.x, c.y + 1);
    else if (c.y == MAP_HEIGHT - 1)
        return Coord(c.x, c.y - 1);
    else
        return c;     // Should not happen
}

void
GameState::HandleKilledLoot (const PlayerID& pId, int chInd,
                             const KilledByInfo& info, StepResult& step)
{
  const PlayerStateMap::const_iterator mip = players.find (pId);
  assert (mip != players.end ());
  const PlayerState& pc = mip->second;
  assert (pc.value >= 0);
  const std::map<int, CharacterState>::const_iterator mic
    = pc.characters.find (chInd);
  assert (mic != pc.characters.end ());
  const CharacterState& ch = mic->second;

  /* If refunding is possible, do this for the locked amount right now.
     Later on, exclude the amount from further considerations.  */
  bool refunded = false;
  if (chInd == 0 && info.CanRefund (nHeight, pc))
    {
      CollectedLootInfo loot;
      loot.SetRefund (pc.value, nHeight);
      CollectedBounty b(pId, chInd, loot, pc.address);
      step.bounties.push_back (b);
      refunded = true;
    }

  /* Calculate loot.  If we kill a general, take the locked coin amount
     into account, as well.  When life-steal is in effect, the value
     should already be drawn to zero (unless we have a cause of death
     that refunds).  */
  int64_t nAmount = ch.loot.nAmount;
  if (chInd == 0 && !refunded)
    {
      assert (!ForkInEffect (FORK_LIFESTEAL, nHeight) || pc.value == 0);
      nAmount += pc.value;
    }

  /* Apply the miner tax: 4%.  */
  if (info.HasDeathTax ())
    {
      const int64_t nTax = nAmount / 25;
      step.nTaxAmount += nTax;
      nAmount -= nTax;
    }

  /* If requested (and the corresponding fork is in effect), add the coins
     to the game fund instead of dropping them.  */
  if (!info.DropCoins (nHeight, pc))
    {
      gameFund += nAmount;
      return;
    }

  /* Just drop the loot.  Push the coordinate out of spawn if applicable.
     After the life-steal fork with dynamic banks, we no longer push.  */
  Coord lootPos = ch.coord;
  if (!ForkInEffect (FORK_LIFESTEAL, nHeight))
    lootPos = PushCoordOutOfSpawnArea (lootPos);
  AddLoot (lootPos, nAmount);
}

void
GameState::FinaliseKills (StepResult& step)
{
  const PlayerSet& killedPlayers = step.GetKilledPlayers ();
  const KilledByMap& killedBy = step.GetKilledBy ();

  /* Kill depending characters.  */
  BOOST_FOREACH(const PlayerID& victim, killedPlayers)
    {
      const PlayerState& victimState = players.find (victim)->second;

      /* Take a look at the killed info to determine flags for handling
         the player loot.  */
      const KilledByMap::const_iterator iter = killedBy.find (victim);
      assert (iter != killedBy.end ());
      const KilledByInfo& info = iter->second;

      /* Kill all alive characters of the player.  */
      BOOST_FOREACH(const PAIRTYPE(int, CharacterState)& pc,
                    victimState.characters)
        HandleKilledLoot (victim, pc.first, info, step);
    }

  /* Erase killed players from the state.  */
  BOOST_FOREACH(const PlayerID& victim, killedPlayers)
    players.erase (victim);
}

bool
GameState::CheckForDisaster (RandomGenerator& rng) const
{
  /* Before the hardfork, nothing should happen.  */
  if (!ForkInEffect (FORK_POISON, nHeight))
    return false;

  /* Enforce max/min times.  */
  const int dist = nHeight - nDisasterHeight;
  assert (dist > 0);
  if (dist < PDISASTER_MIN_TIME)
    return false;
  if (dist >= PDISASTER_MAX_TIME)
    return true;

  /* Check random chance.  */
  return (rng.GetIntRnd (PDISASTER_PROBABILITY) == 0);
}

void
GameState::KillSpawnArea (StepResult& step)
{
  /* Even if spawn death is disabled after the corresponding softfork,
     we still want to do the loop (but not actually kill players)
     because it keeps stay_in_spawn_area up-to-date.  */

  BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, players)
    {
      std::set<int> toErase;
      BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc,
                    p.second.characters)
        {
          const int i = pc.first;
          CharacterState &ch = pc.second;

          if (!IsBank (ch.coord))
            {
              ch.stay_in_spawn_area = 0;
              continue;
            }

          /* Make sure to increment the counter in every case.  */
          assert (IsBank (ch.coord));
          const int maxStay = MaxStayOnBank (nHeight);
          if (ch.stay_in_spawn_area++ < maxStay || maxStay == -1)
            continue;

          /* Handle the character's loot and kill the player.  */
          const KilledByInfo killer(KilledByInfo::KILLED_SPAWN);
          HandleKilledLoot (p.first, i, killer, step);
          if (i == 0)
            step.KillPlayer (p.first, killer);

          /* Cannot erase right now, because it will invalidate the
             iterator 'pc'.  */
          toErase.insert(i);
        }
      BOOST_FOREACH(int i, toErase)
        p.second.characters.erase(i);
    }
}

void
GameState::ApplyDisaster (RandomGenerator& rng)
{
  /* Set random life expectations for every player on the map.  */
  BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState)& p, players)
    {
      /* Disasters should be so far apart, that all currently alive players
         are not yet poisoned.  Check this.  In case we introduce a general
         expiry, this can be changed accordingly -- but make sure that
         poisoning doesn't actually *increase* the life expectation.  */
      assert (p.second.remainingLife == -1);

      p.second.remainingLife = rng.GetIntRnd (POISON_MIN_LIFE, POISON_MAX_LIFE);
    }

  /* Remove all hearts from the map.  */
  if (ForkInEffect (FORK_LESSHEARTS, nHeight))
    hearts.clear ();

  /* Reset disaster counter.  */
  nDisasterHeight = nHeight;
}

void
GameState::DecrementLife (StepResult& step)
{
  BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState)& p, players)
    {
      if (p.second.remainingLife == -1)
        continue;

      assert (p.second.remainingLife > 0);
      --p.second.remainingLife;

      if (p.second.remainingLife == 0)
        {
          const KilledByInfo killer(KilledByInfo::KILLED_POISON);
          step.KillPlayer (p.first, killer);
        }
    }
}

void
GameState::RemoveHeartedCharacters (StepResult& step)
{
  assert (IsForkHeight (FORK_LIFESTEAL, nHeight));

  /* Get rid of all hearts on the map.  */
  hearts.clear ();

  /* Immediately kill all hearted characters.  */
  BOOST_FOREACH (PAIRTYPE(const PlayerID, PlayerState)& p, players)
    {
      std::set<int> toErase;
      BOOST_FOREACH (PAIRTYPE(const int, CharacterState)& pc,
                     p.second.characters)
        {
          const int i = pc.first;
          if (i == 0)
            continue;

          const KilledByInfo info(KilledByInfo::KILLED_POISON);
          HandleKilledLoot (p.first, i, info, step);

          /* Cannot erase right now, because it will invalidate the
             iterator 'pc'.  */
          toErase.insert (i);
        }
      BOOST_FOREACH (int i, toErase)
        p.second.characters.erase (i);
    }
}

void
GameState::UpdateBanks (RandomGenerator& rng)
{
  if (!ForkInEffect (FORK_LIFESTEAL, nHeight))
    return;

  std::map<Coord, unsigned> newBanks;

  /* Create initial set of banks at the fork itself.  */
  if (IsForkHeight (FORK_LIFESTEAL, nHeight))
    assert (newBanks.empty ());

  /* Decrement life of existing banks and remove the ones that
     have run out.  */
  else
    {
      assert (banks.size () == DYNBANKS_NUM_BANKS);
      assert (newBanks.empty ());

      BOOST_FOREACH (const PAIRTYPE(Coord, unsigned)& b, banks)
        {
          assert (b.second >= 1);

          /* Banks with life=1 run out now.  Since banking is done before
             updating the banks in PerformStep, this means that banks that have
             life=1 and are reached in the next turn are still available.  */
          if (b.second > 1)
            newBanks.insert (std::make_pair (b.first, b.second - 1));
        }
    }

  /* Re-create banks that are missing now.  */

  assert (newBanks.size () <= DYNBANKS_NUM_BANKS);

  FillWalkableTiles ();
  std::set<Coord> optionsSet(walkableTiles.begin (), walkableTiles.end ());
  BOOST_FOREACH (const PAIRTYPE(Coord, unsigned)& b, newBanks)
    {
      assert (optionsSet.count (b.first) == 1);
      optionsSet.erase (b.first);
    }
  assert (optionsSet.size () + newBanks.size () == walkableTiles.size ());

  std::vector<Coord> options(optionsSet.begin (), optionsSet.end ());
  for (unsigned cnt = newBanks.size (); cnt < DYNBANKS_NUM_BANKS; ++cnt)
    {
      const int ind = rng.GetIntRnd (options.size ());
      const int life = rng.GetIntRnd (DYNBANKS_MIN_LIFE, DYNBANKS_MAX_LIFE);
      const Coord& c = options[ind];

      assert (newBanks.count (c) == 0);
      newBanks.insert (std::make_pair (c, life));

      /* Do not use a silly trick like swapping in the last element.
         We want to keep the array ordered at all times.  The order is
         important with respect to consensus, and this makes the consensus
         protocol "clearer" to describe.  */
      options.erase (options.begin () + ind);
    }

  banks.swap (newBanks);
  assert (banks.size () == DYNBANKS_NUM_BANKS);
}

/* ************************************************************************** */

void
CollectedBounty::UpdateAddress (const GameState& state)
{
  const PlayerID& p = character.player;
  const PlayerStateMap::const_iterator i = state.players.find (p);
  if (i == state.players.end ())
    return;

  address = i->second.address;
}

bool Game::PerformStep(const GameState &inState, const StepData &stepData, GameState &outState, StepResult &stepResult)
{
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        if (!m.IsValid(inState))
            return false;

    outState = inState;

    /* Initialise basic stuff.  The disaster height is set to the old
       block's for now, but it may be reset later when we decide that
       a disaster happens at this block.  */
    outState.nHeight = inState.nHeight + 1;
    outState.nDisasterHeight = inState.nDisasterHeight;
    outState.hashBlock = stepData.newHash;
    outState.dead_players_chat.clear();

    stepResult = StepResult();

    /* Pay out game fees (except for spawns) to the game fund.  This also
       keeps track of the total fees paid into the game world by moves.  */
    int64_t moneyIn = 0;
    BOOST_FOREACH(const Move& m, stepData.vMoves)
      if (!m.IsSpawn ())
        {
          const PlayerStateMap::iterator mi = outState.players.find (m.player);
          assert (mi != outState.players.end ());
          assert (m.newLocked >= mi->second.lockedCoins);
          const int64_t newFee = m.newLocked - mi->second.lockedCoins;
          outState.gameFund += newFee;
          moneyIn += newFee;
          mi->second.lockedCoins = m.newLocked;
        }
      else
        moneyIn += m.newLocked;

    // Apply attacks
    CharactersOnTiles attackedTiles;
    attackedTiles.ApplyAttacks (outState, stepData.vMoves);
    if (ForkInEffect (FORK_LIFESTEAL, outState.nHeight))
      attackedTiles.DefendMutualAttacks (outState);
    attackedTiles.DrawLife (outState, stepResult);

    // Kill players who stay too long in the spawn area
    outState.KillSpawnArea (stepResult);

    /* Decrement poison life expectation and kill players when it
       has dropped to zero.  */
    outState.DecrementLife (stepResult);

    /* Finalise the kills.  */
    outState.FinaliseKills (stepResult);

    /* Special rule for the life-steal fork:  When it takes effect,
       remove all hearted characters from the map.  Also heart creation
       is disabled, so no hearted characters will ever be present
       afterwards.  */
    if (IsForkHeight (FORK_LIFESTEAL, outState.nHeight))
      outState.RemoveHeartedCharacters (stepResult);

    /* Apply updates to target coordinate.  This ignores already
       killed players.  */
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        if (!m.IsSpawn())
            m.ApplyWaypoints(outState);


#ifdef AUX_STORAGE_VOTING
    if (outState.nHeight >= AUX_MINHEIGHT_VOTING(fTestNet))
    {
        if (votingcache_idx > 0)
        {
          if (votingcache_instate_blockhash == inState.hashBlock)
          {
            for (int i = 0; i < votingcache_idx; i++)
            {
                if (votingcache_vault_exists[i])
                    printf("scanning votes: vault address %s, amount %s\n", votingcache_vault_addr[i].c_str(), FormatMoney(votingcache_amount[i]).c_str());
                else
                    printf("scanning votes: new addr %s, amount %s\n", votingcache_vault_addr[i].c_str(), FormatMoney(votingcache_amount[i]).c_str());

                std::map<std::string, StorageVault>::iterator mi = outState.vault.find(votingcache_vault_addr[i]);
                if (mi != outState.vault.end())
                {
                    // delete (stale or because the output was spent)
                    if (votingcache_amount[i] == -1)
                    {
                        if (mi->second.vote_txid60bit == votingcache_txid60bit[i])
                        {
                            int64 tmp_new_gems = 0;
/*
                            //reward for voting
                            int64 tmp_new_gems = mi->second.vote_raw_amount / 25000 / 50;
                            tmp_new_gems -= (tmp_new_gems % 1000000);
                            if (tmp_new_gems < 0) tmp_new_gems = 0;

                            // keep reward?
                            if (outState.nHeight / 10000 >= int(mi->second.vote_raw_amount % 10000000))
                                tmp_new_gems = 0;

                            mi->second.nGems -= tmp_new_gems;
*/
                            mi->second.vote_raw_amount = 0;
                            mi->second.vote_txid60bit = 0;
                            mi->second.vote_comment = "";

                            printf("scanning votes: vote deleted, fee %s\n",  FormatMoney(tmp_new_gems).c_str());

                            // cleanup
                            if (mi->second.nGems <= 0) outState.vault.erase(mi);
                        }
                    }
                    else if (votingcache_amount[i] > 0)
                    {
                        if (mi->second.vote_raw_amount > 0)
                        {
                            printf("scanning votes: cannot overwrite existing vote\n");
                        }
                        else
                        {
                            mi->second.vote_raw_amount = votingcache_amount[i];
                            mi->second.vote_txid60bit = votingcache_txid60bit[i];
                            printf("scanning votes: vote saved\n");
                        }
                    }
                }
                else if (!votingcache_vault_exists[i])
                {
                    int64 tmp_new_gems = 0;
                    /*
                    int64 tmp_new_gems = votingcache_amount[i] / 25000 / 100;
                    tmp_new_gems -= (tmp_new_gems % 1000000);
                    if (tmp_new_gems < 0) tmp_new_gems = 0;
*/
                    outState.vault.insert(std::make_pair(votingcache_vault_addr[i], StorageVault(tmp_new_gems)));

                    std::map<std::string, StorageVault>::iterator mi2 = outState.vault.find(votingcache_vault_addr[i]);
                    if (mi2 != outState.vault.end())
                    {
                        mi2->second.vote_raw_amount = votingcache_amount[i];
                        mi2->second.vote_txid60bit = votingcache_txid60bit[i];
                        mi2->second.huntername = "#Anonymous";
                        printf("scanning votes: Anonymous: vote saved\n");
                    }
                    else
                    {
                        printf("scanning votes: Anonymous: ERROR\n");
                    }
                }
                else
                {
                    printf("scanning votes: ERROR\n");
                }
            }
          }
          else
          {
              printf("scanning votes: wrong block hash\n");
          }
        }
    }
#endif

#ifdef PERMANENT_LUGGAGE_AUCTION
#ifdef RPG_OUTFIT_ITEMS
    if (outState.nHeight >= RPG_MINHEIGHT_OUTFIT(fTestNet))
    {
        for (int i = 0; i < RPG_NUM_OUTFITS; i++)
        {
            if (i >= RPG_NUM_NPCS) break;

            outfit_cache[i] = false;
            int tmp_interval = fTestNet ? rpg_interval_tnet[i] : rpg_interval[i];
            int tmp_timeshift = fTestNet ? rpg_timeshift_tnet[i] : rpg_timeshift[i];
            int tmp_finished = fTestNet ? rpg_finished_tnet[i] : rpg_finished[i];

            int tmp_step = (outState.nHeight + tmp_timeshift) % tmp_interval;
            if ((tmp_step >= RPG_PATH_LEN-1) && (tmp_step < tmp_finished))
            {
                rpg_spawnpoint_x[i] = rpg_path_x[i][RPG_PATH_LEN-1];
                rpg_spawnpoint_y[i] = rpg_path_y[i][RPG_PATH_LEN-1];
            }
            else
            {
                rpg_spawnpoint_x[i] = rpg_spawnpoint_y[i] = -1;
            }
        }
        printf("outfit test: merchants: mage xy=%d %d, fighter xy=%d %d  hunter xy=%d %d\n", rpg_spawnpoint_x[0], rpg_spawnpoint_y[0], rpg_spawnpoint_x[1], rpg_spawnpoint_y[1], rpg_spawnpoint_x[2], rpg_spawnpoint_y[2]);

    }
#endif
    if (outState.nHeight >= AUX_MINHEIGHT_FEED(fTestNet))
    {
        auctioncache_bid_price = 0;
        auctioncache_bid_size = 0;
        auctioncache_bid_chronon = 0;
        auctioncache_bid_name = "";
        auctioncache_bestask_price = 0;
        auctioncache_bestask_size = 0;
        auctioncache_bestask_chronon = 0;
        auctioncache_bestask_key = "";

#ifdef AUX_STORAGE_VERSION2
        // CRD test
        // fixme (do this only once)
        feedcache_status = FEEDCACHE_NORMAL;
        if (outState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet) == 0) feedcache_status = FEEDCACHE_EXPIRY;
        //
        int tmp_oldexp_chronon = outState.nHeight - (outState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet));
        if (feedcache_status == FEEDCACHE_EXPIRY)
            tmp_oldexp_chronon = outState.nHeight - AUX_EXPIRY_INTERVAL(fTestNet);
        int tmp_newexp_chronon = tmp_oldexp_chronon + AUX_EXPIRY_INTERVAL(fTestNet);
        //
        tradecache_bestbid_price = 0;
        tradecache_bestask_price = 0;
        tradecache_bestbid_size = 0;
        tradecache_bestbid_fullsize = 0;
        tradecache_bestask_size = 0;
        tradecache_bestask_fullsize = 0;
        tradecache_crd_nextexp_mm_adjusted = 0;
        tradecache_bestbid_chronon = 0;
        tradecache_bestask_chronon = 0;
        tradecache_is_print = false;
//        BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
        BOOST_FOREACH(PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
        {
            int64 tmp_bid_price = st.second.ex_order_price_bid;
            int64 tmp_bid_size = st.second.ex_order_size_bid;
            int64 tmp_bid_chronon = st.second.ex_order_chronon_bid; // int64?
            int64 tmp_ask_price = st.second.ex_order_price_ask;
            int64 tmp_ask_size = st.second.ex_order_size_ask;
            int64 tmp_ask_chronon = st.second.ex_order_chronon_ask;

            int tmp_order_flags = st.second.ex_order_flags;
            int64 tmp_position_size = st.second.ex_position_size;
            int64 tmp_position_price = st.second.ex_position_price;


            // market maker
            if ((outState.crd_prevexp_price > 0) &&
                (st.first == "npc.marketmaker.zeSoKxK3rp3dX3it1Y"))
            {
                int out_height = outState.nHeight;

                // reset, initial spread 50 ticks
                if (tmp_bid_price <= 0)
                {
                    tmp_bid_price = outState.crd_prevexp_price;
                    for (int n = 0; n < 30; n++)
                        tmp_bid_price = tradecache_pricetick_down(tmp_bid_price);

                    tmp_bid_size = (st.second.nGems / tmp_bid_price) * COIN / 100; // 1%, rounded down to COIN, minimum 1*COIN
                    tmp_bid_size -= (tmp_bid_size % COIN);
                    if (tmp_bid_size < COIN) tmp_bid_size = COIN;
                    tmp_bid_chronon = out_height;
                }
                if (tmp_ask_price <= 0)
                {
                    tmp_ask_price = outState.crd_prevexp_price;
                    for (int n = 0; n < 20; n++)
                        tmp_ask_price = tradecache_pricetick_up(tmp_ask_price);

                    tmp_ask_size = (st.second.nGems / tmp_ask_price) * COIN / 100; // 1%, rounded down to COIN, minimum 1*COIN
                    tmp_ask_size -= (tmp_ask_size % COIN);
                    if (tmp_ask_size < COIN) tmp_ask_size = COIN;
                    tmp_ask_chronon = out_height;
                }

                // order got hit
                if ((tmp_bid_size == 0) && (tmp_bid_price > 0))
                {
                    tmp_bid_price = tradecache_pricetick_down(tmp_bid_price);

                    tmp_bid_size = (st.second.nGems / tmp_bid_price) * COIN / 100; // 1%, rounded down to COIN, minimum 1*COIN
                    tmp_bid_size -= (tmp_bid_size % COIN);
                    if (tmp_bid_size < COIN) tmp_bid_size = COIN;
                    tmp_bid_chronon = out_height;
                }
                if ((tmp_ask_size == 0) && (tmp_ask_price > 0))
                {
                    tmp_ask_price = tradecache_pricetick_up(tmp_ask_price);

                    tmp_ask_size = (st.second.nGems / tmp_ask_price) * COIN / 100; // 1%, rounded down to COIN, minimum 1*COIN
                    tmp_ask_size -= (tmp_ask_size % COIN);
                    if (tmp_ask_size < COIN) tmp_ask_size = COIN;
                    tmp_ask_chronon = out_height;
                }


                // improve price (up to 3% spread) or size (up to 2% of your coins)
                int n = out_height % MM_AI_TICK_INTERVAL;

                // the following only works because mm is last in the list (after all hunters)
                // and tradecache_best..._price is already known

//                if ((tradecache_bestbid_price > tmp_bid_price) && (n == 6)) n = 1;
//                if ((tradecache_bestask_price < tmp_ask_price) && (n == 6)) n = 1;

                if (n == 1)
                {
                    int64 desired_bid_max = 0;
                    int64 desired_ask_min = 0;
                    MM_ORDERLIMIT_UNPACK(outState.crd_mm_orderlimits, desired_bid_max, desired_ask_min);

                    // make the MM smarter
                    //
                    // notes: - current block processing will not change outState.auction_settle_conservative
                    //          unless (outState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL == 0)
                    //        - outState.feed_nextexp_price is used in calculation of settlement price, before being updated rather late in block processing
                    //        - could use stale data for MM (but order of processing can only change after a new AUX_MINHEIGHT_...)
                    if ((out_height >= AUX_MINHEIGHT_MM_AI_UPGRADE(fTestNet)) &&
                        (outState.auction_settle_conservative > 0) && (outState.feed_nextexp_price > 0) && (st.second.nGems > 0))
                    {
                        int64 tmp_settlement = (((COIN * COIN) / outState.auction_settle_conservative) * COIN) / outState.feed_nextexp_price;
                        tmp_settlement = tradecache_pricetick_down(tradecache_pricetick_up(tmp_settlement)); // snap to grid

                        int maxed_out_50th = int(((tmp_position_size / COIN) * tmp_settlement * 50) / st.second.nGems); // -50 ... +50
//                        double adjustment = 1.0 + (double(maxed_out_50th) / 100.0) // 0.5 ... 1.5
                        if (maxed_out_50th > 0)
                            for (int n = 0; n < maxed_out_50th; n++)
                                tmp_settlement = tradecache_pricetick_down(tmp_settlement);
                        if (maxed_out_50th < 0)
                            for (int n = 0; n > maxed_out_50th; n--)
                                tmp_settlement = tradecache_pricetick_up(tmp_settlement);

                        if (tmp_settlement < desired_bid_max) desired_bid_max = tmp_settlement;
                        if (tmp_settlement > desired_ask_min) desired_ask_min = tmp_settlement;
                        tradecache_crd_nextexp_mm_adjusted = tmp_settlement;

                        // cancel bid and set lower
                        if (tmp_bid_price > desired_bid_max)
                        {
                            tmp_bid_price = tradecache_pricetick_down(tmp_bid_price);
                            tmp_bid_size = (st.second.nGems / tmp_bid_price) * COIN / 100; // 1%, rounded down to COIN, minimum 1*COIN
                            tmp_bid_size -= (tmp_bid_size % COIN);
                            if (tmp_bid_size < COIN) tmp_bid_size = COIN;
                            tmp_bid_chronon = out_height;
                        }
                        // cancel ask and set higher
                        if (tmp_ask_price < desired_ask_min)
                        {
                            tmp_ask_price = tradecache_pricetick_up(tmp_ask_price);
                            tmp_ask_size = (st.second.nGems / tmp_ask_price) * COIN / 100; // 1%, rounded down to COIN, minimum 1*COIN
                            tmp_ask_size -= (tmp_ask_size % COIN);
                            if (tmp_ask_size < COIN) tmp_ask_size = COIN;
                            tmp_ask_chronon = out_height;
                        }
                    }

                    double spread_bid = 1.03;
                    double spread_ask = 0.97;
                    if (st.second.ex_trade_profitloss < 0)
                    {
                        spread_bid = 1.06;
                        spread_ask = 0.94;
                    }

                    // improve bid
                    if ((n == 1) && (tmp_bid_price > 0))
                    {
                        int64 better_bid = tradecache_pricetick_up(tmp_bid_price);
                        if ((better_bid * spread_bid < tmp_ask_price) && (better_bid <= desired_bid_max))
                        {
                            tmp_bid_price = better_bid;
                            tmp_bid_chronon = out_height;
                        }
                        else
                        {
                            int64 better_bid_size_increment = (st.second.nGems / tmp_bid_price) * COIN / 500; // 0.2%, rounded down to COIN, always >=COIN
                            better_bid_size_increment -= (better_bid_size_increment % COIN);
                            if (better_bid_size_increment < COIN) better_bid_size_increment = COIN;

                            int64 better_bid_size = tmp_bid_size + better_bid_size_increment;
                            if (better_bid_size <= (st.second.nGems / tmp_bid_price) * COIN / 50) // 2% max
                            {
                                tmp_bid_size = better_bid_size;
                                tmp_bid_chronon = out_height;
                            }

//                            if (tmp_ask_size < tmp_bid_size)
                            if ((tmp_ask_size < tmp_bid_size) && (tmp_ask_price > 0))
                            {
                                tmp_ask_size = tmp_bid_size;
                                tmp_ask_chronon = out_height;
                            }
                        }
                    }
                    // improve ask
                    if ((n == 1) && (tmp_ask_price > 0))
                    {
                        int64 better_ask = tradecache_pricetick_down(tmp_ask_price);
                        if ((better_ask * spread_ask > tmp_bid_price) && (better_ask >= desired_ask_min))
                        {
                            tmp_ask_price = better_ask;
                            tmp_ask_chronon = out_height;
                        }
                        else
                        {
                            int64 better_ask_size_increment = (st.second.nGems / tmp_ask_price) * COIN / 500; // 0.2%, rounded down to COIN, always >=COIN
                            better_ask_size_increment -= (better_ask_size_increment % COIN);
                            if (better_ask_size_increment < COIN) better_ask_size_increment = COIN;

                            int64 better_ask_size = tmp_ask_size + better_ask_size_increment;
                            if (better_ask_size <= (st.second.nGems / tmp_ask_price) * COIN/ 50) // 2% max
                            {
                                tmp_ask_size = better_ask_size;
                                tmp_ask_chronon = out_height;
                            }

//                            if (tmp_bid_size < tmp_ask_size)
                            if ((tmp_bid_size < tmp_ask_size) && (tmp_bid_price > 0))
                            {
                                tmp_bid_size = tmp_ask_size;
                                tmp_bid_chronon = out_height;
                            }
                        }
                    }

                    if (tmp_order_flags & (ORDERFLAG_BID_INVALID | ORDERFLAG_ASK_INVALID))
                    {
                        tmp_bid_size = tmp_bid_price = 0;
                        tmp_ask_size = tmp_ask_price = 0;
                    }
                }

                // write back
                st.second.ex_order_price_bid = tmp_bid_price;
                st.second.ex_order_size_bid = tmp_bid_size;
                st.second.ex_order_chronon_bid = tmp_bid_chronon;
                st.second.ex_order_price_ask = tmp_ask_price;
                st.second.ex_order_size_ask = tmp_ask_size;
                st.second.ex_order_chronon_ask = tmp_ask_chronon;

                // MM receives 7/3 of gems that spawn on map
                if (out_height >= AUX_MINHEIGHT_MM_AI_UPGRADE(fTestNet))
                {
                    if (outState.nHeight % GEM_RESET_INTERVAL(fTestNet) == 0)
                        st.second.nGems += 34000000 * 7;
                }
            }

            // settlement test
            if ((outState.nHeight >= AUX_MINHEIGHT_SETTLE(fTestNet)) &&
                 (outState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet) == 2))
            {
                if (tmp_order_flags & ORDERFLAG_BID_SETTLE) tmp_bid_price = outState.crd_prevexp_price; // price is correct for a short time after exp. block
                if (tmp_order_flags & ORDERFLAG_ASK_SETTLE) tmp_ask_price = outState.crd_prevexp_price;
            }

            // check if we can afford our orders
            // note: all price and size values are in standard bitcoin notation (100000000 means 1.0)
            //       this is different from the "playground" testnet
            //       size is multiple of TRADE_CRD_MIN_SIZE (TRADE_CRD_MIN_SIZE == COIN, i.e. 1 dollar minimum size)
            //   if we get a fill, this will be our (additional) profit or loss
            int64 pl_a = 0;
            int64 pl_b = 0;
            if (tmp_ask_price) pl_a = (tmp_position_size / COIN) * (tmp_ask_price - tmp_position_price);
            if (tmp_bid_price) pl_b = (tmp_position_size / COIN) * (tmp_bid_price - tmp_position_price);
            int64 pl = pl_b < pl_a ? pl_b : pl_a;
            //   net worth ignoring "unsettled profits"
            int64 nw = st.second.ex_trade_profitloss < 0 ? pl + st.second.nGems + st.second.ex_trade_profitloss : pl + st.second.nGems;
            // if collateral is about to be sold for coins
            if (st.second.auction_ask_size > 0) nw -= st.second.auction_ask_size;

            //   can drop to 0
            int64 risk_bidorder = ((tmp_position_size + tmp_bid_size) / COIN) * tmp_bid_price;
            //   can go to strike price
            int64 risk_askorder = ((-tmp_position_size + tmp_ask_size) / COIN) * (outState.crd_prevexp_price * 3 - tmp_ask_price);
            //   if order would reduce position size
            if (risk_bidorder < 0) risk_bidorder = 0;
            if (risk_askorder < 0) risk_askorder = 0;
            // in case above division has rounded it down (which is ok for risk_bidorder)
            if (risk_askorder) risk_askorder = tradecache_pricetick_up(risk_askorder);

            //   autocancel unfunded bid order
            if (risk_bidorder > nw)
            {
                tmp_order_flags |= ORDERFLAG_BID_INVALID;
            }
            else
            {
                tmp_order_flags |= ORDERFLAG_BID_ACTIVE;
                if (tmp_order_flags & ORDERFLAG_BID_INVALID) tmp_order_flags -= ORDERFLAG_BID_INVALID;
            }

            if ((tmp_order_flags & ORDERFLAG_BID_INVALID) && (tmp_order_flags & ORDERFLAG_BID_ACTIVE))
              tmp_order_flags -= ORDERFLAG_BID_ACTIVE;

            //   autocancel unfunded ask order
            if (risk_askorder > nw)
            {
                tmp_order_flags |= ORDERFLAG_ASK_INVALID;
            }
            else
            {
                tmp_order_flags |= ORDERFLAG_ASK_ACTIVE;
                if (tmp_order_flags & ORDERFLAG_ASK_INVALID) tmp_order_flags -= ORDERFLAG_ASK_INVALID;
            }

            if ((tmp_order_flags & ORDERFLAG_ASK_INVALID) && (tmp_order_flags & ORDERFLAG_ASK_ACTIVE))
              tmp_order_flags -= ORDERFLAG_ASK_ACTIVE;


            // settlement test
            if ((outState.nHeight >= AUX_MINHEIGHT_SETTLE(fTestNet)) &&
                (outState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet) == 2))
            {
                if ((tmp_order_flags & ORDERFLAG_BID_SETTLE) && (tmp_order_flags & ORDERFLAG_BID_ACTIVE) && (st.second.ex_position_size == 0))
                {
                    int64 print_price = tmp_bid_price = outState.crd_prevexp_price; // price is correct for a short time after exp. block
                    int64 s = tmp_bid_size;

//                    st.second.ex_order_flags -= (ORDERFLAG_BID_ACTIVE + ORDERFLAG_BID_SETTLE); // filled
                    st.second.ex_order_flags -= ORDERFLAG_BID_ACTIVE; // filled (settle flag remains, active flag is re-set automatically next block)
                    st.second.ex_order_size_bid = 0;

                    st.second.ex_position_size += s; // we bought something
                    st.second.ex_position_price = print_price; // start new pl calculation

                }
                if ((tmp_order_flags & ORDERFLAG_ASK_SETTLE) && (tmp_order_flags & ORDERFLAG_ASK_ACTIVE) && (st.second.ex_position_size == 0))
                {
                    int64 print_price = tmp_ask_price = outState.crd_prevexp_price; // price is correct for a short time after exp. block
                    int64 s = tmp_ask_size;

//                    st.second.ex_order_flags -= (ORDERFLAG_ASK_ACTIVE + ORDERFLAG_ASK_SETTLE); // filled
                    st.second.ex_order_flags -= ORDERFLAG_ASK_ACTIVE; // filled (settle flag remains, active flag is re-set automatically next block)
                    st.second.ex_order_size_ask = 0;

                    st.second.ex_position_size -= s; // we sold something
                    st.second.ex_position_price = print_price; // start new pl calculation
                }
            }


            st.second.ex_order_flags = tmp_order_flags;

            if (tmp_order_flags & ORDERFLAG_BID_ACTIVE)
            if ((tmp_bid_size) && (tmp_bid_price))
            if ((tmp_bid_price > tradecache_bestbid_price))
            {
                tradecache_bestbid_price = tmp_bid_price;
                tradecache_bestbid_size = tmp_bid_size;
                tradecache_bestbid_fullsize = tmp_bid_size;
                tradecache_bestbid_chronon = tmp_bid_chronon;
            }
            else if ((tmp_bid_price == tradecache_bestbid_price) && (tmp_bid_chronon < tradecache_bestbid_chronon))
            {
//                tradecache_bestbid_price = tmp_bid_price;
                tradecache_bestbid_size = tmp_bid_size;
                tradecache_bestbid_fullsize += tmp_bid_size;
                tradecache_bestbid_chronon = tmp_bid_chronon;
            }

            if (tmp_order_flags & ORDERFLAG_ASK_ACTIVE)
            if ((tmp_ask_price) && (tmp_ask_size))
            if ((tmp_ask_price < tradecache_bestask_price) || (tradecache_bestask_price == 0))
            {
                tradecache_bestask_price = tmp_ask_price;
                tradecache_bestask_size = tmp_ask_size;
                tradecache_bestask_fullsize = tmp_ask_size;
                tradecache_bestask_chronon = tmp_ask_chronon;
            }
            else if ((tmp_ask_price == tradecache_bestask_price) && (tmp_ask_chronon < tradecache_bestask_chronon))
            {
//                tradecache_bestask_price = tmp_ask_price;
                tradecache_bestask_size = tmp_ask_size;
                tradecache_bestask_fullsize += tmp_ask_size;
                tradecache_bestask_chronon = tmp_ask_chronon;
            }
        }
        //                                                                                              do we have correct status here?
        if ((tradecache_bestask_price > 0) && (tradecache_bestbid_price >= tradecache_bestask_price) && (feedcache_status == FEEDCACHE_NORMAL))
        {
            tradecache_is_print = true;
        }
#define ORDER_BID_FILL ((tmp_bid_price == tradecache_bestbid_price) && (tmp_bid_size == tradecache_bestbid_size) && \
                    (tmp_bid_chronon == tradecache_bestbid_chronon) && (tmp_order_flags & ORDERFLAG_BID_ACTIVE))

#define ORDER_ASK_FILL ((tmp_ask_price == tradecache_bestask_price) && (tmp_ask_size == tradecache_bestask_size) && \
                    (tmp_ask_chronon == tradecache_bestask_chronon) && (tmp_order_flags & ORDERFLAG_ASK_ACTIVE))

//        BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
        BOOST_FOREACH(PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
        {
            int64 tmp_bid_price = st.second.ex_order_price_bid;
            int64 tmp_bid_size = st.second.ex_order_size_bid;
            int64 tmp_bid_chronon = st.second.ex_order_chronon_bid;
            int64 tmp_ask_price = st.second.ex_order_price_ask;
            int64 tmp_ask_size = st.second.ex_order_size_ask;
            int64 tmp_ask_chronon = st.second.ex_order_chronon_ask;

            int tmp_order_flags = st.second.ex_order_flags;
            int tmp_position_size = st.second.ex_position_size;
            int tmp_position_price = st.second.ex_position_price;

            // if we can modify our orders right now
            // - delete me (can always modify)
            bool can_modify = false;
            if ((feedcache_status == FEEDCACHE_NORMAL) && (outState.feed_prevexp_price > 0))
            {
                if (!tradecache_is_print)
                    can_modify = true;

                if (false) // if (AI_dbg_allow_matching_engine_optimisation)
                {
                    if ((!(ORDER_BID_FILL)) && (!(ORDER_ASK_FILL)))
                        can_modify = true;
                }
            }

            // size 0 cancels
            if ((tmp_bid_size == 0) && (tmp_order_flags & ORDERFLAG_BID_ACTIVE))
            {
              tmp_order_flags -= ORDERFLAG_BID_ACTIVE;
//              st.second.ex_order_price_bid = 0;
            }
            if ((tmp_ask_size == 0) && (tmp_order_flags & ORDERFLAG_ASK_ACTIVE))
            {
              tmp_order_flags -= ORDERFLAG_ASK_ACTIVE;
//              st.second.ex_order_price_ask = 0;
            }

            st.second.ex_order_flags = tmp_order_flags;

            // do the actual matching
            // notes: - order book is built every block
            //        - no matching on expiry block
            //        - no matching 1 block after expiry (to autocancel orders which are now unaffordable)  <- no longer needed
            if ((tradecache_is_print) && (feedcache_status == FEEDCACHE_NORMAL) && (outState.nHeight > tmp_oldexp_chronon + 1))
            {

                if (ORDER_BID_FILL)
                {
                    int64 print_price = (tradecache_bestbid_price + tradecache_bestask_price) / 2; // fair, because we fill the same size of both orders
                    outState.crd_last_price = print_price;

                    int64 s = tmp_bid_size;
                    if (tradecache_bestask_size >= s)
                    {
                        st.second.ex_order_flags -= ORDERFLAG_BID_ACTIVE; // filled
                        st.second.ex_order_size_bid = 0;
                    }
                    else
                    {
                        s = tradecache_bestask_size;
                        st.second.ex_order_size_bid -= s;
                    }

                    if ((s != outState.crd_last_size) && (outState.crd_last_chronon == outState.nHeight))
                        printf("ERROR matching different bid and ask size\n");
                    outState.crd_last_size = s;
                    outState.crd_last_chronon = outState.nHeight;

                    int64 profitloss = (st.second.ex_position_size / COIN) * (print_price - st.second.ex_position_price);
                    st.second.ex_position_size += s; // we bought something
                    st.second.ex_position_price = print_price; // start new pl calculation

                    st.second.ex_trade_profitloss += profitloss;
                }
                if (ORDER_ASK_FILL)
                {
                    int64 print_price = (tradecache_bestbid_price + tradecache_bestask_price) / 2; // fair, because we fill the same size of both orders
                    outState.crd_last_price = print_price;

                    int64 s = tmp_ask_size;
                    if (tradecache_bestbid_size >= s)
                    {
                        st.second.ex_order_flags -= ORDERFLAG_ASK_ACTIVE; // filled
                        st.second.ex_order_size_ask = 0;
                    }
                    else
                    {
                        s = tradecache_bestbid_size;
                        st.second.ex_order_size_ask -= s;
                    }

                    if ((s != outState.crd_last_size) && (outState.crd_last_chronon == outState.nHeight))
                        printf("ERROR matching different bid and ask size\n");
                    outState.crd_last_size = s;
                    outState.crd_last_chronon = outState.nHeight;

                    int64 profitloss = (st.second.ex_position_size / COIN) * (print_price - st.second.ex_position_price);
                    st.second.ex_position_size -= s; // we sold something
                    st.second.ex_position_price = print_price; // start new pl calculation

                    st.second.ex_trade_profitloss += profitloss;
                }
            }
        }
#endif

        // - process the automatic downtick (if downtick would be done elewhere, it could be "const PAIRTYPE", i.e. faster)
        // - then cache best ask
//        BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
        BOOST_FOREACH( PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
        {
            int64 tmp_size = st.second.auction_ask_size;
            int64 tmp_price = st.second.auction_ask_price;
            int64 tmp_chronon = st.second.auction_ask_chronon;
            if ((tmp_size) && (tmp_price))
            {
                if (outState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL == 0)
                {
                    tmp_price = auctioncache_pricetick_down(tmp_price);
                    st.second.auction_ask_price = tmp_price;
                }

                if ((auctioncache_bestask_price == 0) || (tmp_price < auctioncache_bestask_price) || ((tmp_price == auctioncache_bestask_price) && (tmp_chronon < auctioncache_bestask_chronon)))
                {
                    auctioncache_bestask_price = tmp_price;
                    auctioncache_bestask_size = tmp_size;
                    auctioncache_bestask_chronon = tmp_chronon;
                    auctioncache_bestask_key = st.first;
                }
            }
        }
        if (outState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL == 0)
        {
            if ((auctioncache_bestask_price == 0) || (auctioncache_bestask_price > outState.auction_settle_price))
                outState.auction_settle_price = auctioncache_pricetick_up(outState.auction_settle_price);
            else if (auctioncache_bestask_price < outState.auction_settle_price)
                outState.auction_settle_price = auctioncache_pricetick_down(outState.auction_settle_price);

#ifdef AUX_STORAGE_VERSION2
            // "conservative" settlement price -- never higher than last price
            if (outState.auction_settle_conservative == 0) // for testing, delete me
            {
                outState.auction_settle_conservative = (outState.auction_settle_price < outState.auction_last_price) ? outState.auction_settle_price : outState.auction_last_price;
            }
            else if (outState.auction_settle_price < outState.auction_settle_conservative)
            {
                outState.auction_settle_conservative = outState.auction_settle_price;
            }
            else if ((auctioncache_bestask_price == 0) || (auctioncache_bestask_price > outState.auction_settle_conservative))
            {
                if (outState.auction_settle_conservative < outState.auction_last_price)
                    outState.auction_settle_conservative = auctioncache_pricetick_up(outState.auction_settle_conservative);
            }
#endif
        }

        // best ask is reserved for the "hunter" who posted the oldest bid that is
        // - not older than AUCTION_BID_PRIORITY_TIMEOUT
        // - newer than last print (outState.auction_last_chronon) because we can not process 2 at the same time
        BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.players)
        {
            if ((p.second.message_block >= outState.nHeight - AUCTION_BID_PRIORITY_TIMEOUT) && (p.second.message_block <= outState.nHeight - 1))
            {
                std::string s_amount = "";
                std::string s_price = "";
                int64 tmp_amount = 0;
                int64 tmp_price = 0;

                int l = p.second.message.length();
                int lbid2 = p.second.message.find("GEM:HUC set bid "); // length of "key phrase" is 16
                int lat3 = p.second.message.find(" at ");

//                if ((lbid2 == 0) && (lat3 >= 17) && (l >= 22) && (l <= 100))
                if ((lbid2 == 0) && (lat3 >= 17) && (l >= lat3 + 5) && (l <= 100))
                {
                    s_amount = p.second.message.substr(16, lat3 - 16);
                    s_price = p.second.message.substr(lat3 + 4);
                    if ((ParseMoney(s_amount, tmp_amount)) &&
                        (ParseMoney(s_price, tmp_price)))
                    {
                        // oldest one has priority
                        if ((auctioncache_bid_chronon == 0) || (p.second.message_block < auctioncache_bid_chronon))
                        {
                            // fill or kill
                            if ((auctioncache_bestask_price > 0) && (tmp_price >= auctioncache_bestask_price) && (tmp_amount >= AUCTION_MIN_SIZE) && (p.second.message_block > outState.auction_last_chronon))
                            {
                                tmp_amount -= (tmp_amount % AUCTION_MIN_SIZE);

                                auctioncache_bid_chronon = p.second.message_block;
                                auctioncache_bid_price = auctioncache_bestask_price;
                                auctioncache_bid_size = tmp_amount <= auctioncache_bestask_size ? tmp_amount : auctioncache_bestask_size;
                                auctioncache_bid_name = p.first;
                                printf("parsing message: bid can fill: hunter %s: %s at %s\n", p.first.c_str(), FormatMoney(tmp_amount).c_str(), FormatMoney(tmp_price).c_str());
                            }
                            else
                            {
                                printf("parsing message: bid autocanceled: hunter %s: %s at %s\n", p.first.c_str(), FormatMoney(tmp_amount).c_str(), FormatMoney(tmp_price).c_str());
                            }
                        }
                    }
                }
            }
        }

        BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.players)
        {
            // check payments for auction and execute the trade
            // - don't change auctioncache_bid_... and auctioncache_ask_... here
            if (auctioncache_bid_chronon > outState.auction_last_chronon)
            {
              if (paymentcache_idx > 0)
              {
                if (paymentcache_instate_blockhash == inState.hashBlock)
                {
                  printf("parsing message: scanning payments: hunter %s, %s at %s\n", auctioncache_bid_name.c_str(), FormatMoney(auctioncache_bid_size).c_str(), FormatMoney(auctioncache_bid_price).c_str());

                  for (int i = 0; i < paymentcache_idx; i++)
                  {
                    if ((paymentcache_vault_addr[i] == auctioncache_bestask_key) &&
                        (paymentcache_amount[i] >= auctioncache_bestask_price * auctioncache_bid_size / COIN))
                    {
                        outState.auction_last_price = auctioncache_bestask_price;
                        outState.auction_last_chronon = outState.nHeight;

                        std::map<std::string, StorageVault>::iterator mi = outState.vault.find(auctioncache_bestask_key);
                        if (mi != outState.vault.end())
                        {
                            mi->second.nGems -= auctioncache_bid_size;
                            mi->second.auction_ask_size -= auctioncache_bid_size;
                            if (mi->second.auction_ask_size <= 0)
                            {
                                mi->second.auction_ask_price = 0;
                                mi->second.auction_ask_size = 0;
                            }
                        }

                        printf("parsing message: payment received\n");
                    }
                  }
                }
                else
                {
                    printf("parsing message: wrong block hash: hunter %s, %s at %s\n", auctioncache_bid_name.c_str(), FormatMoney(auctioncache_bid_size).c_str(), FormatMoney(auctioncache_bid_price).c_str());
                }
              }
            }

            // parse last message (auction sell orders, price feed)
            if (!(p.second.playerflags & PLAYER_SUSPEND))
            if (p.second.message_block == outState.nHeight - 1) //message for current block is only available after ApplyCommon
            {
                // auction alert (display a warning)
                if (((fTestNet) && (p.second.playernameaddress == "hTHxwjD6askQj6Y4QiiEDWbS323P5fHCBg")) ||
                    ((!fTestNet) && (p.second.playernameaddress == "HURqbkug5dkrRCKxqqXqDHqwSWgNsuQSQC")))
                {
#ifdef AUX_STORAGE_VERSION2
                    outState.upgrade_test = -1;
#else
                    outState.upgrade_test--;
#endif
                }

                std::map<std::string, StorageVault>::iterator mi = outState.vault.find(p.second.playernameaddress);
                if (mi != outState.vault.end())
                {
                    std::string s_amount = "";
                    std::string s_price = "";
                    std::string s_feed = "";
                    int64 tmp_amount = 0;
                    int64 tmp_price = 0;
                    int64 tmp_feed = 0;

                    int l = p.second.message.length();
                    int lat3 = p.second.message.find(" at ");

                    int lask2 = p.second.message.find("GEM:HUC set ask "); // length of "key phrase" is 16
                    int lfeed1 = p.second.message.find("HUC:USD feed price "); // length of "key phrase" is 19
//                    printf("parsing message: found storage: l=%d lfeed1=%d lask2=%d\n", l, lfeed1, lask2);

#ifdef AUX_STORAGE_VERSION2
                    // CRD test
                    int lbid3 = p.second.message.find("CRD:GEM set bid "); // length of "key phrase" is 16
                    int lask3 = p.second.message.find("CRD:GEM set ask "); // length of "key phrase" is 16

                    // market maker -- parse vote
                    if (outState.nHeight >= AUX_MINHEIGHT_TRADE(fTestNet))
                    {
                      int lvbidmax = p.second.message.find("CRD:GEM vote MM max bid "); // length of "key phrase" is 24
                      int lvaskmin = p.second.message.find(" min ask "); // length of "key phrase" is 9
                      if ((lvbidmax == 0) && (lvaskmin >= 25) && (l >= lvaskmin + 10) && (l <= 100))
                      {
                        s_price = p.second.message.substr(24, lvaskmin - 24);
                        std::string s_price2 = p.second.message.substr(lvaskmin + 9);
                        int64 tmp_price2 = 0;

                        if ((ParseMoney(s_price, tmp_price)) &&
                            (ParseMoney(s_price2, tmp_price2)))
                        {
                            printf("parsing message (MM vote): max bid=%15"PRI64d"  min ask=%15"PRI64d" \n", tmp_price, tmp_price2);

                            // - tradecache_pricetick_... does this already
                            if (tmp_price > 1000 * COIN) tmp_price = 1000 * COIN;
                            else if (tmp_price < 100000) tmp_price = 100000;
                            if (tmp_price2 > 1000 * COIN) tmp_price2 = 1000 * COIN;
                            else if (tmp_price2 < 100000) tmp_price2 = 100000;

                            tmp_price = tradecache_pricetick_down(tradecache_pricetick_up(tmp_price)); // snap to grid
                            tmp_price2 = tradecache_pricetick_down(tradecache_pricetick_up(tmp_price2)); // snap to grid

                            // squeeze the 2 numbers into 1 int64
                            MM_ORDERLIMIT_PACK(mi->second.ex_vote_mm_limits, tmp_price, tmp_price2);
                            mi->second.ex_reserve1 = outState.nHeight;
                        }

                      }
                    }
#endif

#ifdef AUX_STORAGE_VOTING
                    int lcomment = p.second.message.find("#");
                    int lcomment2 = p.second.message.find("motion ");
                    if (lcomment2 == 0) lcomment = p.second.message.find(":");
#ifdef AUX_STORAGE_VERSION2
                    if (lcomment >= 0)
                    {
                        mi->second.vote_comment = p.second.message.substr(lcomment + 2);
                    }
#endif
#endif
                    if ((lask2 == 0) && (lat3 >= 17) && (l >= lat3 + 5) && (l <= 100))
                    {
                        s_amount = p.second.message.substr(16, lat3 - 16);
                        s_price = p.second.message.substr(lat3 + 4);
                        {
                            if ((ParseMoney(s_amount, tmp_amount)) &&
                                (ParseMoney(s_price, tmp_price)))
                            {
                                printf("parsing message: ask: amount=%15"PRI64d" price=%15"PRI64d" \n", tmp_amount, tmp_price);

                                // - auctioncache_pricetick_... does this already
                                // - if not enforced, numbers higher than MAX_MONEY will freeze the node
                                if (tmp_price > 1000000 * COIN) tmp_price = 1000000 * COIN;
                                else if (tmp_price < COIN) tmp_price = COIN;

                                // this is not ripple
                                if (tmp_amount > mi->second.nGems)
                                    tmp_amount = mi->second.nGems;

                                tmp_amount -= (tmp_amount % AUCTION_MIN_SIZE);
                                if (tmp_amount == 0)
                                    tmp_price = 0; // size 0 == cancel
                                else
                                    tmp_price = auctioncache_pricetick_down(auctioncache_pricetick_up(tmp_price)); // snap to grid

                                // - can modify an existing sell order if current best bid is lower, or send a new one
                                // - make sure the new ask price doesn't interfere with the auctioncache_bid_... order (because it's already executing)
                                // - could also rely on time priority:
                                //   (auctioncache_bestask_chronon < mi->second.auction_ask_chronon) // our order is not first in queue
                                //   (auctioncache_bid_price <= tmp_price)                           // there's another ask at same price level and it's at least 1 block old
                                if (((auctioncache_bid_price < mi->second.auction_ask_price) || (mi->second.auction_ask_price == 0)) &&
                                    ((auctioncache_bid_price < tmp_price) || (tmp_price == 0)))
                                {
                                    mi->second.auction_ask_size = tmp_amount;
                                    mi->second.auction_ask_price = tmp_price;
                                    mi->second.auction_ask_chronon = outState.nHeight;
                                }
                                else
                                {
                                    printf("parsing message: order already executing\n");
                                }
                            }
                        }
                    }

                    if ((lfeed1 == 0) && (l >= 20) && (l <= 100))
                    {
                        printf("parsing message: possible price feed\n");

                        s_feed = p.second.message.substr(19);
                        if (ParseMoney(s_feed, tmp_feed))
                        {
                            printf("parsing message: feed=%15"PRI64d" \n", tmp_feed);

                            // feedcache_pricetick_... does this already
                            if (tmp_feed > 100 * COIN) tmp_feed = 100 * COIN;
                            else if (tmp_feed < 10000) tmp_feed = 10000;

                            tmp_feed = feedcache_pricetick_down(feedcache_pricetick_up(tmp_feed)); // snap to grid
                            mi->second.feed_price = tmp_feed;
                            mi->second.feed_chronon = outState.nHeight;
                        }
                    }
#ifdef AUX_STORAGE_VERSION2
                    // CRD test
                    else if (outState.nHeight >= AUX_MINHEIGHT_TRADE(fTestNet))
                    {
                      if ((lbid3 == 0) && (lat3 >= 17) && (l >= lat3 + 5) && (l <= 100))
                      {
                        s_amount = p.second.message.substr(16, lat3 - 16);
                        s_price = p.second.message.substr(lat3 + 4);

                        // settlement test
                        if (outState.nHeight >= AUX_MINHEIGHT_SETTLE(fTestNet))
                        {
                            if (s_price == "settlement")
                            {
                                s_price = "0.001"; // set to minimum
                                mi->second.ex_order_flags |= ORDERFLAG_BID_SETTLE;
                            }
                        }

                        if (true)
                        {
                            if ((ParseMoney(s_amount, tmp_amount)) &&
                                (ParseMoney(s_price, tmp_price)))
                            {
                                printf("parsing message: CRD:GEM bid: amount=%15"PRI64d" price=%15"PRI64d" \n", tmp_amount, tmp_price);

                                // - tradecache_pricetick_... does this already
                                if (tmp_price > 1000 * COIN) tmp_price = 1000 * COIN;
                                else if (tmp_price < 100000) tmp_price = 100000;

                                tmp_amount -= (tmp_amount % TRADE_CRD_MIN_SIZE);
                                if (tmp_amount == 0)
                                    tmp_price = 0; // size 0 == cancel
                                else
                                    tmp_price = tradecache_pricetick_down(tradecache_pricetick_up(tmp_price)); // snap to grid

                                // can always modify
                                if (true)
                                {
                                    mi->second.ex_order_size_bid = tmp_amount;
                                    mi->second.ex_order_price_bid = tmp_price;
                                    mi->second.ex_order_chronon_bid = outState.nHeight;
//                                    mi->second.ex_order_flags |= ORDERFLAG_BID_ACTIVE;
                                }
                            }
                        }
                      }
                      else if ((lask3 == 0) && (lat3 >= 17) && (l >= lat3 + 5) && (l <= 100))
                      {
                        s_amount = p.second.message.substr(16, lat3 - 16);
                        s_price = p.second.message.substr(lat3 + 4);

                        // settlement test
                        if (outState.nHeight >= AUX_MINHEIGHT_SETTLE(fTestNet))
                        {
                            if (s_price == "settlement")
                            {
                                s_price = "1000.0"; // set to maximum
                                mi->second.ex_order_flags |= ORDERFLAG_ASK_SETTLE;
                            }
                        }

                        if (true)
                        {
                            if ((ParseMoney(s_amount, tmp_amount)) &&
                                (ParseMoney(s_price, tmp_price)))
                            {
                                printf("parsing message: CRD:GEM ask: amount=%15"PRI64d" price=%15"PRI64d" \n", tmp_amount, tmp_price);

                                // - tradecache_pricetick_... does this already
                                if (tmp_price > 1000 * COIN) tmp_price = 1000 * COIN;
                                else if (tmp_price < 100000) tmp_price = 100000;

                                tmp_amount -= (tmp_amount % TRADE_CRD_MIN_SIZE);
                                if (tmp_amount == 0)
                                    tmp_price = 0; // size 0 == cancel
                                else
                                    tmp_price = tradecache_pricetick_down(tradecache_pricetick_up(tmp_price)); // snap to grid

                                // - can always modify
                                if (true)
                                {
                                    mi->second.ex_order_size_ask = tmp_amount;
                                    mi->second.ex_order_price_ask = tmp_price;
                                    mi->second.ex_order_chronon_ask = outState.nHeight;
//                                    mi->second.ex_order_flags |= ORDERFLAG_ASK_ACTIVE;
                                }
                            }
                        }
                      }
                    }
#endif
                }
            }

        }
    }
#endif


    // For all alive players perform path-finding
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.players)
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            // gems and storage
#ifdef PERMANENT_LUGGAGE
            if (GEM_ALLOW_SPAWN(fTestNet, outState.nHeight))
            {
                CharacterState &ch = pc.second;
                if ((outState.gemSpawnState == GEM_SPAWNED) || (outState.gemSpawnState == GEM_HARVESTING))
                {
                    if (ch.coord == outState.gemSpawnPos)
                    {
                        outState.gemSpawnState = GEM_HARVESTING;
                        gem_visualonly_state = GEM_HARVESTING; // keep in sync
                        gem_cache_winner_name = p.first;
                    }
                }

#ifdef RPG_OUTFIT_ITEMS
                if (outState.nHeight >= RPG_MINHEIGHT_OUTFIT(fTestNet))
                {
                    for (int i = 0; i < RPG_NUM_OUTFITS; i++)
                    {
                        if ((ch.coord.x == rpg_spawnpoint_x[i]) && (ch.coord.y == rpg_spawnpoint_y[i]))
                        {
//                            ch.rpg_gems_in_purse = 1<<i;
                            if (i == 0) ch.rpg_gems_in_purse = 1;
                            else if (i == 1) ch.rpg_gems_in_purse = 2;
                            else if (i == 2) ch.rpg_gems_in_purse = 4;

                            outfit_cache[i] = true;
                            outfit_cache_name[i] = p.first;

                            printf("outfit test: %s got outfit %d\n", p.first.c_str(), i);
                        }
//                      else
//                          printf("outfit test: %s got nothing (%d)\n", p.first.c_str(), i);
                    }
                }
#endif
            }

#elif GUI
            if (GEM_ALLOW_SPAWN(fTestNet, outState.nHeight))
            {
              if ((gem_visualonly_state == GEM_SPAWNED) || (gem_visualonly_state == GEM_HARVESTING))
              {
                  CharacterState &ch = pc.second;
                  if ((ch.coord.x == gem_visualonly_x) && (ch.coord.y == gem_visualonly_y))
                  {
                      gem_visualonly_state = GEM_HARVESTING;
                      gem_cache_winner_name = p.first;
                  }
              }
            }
#endif

            pc.second.MoveTowardsWaypoint();
        }


#ifdef PERMANENT_LUGGAGE_AUCTION
    // process price feed
    if (GEM_ALLOW_SPAWN(fTestNet, outState.nHeight))
    {
        feedcache_volume_total = feedcache_volume_participation = 0;
        feedcache_volume_bull = feedcache_volume_bear = feedcache_volume_neutral = 0;
        feedcache_volume_reward = 0;

        // market maker -- clear cache
        mmlimitcache_volume_total = mmlimitcache_volume_participation = 0;
        mmmaxbidcache_volume_bull = mmmaxbidcache_volume_bear = mmmaxbidcache_volume_neutral = 0;
        mmminaskcache_volume_bull = mmminaskcache_volume_bear = mmminaskcache_volume_neutral = 0;

        if (outState.nHeight > AUX_MINHEIGHT_FEED(fTestNet))
        {
            feedcache_status = FEEDCACHE_NORMAL;
            if (outState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet) == 0) feedcache_status = FEEDCACHE_EXPIRY;

            outState.upgrade_test += 1;

#ifdef AUX_STORAGE_VERSION2
            // market maker -- initialize
            if (outState.nHeight == AUX_MINHEIGHT_TRADE(fTestNet))
            {
                int64 tmp_median_mm_maxbid = 3 * COIN; // give free reign by default
                int64 tmp_median_mm_minask = 0.03 * COIN;
                MM_ORDERLIMIT_PACK(outState.crd_mm_orderlimits, tmp_median_mm_maxbid, tmp_median_mm_minask);
            }
#endif
        }
        else if (outState.nHeight == AUX_MINHEIGHT_FEED(fTestNet)) // initialize
        {
            feedcache_status = FEEDCACHE_EXPIRY;
            outState.feed_nextexp_price = 200000; // 0.002 dollar, could start from 0 but would take longer to indicate actual price level
            outState.feed_reward_remaining = 100000000; // 1 gem
            outState.liquidity_reward_remaining = 100000000; // 1 gem
            outState.auction_settle_price = 10000000000; // 100 coins
            outState.auction_settle_conservative = 10000000000; // 100 coins

            outState.upgrade_test += 1;
        }
        else
        {
            feedcache_status = 0;

            // todo: move to GameState::GameState() with storage version 2
#ifndef AUX_STORAGE_VERSION2
            outState.feed_nextexp_price = 0;
            outState.feed_prevexp_price = 0;
            outState.feed_reward_dividend = 0;
            outState.feed_reward_divisor = 0;
            outState.feed_reward_remaining = 0;
            outState.upgrade_test = outState.nHeight;
            outState.liquidity_reward_remaining = 0;
            outState.auction_settle_price = 0;
            outState.auction_last_price = 0;
            outState.auction_last_chronon = 0;
#endif
        }

        int tmp_oldexp_chronon = outState.nHeight - (outState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet));
        if (feedcache_status == FEEDCACHE_EXPIRY)
            tmp_oldexp_chronon = outState.nHeight - AUX_EXPIRY_INTERVAL(fTestNet);
        int tmp_newexp_chronon = tmp_oldexp_chronon + AUX_EXPIRY_INTERVAL(fTestNet);

//        if (feedcache_status == FEEDCACHE_EXPIRY)
//        {
//            outState.feed_prevexp_price = outState.feed_nextexp_price;
//        }
        if (feedcache_status == FEEDCACHE_EXPIRY)
        {
            int64 tmp_unified_exp_price = outState.feed_prevexp_price = outState.feed_nextexp_price;
#ifdef AUX_STORAGE_VERSION2
            // CRD test
            // note: feedcache_status>0 implies AUX_MINHEIGHT_FEED
            int64 tmp_old_crd_prevexp_price = outState.crd_prevexp_price;

            if (outState.auction_settle_price == 0)
            {
                 printf("trade test: ERROR: outState.auction_settle_price == 0\n");
            }
            else if (outState.feed_nextexp_price == 0)
            {
                 printf("trade test: ERROR: outState.feed_nextexp_price == 0\n");
            }
            else
            {
              int64 tmp_conservative_settle_price = outState.auction_settle_conservative;
              if (tmp_conservative_settle_price <= 0)
              {
                  tmp_conservative_settle_price = outState.auction_settle_price;
                  printf("trade test: WARNING: outState.auction_settle_conservative == 0\n");
              }

//              int64 tmp_settlement = (((COIN * COIN) / outState.auction_settle_price) * COIN) / tmp_unified_exp_price;
              int64 tmp_settlement = (((COIN * COIN) / tmp_conservative_settle_price) * COIN) / tmp_unified_exp_price;
              tmp_settlement = tradecache_pricetick_down(tradecache_pricetick_up(tmp_settlement)); // snap to grid
              outState.crd_prevexp_price = tmp_settlement;

              // do automatic exercise if in the money
              if (outState.crd_prevexp_price > 0)
              {
                BOOST_FOREACH(PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
                {

                    // settle previous profit/loss...
                    if (st.second.ex_trade_profitloss != 0)
                    {
                        int64 profitloss = st.second.ex_trade_profitloss;
                        if (profitloss >= 0) profitloss -= (profitloss % 1000000);
                        else profitloss += (profitloss % 1000000);

                        st.second.nGems += profitloss;
                        st.second.ex_trade_profitloss = 0;
                    }

                    // ...and calculate new one
                    int64 print_price = tmp_old_crd_prevexp_price * 3; // covered call strike
                    if (outState.crd_prevexp_price < tmp_old_crd_prevexp_price * 3)
                    {
                        print_price = outState.crd_prevexp_price;
                    }

                    // postpone because we don't have a reasonable implementation yet
                    if (outState.nHeight >= AUX_MINHEIGHT_SETTLE(fTestNet))
                    {
                        if (st.second.ex_position_size != 0)
                        {
                            int64 profitloss = (st.second.ex_position_size / COIN) * (print_price - st.second.ex_position_price);
                            st.second.ex_position_size = 0;
                            st.second.ex_position_price = 0;

                            st.second.ex_trade_profitloss += profitloss;
                        }
                    }

                    // market maker -- don't cancel remaining orders if price is mostly unchanged
                    if (outState.crd_prevexp_price > tmp_old_crd_prevexp_price * 1.5)
                    {
                        st.second.ex_order_price_bid = st.second.ex_order_price_ask = st.second.ex_order_size_bid = st.second.ex_order_size_ask = 0;
                        st.second.ex_order_flags = 0;
                    }
                }
              }
            }
#endif
        }

        // distribute reward
        if ((feedcache_status == FEEDCACHE_NORMAL) && (outState.nHeight == tmp_oldexp_chronon + 50))
        {
            BOOST_FOREACH(PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
            {
                if ((st.second.vaultflags & VAULTFLAG_FEED_REWARD) && (outState.feed_reward_divisor > 0))
                {
                    st.second.vaultflags -= VAULTFLAG_FEED_REWARD;

                    int64 reward = st.second.nGems * outState.feed_reward_dividend / outState.feed_reward_divisor;
                    if (reward < outState.feed_reward_remaining)
                    {
                        reward -= (reward % 1000000);
                        st.second.nGems += reward;
                        outState.feed_reward_remaining -= reward;
                    }
                }
            }
        }

        if (feedcache_status == FEEDCACHE_NORMAL)
        {
            // market maker -- update median vote
            int64 tmp_median_mm_maxbid = 0;
            int64 tmp_median_mm_minask = 0;
            if (outState.nHeight >= AUX_MINHEIGHT_TRADE(fTestNet))
            {
                MM_ORDERLIMIT_UNPACK(outState.crd_mm_orderlimits, tmp_median_mm_maxbid, tmp_median_mm_minask);
//                printf("MM test: median limits packed %s\n", FormatMoney(outState.crd_mm_orderlimits).c_str());
//                printf("MM test: median limits unpacked bid %s ask %s\n", FormatMoney(tmp_median_mm_maxbid).c_str(), FormatMoney(tmp_median_mm_minask).c_str());
            }

            BOOST_FOREACH(PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
            {
                int64 tmp_price = st.second.feed_price;
                int64 tmp_volume = st.second.nGems;
                if (tmp_volume > 0)
                {
                    feedcache_volume_total += tmp_volume;
                    if ((tmp_price > 0) && (st.second.feed_chronon > tmp_oldexp_chronon))
                    {
                        feedcache_volume_participation += tmp_volume;

                        if (tmp_price > outState.feed_nextexp_price)      feedcache_volume_bull += tmp_volume;
                        else if (tmp_price < outState.feed_nextexp_price) feedcache_volume_bear += tmp_volume;
                        else                                           feedcache_volume_neutral += tmp_volume;
                    }

                    // market maker
                    if (outState.nHeight >= AUX_MINHEIGHT_TRADE(fTestNet))
                    if (st.second.ex_vote_mm_limits > 0)
                    {
                        int64 tmp_max_bid = 0;
                        int64 tmp_min_ask = 0;
                        MM_ORDERLIMIT_UNPACK(st.second.ex_vote_mm_limits, tmp_max_bid, tmp_min_ask);

                        if ((tmp_max_bid > 0) && (tmp_min_ask > 0))
                        {
                            mmlimitcache_volume_participation += tmp_volume;

                            if (tmp_max_bid > tmp_median_mm_maxbid)      mmmaxbidcache_volume_bull += tmp_volume;
                            else if (tmp_max_bid < tmp_median_mm_maxbid) mmmaxbidcache_volume_bear += tmp_volume;
                            else                                      mmmaxbidcache_volume_neutral += tmp_volume;

                            if (tmp_min_ask > tmp_median_mm_minask)      mmminaskcache_volume_bull += tmp_volume;
                            else if (tmp_min_ask < tmp_median_mm_minask) mmminaskcache_volume_bear += tmp_volume;
                            else                                      mmminaskcache_volume_neutral += tmp_volume;
                        }
                    }
                }
            }

            // market maker
            if (mmmaxbidcache_volume_bull > mmmaxbidcache_volume_bear + mmmaxbidcache_volume_neutral)
              tmp_median_mm_maxbid = tradecache_pricetick_up(tmp_median_mm_maxbid);
            else if (mmmaxbidcache_volume_bear > mmmaxbidcache_volume_bull + mmmaxbidcache_volume_neutral)
              tmp_median_mm_maxbid = tradecache_pricetick_down(tmp_median_mm_maxbid);

            if (mmminaskcache_volume_bull > mmminaskcache_volume_bear + mmminaskcache_volume_neutral)
              tmp_median_mm_minask = tradecache_pricetick_up(tmp_median_mm_minask);
            else if (mmminaskcache_volume_bear > mmminaskcache_volume_bull + mmminaskcache_volume_neutral)
              tmp_median_mm_minask = tradecache_pricetick_down(tmp_median_mm_minask);

            MM_ORDERLIMIT_PACK(outState.crd_mm_orderlimits, tmp_median_mm_maxbid, tmp_median_mm_minask);
//            printf("MM test: median limits unpacked (updated) bid %s ask %s\n", FormatMoney(tmp_median_mm_maxbid).c_str(), FormatMoney(tmp_median_mm_minask).c_str());
//            printf("MM test: median limits packed %s\n", FormatMoney(outState.crd_mm_orderlimits).c_str());
        }
        if (feedcache_status == FEEDCACHE_EXPIRY)
        {
          BOOST_FOREACH(PAIRTYPE(const std::string, StorageVault) &st, outState.vault)
          {
            int64 tmp_price = st.second.feed_price;
            int64 tmp_volume = st.second.nGems;
            if ((tmp_volume > 0) && ((st.second.feed_chronon > tmp_oldexp_chronon)))
            {
                if ((tmp_price > outState.feed_nextexp_price * 0.95) &&
                    (tmp_price < outState.feed_nextexp_price * 1.05))
                {
                    st.second.vaultflags |= VAULTFLAG_FEED_REWARD;
                    feedcache_volume_reward += tmp_volume;
                }
            }
          }
        }

        // update median feed price
        if (feedcache_status == FEEDCACHE_NORMAL)
        {
            if (feedcache_volume_bull > feedcache_volume_bear + feedcache_volume_neutral)
                outState.feed_nextexp_price = feedcache_pricetick_up(outState.feed_nextexp_price);
            else if (feedcache_volume_bear > feedcache_volume_bull + feedcache_volume_neutral)
                outState.feed_nextexp_price = feedcache_pricetick_down(outState.feed_nextexp_price);
        }

        if (feedcache_status)
        {
            // reward for price feed, and reward for providing liquidity: each 1/3 of gems that spawn on map
            if (outState.nHeight % GEM_RESET_INTERVAL(fTestNet) == 0)
            {
                outState.feed_reward_remaining += 34000000;
                outState.liquidity_reward_remaining += 34000000;
            }
        }
        if (feedcache_status == FEEDCACHE_EXPIRY)
        {
//            outState.feed_reward_dividend = outState.feed_reward_remaining / COIN / 2; // distribute half of your gems
//            outState.feed_reward_divisor = feedcache_volume_reward / COIN;
            outState.feed_reward_dividend = outState.feed_reward_remaining / CENT / 2; // distribute half of your gems
            outState.feed_reward_divisor = feedcache_volume_reward / CENT;
        }
    }
#endif

#ifdef PERMANENT_LUGGAGE
    if (GEM_ALLOW_SPAWN(fTestNet, outState.nHeight))
    {
      BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.players)
      {
        // update player name address on record in gamestate if reward address was updated in previous block
        // or if we just found a gem
        int64 tmp_gems = 0;
        int64 tmp_new_gems = 0;
        bool tmp_disconnect_storage = false;
        bool tmp_reward_addr_set = (p.second.address.length() > 1); // (IsValidBitcoinAddress(p.second.address)) fast enough?

#ifdef RPG_OUTFIT_ITEMS
        int64 tmp_new_outfit = 0;
        unsigned char tmp_outfit = 0;
        for (int i = 0; i < RPG_NUM_OUTFITS; i++)
        {
            if ((outfit_cache[i]) && (outfit_cache_name[i] == p.first))
            {
//                tmp_new_outfit = 1<<i;
                if (i == 0) tmp_new_outfit = 1;
                else if (i == 1) tmp_new_outfit = 2;
                else if (i == 2) tmp_new_outfit = 4;
                p.second.playerflags |= PLAYER_FOUND_ITEM;
            }

        }
#endif

        // found a gem
        if ((outState.gemSpawnState == GEM_HARVESTING) &&
            (gem_cache_winner_name == p.first))
        {
            p.second.playerflags |= PLAYER_FOUND_ITEM;
            tmp_new_gems = GEM_NORMAL_VALUE;
        }
#ifdef PERMANENT_LUGGAGE_AUCTION
        // bought a gem
        if ((outState.auction_last_chronon == outState.nHeight) &&
            (auctioncache_bid_name == p.first))
        {
            p.second.playerflags |= PLAYER_BOUGHT_ITEM;
            tmp_new_gems = auctioncache_bid_size;

            // liquidity reward
            // 2% when filling the best ask (if best ask was not modified for almost a day on maínnet)
            // but 10% if best ask price is not higher than collateral value (dragging it down)
            if (auctioncache_bestask_chronon < outState.nHeight - GEM_RESET_INTERVAL(fTestNet))
            {
                int64 tmp_r = outState.liquidity_reward_remaining;
                if (tmp_new_gems < tmp_r) tmp_r = tmp_new_gems;
                tmp_r /= 10;
                if (auctioncache_bestask_price > outState.auction_settle_price) tmp_r /= 5;
                tmp_r -= (tmp_r % CENT);

                tmp_new_gems += tmp_r;
                outState.liquidity_reward_remaining -= tmp_r;
            }
        }
#endif
        if (p.second.playerflags)
        {
          int f_pf = 0;
          if (p.second.playerflags & PLAYER_TRANSFERRED)  f_pf = PLAYER_TRANSFERRED1;
          else if (p.second.playerflags & PLAYER_TRANSFERRED1) f_pf = PLAYER_TRANSFERRED2;
          else if (p.second.playerflags & PLAYER_TRANSFERRED2) f_pf = PLAYER_TRANSFERRED3;
          if (p.second.playerflags & PLAYER_DO_PURSE)
          {
            // possibly true if an hunter never moved but found a gem (spawned at gem position)?
            if (!p.second.playernameaddress.empty())
            {
                if (tmp_reward_addr_set)
                {
                    if (p.second.playernameaddress == p.second.address)
                    {
                        // connect to storage if reward address is set and same as name address
                        tmp_disconnect_storage = false;
                        Huntermsg_cache_address = p.second.playernameaddress;
                    }
                    else
                    {
                        // reward address different than name address, found gems will go to reward address
                        tmp_disconnect_storage = true;
                        Huntermsg_cache_address = p.second.address;
                    }
                }
                // no reward address: disconnect, gems stored with playernameaddress
                else
                {
                    tmp_disconnect_storage = true;
                    Huntermsg_cache_address = p.second.playernameaddress;
                }


                if (true)
                {
                    // already have a storage
                    // was:              if (outState.vault.count(Huntermsg_cache_address) > 0)
                    std::map<std::string, StorageVault>::iterator mi = outState.vault.find(Huntermsg_cache_address);
                    if (mi != outState.vault.end())
                    {
                        if (tmp_new_gems)
                        {
                            // was:                        outState.vault[Huntermsg_cache_address] += tmp_new_gems;
                            mi->second.nGems += tmp_new_gems;
                            mi->second.huntername = p.first;

                            printf("luggage test: %s added item(s) to storage %s\n", p.first.c_str(), Huntermsg_cache_address.c_str());
                            if (tmp_disconnect_storage) printf("luggage test: storage is disconnected\n");
                        }
#ifdef RPG_OUTFIT_ITEMS
                        else if (tmp_new_outfit)
                        {
                            mi->second.item_outfit = tmp_new_outfit;
                            mi->second.huntername = p.first;
                        }
#endif

                        if (!tmp_disconnect_storage)
                        {
                            // was:                       tmp_gems = outState.gems[Huntermsg_cache_address];
                            tmp_gems = mi->second.nGems;
#ifdef RPG_OUTFIT_ITEMS
                            tmp_outfit = mi->second.item_outfit;
#endif

                            printf("luggage test: %s retrieved item(s) from storage %s\n", p.first.c_str(), Huntermsg_cache_address.c_str());
                            printf("luggage test: %15"PRI64d" gem sats found\n", tmp_gems);
                        }
                    }
                    // need storage for new gem
                    else if (tmp_new_gems > GEM_ONETIME_STORAGE_FEE)
                    {
                        tmp_new_gems -= GEM_ONETIME_STORAGE_FEE;

                        // was:                        outState.gems.insert(std::pair<std::string,int64>(Huntermsg_cache_address, tmp_new_gems));
                        outState.vault.insert(std::make_pair(Huntermsg_cache_address, StorageVault(tmp_new_gems)));

                        std::map<std::string, StorageVault>::iterator mi2 = outState.vault.find(Huntermsg_cache_address);
                        if (mi2 != outState.vault.end())
                        {
                            mi2->second.huntername = p.first;
                        }

                        // probably faster version:
//                        std::pair<std::map<std::string, StorageVault>::iterator,bool> ret;
//                        ret = outState.vault.insert(std::make_pair(Huntermsg_cache_address, StorageVault(tmp_new_gems)));
//                        if (ret.second == true)
//                        {
//                            ret.first->second.huntername = p.first;
//                        }

                        tmp_gems = tmp_new_gems;
                        printf("luggage test: gem found, new storage for name %s, addr %s\n", p.first.c_str(), Huntermsg_cache_address.c_str());
                    }
                    else
                    {
                        printf("luggage test: there is no storage for name %s, addr %s\n", p.first.c_str(), Huntermsg_cache_address.c_str());
                    }
                }
            }
            else
            {
                printf("luggage test: ERROR: no addr for name %s\n", p.first.c_str());
            }
          }
          p.second.playerflags = f_pf;
        }

        if ((tmp_disconnect_storage) || (tmp_gems))
        {
            BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
            {
                int i = pc.first;
                CharacterState &ch = pc.second;

                if (tmp_disconnect_storage)
                {
                    if (ch.rpg_gems_in_purse > 0)
                    {
                        ch.rpg_gems_in_purse = 0;
                        printf("luggage test: storage disconnected, name %s, idx %d\n", p.first.c_str(), i);
                    }
                }
#ifdef RPG_OUTFIT_ITEMS
                else if ((tmp_outfit) || (tmp_gems))
                {
                    if (tmp_gems < 0)
                        tmp_gems = 0;
                    ch.rpg_gems_in_purse = tmp_gems + tmp_outfit;
                    tmp_gems = 0;
                    tmp_outfit = 0;
                }
#else
                else if (tmp_gems)
                {
                    ch.rpg_gems_in_purse = tmp_gems;
                    tmp_gems = 0;
                }
#endif
            }
        }
      }
    }
#endif

    bool respawn_crown = false;
    outState.UpdateCrownState(respawn_crown);

    // Caution: banking must not depend on the randomized events, because they depend on the hash -
    // miners won't be able to compute tax amount if it depends on the hash.

    // Banking
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.players)
        BOOST_FOREACH(PAIRTYPE(const int, CharacterState) &pc, p.second.characters)
        {
            int i = pc.first;
            CharacterState &ch = pc.second;

            if (ch.loot.nAmount > 0 && outState.IsBank (ch.coord))
            {
                // Tax from banking: 10%
                int64_t nTax = ch.loot.nAmount / 10;
                stepResult.nTaxAmount += nTax;
                ch.loot.nAmount -= nTax;

                CollectedBounty b(p.first, i, ch.loot, p.second.address);
                stepResult.bounties.push_back (b);
                ch.loot = CollectedLootInfo();
            }
        }

    // Miners set hashBlock to 0 in order to compute tax and include it into the coinbase.
    // At this point the tax is fully computed, so we can return.
    if (outState.hashBlock == 0)
        return true;

    RandomGenerator rnd(outState.hashBlock);

    /* Decide about whether or not this will be a disaster.  It should be
       the first action done with the RNG, so that it is possible to
       verify whether or not a block hash leads to a disaster
       relatively easily.  */
    const bool isDisaster = outState.CheckForDisaster (rnd);
    if (isDisaster)
      {
        printf ("DISASTER @%d!\n", outState.nHeight);
        outState.ApplyDisaster (rnd);
        assert (outState.nHeight == outState.nDisasterHeight);
      }

    /* Transfer life from attacks.  This is done randomly, but the decision
       about who dies is non-random and already set above.  */
    if (ForkInEffect (FORK_LIFESTEAL, outState.nHeight))
      attackedTiles.DistributeDrawnLife (rnd, outState);

    // Spawn new players
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        if (m.IsSpawn())
            m.ApplySpawn(outState, rnd);

    // Apply address & message updates
    BOOST_FOREACH(const Move &m, stepData.vMoves)
        m.ApplyCommon(outState);

    /* In the (rare) case that a player collected a bounty, is still alive
       and changed the reward address at the same time, make sure that the
       bounty is paid to the new address to match the old network behaviour.  */
    BOOST_FOREACH(CollectedBounty& bounty, stepResult.bounties)
      bounty.UpdateAddress (outState);

    // Set colors for dead players, so their messages can be shown in the chat window
    BOOST_FOREACH(PAIRTYPE(const PlayerID, PlayerState) &p, outState.dead_players_chat)
    {
        std::map<PlayerID, PlayerState>::const_iterator mi = inState.players.find(p.first);
        assert(mi != inState.players.end());
        const PlayerState &pl = mi->second;
        p.second.color = pl.color;
    }

    // Drop a random rewards onto the harvest areas
    const int64_t nCrownBonus
      = CROWN_BONUS * stepData.nTreasureAmount / TOTAL_HARVEST;
    int64_t nTotalTreasure = 0;
    for (int i = 0; i < NUM_HARVEST_AREAS; i++)
    {
        int a = rnd.GetIntRnd(HarvestAreaSizes[i]);
        Coord harvest(HarvestAreas[i][2 * a], HarvestAreas[i][2 * a + 1]);
        const int64_t nTreasure
          = HarvestPortions[i] * stepData.nTreasureAmount / TOTAL_HARVEST;
        outState.AddLoot(harvest, nTreasure);
        nTotalTreasure += nTreasure;
    }
    assert(nTotalTreasure + nCrownBonus == stepData.nTreasureAmount);

    // Players collect loot
    outState.DivideLootAmongPlayers();
    outState.CrownBonus(nCrownBonus);

    /* Update the banks.  */
    outState.UpdateBanks (rnd);

    /* Drop heart onto the map.  They are not dropped onto the original
       spawn area for historical reasons.  After the life-steal fork,
       we simply remove this check (there are no hearts anyway).  */
    if (DropHeart (outState.nHeight))
    {
        assert (!ForkInEffect (FORK_LIFESTEAL, outState.nHeight));

        Coord heart;
        do
        {
            heart.x = rnd.GetIntRnd(MAP_WIDTH);
            heart.y = rnd.GetIntRnd(MAP_HEIGHT);
        } while (!IsWalkable(heart) || IsOriginalSpawnArea (heart));
        outState.hearts.insert(heart);
    }

    outState.CollectHearts(rnd);
    outState.CollectCrown(rnd, respawn_crown);

    /* Compute total money out of the game world via bounties paid.  */
    int64_t moneyOut = stepResult.nTaxAmount;
    BOOST_FOREACH(const CollectedBounty& b, stepResult.bounties)
      moneyOut += b.loot.nAmount;

    /* Compare total money before and after the step.  If there is a mismatch,
       we have a bug in the logic.  Better not accept the new game state.  */
    const int64_t moneyBefore = inState.GetCoinsOnMap () + inState.gameFund;
    const int64_t moneyAfter = outState.GetCoinsOnMap () + outState.gameFund;
    if (moneyBefore + stepData.nTreasureAmount + moneyIn
          != moneyAfter + moneyOut)
      {
        printf ("Old game state: %lld (@%d)\n", moneyBefore, inState.nHeight);
        printf ("New game state: %lld\n", moneyAfter);
        printf ("Money in:  %lld\n", moneyIn);
        printf ("Money out: %lld\n", moneyOut);
        printf ("Treasure placed: %lld\n", stepData.nTreasureAmount);
        return error ("total amount before and after step mismatch");
      }

#ifdef GUI
    // pending tx monitor -- acoustic alarm
    bool do_sound_alarm = false;
    if (pmon_noisy)
    {
        for (int m = 0; m < PMON_MY_MAX; m++)
        {
            if (pmon_my_alarm_state[m] == 1)
            {
                 pmon_my_alarm_state[m] = 2;
                 do_sound_alarm = true;
            }
        }

        if (do_sound_alarm)
        {
            {
                boost::filesystem::path pathDebug = boost::filesystem::path(GetDataDir()) / "small_wave_file.wav";

                // Open file with the associated application
                if (boost::filesystem::exists(pathDebug))
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(pathDebug.string())));
            }
        }
    }
#endif

#ifdef PERMANENT_LUGGAGE_OR_GUI
    // gems and storage
    if (GEM_ALLOW_SPAWN(fTestNet, outState.nHeight))
    {
      char buf[2] = { '\0', '\0' };
      std::string s = outState.hashBlock.ToString();
      buf[0] = s.at(s.length() - 1);
      int h = strtol(buf, NULL, 16);

      if ((GEM_RESET(fTestNet, outState.nHeight)) ||
          (GEM_RESET_HOTFIX(fTestNet, outState.nHeight)))
      {
        gem_visualonly_state = GEM_SPAWNED;
        gem_cache_winner_name = "";

        int idx_sp = (h & 4) ? 0 : 1;
        gem_visualonly_x = gem_spawnpoint_x[idx_sp];
        gem_visualonly_y = gem_spawnpoint_y[idx_sp];

#ifdef PERMANENT_LUGGAGE
        outState.gemSpawnState = gem_visualonly_state;
        outState.gemSpawnPos.x = gem_visualonly_x;
        outState.gemSpawnPos.y = gem_visualonly_y;
#endif
      }
      else
      {
#ifdef PERMANENT_LUGGAGE
        if (outState.gemSpawnState == GEM_HARVESTING)
            outState.gemSpawnState = GEM_UNKNOWN_HUNTER; // the hunter will keep track of their new gem,
                                                         // and "visualonly state" will (try to) keep track of the blue icon
#endif
        if ((gem_visualonly_state == GEM_HARVESTING) || (gem_visualonly_state == GEM_ININVENTORY))
            gem_visualonly_state = GEM_UNKNOWN_HUNTER;
        else if (gem_visualonly_state == GEM_UNKNOWN_HUNTER)
            gem_visualonly_state = 0;
      }
      printf("luggage test: nHeight %d, hex digit %d, spawn state %d, xy %d %d\n", outState.nHeight, h, gem_visualonly_state, gem_visualonly_x, gem_visualonly_y);
    }
#endif

#ifdef AUX_STORAGE_VERSION2
    // market maker -- spawn
    if (outState.nHeight == AUX_MINHEIGHT_TRADE(fTestNet))
    {
        std::string s = "npc.marketmaker.zeSoKxK3rp3dX3it1Y";

        // formula from PERMANENT_LUGGAGE_TALLY
        int tmp_intervals = (outState.nHeight - AUX_MINHEIGHT_FEED(fTestNet)) / GEM_RESET_INTERVAL(fTestNet);
        int64 tmp_new_gems = (int64)(GEM_NORMAL_VALUE * tmp_intervals) * 7 / 3 + 700000000;
        tmp_new_gems -= (tmp_new_gems % 1000000);

        outState.vault.insert(std::make_pair(s, StorageVault(tmp_new_gems)));

        std::map<std::string, StorageVault>::iterator mi2 = outState.vault.find(s);
        if (mi2 != outState.vault.end())
            mi2->second.huntername = "Sox'xiti";
    }
#endif

    return true;
}
