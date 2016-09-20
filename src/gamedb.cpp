#include "gamedb.h"
#include "gamestate.h"
#include "gametx.h"

#include "headers.h"
#include "huntercoin.h"

#include <boost/filesystem.hpp>

#include <list>
#include <map>

using namespace Game;

static const int KEEP_EVERY_NTH_STATE = 2000;
static const unsigned IN_MEMORY_STATE_CACHE = 10;

class CGameDB : public CDB
{
public:
#ifdef AUX_STORAGE_VERSION4
    CGameDB(const char* pszMode="r+") : CDB("game_sv4.dat", pszMode) { }

    CGameDB(const char* pszMode, CDB& parent) : CDB("game_sv4.dat", pszMode)
#else
#ifdef AUX_STORAGE_VERSION3
    CGameDB(const char* pszMode="r+") : CDB("game_sv3.dat", pszMode) { }

    CGameDB(const char* pszMode, CDB& parent) : CDB("game_sv3.dat", pszMode)
#else
    CGameDB(const char* pszMode="r+") : CDB("game.dat", pszMode) { }

    CGameDB(const char* pszMode, CDB& parent) : CDB("game.dat", pszMode)
#endif
#endif
    {
      vTxn.push_back (parent.GetTxn ());
      ownTxn.push_back (false);
    }

    inline bool
    Exists (unsigned nHeight) 
    {
      return CDB::Exists (nHeight);
    }

    bool Read(unsigned int nHeight, GameState &gameState)
    {
        return CDB::Read(nHeight, gameState);
    }

    bool Write(unsigned int nHeight, const GameState &gameState)
    {
        return CDB::Write(nHeight, gameState);
    }

    bool Erase(unsigned int nHeight)
    {
        return CDB::Erase(nHeight);
    }
};

class GameStepValidator
{
    bool fOwnState;
    bool fOwnDb;
    
    DatabaseSet* pdbset;

    // Detect duplicates (multiple moves per block). Probably already handled by NameDB and not needed.
    std::set<PlayerID> dup;

    // Temporary variables
    vchType vchName;
    vchType vchValue;

protected:
    const GameState *pstate;

public:
    GameStepValidator(const GameState *pstate_)
        : fOwnState(false), fOwnDb(false), pdbset(NULL), pstate(pstate_)
    {
    }

    GameStepValidator(DatabaseSet& dbset, CBlockIndex *pindex)
        : fOwnState(true), fOwnDb(false), pdbset(&dbset)
    {
        GameState *newState = new GameState;
        if (!GetGameState (dbset, pindex, *newState))
        {
            delete newState;
            throw std::runtime_error("GameStepValidator : cannot get previous game state");
        }
        pstate = newState;
    }

    ~GameStepValidator()
    {
      if (fOwnState)
        delete pstate;
      if (pdbset && fOwnDb)
        delete pdbset;
    }

    // Returns:
    //   false - invalid move tx
    //   true  - non-move tx or valid tx
    bool IsValid(const CTransaction& tx, Move &outMove)
    {
        if (tx.nVersion != NAMECOIN_TX_VERSION)
        {
#ifdef AUX_STORAGE_VOTING
            int64 tmp_tag_official = 0;
            int64 tmp_tag_min = 0;
            int64 tmp_tag_max = 0;
            int64 tmp_txid60bit = 0;
            if (pstate->nHeight >= AUX_MINHEIGHT_VOTING(fTestNet))
            {
                int tmp_blocks = (pstate->nHeight % AUX_VOTING_INTERVAL(fTestNet));
                tmp_tag_official = pstate->nHeight - tmp_blocks + AUX_VOTING_INTERVAL(fTestNet) - 25; // xxx9975 (testnet: xxx975)
                tmp_tag_min = pstate->nHeight + 1;
                tmp_tag_max = pstate->nHeight + (AUX_VOTING_INTERVAL(fTestNet) * 2) - 25;
                if ((tmp_tag_official < tmp_tag_min) || (tmp_tag_official > tmp_tag_max)) tmp_tag_official == 0;

                char buf[100];
                strncpy(buf, tx.GetHash().GetHex().c_str(), 64);
                buf[15] = '\0';
                tmp_txid60bit = strtoll (buf, NULL, 16);

                if (votingcache_idx)
                {
                    BOOST_FOREACH(const CTxIn& txin, tx.vin)
                    {
                        if (!tx.IsCoinBase())
                        {
                            if (!tx.IsGameTx())
                            {
                                strncpy(buf, txin.prevout.hash.GetHex().c_str(), 64);
                                buf[15] = '\0';
                                int64 tmp_txid60bit_input = strtoll (buf, NULL, 16);

                                printf("scanning votes: scanning input, txid60bit_input: %15"PRI64d"\n", tmp_txid60bit_input);

                                for (int i = 0; i < votingcache_idx; i++)
                                {
                                    if (votingcache_txid60bit[i] == tmp_txid60bit_input)
                                    {
                                        votingcache_amount[i] = -1;
                                        printf("scanning votes: delete vote: vault address %s\n", votingcache_vault_addr[i].c_str(), FormatMoney(votingcache_amount[i]).c_str());
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
#endif
#ifdef PERMANENT_LUGGAGE_AUCTION
            // gems and storage
            for (unsigned int i = 0; i < tx.vout.size(); i++)
            {
                const CTxOut& txout = tx.vout[i];
                int64 nSingleValueOut = txout.nValue;
                std::string address;
                CScript script(txout.scriptPubKey);
#ifdef AUX_STORAGE_TAGTEST
                std::string tag;
                if (txout.scriptPubKey.GetTag (tag))
                {
                    printf("luggage test: tag %s\n", tag.c_str());
                }
#endif
                if (ExtractDestination(script, address))
                {
                    bool fv = false;

                    if (pstate->vault.count(address) > 0)
                    {
                        if (paymentcache_idx < PAYMENTCACHE_MAX)
                        {
                            paymentcache_vault_addr[paymentcache_idx] = address;
                            paymentcache_amount[paymentcache_idx] = nSingleValueOut;
                            paymentcache_idx++;
                        }
                        printf("luggage test: storage address %s received payment: %15"PRI64d" ", address.c_str(), nSingleValueOut);
#ifdef AUX_STORAGE_TAGTEST
                        std::string tag;
                        if (txout.scriptPubKey.GetTag (tag))
                        {
                            printf(" tag %s", tag.c_str());
                        }
#endif
                        printf(" height %d, block hash %s\n", pstate->nHeight, pstate->hashBlock.GetHex().c_str());

#ifdef AUX_STORAGE_VOTING
#ifdef AUX_STORAGE_ZHUNT
                        // "zhunt orders" work somewhat different than "votes"
                        // - spending these coins (or the change) will not delete it
                        // - vault must already exist
                        if ((pstate->nHeight >= AUX_MINHEIGHT_ZHUNT(fTestNet)) &&
                            (nSingleValueOut % 10000 == 5501) && (nSingleValueOut >= 3000 * COIN) &&
                            (votingcache_idx < VOTINGCACHE_MAX))
                        {
                            votingcache_vault_addr[votingcache_idx] = address;
                            votingcache_vault_exists[votingcache_idx] = true;
                            votingcache_amount[votingcache_idx] = nSingleValueOut;
                            votingcache_txid60bit[votingcache_idx] = tmp_txid60bit;
                            votingcache_idx++;

                            printf("zhunt order cached (existing vault)\n");
                        }
                        else
#endif
                        if (pstate->nHeight >= AUX_MINHEIGHT_VOTING(fTestNet))
                        {
                            if ((nSingleValueOut % 10000000 >= tmp_tag_min) && (nSingleValueOut % 10000000 <= tmp_tag_max) &&
                                (nSingleValueOut >= 1000 * COIN))
                            {
                                fv = true;
                            }

                            if ((fv) && (votingcache_idx < VOTINGCACHE_MAX))
                            {
                                votingcache_vault_addr[votingcache_idx] = address;
                                votingcache_vault_exists[votingcache_idx] = true;
                                votingcache_amount[votingcache_idx] = nSingleValueOut;
                                votingcache_txid60bit[votingcache_idx] = tmp_txid60bit;
                                votingcache_idx++;

                                printf("scanning votes: cached (existing vault)\n");
                            }
                        }
#endif
                    }
#ifdef AUX_STORAGE_VOTING
                    else if ((pstate->nHeight >= AUX_MINHEIGHT_VOTING(fTestNet)) &&
                             (tmp_tag_official) && (nSingleValueOut % 10000000 == tmp_tag_official) &&
                             (nSingleValueOut >= 25000 * COIN))
                    {
                        fv = true;

                        if ((fv) && (votingcache_idx < VOTINGCACHE_MAX))
                        {
                            votingcache_vault_addr[votingcache_idx] = address;
                            votingcache_vault_exists[votingcache_idx] = false;
                            votingcache_amount[votingcache_idx] = nSingleValueOut;
                            votingcache_txid60bit[votingcache_idx] = tmp_txid60bit;
                            votingcache_idx++;

                            printf("luggage test: address %s received payment: %15"PRI64d" ", address.c_str(), nSingleValueOut);
                            printf(" height %d, block hash %s\n", pstate->nHeight, pstate->hashBlock.GetHex().c_str());
                            printf("scanning votes: cached (need new vault)\n");
                        }
                    }
#endif
#ifdef PERMANENT_LUGGAGE_LOG_PAYMENTS
                    else
                    {
                        printf("luggage test: address %s received payment: %15"PRI64d" ", address.c_str(), nSingleValueOut);
                        printf(" height %d, block hash %s\n", pstate->nHeight, pstate->hashBlock.GetHex().c_str());
                    }
#endif
                }
            }
#endif

            return true;
        }

        std::vector<vchType> vvchArgs;
        int op, nOut;
        if (!DecodeNameTx (tx, op, nOut, vvchArgs))
          return error ("GameStepValidator: could not decode a name tx");

        vchType vchName, vchValue;
        switch (op)
        {
        case OP_NAME_FIRSTUPDATE:
          vchName = vvchArgs[0];
          if (vvchArgs.size () == 3)
            vchValue = vvchArgs[2];
          else
            {
              assert (vvchArgs.size () == 2);
              vchValue = vvchArgs[1];
            }
          break;

        case OP_NAME_UPDATE:
          vchName = vvchArgs[0];
          vchValue = vvchArgs[1];
          break;

        case OP_NAME_NEW:
          return true;

        default:
          return error ("GameStepValidator: invalid name tx found");
        }

        const std::string sName = stringFromVch(vchName);
        const std::string sValue = stringFromVch(vchValue);
        if (dup.count(sName))
            return error ("GameStepValidator: duplicate player name %s",
                          sName.c_str ());
        dup.insert(sName);

        Move m;
        m.newLocked = tx.vout[nOut].nValue;

        m.Parse(sName, sValue);
        if (!m)
            return error("GameStepValidator: cannot parse move %s for player %s", sValue.c_str(), sName.c_str());
        if (!m.IsValid(*pstate))
            return error("GameStepValidator: invalid move for the game state: move %s for player %s", sValue.c_str(), sName.c_str());

        if (m.IsSpawn ())
          {
            if (op != OP_NAME_FIRSTUPDATE)
              return error ("GameStepValidator: spawn is not firstupdate");
          }
        else if (op != OP_NAME_UPDATE)
          return error ("GameStepValidator: name_firstupdate is not spawn");

        std::string addressLock = m.AddressOperationPermission(*pstate);

#ifdef GUI
        // pending tx monitor
        if ((pmon_tx_count) && (pmon_why_validate == WHYVALIDATE_CONNECTBLOCK))
        {
            for (int k2 = 0; k2 < pmon_tx_count; k2++)
            {
                if (pmon_tx_names[k2] == sName)
                {
                    pmon_tx_names[k2] = "";
                    break;
                }
            }
        }
#endif
#ifdef PERMANENT_LUGGAGE
        // gems and storage
        if (GEM_ALLOW_SPAWN(fTestNet, pstate->nHeight))
        {
            std::string sa;
            GetNameAddress(tx, sa);
            m.playernameaddress = sa;
            printf("luggage test: move for name %s, playernameaddr %s\n", sName.c_str(), sa.c_str());
        }
#endif

        if (!addressLock.empty())
        {
            // If one of inputs has address equal to addressLock, then that input has been signed by the address owner
            // and thus authorizes the address change operation
            bool found = false;
            if (!pdbset)
            {
                pdbset = new DatabaseSet("r");
                fOwnDb = true;
            }
            for (int i = 0; i < tx.vin.size(); i++)
            {
                COutPoint prevout = tx.vin[i].prevout;
                CTransaction txPrev;
                CTxIndex txindex;
                if (!pdbset->tx ().ReadTxIndex (prevout.hash, txindex)
                    || txindex.pos == CDiskTxPos(1,1,1))
                    continue;
                else if (!txPrev.ReadFromDisk(txindex.pos))
                    continue;
                if (prevout.n >= txPrev.vout.size())
                    continue;
                const CTxOut &vout = txPrev.vout[prevout.n];
                std::string address;
                if (ExtractDestination(vout.scriptPubKey, address) && address == addressLock)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return error("GameStepValidator: address operation permission denied: move %s for player %s", sValue.c_str(), sName.c_str());
        }
        outMove = m;
        return true;
    }

    bool IsValid(const CTransaction& tx)
    {
        Move m;
        if (!IsValid(tx, m))
            return false;
        return true;
    }
};

bool
IsMoveValid (const GameState& state, const CTransaction& tx)
{
  GameStepValidator validator(&state);
  return validator.IsValid (tx);
}

static void InitStepData(StepData &stepData, const GameState &state)
{
    const int64_t nSubsidy = GetBlockValue(state.nHeight + 1, 0);
    // Miner subsidy is 10%, thus game treasure is 9 times the subsidy
    stepData.nTreasureAmount = nSubsidy * 9;
}

class GameStepMinerImpl : public GameStepValidator
{
    StepData stepData;
public:
    GameStepMinerImpl (DatabaseSet& dbset, CBlockIndex *pindex)
      : GameStepValidator (dbset, pindex)
    {
      InitStepData (stepData, *pstate);
    }

    bool AddTx(const CTransaction& tx)
    {
        Move m;
        if (!IsValid(tx, m))
            return false;
        if (m)
            stepData.vMoves.push_back(m);

        return true;
    }

    int64 ComputeTax()
    {
        StepResult stepResult;
        Game::GameState outState;
        if (!Game::PerformStep(*pstate, stepData, outState, stepResult))
        {
            error("GameStepMinerImpl::ComputeTax failed");
            return 0;
        }

        return stepResult.nTaxAmount;
    }
};

// A simple wrapper (pImpl pattern) to remove dependency on the game-related headers when miner just wants to check transactions
// (declared in hooks.h)
GameStepMiner::GameStepMiner (DatabaseSet& dbset, CBlockIndex *pindex)
  : pImpl(new GameStepMinerImpl(dbset, pindex))
{}

GameStepMiner::~GameStepMiner()
{
    delete pImpl;
}

bool GameStepMiner::AddTx(const CTransaction& tx)
{
    return pImpl->AddTx(tx);
}

int64 GameStepMiner::ComputeTax()
{
    return pImpl->ComputeTax();
}

bool
PerformStep (CNameDB& nameDb, const GameState& inState, const CBlock* block,
             int64& nTax, GameState& outState,
             std::vector<CTransaction>* outvgametx)
{
    if (block->hashPrevBlock != inState.hashBlock)
        return error("PerformStep: game state for wrong block");

    StepData stepData;
    InitStepData(stepData, inState);
    stepData.newHash = block->GetHash();

#ifdef PERMANENT_LUGGAGE_AUCTION
    paymentcache_instate_blockhash = inState.hashBlock;
    paymentcache_idx = 0;
#endif
#ifdef AUX_STORAGE_VOTING
    if (inState.nHeight >= AUX_MINHEIGHT_VOTING(fTestNet))
    {
      votingcache_instate_blockhash = inState.hashBlock;
      votingcache_idx = 0;
      BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, inState.vault)
      {
        if ((st.second.vote_raw_amount > 0) || (st.second.vote_txid60bit > 0))
        {
            int64 tmp_txid60bit = st.second.vote_txid60bit;
            if (votingcache_idx < VOTINGCACHE_MAX)
            {
                votingcache_vault_addr[votingcache_idx] = st.first;
                votingcache_vault_exists[votingcache_idx] = true;
                votingcache_amount[votingcache_idx] = 0;

                // cleanup
                if (int(st.second.vote_raw_amount % 10000000) < inState.nHeight - AUX_VOTING_CLEANUP_PERIOD(fTestNet))
                    votingcache_amount[votingcache_idx] = -1;

                votingcache_txid60bit[votingcache_idx] = tmp_txid60bit;
                votingcache_idx++;

                printf("scanning votes: existing vote, addr %s, amount %15"PRI64d", txid60bit %15"PRI64d"\n", st.first.c_str(), st.second.vote_raw_amount, tmp_txid60bit);
            }
        }
      }
      printf("scanning votes: %d existing votes, height %d\n", votingcache_idx, inState.nHeight);
    }
#endif

    GameStepValidator gameStepValidator(&inState);
    // Create moves for all move transactions
    BOOST_FOREACH(const CTransaction& tx, block->vtx)
    {
        Move m;
        if (!gameStepValidator.IsValid(tx, m))
            return error("GameStepValidator rejected transaction %s in block %s", tx.GetHash().ToString().substr(0,10).c_str(), block->GetHash().ToString().c_str());
        if (m)
            stepData.vMoves.push_back(m);
    }

    StepResult stepResult;
    if (!Game::PerformStep(inState, stepData, outState, stepResult))
        return error("PerformStep failed for block %s", block->GetHash().ToString().c_str());

    nTax = stepResult.nTaxAmount;

    if (!outvgametx)
      return true;

    return CreateGameTransactions (nameDb, outState, stepResult, *outvgametx);
}

/* ************************************************************************** */
/* GameStateCache.  */

/**
 * This class holds a cache of recently calculated game states just in memory
 * (this is never written to disk) and can be used to get the current state
 * as well as very recent "old" ones efficiently (without recalculation).
 * The recent (but not current) states are necessary to perform efficient
 * reorganisations after orphan blocks.
 */
class GameStateCache
{

private:

  /** Type used for the map blockhash -> state.  */
  typedef std::map<uint256, GameState*> gameStateMap;

  /** Map holding the data.  */
  gameStateMap map;

  /** Maximum size, after which elements are pruned.  */
  unsigned maxSize;

public:

  /**
   * Construct it empty.
   * @param sz Maximum size after which we remove old entries.
   */
  inline GameStateCache (unsigned sz)
    : map(), maxSize(sz)
  {}

  /**
   * Destruct and free all memory.
   */
  ~GameStateCache ();

  /**
   * Retrieve a game state if it is stored.
   * @param hash Block hash for which we want the state.
   * @return Pointer to stored state or NULL.
   */
  inline const GameState*
  query (const uint256& hash) const
  {
    const gameStateMap::const_iterator i = map.find (hash);
    if (i == map.end ())
      return NULL;

    return i->second;
  }

  /**
   * Retrieve a game state if it is stored.
   * @param hash Block hash for which we want the state.
   * @param out Write the game state here.
   * @return True iff the state was found.
   */
  inline bool
  query (const uint256& hash, GameState& out) const
  {
    const GameState* ptr = query (hash);
    if (!ptr)
      return false;

    out = *ptr;
    return true;
  }

  /**
   * Insert the given game state into the cache.
   * @param state Game state to store.
   */
  void store (const GameState& state);

};

GameStateCache::~GameStateCache ()
{
  for (gameStateMap::iterator i = map.begin (); i != map.end (); ++i)
    delete i->second;
}

void
GameStateCache::store (const GameState& state)
{
  gameStateMap::iterator i;

  /* See if the state is there first, and overwrite it if yes.  */
  i = map.find (state.hashBlock);
  if (i != map.end ())
    {
      *i->second = state;
      return;
    }

  /* Insert the new entry.  */
  printf ("GameStateCache: storing for block @%d %s\n",
          state.nHeight, state.hashBlock.GetHex ().c_str ());
  map.insert (std::make_pair (state.hashBlock, new GameState (state)));

  /* Drop entries until we reach the maximal size goal.  */
  while (map.size () > maxSize)
    {
      bool deleted = false;

      /* See if there are entries for blocks not on the main chain.  Remove
         those first.  */
      for (i = map.begin (); i != map.end () && !deleted; ++i)
        {
          std::map<uint256, CBlockIndex*>::const_iterator j;
          j = mapBlockIndex.find (i->second->hashBlock);

          if (j == mapBlockIndex.end () || !j->second->IsInMainChain ())
            {
              if (j == mapBlockIndex.end ())
                printf ("Warning: Block in GameStateCache not found in"
                        " mapBlockIndex.  Removing.\n");

              printf ("GameStateCache: removing block %s not in main chain\n", 
                      i->second->hashBlock.GetHex ().c_str ());

              delete i->second;
              map.erase (i);
              deleted = true;
            }
        }
      
      /* If we already deleted something, check size again.  */
      if (deleted)
        continue;

      /* Remove entry with lowest block height.  */

      gameStateMap::iterator bestPosition = map.end ();
      for (i = map.begin (); i != map.end (); ++i)
        {
          if (bestPosition == map.end ()
              || i->second->nHeight < bestPosition->second->nHeight)
            bestPosition = i;
        }
      assert (bestPosition != map.end ());
      printf ("GameStateCache: removing block with lowest height %d\n",
              bestPosition->second->nHeight);

      delete bestPosition->second;
      map.erase (bestPosition);
    }
}

/** Our game state cache instance.  */
static GameStateCache stateCache(IN_MEMORY_STATE_CACHE);

/* ************************************************************************** */

// Caller must hold cs_main lock
const GameState& GetCurrentGameState()
{
    const GameState* state;

    /* If the state is in the cache, return it immediately.  */
    state = stateCache.query (*pindexBest->phashBlock);
    if (state)
      return *state;

    /* Else, calulate the state.  */
    GameState cur;
    DatabaseSet dbset("r");
    GetGameState (dbset, pindexBest, cur);

    /* If it is still not in the cache, store it explicitly.  */
    state = stateCache.query (*pindexBest->phashBlock);
    if (state)
      return *state;
    stateCache.store (cur);

    /* Finally, it should indeed be there.  */
    state = stateCache.query (*pindexBest->phashBlock);
    assert (state);

    return *state;
}

// pindex must belong to the main branch, i.e. corresponding blocks must be connected
// Returns a copy of the game state
bool
GetGameState (DatabaseSet& dbset, CBlockIndex *pindex, GameState &outState)
{
    if (!pindex)
    {
        outState = GameState();
        return true;
    }

    /* See if we have the block in the state cache.  */
    if (stateCache.query (*pindex->phashBlock, outState))
      return true;

    // Get the latest saved state
    CGameDB gameDb("r", dbset.tx ());

    if (gameDb.Read(pindex->nHeight, outState))
    {
        if (outState.nHeight != pindex->nHeight)
            return error("GetGameState: wrong height");
        if (outState.hashBlock != *pindex->phashBlock)
            return error("GetGameState: wrong hash");
        return true;
    }

    if (!pindex->IsInMainChain())
        return error("GetGameState called for non-main chain");

    printf("%s ", DateTimeStrFormat("%x %H:%M:%S", GetTime()).c_str());
    printf("GetGameState: need to integrate state for height %d (current %d)\n",
           pindex->nHeight, nBestHeight);

    CBlockIndex *plast = pindex;
    GameState lastState;
    for (; plast->pprev; plast = plast->pprev)
    {
        if (stateCache.query (*plast->pprev->phashBlock, lastState))
            break;
        if (gameDb.Read(plast->pprev->nHeight, lastState))
            break;
    }

    printf("%s ", DateTimeStrFormat("%x %H:%M:%S", GetTime()).c_str());
    printf("GetGameState: last saved block has height %d\n", lastState.nHeight);

    // Integrate steps starting from the last saved state
    // FIXME: Might want to store intermediate steps in stateCache, too.
    loop
    {
        CBlock block;
        block.ReadFromDisk(plast);

        int64 nTax;
        if (!PerformStep (dbset.name (), lastState, &block, nTax, outState))
            return false;
        if (outState.nHeight != plast->nHeight)
            return error("GetGameState: wrong height");
        if (outState.hashBlock != *plast->phashBlock)
            return error("GetGameState: wrong hash");
        if (plast == pindex)
            break;
        plast = plast->pnext;
        lastState = outState;

        /* Write the state to DB.  This is done during integration already
           so that it is ensured that every other state is stored even
           if the game db is reconstructed from scratch.  (Otherwise,
           it would only contain the last state in that case.)  */
        if (outState.nHeight % KEEP_EVERY_NTH_STATE == 0)
          {
            CGameDB gameDb("r+", dbset.tx ());
            gameDb.Write(outState.nHeight, outState);
            printf ("Saved game state @%d to database.\n", outState.nHeight);
          }
    }

    /* Store into game state cache.  */
    stateCache.store (outState);

    printf("%s ", DateTimeStrFormat("%x %H:%M:%S", GetTime()).c_str());
    printf("GetGameState: done integrating\n");

    return true;
}

// Called from ConnectBlock
bool
AdvanceGameState (DatabaseSet& dbset, CBlockIndex* pindex,
                  CBlock* block, int64& nFees)
{
    GameState currentState, outState;

    if (!GetGameState (dbset, pindex->pprev, currentState))
        return error("AdvanceGameState: cannot get current game state");

    if (currentState.nHeight != pindex->nHeight - 1)
        return error("AdvanceGameState: incorrect height encountered");
    if (currentState.hashBlock != block->hashPrevBlock)
        return error("AdvanceGameState: incorrect hash encountered");

    int64 nTax = 0;

    if (!PerformStep (dbset.name (), currentState, block, nTax,
                      outState, &block->vgametx))
      return false;

    if (outState.nHeight != pindex->nHeight)
        return error("AdvanceGameState: incorrect height stored");
    if (outState.hashBlock != *pindex->phashBlock)
        return error("AdvanceGameState: incorrect hash stored");

    /* Create the db if necessary.  This is the case when we attach
       the genesis block initially in LoadBlockIndex.  */
    CGameDB gameDb("cr+", dbset.tx ());

    gameDb.Write(pindex->nHeight, outState);
    // Prune old states from DB, keeping every Nth for quick lookup (intermediate states can be obtained by integrating blocks)
    if (pindex->nHeight - 1 <= 0 || (pindex->nHeight - 1) % KEEP_EVERY_NTH_STATE != 0)
        gameDb.Erase(pindex->nHeight - 1);

    nFees += nTax;

    return true;
}

// Called from DisconnectBlock
void RollbackGameState(CTxDB& txdb, CBlockIndex* pindex)
{
    if (!pindex->IsInMainChain())
    {
        error("RollbackGameState called for non-main chain");
        return;
    }

    CGameDB("r+", txdb).Erase(pindex->nHeight);
}

extern CWallet* pwalletMain;

// Erase unconfirmed transactions, that should not be re-broadcasted, because they're not
// valid for the current game state (e.g. attack on some player who's already killed)
void EraseBadMoveTransactions()
{
    /* If we do not have a wallet yet, nothing to do.  This can happen
       with a completely fresh initialisation.  */
    if (!pwalletMain)
        return;

    CRITICAL_BLOCK(cs_main)
    CRITICAL_BLOCK(pwalletMain->cs_mapWallet)
    {
        std::map<uint256, CWalletTx> mapRemove;
        std::vector<unsigned char> vchName;

        GameStepValidator gameStepValidator(&GetCurrentGameState ());

        {
            DatabaseSet dbset("r");
            BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
            {
                CWalletTx& wtx = item.second;

                if (wtx.GetDepthInMainChain () < 1
                    && (IsConflictedTx (dbset, wtx, vchName)
                        || !gameStepValidator.IsValid (wtx)))
                  mapRemove[wtx.GetHash()] = wtx;
            }
        }

        if (mapRemove.empty())
            return;

        int countRemove = mapRemove.size();
        printf("EraseBadMoveTransactions : erasing %d transactions\n", countRemove);

        bool fRepeat = true;
        while (fRepeat)
        {
            fRepeat = false;
            BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, pwalletMain->mapWallet)
            {
                CWalletTx& wtx = item.second;
                BOOST_FOREACH(const CTxIn& txin, wtx.vin)
                {
                    uint256 hash = wtx.GetHash();

                    // If this tx depends on a tx to be removed, remove it too
                    if (mapRemove.count(txin.prevout.hash) && !mapRemove.count(hash))
                    {
                        mapRemove[hash] = wtx;
                        fRepeat = true;
                    }
                }
            }
        }
        if (mapRemove.size() > countRemove)
            printf("EraseBadMoveTransactions : erasing additional %d dependent transactions\n", mapRemove.size() - countRemove);

        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapRemove)
        {
            CWalletTx& wtx = item.second;

            UnspendInputs(wtx);
            wtx.RemoveFromMemoryPool();
            pwalletMain->EraseFromWallet(wtx.GetHash());
            if (GetNameOfTx(wtx, vchName) && mapNamePending.count(vchName))
            {
                std::string name = stringFromVch(vchName);
                printf("EraseBadMoveTransactions : erase %s from pending of name %s",
                        wtx.GetHash().GetHex().c_str(), name.c_str());
                if (!mapNamePending[vchName].erase(wtx.GetHash()))
                    error("EraseBadMoveTransactions : erase but it was not pending");
            }
            wtx.print();
        }
    }
}

void
PruneGameDB (unsigned nHeight)
{
  CGameDB gameDb("r+");

  std::set<unsigned> toRemove;
  unsigned cnt = 0;
  unsigned last = 0;
  for (unsigned i = 0; i < nHeight; ++i)
    if (gameDb.Exists (i))
      {
        ++cnt;
        toRemove.insert (i);
        last = i;
      }
  if (cnt > 0)
    {
      toRemove.erase (last);
      --cnt;
    }

  printf ("Pruning %d game states before %d from the GameDB...\n", cnt, last);

  BOOST_FOREACH(unsigned i, toRemove)
    gameDb.Erase (i);

  gameDb.Rewrite ();
}

bool UpgradeGameDB()
{
    int nGameDbVersion = VERSION;

    {
        CGameDB gameDb("cr");
        gameDb.ReadVersion(nGameDbVersion);
        gameDb.Close();
    }

    /* If the version is too old, recreate it from scratch.  The change in
       question is the addition of the "coins lost due to crown" field,
       which requires to re-integrate all game states anyway.  */
    if (nGameDbVersion < 1001100)
      {
        printf ("Re-creating GameDB from scratch...\n");

        DBFlush (false);
        boost::filesystem::path fileGame;
#ifdef AUX_STORAGE_VERSION4
        fileGame = boost::filesystem::path (GetDataDir ()) / "game_sv4.dat";
#else
#ifdef AUX_STORAGE_VERSION3
        fileGame = boost::filesystem::path (GetDataDir ()) / "game_sv3.dat";
#else
        fileGame = boost::filesystem::path (GetDataDir ()) / "game.dat";
#endif
#endif
        boost::filesystem::remove (fileGame);

        CGameDB gameDb("cr+");
        if (!gameDb.WriteVersion (VERSION))
          return error ("WriteVersion failed for new game DB.");
        gameDb.Close ();

        GetCurrentGameState ();
        return true;
      }

    /* Upgrade the game state format in-place if this is possible.  */
    if (nGameDbVersion < 1030000)
    {
        printf("Updating GameDB...\n");

        CGameDB gameDb("r+");

        GameState state;
        for (unsigned int i = 0; i <= nBestHeight; i++)
        {
            gameDb.SetSerialisationVersion (nGameDbVersion);
            if (gameDb.Read(i, state))
            {
                state.UpdateVersion (nGameDbVersion);
                gameDb.SetSerialisationVersion (VERSION);
                if (!gameDb.Write(i, state))
                    return false;
            }
        }

        if (!gameDb.WriteVersion(VERSION))
            return false;
        printf("GameDB updated\n");
    }

    return true;
}
