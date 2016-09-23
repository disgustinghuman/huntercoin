Source
======

https://github.com/wiggi/huntercoin


Binaries
========

huntercoin-betterQt-exp-binaries-20160922.zip, 31.7 MB
https://mega.nz/#!WVdixSzL!Jxkf-ijnnvBtSifVR0CQNoTutgyRzc6GyndJlPBqc_E

  for Windows:
  - Safemode

  for Windows, Linux Mint13/Ubuntu12.04 32bit, Linux Mint17.3/Ubuntu14.04 32+64bit, Linux Mint18/Ubuntu16.04 64bit
  - "Self defense for hunters" experimental build
  - uses alternative gamestate game_sv4.dat (included, client can make its own but first start
    would take ~5 hours on mainnet if game_sv4.dat is not in the same folder as game.dat)


New: instant creation of storage vaults (20160923)
enabled on mainnet after block 1400000
======================================

(Cortez's test continues, first part was conversion of gems to coins at a fixed rate, see
https://bitcointalk.org/index.php?topic=435170.msg16312993#msg16312993)

After block 1400000, it will be possible to convert coins to gems at a fixed rate,
instantly, trustless, and with a single (atomic) transaction.

While Cortez's Huntercoin address (HMFESBYnkoTHYVtyMyFVDFXQG5R1nkAzZX) still belongs to the
same user (wiggi), it's also a game entity now, and can act independently
with no need of a specific node being online.


Example use (see also "help sendtoaddress" in the client's console window):

to send gems to the storage vault of "some_huntercoinaddress" (will be created it not already existing):
sendtoaddress HMFESBYnkoTHYVtyMyFVDFXQG5R1nkAzZX coin_amount comment1 comment2 "GEM some_huntercoinaddress"

to donate 1k coins worth of gems:
sendtoaddress HMFESBYnkoTHYVtyMyFVDFXQG5R1nkAzZX 1000.0 comment1 comment2 "GEM HMFESBYnkoTHYVtyMyFVDFXQG5R1nkAzZX"


Notes:
- the coin:gem rate is variable and taken from the in-game auction settlement
- standard fee of 0.02 gems for storage vault generation applies
- this function uses the "transaction comment", aka OP_RETURN
- in Huntercore client and daemon, sendtoaddress command is different.
  They probably can't be used for this right now.

- Each gem bought will result in -1 "immature gems from trade P/L" for Cortez, with a hard limit of -100
- the total amount of gems in existence will be unchanged (but short term spike of gems in circulation is possible)
- The adventurers inventory page (adventurers.txt) will state the available amount,
  recalculated for each block


New: Huntercoin creatures (20160923)
enabled on mainnet after block 1400000
======================================

Map layout:
https://bitcointalk.org/index.php?topic=435170.msg16257618#msg16257618

General creature role:
lemure     take gems, kill zombies
zoombie    take gems, teleport

The crunch:

to spawn a creature:
- send coins to your own Huntercoin address
  amount >3000 coins, must end with the magic number "5501",
  the 4 digits left and the 4 digits right of the decimal point will become the "order"
- the order is the *only* way to control a creature
- address must be a storage vault key (see adventurers.txt) with no open order or voting
- address must own at least 0.04 gems (non-refundable fee per creature)

order:
1st digit: 3..spawn zombie
           4..spawn lemure
           5-9 also zombie
2nd digit  spawnpoint (0-2 for lemures, 0-5 for zombies)
3rd digit  chance to change direction while shuffling along (in 1/16)
4th digit  fireball range, and zombie detection range (lemures)
           distance of nearest lemure that make them use one of the six teleporters (zombies, if in range of said teleporter)
5th digit  distance of nearest lemure that make them wait (zombies, if in range of a teleporter)
6-8th digit not used

mana:
all creatures start with 100 mana
lemure     fireball cost (range*range) per use
zombie     teleportation cost 5 per use

life:
lemure     start with 100, ticks down 1 per chronon
           killing a zombie will replenish 100 life for a lemure up to a maximum of 200
zombie     start with 100, ticks down 1 per chronon if waiting in range of a teleporter (minimum of 1, they can't die this way)

death condition:
lemure     life=0
zombie     if fireballed
all        expire 10000 blocks after spawn

all creatures can "call dibs" on a gem if nearer than 5 tiles
this instantly gives the gem and replenishes mana to 100
"Tia's ghost" gem is the same as the one from Tia'tha (taking one let the other disappear)

teleporters can only be used by zombies, and only if nearer than 12 tiles to the entrance (the tile with the bright teleporter glow)
all teleporters have a fixed exit (faint glow), exit tiles are the same as the zombie spawnpoints

Monster infighting:
2 lemures, if in spell range, will pelt each other with a weak lightning attack for (1d8 - 1) damage per chronon.


New: better AI (20160826)
see "Additional config options in names.txt" below
==================================================

    config:afk_defence 2           use the first waypoint from an enemy hunter's pending unconfirmed move for calculations
                                   if the pending transaction is older than "config:afk_ticks_confirm"
                                   (highly experimental, only in binary release)

    config:afk_defence 4           grab nearby coins if idle, and no enemy is in alarm range, every X chronons.
                                   (X == min(30, distance to nearest unknown hunter))

    config:afk_defence 7           do all of the above


New: Additional config options in names.txt:
show movement of unknown hunters as floating arrows (20160705)
==============================================================

    config:show_wps 1              show next position (adjacent tile)

    config:show_wps 2              show first waypoint
                                   (arrow direction == direction of the next step)

    config:show_wps 4              show destination (final waypoint)

    config:show_wps 7              default 7, do all of the above


New: Bitasset transfer (20160605)
=================================


VAULT: raze                   Chat message command: Transfer all items to the storage vault at the hunter's reward address,
                              and delete the old storage vault at the hunter's name address
                              (but a new vault can be created later with the same address as key)

                              The command will silently fail if the reward address is not already a storage vault key.
                              (reward address can be set using the same transaction)

                              Razing will release 1/2 of the vault creation cost.

                              console versions:
                              name_update Alice {\"msg\":\"VAULT: raze\"}
                              name_update Alice {\"msg\":\"VAULT: raze\",\"address\":\"H................................\"}

VAULT: transfer 1.0 gems      Chat message command: Transfer the specified amount of gems (must be multiple of 0.01) to the hunter's reward address.
                              The command will silently fail if the reward address is not already a storage vault key.
                              (reward address can be set using the same transaction)

                              console versions:
                              name_update Alice {\"msg\":\"VAULT: transfer 1.0 gems\"}
                              name_update Alice {\"msg\":\"VAULT: transfer 1.0 gems\",\"address\":\"H................................\"}

notes:

Only the blockchain is parsed to determine whether a transfer is executed, not client input.
All inputs are standard Huntercoin transactions

The transfers are irrevocable, done 1 block after the tx is in the blockchain,
and in case of a chain reorg they will be handled correctly like any other gamestate data


New: Hit+Run AI (20160523)
==========================

  Purpose:
  - in thick melee (like 3 vs 3 in the center) it's better to keep focus on hunter positions and movement, and to leave
    the exact timing of "destruct" to the software.
  - attacking players will be less able to steal time and human attention


  Set a single waypoint (grey line) to become this hunter's "Hit+Run point".
  The client will determine 1 out of 3 possible states:

  "punch through"                            This hunter is cornered by the nearest enemy. Upon contact, the hunter
                                             will observe config:afk_ticks_hold, send destruct, and move to the Hit+Run point.

  "hit+run"                                  Enemy is attacking us from a direction that allows to send destruct immediately,
                                             and move away from it to the Hit+Run point.

  "cornered?"                                looks like a "punch through" situation, but not clear because the enemy is too far away


  Any click on an unwalkable tile will clear the current Hit+Run point. However,
  it's (probably) never a disadvantage to have a Hit+Run point set, no matter the coordinates.


New: Auction Bot (20160503)
===========================

  The purpose is to make buying in the Gem:Huntercoin in-game auction easier and safer and
  to provide a "hidden bid".

  Config options (in names.txt)

    config:auctionbot_hunter_name #Alice     name of the hunter "on duty" (players can't trade in Huntercoin, only hunters)
                                             default ""

                                             IMPORTANT NOTE: comment out the name (i.e. #Alice instead of Alice) if not in use

    config:auctionbot_trade_price 100        bid limit price (in coins)

    config:auctionbot_trade_size 0.1         bid size (in gems)

    config:auctionbot_limit_coins 0          session limit (in coins)
                                             default 0
                                             This is the maximum amount of coins to spent until the client is restarted.
                                             (0 means the auction bot is not active)

  The bot will stop trading under a number of conditions that can happen in the dangerous game world of Huntercoin.
  Current status of the bot is in auction.txt, updated each block. Restarting the tx monitor (i.e. click middle mouse button 2 times)
  will also restart the auction bot.


Additional config options in names.txt:
=======================================

    config:afk_defence 1           hunters have the capability to "destruct" in self defense,
                                   other hunters listed in names.txt are seen as friendlies
                                   but no attempt is made to avoid collateral damage, default 1

    config:afk_ticks_hold 5        wait time after a block was received,
                                   on "CONTACT" the player has by default 20 seconds to override
                                   (by either sending a move, or middle mouse button to switch the tx monitor off)
                                   default 5

    config:afk_ticks_confirm 7     1/2 of the estimated time a transaction needs to confirm,
                                   default 7


Additional config options in names.txt:
(Windows only)
=======================================

    config:dbg_win32_qt_threads 12    Windows stability bug workaround (force some threads to wait for each other)
                                      0...off
                                      12...default
                                      28...same as 12, print stats in debug.log

Pending Transaction Monitor
===========================

  The "pending tx monitor" can reroute output from the console command "name_pending" to GUI

  input:
    middle mouse button       start tx monitor (default update interval: 5 seconds)

    name_pending 5            "legacy" console command to start with update interval every 5 seconds
                              (min update interval is 2 seconds, console is not available while it runs)

    middle mouse button       stop tx monitor

    Ctrl+middle mouse button  toggle silent mode

    names.txt                 up to 48 names of "friendly" hunters + distance to trigger alarm. Changes are effective after stopping and restarting the tx monitor.
                              - if name is in names.txt, hunters ignore each other for alarm purposes
                              - it's not required that all friendlies are controlled by the same node/wallet

  output:
    small_wave_file.wav      played (by asking the OS to open it) on alarm, must be in same folder as debug.log (not included, windows system sounds work just fine)

    *ALARM*: <name> [<name>]  after all names from names.txt in case of alarm
    <n> min: <name>           after all names from names.txt, longest idle hunter (out of waypoints for n minutes)
    (OK)                      after all names from names.txt, in case of no alarm and no idle hunter

    Full: <name>              after all names from names.txt, if reached maximum loot (carrying capacity)
    Bank: <name>              after all names from names.txt, if reached 50% of carrying capacity, and a bank is nearby
                              (see "config:bank_notice" and "config:loot_notice")

    x,y->x2,y2               after hunter name, coors currently and expected after next block
    wp:...                   after hunter name, next waypoint
    tx*<age>:...             after hunter name, value of pendig tx, and how "old" it is
    CONTACT*<age>            we will be in destruct range with an enemy player after next block. If started with update interval of 5 seconds,
                             then "CONTACT*4" or "CONTACT*5" (20 or 25 seconds) is basically seeing the white in their eyes


Config options (in names.txt)
=============================

    config:overview_zoom 20        minimum zoom level to display "where-are-my-hunters" marker
                                   (zoom level is 10...200, 10 means completely zoomed out)
                                   default 20

    config:afk_leave_map 60        hunters will leave the map (if they notice a bank), default 0
                                   - recommended value 60 (if bank_notice is smaller, it will inrease at a rate of 1 per tick)
                                   - nothing is done if the hunter's last waypoint is already a bank tile

    config:bank_notice 10          maximum distance to notice a bank if an hunter can walk there in a straight line, default 0

    config:loot_notice 50          show 'Bank' reminder if an hunter carries more coins, default 50

    config:warn_stalled 36         red blinking lights on hunters if last block is older than n "ticks",
                                   default 36 (normally 3 minutes)

    config:warn_disaster 50        green blinking lights on hunters for n blocks after disaster, default 50


Safemode, advanced mode, gems and in-game auction
=================================================

Safemode:       uses regular game.dat

Advanced mode:  compiled with "#define PERMANENT_LUGGAGE" for additional variables in the gamestate (game_sv4.dat)
                -> copy game_sv4.dat to the same folder as game.dat,
                   if no game_sv4.dat is present, a new one is generated
                   (takes ~15 minutes on testnet, ~5 hours on mainnet)

Gems:           a NPC by the name of "Tia'tha" will spawn every 1242 chronons near the middle of the right or left map border
                (1 free gem, and 1 free storage vault for the first hunter on same tile),
                and in advanced mode can be found near the blue water announcing the next gem spawn.

                In safemode, the client is amnesiac about current gem spawn state if restarted.

Storage vaults: Item storage for hunters, not affected in case of death or disaster. Player reward address is used as "vault key" if possible,
                or player name address otherwise (this is the same for bought and "harvested" items).
                Cost of creating a new vault is 0.02 gems.

                The blue icon means that an hunter picked up the last spawned gem (safemode),
                or owns at least 1.00 gems (advanced mode)
                - set reward address to something different than name address: storage is "closed"
                - set reward address to same as name address: storage is "opened", amount displayed after hunter name
                (indicative, only the list in adventurers.txt is always up to date)

                Another hunter can later inherit the inventory if they transfer to the "storage key address".

                If exported from an empty wallet (console command: dumpprivkey <storage key address>), storage keys
                can be imported without rescan (console command: importprivkey <privkey> <huntername or other label> false).

Auction tool:   auction.txt is updated for new blocks if tx monitor was started,
                with example lines to communicate with the game for each possible action:

                - send a sell order (limited to 1 open order per vault, only possible if player name address is a vault key,
                  and the hunter was not transferred during the last 3 blocks),
                  the order is active until filled, with automatic price down-tick every 100 blocks
                - modify a sell order (size 0 == cancel)
                - send a "Fill-or-Kill" buy order (every hunter can do that)
                - send coins to actually execute the buy order

                  IMPORTANT NOTE: The system will ignore partial payments, if not confirmed before timeout,
                                  or if not parsable (multisignature or other non-simple transaction).
                                  see "Precautions" below

                - cast a vote about HUC price (once every 10000 blocks, for a reward if within +-5% of median)

                Limits for gems:
                 size: 0.1 minimum
                 price: 1.00...1000000.00, but "settlement price" is minimum
                        Settlement price will tick down if best ask is not higher, or tick up otherwise.


Precautions
===========

on Windows:            nodes may (randomly after some hours) stop receiving data, and need restart
                       (this takes less time if "Detach databases at shutdown" is not checked)

                       for a really reliable Huntercoin node it's recommended to use Oracle VirtualBox, e.g:

                       available                         virtual disk space
                       RAM (host)        RAM (guest)     (fixed size)           virtual machine OS

                       >= 8 GB           2 GB            35 GB                  Linux Mint 17.3LTS, 64bit, Mate
                       4 GB              1.5 GB          35 GB                  Linux Mint 17.3LTS or 13LTS, 32bit, Mate

if using the auction:  before sending coins
                       - backup wallet in console window (i.e. "backupwallet wallet.dat")
                       - if connection is lost in this moment or the transaction doesn't confirm normally,
                         shut down, discard the original wallet and use the backup.

pending tx monitor:    if either "(OK)", the idle time "n min" or "*ALARM*" is displayed for the hunter
                       last in the list of hunters in names.txt, then the entire list was correctly parsed

                       it's dangerous to have valid names of hunters in the list when these hunters are not currently alive,
                       hostiles with same name would go undetected (e.g. "#Bob" is not a valid hunter name)


Other Resources
===============

hunttest-playground-20151226.zip, 19.3 MB
https://mega.nz/#!fUMWFRAZ!mYceRV0s91iokbMvkN6_vSKKbIzroi_WnnPa0uOhbeY

  Testnet in a box, with lots of stuff to salvage for Huntercoin
  - more player and monster sprites
  - monster pathfinding
  - an in-game exchange that can be modified to do >100 trades per block without becoming a resource hog
  - NPCs, items, ranged combat with spells, and more


Asciiartmap editing guide
=========================

note: Up to 7 map tiles can be placed on top of each other (1 terrain layer, 3 shadow layers, and 3 normal layers
      for map objects like trees, cliffs or palisades)

      It's possible that the number of layers is insufficient (e.g. dense forest). In this case all tiles for 1 entire map object
      will be skipped. If only the number of shadow layers is insufficient, 1 visually unimportant part of the shadow will be skipped,
      but the map object is rendered.


Terrain
-------

0   grass terrain
1   grass terrain, unwalkable (no difference to '0', because hardcoded ObstacleMap is used to determine whether a tile is walkable)

.   "dirt" terrain

w   muddy water
W   blue water

O   cobblestone
o   cobblestone
Q   cobblestone
q   cobblestone
8   cobblestone


    grass terrain tiles adjacent to '.' are automatically painted with grass/dirt transition tiles

    "dirt" terrain tiles adjacent to cobblestone are automatically painted with cobblestone/dirt transition tiles


Grass
-----

"   high green grass (semi-random offset)
'   same as above, but green-to-yellow
v   same as above, but red (2 versions chosen at random)
y   same as above, but yellow (2 versions chosen at random)

    these grass tiles have their own small shadow baked in and (unlike boulders) don't use up shadow layers


Trees
-----

B   broadleaf, dark (T for smaller version, to be used as "no clip" object)
b   broadleaf, bright (t for smaller version)
C   conifer, dark (F for smaller version)
c   conifer, bright (f for smaller version)

00000
00000
001B0       <- broadleaf tree: the 'B' tile is 1 tile right of the "unwalkable" center
00000
001C0       <- conifer tree: 2 unwalkable tiles
00000


Trees (or rocks, or high grass) stand visually on "dirt terrain" if
the tile under the tree is dirt, otherwise they would stand on grass terrain.

00000000000
000H0G00000    <- menhir and boulder on grass terrain
00B00G00"00    <- tree, boulder and high grass on dirt terrain
00.00.00.00
00000000000


Rocks
-----

G   boulder, dark
g   boulder, bright
H   menhir, dark
h   menhir, bright


Palisades + Gate
------------------

P   palisade, bird looking left
p   palisade, bird looking right
U   gate


Terrain (on cliff)
------------------

;   sand terrain on cliffs
;   alternative version
,   alternative version

    grass terrain tiles adjacent to ';', ':' or ',' are automatically painted with grass/sand transition tiles
    (unfinished, expect glitches)

+   filler (painted as grass, but normally not visible)
#   filler


Cliffs
------

cliff from CLIFFVEG.bmp

  v-v-------- 2 versions of normal "column" ('!', '?' and '|', '_') can be mixed randomly

           v---- conifer tree, need 1 tile distance to cliff (if on right side)

             v----- broadleaf tree, need 2 tiles distance to cliff (if on right side)
1?__??_?_1
1:;,:;,:,1                                                              <----  3 versions of cliff "dirt" (';', ':' and ',') can be mixed randomly
<;:;,:;;:> C       <---- special version of "line" ('<', '>')           <----
(,:,,::,;)                                                              <----
{++++++++}  B      <--- 2 versions of normal "line" ('(', ')' and '{', '}') can be mixed randomly
(++++++++)         <---
[!||!!|!|]C
  B   C            <--- trees

inverse cliff (cliff corners from CLIFVEG2.bmp)

 l|!|!||r
1000000001
}00000000(
)00000000{
}00000000(
)00000000(
)L_?_?__R{


cliff with special pieces from CLIFVEG2.bmp

                               ##?##
1?__??_?_1    1?__??_?_1    1?_#/?#\_1
1:;,:;,:,1    1:;,:;,:,1    1:;,:;,:,1
<;:;,:;;:>    <;:;,:;;:>    <;:;,:;;:>
(,:,,::,;)    (,:,,::,;)    (,:,,::,;)
{++++++++}    {++++:+++}    {++++++++}
(++++++++)    (++++,+++)    (++++++++)
[!|##!##|]    [!|##+##|]    [!||!!|!|]
   #S!#s         ##+##
                 #Z!#z


grass tiles on cliff        Trees on cliff stand visually on "sand" if
need adjacent               the tile under the tree is sand, otherwise they would stand on grass terrain.
"sand on cliff" tiles       (same for rocks or high grass)

                              C B
1?__??_?_1                  1?__??_?_1
1:;,:;,:,1                  1:;,C;B:,1
<;000000:>                  <;:;,:;;:>
(,000000;)                  iC:c,::,;J
{,00000;+}                  I;+;++++Cj
(+,:;++++)                 Bi+++++++;J
[!|##!##|]                  m!||!!|!|]
   #S!#s
                                     ^-- right side of cliff: tiles painted in terrain layer if 'J' and 'j' is used instead of ')' and '}'
                                         (otherwise shadow tiles from the adjacent tree would not be visible)

                            ^-- left side of cliff: tiles painted in terrain layer if 'I', 'i' and 'm' is used instead of '(' and '{' and '['
                                (otherwise parts of the adjacent conifer tree and its shadow would not be visible)

