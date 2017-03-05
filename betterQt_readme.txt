Source
------

https://github.com/wiggi/huntercoin


Latest Windows build
--------------------

huntercoin-qt-v140-win32-20170304.zip, 14.6 MB
https://mega.nz/#!rAdgxKIS!I3zoMMrGptIPjzFxzlMccAN_d3Ov_XAgH0qw7_iO598


Start betterQt functions:
-------------------------

* Wait until blocks are synced, then open the game tab, then click middle mouse button.
  Middle mouse button will toggle all betterQt-specific functions.

* If an hunter is moving, their next tile, next waypoint and final destination will be shown as floating arrows.

* If an hunter has a pending, unconfirmed transaction, the new move is displayed in plain text.


Optional: acoustic alarm, hit+run points and auto destruct:
-----------------------------------------------------------

* To use these, copy the 3 files from "acoustic alarm sounds" to Huntercoin's data folder.
  (I.e. same folder where debug.log is. Click "Help, Debug window, Open debug log file" to find it)

* Add a line in adv_names.txt for each currently used hunter.
  This line must contain the hunter's name and 4 parameters separated by space (and nothing else)
  1. parameter:  acoustic alarm distance (recommended: 5...9)
  2. parameter:  color (0...3)
  3. parameter:  client will not override moves sent by player for a number of blocks
  4. parameter:  if valid address, spawn the hunter on that address and set as reward address
                 (ignored if not a valid address)
  e.g. "Bob 9 0 0 0" without the quotation marks.

* After editing adv_names.txt, click middle mouse button 2 times to stop and restart all betterQt functions.
  This will reload adv_names.txt.

* If a hostile hunter is nearby, click on a (nearby) tile in a direction away from the enemy.
  A floating "hit+run" arrow will appear.

* If the text on the arrow says "cornered?" instead of "hit+run", try a different tile.
  (the client can do this automatically, see "config:afk_defence")

* In case of contact with the hostile hunter, the client will try to kill this enemy
  (without having to click "Destruct" and "Go" button).


betterQt function: Pending Transaction Monitor
----------------------------------------------

  The "pending tx monitor" can display unconfirmed transactios for all hunters in map view:

  input:
    middle mouse button       start tx monitor (default update interval: 5 seconds)

    name_pending 5            "legacy" console command to start with update interval every 5 seconds
                              (min update interval is 2 seconds, console is not available while it runs)

    middle mouse button       stop tx monitor

    Ctrl+middle mouse button  toggle silent mode

    adv_names.txt             up to 60 names of "friendly" hunters + distance to trigger alarm. Changes are effective after stopping and restarting the tx monitor.
                              - if name is in adv_names.txt, hunters ignore each other for alarm purposes
                              - it's not required that all friendlies are controlled by the same node/wallet

  output:
    small_wave_file.wav      played (by asking the OS to open it) on alarm, must be in same folder as debug.log (not included, windows system sounds work just fine)

    *ALARM*: <name> [<name>]  after all names from adv_names.txt in case of alarm
    <n> min: <name>           after all names from adv_names.txt, longest idle hunter (out of waypoints for n minutes)
    (OK)                      after all names from adv_names.txt, in case of no alarm and no idle hunter

    Full: <name>              after all names from adv_names.txt, if reached maximum loot (carrying capacity)
    Bank: <name>              after all names from adv_names.txt, if reached 50% of carrying capacity, and a bank is nearby
                              (see "config:bank_notice" and "config:loot_notice")

    x,y->x2,y2               after hunter name, coors currently and expected after next block
    wp:...                   after hunter name, next waypoint
    tx*<age>:...             after hunter name, value of pendig tx, and how "old" it is
    CONTACT*<age>            we will be in destruct range with an enemy player after next block. If started with update interval of 5 seconds,
                             then "CONTACT*4" or "CONTACT*5" (20 or 25 seconds) is basically seeing the white in their eyes


betterQt function: Hit+Run AI
-----------------------------

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

  Any click on an unwalkable tile will clear the current Hit+Run point.


Hit+Run AI config options in adv_config.txt:
-------------------------------------------

    config:afk_defence 1           (or any value >0) hunters have the capability to "destruct" in self defense,
                                   other hunters listed in adv_names.txt are seen as friendlies
                                   but no attempt is made to avoid collateral damage, default 1

    config:afk_defence 2           use the first waypoint from an enemy hunter's pending unconfirmed move for calculations
                                   if the pending transaction is older than "config:afk_ticks_confirm"
                                   (only in binary release, experimental and currently not recommended,
                                   use high values of config:afk_ticks_confirm to disable)

                                   automatically correct player mistakes when setting hit+run points,
                                   so that the new point is on a player spawn strip tile

    config:afk_defence 4           automatically correct player mistakes when setting hit+run points,
                                   so that the new point is on a player spawn strip tile
                                   (different algorithm than "config:afk_defence 2")

    config:afk_defence 8           grab nearby coins and let path end on banking tile
                                   (if idle, and no enemy is in range)

    config:afk_defence 15          do all of the above

    config:afk_defence 31          additionally respawn all hunters from adv_names.txt if dead

    config:afk_ticks_hold 4        normal wait time after a block was received, before sending destruct in case of self defense
                                   default 5

    config:afk_ticks_confirm 10    1/2 of the estimated time a transaction needs to confirm
                                   default 7
    config:afk_ticks_confirm 99    but higher values can be safer if enemy hunters try some "tricks"

                                   note that tick count now starts with 1, not 0
                                   (and "config:afk_ticks_hold" and "config:afk_ticks_confirm" should be adjusted by adding 1)

    config:afk_safe_dist 5         minimum distance to enemy hunter for sending non-critical moves,
                                   the alarm distance is now only used to determine of whether we have >1 enemy nearby
                                   (also no non-critical moves in this case)
                                   default 6

                                   distance is now: minimum(current distance, predicted distance next block)

    config:afk_attack_dist 0       go pester other hunters up to this distance (0..off)

    config:afk_flags 1             if the attempt to set a good hit+run point (see "config:afk_defence 2")
                                   on player spawn strip tile has failed, try again and allow any adjacent tile

    config:afk_flags 2             go to far away spawn tile in case of no nearby coins (to stay longer on map)

    config:afk_flags 4             go pester other hunters even if standing on spawn strip

    config:afk_flags 8             leave if outnumbered
    config:afk_flags 16            leave if outnumbered (smarter version)

    config:afk_flags 24            recommended setting


Additional config options in adv_config.txt:
show movement of unknown hunters as floating arrows
---------------------------------------------------

    config:show_wps 1              show next position (adjacent tile)

    config:show_wps 2              show first waypoint
                                   (arrow direction == direction of the next step)

    config:show_wps 4              show destination (final waypoint)

    config:show_wps 7              default 7, do all of the above


Misc. config options (in adv_config.txt)
---------------------------------------

    config:overview_zoom 20        minimum zoom level to display "where-are-my-hunters" marker
                                   (zoom level is 10...200, 10 means completely zoomed out)
                                   default 20

    config:afk_leave_map 60        hunters will leave the map (if they notice a bank), default 0
                                   - recommended value 60 (if bank_notice is smaller, it will inrease at a rate of 1 per tick)
                                   - nothing is done if the hunter's last waypoint is already a bank tile
                                   (obsolete after timesave fork)

    config:bank_notice 10          maximum distance to notice a bank if an hunter can walk there in a straight line, default 0
                                   (obsolete after timesave fork)

    config:loot_notice 50          show 'Bank' reminder if an hunter carries more coins, default 50

    config:warn_stalled 36         red blinking lights on hunters if last block is older than n "ticks",
                                   default 36 (normally 3 minutes)

    config:warn_disaster 50        green blinking lights on hunters for n blocks after disaster, default 50


Additional config options in adv_config.txt: (Windows only)
----------------------------------------------------------

    config:dbg_win32_qt_threads 12    Windows stability bug workaround (force some threads to wait for each other)
                                      0...off
                                      12...default
                                      28...same as 12, print stats in debug.log


Precautions:
------------

on Windows:            nodes may (randomly after some hours) stop receiving data, and need restart
                       (this takes less time if "Detach databases at shutdown" is not checked)

                       for a really reliable Huntercoin node it's recommended to use Oracle VirtualBox, e.g:

                       available                         virtual disk space
                       RAM (host)        RAM (guest)     (fixed size)           virtual machine OS

                       >= 8 GB           2 GB            35 GB                  Linux Mint 17.3LTS, 64bit, Mate
                       4 GB              1.75 GB         35 GB                  Linux Mint 17.3LTS, 32bit, Mate

pending tx monitor:    if either "(OK)", the idle time "n min" or "*ALARM*" is displayed for the hunter
                       last in the list of hunters in adv_names.txt, then the entire list was correctly parsed

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

