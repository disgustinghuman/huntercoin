#include "gamemapview.h"

#include "../gamestate.h"
#include "../gamemap.h"
#include "../util.h"

#include <QImage>
#include <QGraphicsItem>
#include <QGraphicsSimpleTextItem>
#include <QStyleOptionGraphicsItem>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimeLine>

#include <boost/foreach.hpp>
#include <cmath>

using namespace Game;

static const int TILE_SIZE = 48;

// Container for graphic objects
// Objects like pen could be made global variables, but sprites would crash, since QPixmap cannot be initialized
// before QApplication is initialized
struct GameGraphicsObjects
{
    // Player sprites for each color and 10 directions (with 0 and 5 being null, the rest are as on numpad)
    // better GUI -- more player sprites
    QPixmap player_sprite[Game::NUM_TEAM_COLORS + RPG_EXTRA_TEAM_COLORS][10];

    QPixmap coin_sprite, heart_sprite, crown_sprite;
    QPixmap tiles[NUM_TILE_IDS];

    QBrush player_text_brush[Game::NUM_TEAM_COLORS + RPG_EXTRA_TEAM_COLORS];

    QPen magenta_pen, gray_pen;

    GameGraphicsObjects()
        : magenta_pen(Qt::magenta, 2.0),
        gray_pen(QColor(170, 170, 170), 2.0)
    {
        player_text_brush[0] = QBrush(QColor(255, 255, 100));
        player_text_brush[1] = QBrush(QColor(255, 120, 150));
        player_text_brush[2] = QBrush(QColor(100, 255, 100));
        player_text_brush[3] = QBrush(QColor(20, 190, 255));

        // better GUI -- more player sprites
        player_text_brush[4] = QBrush(QColor(255, 255, 255)); // NPCs
        player_text_brush[5] = QBrush(QColor(255, 255, 255));

        player_text_brush[6] = QBrush(QColor(255, 255, 100)); // yellow
        player_text_brush[7] = QBrush(QColor(255, 120, 150)); // red 255, 80, 80
        player_text_brush[8] = QBrush(QColor(100, 255, 100)); // green
        player_text_brush[9] = QBrush(QColor(20, 190, 255));  // blue 0, 170, 255

        player_text_brush[10] = QBrush(QColor(255, 255, 255)); // NPCs
        player_text_brush[11] = QBrush(QColor(255, 255, 255));
        player_text_brush[12] = QBrush(QColor(255, 255, 255));
        player_text_brush[13] = QBrush(QColor(255, 255, 255));

        player_text_brush[14] = QBrush(QColor(20, 190, 255));  // blue 0, 170, 255
        player_text_brush[15] = QBrush(QColor(255, 120, 150)); // red 255, 80, 80

        player_text_brush[16] = QBrush(QColor(255, 255, 255)); // NPCs
        player_text_brush[17] = QBrush(QColor(255, 255, 255));
        player_text_brush[18] = QBrush(QColor(255, 255, 255));
        player_text_brush[19] = QBrush(QColor(255, 255, 255));
        player_text_brush[20] = QBrush(QColor(255, 255, 255));
        player_text_brush[21] = QBrush(QColor(255, 255, 255));
        player_text_brush[22] = QBrush(QColor(255, 255, 255));

        player_text_brush[23] = QBrush(QColor(255, 255, 100)); // yellow
        player_text_brush[24] = QBrush(QColor(100, 255, 100)); // green

        player_text_brush[25] = QBrush(QColor(255, 255, 255)); // NPCs
        player_text_brush[26] = QBrush(QColor(255, 255, 255));
        player_text_brush[27] = QBrush(QColor(255, 255, 255));
        player_text_brush[28] = QBrush(QColor(255, 255, 255));
        player_text_brush[29] = QBrush(QColor(255, 255, 255));

        player_text_brush[30] = QBrush(QColor(255, 255, 255)); // sfx
        player_text_brush[31] = QBrush(QColor(255, 255, 255));
        player_text_brush[32] = QBrush(QColor(255, 255, 255));

        player_text_brush[33] = QBrush(QColor(255, 255, 100)); // yellow
        player_text_brush[34] = QBrush(QColor(255, 120, 150)); // red 255, 80, 80
        player_text_brush[35] = QBrush(QColor(100, 255, 100)); // green
        player_text_brush[36] = QBrush(QColor(20, 190, 255));  // blue 0, 170, 255

        for (int i = 0; i < Game::NUM_TEAM_COLORS + RPG_EXTRA_TEAM_COLORS; i++)
            for (int j = 1; j < 10; j++)
            {
                if (j != 5)
                    player_sprite[i][j].load(":/gamemap/sprites/" + QString::number(i) + "_" + QString::number(j));
            }

        coin_sprite.load(":/gamemap/sprites/coin");
        heart_sprite.load(":/gamemap/sprites/heart");
        crown_sprite.load(":/gamemap/sprites/crown");

        for (short tile = 0; tile < NUM_TILE_IDS; tile++)
            tiles[tile].load(":/gamemap/" + QString::number(tile));
    }
};

// for FORK_TIMESAVE -- visualize player spawns
QPen visualize_spawn_pen(Qt::NoPen);
bool visualize_spawn_done = false;
int visualize_nHeight;
int visualize_x;
int visualize_y;
#define VISUALIZE_TIMESAVE_IN_EFFECT(H) (((fTestNet)&&(H>331500))||((!fTestNet)&&(H>1521500)))

// Cache scene objects to avoid recreating them on each state update
class GameMapCache
{
    struct CacheEntry
    {
        bool referenced;
    };    

    class CachedCoin : public CacheEntry
    {
        QGraphicsPixmapItem *coin;
        QGraphicsTextItem *text;
        int64 nAmount;

    public:

        CachedCoin() : coin(NULL) { }

        void Create(QGraphicsScene *scene, const GameGraphicsObjects *grobjs, int x, int y, int64 amount)
        {
            nAmount = amount;
            referenced = true;
            coin = scene->addPixmap(grobjs->coin_sprite);
            coin->setOffset(x, y);
            coin->setZValue(0.1);

            // for FORK_TIMESAVE -- reward coordinated attack against 24/7 players, if any
            if (VISUALIZE_TIMESAVE_IN_EFFECT (visualize_nHeight))
            {
                if ((((visualize_x % 2) + (visualize_y % 2) > 1) && (visualize_nHeight % 500 >= 300)) ||  // for 150 blocks, every 4th coin spawn is ghosted
                    (((visualize_x % 2) + (visualize_y % 2) > 0) && (visualize_nHeight % 500 >= 450)) ||  // for 30 blocks, 3 out of 4 coin spawns are ghosted
                    (visualize_nHeight % 500 >= 480))                                             // for 20 blocks, full ghosting
                {
                    coin->setOpacity(0.4);
                    if (IsInsideMap(visualize_x, visualize_y)) AI_coinmap_copy[visualize_y][visualize_x] = 0;
                }
                else
                {
                    coin->setOpacity(1.0);
                }
            }
            else
            {
                 coin->setOpacity(1.0);
            }

            text = new QGraphicsTextItem(coin);
            text->setHtml(
                    "<center>"
                    + QString::fromStdString(FormatMoney(nAmount))
                    + "</center>"
                );
            text->setPos(x, y + 13);
            text->setTextWidth(TILE_SIZE);
        }

        void Update(int64 amount)
        {
            // for FORK_TIMESAVE -- reward coordinated attack against 24/7 players, if any
            if (VISUALIZE_TIMESAVE_IN_EFFECT (visualize_nHeight))
            {
                if ((((visualize_x % 2) + (visualize_y % 2) > 1) && (visualize_nHeight % 500 >= 300)) ||  // for 150 blocks, every 4th coin spawn is ghosted
                    (((visualize_x % 2) + (visualize_y % 2) > 0) && (visualize_nHeight % 500 >= 450)) ||  // for 30 blocks, 3 out of 4 coin spawns are ghosted
                    (visualize_nHeight % 500 >= 480))                                             // for 20 blocks, full ghosting
                {
                    coin->setOpacity(0.4);
                    if (IsInsideMap(visualize_x, visualize_y)) AI_coinmap_copy[visualize_y][visualize_x] = 0;
                }
                else
                {
                    coin->setOpacity(1.0);
                }
            }
            else
            {
                 coin->setOpacity(1.0);
            }

            referenced = true;
            if (amount == nAmount)
                return;
            // If only the amount changed, update text
            nAmount = amount;
            text->setHtml(
                    "<center>"
                    + QString::fromStdString(FormatMoney(nAmount))
                    + "</center>"
                );
        }

        operator bool() const { return coin != NULL; }

        void Destroy(QGraphicsScene *scene)
        {
            scene->removeItem(coin);
            delete coin;
            //scene->invalidate();
        }
    };

    class CachedHeart : public CacheEntry
    {
        QGraphicsPixmapItem *heart;

    public:

        CachedHeart() : heart(NULL) { }

        void Create(QGraphicsScene *scene, const GameGraphicsObjects *grobjs, int x, int y)
        {
            referenced = true;
            heart = scene->addPixmap(grobjs->heart_sprite);
            heart->setOffset(x, y);
            heart->setZValue(0.2);
        }

        void Update()
        {
            referenced = true;
        }

        operator bool() const { return heart != NULL; }

        void Destroy(QGraphicsScene *scene)
        {
            scene->removeItem(heart);
            delete heart;
            //scene->invalidate();
        }
    };

    class CachedPlayer : public CacheEntry
    {
        QGraphicsPixmapItem *sprite;

        // better GUI -- better player sprites
        QGraphicsPixmapItem *shadow_sprite1;
        QGraphicsPixmapItem *shadow_sprite2;
        QGraphicsPixmapItem *symbol_sprite_a1;
        QGraphicsPixmapItem *symbol_sprite_d1;
        QGraphicsPixmapItem *symbol_sprite_d2;
        int color_a1, color_d1, color_d2;

        QGraphicsSimpleTextItem *text;
        const GameGraphicsObjects *grobjs;
        QString name;
        int x, y, color, dir;
        int z_order;
        int64 nLootAmount;

        void UpdPos()
        {
            sprite->setOffset(x, y);

            // better GUI -- better player sprites
            shadow_sprite1->setOffset(x, y);
            shadow_sprite2->setOffset(x, y + TILE_SIZE);
            symbol_sprite_a1->setOffset(x, y + 6);
            symbol_sprite_d1->setOffset(x, y + 6);
            symbol_sprite_d2->setOffset(x, y + 12 + 6);

            UpdTextPos(nLootAmount);
        }

        void UpdTextPos(int64 lootAmount)
        {
            if (lootAmount > 0)
                text->setPos(x, std::max(0, y - 20));
            else
                text->setPos(x, std::max(0, y - 12));
        }

        void UpdText()
        {
            if (nLootAmount > 0)
                text->setText(name + "\n" + QString::fromStdString(FormatMoney(nLootAmount)));
            else
                text->setText(name);
        }

        void UpdSprite()
        {
            sprite->setPixmap(grobjs->player_sprite[color][dir]);
        }

        void UpdColor()
        {
            text->setBrush(grobjs->player_text_brush[color]);
        }

    public:

        // better GUI -- better player sprites
        CachedPlayer() : sprite(NULL), shadow_sprite1(NULL), shadow_sprite2(NULL), symbol_sprite_a1(NULL), symbol_sprite_d1(NULL), symbol_sprite_d2(NULL) { }

        void Create(QGraphicsScene *scene, const GameGraphicsObjects *grobjs_, int x_, int y_, int z_order_, QString name_, int color_, int color_a1_, int color_d1_, int color_d2_, int dir_, int64 amount)
        {
            grobjs = grobjs_;
            x = x_;
            y = y_;
            z_order = z_order_;
            name = name_;
            color = color_;
            dir = dir_;
            nLootAmount = amount;
            referenced = true;
            sprite = scene->addPixmap(grobjs->player_sprite[color][dir]);
            sprite->setOffset(x, y);
            sprite->setZValue(z_order);
#ifdef AUX_STORAGE_ZHUNT_INFIGHT
//            if ((color == 30) && (dir == 3))
//                sprite->setOpacity(0.8);
#endif

            // better GUI -- better player sprites
            color_a1 = color_a1_;
            color_d1 = color_d1_;
            color_d2 = color_d2_;
            // blood stains have no shadows, flosting arrows have small shadows
            shadow_sprite1 = scene->addPixmap(grobjs->tiles[ color == 31 ? 276 : (color==32?465:463) ]);
            shadow_sprite1->setOffset(x, y);
            shadow_sprite1->setZValue(z_order);
            shadow_sprite1->setOpacity(0.4);
            shadow_sprite2 = scene->addPixmap(grobjs->tiles[ color == 31 ? 276 : (color==32?466:464) ]);
            shadow_sprite2->setOffset(x, y + TILE_SIZE);
            shadow_sprite2->setZValue(z_order);
            shadow_sprite2->setOpacity(0.4);

            symbol_sprite_a1 = scene->addPixmap(grobjs->tiles[color_a1]);
            symbol_sprite_a1->setOffset(x, y + 6);
            symbol_sprite_a1->setZValue(z_order);

            symbol_sprite_d1 = scene->addPixmap(grobjs->tiles[color_d1]);
            symbol_sprite_d1->setOffset(x, y + 6);
            symbol_sprite_d1->setZValue(z_order);
//            symbol_sprite_d1->setOpacity(color_d1 == RPG_TILE_TPGLOW ? 0.65 : 1.0);

            symbol_sprite_d2 = scene->addPixmap(grobjs->tiles[color_d2]);
            symbol_sprite_d2->setOffset(x, y + 12 + 6);
            symbol_sprite_d2->setZValue(z_order);


            text = scene->addSimpleText("");
            text->setZValue(1e9);
            UpdPos();
            UpdText();
            UpdColor();
        }

        // better GUI -- better player sprites
        void Update(int x_, int y_, int z_order_, int color_, int color_a1_, int color_d1_, int color_d2_, int dir_, int64 amount)
        {
            referenced = true;
            if (amount != nLootAmount)
            {
                if ((amount > 0) != (nLootAmount > 0))
                    UpdTextPos(amount);
                nLootAmount = amount;
                UpdText();
            }
            if (x != x_ || y != y_)
            {
                x = x_;
                y = y_;
                UpdPos();
            }
            if (z_order != z_order_)
            {
                z_order = z_order_;
                sprite->setZValue(z_order);
            }
            if (color != color_)
            {
                color = color_;
                dir = dir_;
                UpdSprite();
                UpdColor();
            }
            else if (dir != dir_)
            {
                dir = dir_;
                UpdSprite();
            }

            // better GUI -- better player sprites
            if (color_a1 != color_a1_)
            {
                color_a1 = color_a1_;
                symbol_sprite_a1->setPixmap(grobjs->tiles[color_a1]);
            }
            if (color_d1 != color_d1_)
            {
                color_d1 = color_d1_;
                symbol_sprite_d1->setPixmap(grobjs->tiles[color_d1]);
//                symbol_sprite_d1->setOpacity(color_d1 == RPG_TILE_TPGLOW ? 0.65 : 1.0);
            }
            if (color_d2 != color_d2_)
            {
                color_d2 = color_d2_;
                symbol_sprite_d2->setPixmap(grobjs->tiles[color_d2]);
            }
        }

        operator bool() const { return sprite != NULL; }

        void Destroy(QGraphicsScene *scene)
        {
            scene->removeItem(sprite);
            scene->removeItem(text);

            // better GUI -- better player sprites
            scene->removeItem(shadow_sprite1);
            scene->removeItem(shadow_sprite2);
            scene->removeItem(symbol_sprite_a1);
            scene->removeItem(symbol_sprite_d1);
            scene->removeItem(symbol_sprite_d2);
            delete shadow_sprite1;
            delete shadow_sprite2;
            delete symbol_sprite_a1;
            delete symbol_sprite_d1;
            delete symbol_sprite_d2;

            delete sprite;
            delete text;
            //scene->invalidate();
        }
    };

    QGraphicsScene *scene;
    const GameGraphicsObjects *grobjs;

    std::map<Coord, CachedCoin> cached_coins;
    std::map<Coord, CachedHeart> cached_hearts;
    std::map<QString, CachedPlayer> cached_players;

    template<class CACHE>
    void EraseUnreferenced(CACHE &cache)
    {
        for (typename CACHE::iterator mi = cache.begin(); mi != cache.end(); )
        {
            if (mi->second.referenced)
                ++mi;
            else
            {
                mi->second.Destroy(scene);
                cache.erase(mi++);
            }
        }
    }

public:

    GameMapCache(QGraphicsScene *scene_, const GameGraphicsObjects *grobjs_)
        : scene(scene_), grobjs(grobjs_)
    {
    }

    void StartCachedScene()
    {
        // Mark each cached object as unreferenced
        for (std::map<Coord, CachedCoin>::iterator mi = cached_coins.begin(); mi != cached_coins.end(); ++mi)
            mi->second.referenced = false;
        for (std::map<Coord, CachedHeart>::iterator mi = cached_hearts.begin(); mi != cached_hearts.end(); ++mi)
            mi->second.referenced = false;
        for (std::map<QString, CachedPlayer>::iterator mi = cached_players.begin(); mi != cached_players.end(); ++mi)
            mi->second.referenced = false;
    }

    void PlaceCoin(const Coord &coord, int64 nAmount)
    {
        CachedCoin &c = cached_coins[coord];

        // for FORK_TIMESAVE
        visualize_x = coord.x;
        visualize_y = coord.y;

        if (!c)
            c.Create(scene, grobjs, coord.x * TILE_SIZE, coord.y * TILE_SIZE, nAmount);
        else
            c.Update(nAmount);
    }

    void PlaceHeart(const Coord &coord)
    {
        CachedHeart &h = cached_hearts[coord];

        if (!h)
            h.Create(scene, grobjs, coord.x * TILE_SIZE, coord.y * TILE_SIZE);
        else
            h.Update();
    }

    // better GUI -- better player sprites
    void AddPlayer(const QString &name, int x, int y, int z_order, int color, int color_a1, int color_d1, int color_d2, int dir, int64 nAmount)
    {
        CachedPlayer &p = cached_players[name];
        if (!p)
            p.Create(scene, grobjs, x, y, z_order, name, color, color_a1, color_d1, color_d2, dir, nAmount);
        else
            p.Update(x, y, z_order, color, color_a1, color_d1, color_d2, dir, nAmount);
    }

    // Erase unreferenced objects from cache
    void EndCachedScene()
    {
        EraseUnreferenced(cached_coins);
        EraseUnreferenced(cached_hearts);
        EraseUnreferenced(cached_players);
    }
};


// better GUI -- more map tiles
bool Display_dbg_allow_tile_offset = true;
bool Display_dbg_obstacle_marker = false;

int Display_dbg_maprepaint_cachemisses = 0;
int Display_dbg_maprepaint_cachehits = 0;

int Displaycache_grassoffs_x[RPG_MAP_HEIGHT][RPG_MAP_WIDTH][MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS];
int Displaycache_grassoffs_y[RPG_MAP_HEIGHT][RPG_MAP_WIDTH][MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS];

int Display_go_x[7] = {12, 26, 7, 13, 34, 18, 1};
int Display_go_y[7] = {19, 1, 29, 8, 16, 20, 34};
int Display_go_idx = 0;

uint64_t Display_rng[2] = {98347239859043, 653935414278534};
uint64_t Display_xorshift128plus(void)
{
    uint64_t x = Display_rng[0];
    uint64_t const y = Display_rng[1];
    Display_rng[0] = y;
    x ^= x << 23; // a
    x ^= x >> 17; // b
    x ^= y ^ (y >> 26); // c
    Display_rng[1] = x;
    return x + y;
}

// to parse the asciiart map
//#define SHADOWMAP_AAOBJECT_MAX 129
//#define SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS 127
//#define SHADOWMAP_AAOBJECT_MAX_NO_GRASS 126
//#define SHADOWMAP_AAOBJECT_MAX 143
//#define SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS 141
//#define SHADOWMAP_AAOBJECT_MAX_NO_GRASS 140
//#define SHADOWMAP_AAOBJECT_MAX 155
//#define SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS 153
//#define SHADOWMAP_AAOBJECT_MAX_NO_GRASS 152
#define SHADOWMAP_AAOBJECT_MAX 156
#define SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS 154
#define SHADOWMAP_AAOBJECT_MAX_NO_GRASS 153
int ShadowAAObjects[SHADOWMAP_AAOBJECT_MAX][4] = {{ 0, 0, 'H', 251},  // menhir
                                                  { 0, 0, 'h', 252},
                                                  { 0, 1, 'H', 250},
                                                  { 0, 1, 'h', 253},

                                                  { 0, 0, 'G', 212},  // boulder
                                                  { 0, 0, 'g', 249},

                                                  { 2, 2, 'b', 122},  // broadleaf, bright
                                                  { 1, 2, 'b', 123},
                                                  { 0, 2, 'b', 124},
                                                  { 2, 1, 'b', 138},
                                                  { 1, 1, 'b', 139},
                                                  { 0, 1, 'b', 160},
                                                  { 2, 0, 'b', 156},
                                                  { 1, 0, 'b', 157},
                                                  { 0, 0, 'b', 173},

                                                  { 2, 2, 'B', 117},  // broadleaf, dark
                                                  { 1, 2, 'B', 118},
                                                  { 0, 2, 'B', 119},
                                                  { 2, 1, 'B', 133},
                                                  { 1, 1, 'B', 134},
                                                  { 0, 1, 'B', 135},
                                                  { 2, 0, 'B', 151},
                                                  { 1, 0, 'B', 152},
                                                  { 0, 0, 'B', 153},

                                                  { 1, 2, 't', 467},  // broadleaf, bright, small
                                                  { 2, 1, 't', 468},
                                                  { 1, 1, 't', 469},
                                                  { 0, 1, 't', 470},
                                                  { 2, 0, 't', 471},
                                                  { 1, 0, 't', 472},
                                                  { 0, 0, 't', 473},

                                                  { 1, 2, 'T', 479},  // broadleaf, dark, small
                                                  { 2, 1, 'T', 480},
                                                  { 1, 1, 'T', 481},
                                                  { 0, 1, 'T', 482},
                                                  { 2, 0, 'T', 483},
                                                  { 1, 0, 'T', 484},
                                                  { 0, 0, 'T', 485},

                                                  { 1, 2, 'c', 140},  // conifer, bright
                                                  { 0, 2, 'c', 141},
                                                  { 1, 1, 'c', 158},
                                                  { 0, 1, 'c', 159},
                                                  { 1, 0, 'c', 171},
                                                  { 0, 0, 'c', 172},

                                                  { 1, 2, 'C', 120},  // conifer, dark
                                                  { 0, 2, 'C', 121},
                                                  { 1, 1, 'C', 136},
                                                  { 0, 1, 'C', 137},
                                                  { 1, 0, 'C', 154},
                                                  { 0, 0, 'C', 155},

                                                  { 1, 2, 'f', 486},  // conifer, bright, small
                                                  { 0, 2, 'f', 487},
                                                  { 1, 1, 'f', 488},
                                                  { 0, 1, 'f', 489},
                                                  { 1, 0, 'f', 490},
                                                  { 0, 0, 'f', 491},

                                                  { 1, 2, 'F', 492},  // conifer, dark, small
                                                  { 0, 2, 'F', 493},
                                                  { 1, 1, 'F', 494},
                                                  { 0, 1, 'F', 495},
                                                  { 1, 0, 'F', 496},
                                                  { 0, 0, 'F', 497},

                                                  { 0, 2, 'p', 111},  // big palisade, left
                                                  { 0, 1, 'p', 113},
                                                  { 0, 0, 'p', 115},
                                                  { 0, 2, 'P', 187},  // big palisade, right
                                                  { 0, 1, 'P', 189},
                                                  { 0, 0, 'P', 191},

                                                  { 1, 2, '[', 91},  // cliff, lower left corner
                                                  { 0, 2, '[', 92},
                                                  { 1, 1, '[', 74},
                                                  { 0, 1, '[', 75},
                                                  { 1, 0, '[', 85},
                                                  { 0, 0, '[', 86},

                                                  // alternative terrain version (some tiles converted to terrain)
                                                  { 1, 2, 'm', 91},  // cliff, lower left corner
                                                  { 1, 1, 'm', 74},
                                                  { 1, 0, 'm', 85},
                                                  { 0, 0, 'm', 86},

                                                  // tiles converted to terrain commented out
//                                                { 0, 2, ']', 69},  // cliff, lower right corner
                                                  { -1, 2, ']', 70},
//                                                { 0, 1, ']', 71},
                                                  { -1, 1, ']', 72},
                                                  { 0, 0, ']', 83},
                                                  { -1, 0, ']', 84},

//                                                { 0, 2, '!', 103},  // cliff, lower end of normal column (2 versions)
//                                                { 0, 1, '!', 105},
                                                  { 0, 0, '!', 101},
//                                                { 0, 2, '|', 107},
//                                                { 0, 1, '|', 109},
                                                  { 0, 0, '|', 73},

                                                  { 1, 2, '{', 210},  // cliff, left/right end of normal line (2 versions)
                                                  { 0, 2, '{', 97},
                                                  { 1, 2, '(', 202},
                                                  { 0, 2, '(', 203},
                                                  { 0, 2, '}', 95},
                                                  { -1, 2, '}', 99},
                                                  { 0, 2, ')', 177},
                                                  { -1, 2, ')', 179},

                                                  // alternative terrain version (some tiles converted to terrain)
                                                  { -1, 2, 'j', 99},  // cliff, left/right end of normal line
                                                  { -1, 2, 'J', 179},
                                                  { 1, 2, 'i', 210},
                                                  { 1, 2, 'I', 202},

                                                  { 0, 3, '<', 185},   // cliff, left/right side of "special" line
                                                  { 1, 2, '<', 221},
                                                  { 0, 2, '<', 216},
                                                  { 0, 2, '>', 181},
                                                  { -1, 2, '>', 182},
                                                  { 0, 3, '>', 196},

                                                  { 0, 1, '?', 198},  // cliff, upper end of normal column (2 versions)
                                                  { 0, 0, '?', 200},
                                                  { 0, 1, '_', 218},
                                                  { 0, 0, '_', 213},

                                                  { 0, 1, 'r', 279},  // cliff, "concave" lower right corner
                                                  { -1, 1, 'r', 280},
                                                  { 0, 0, 'r', 281},
                                                  { -1, 0, 'r', 282},

                                                  { 1, 1, 'l', 283},  // cliff, "concave" lower left corner
                                                  { 0, 1, 'l', 284},
                                                  { 1, 0, 'l', 285},
                                                  { 0, 0, 'l', 286},

                                                  // tiles converted to terrain commented out
//                                                { 0, 2, 'R', 287},  // cliff, "concave" upper right corner
//                                                { -1, 2, 'R', 288},
                                                  { 0, 1, 'R', 289},
//                                                { -1, 1, 'R', 290},
                                                  { 0, 0, 'R', 291},
                                                  { -1, 0, 'R', 292},

//                                                { 1, 2, 'L', 293},  // cliff, "concave" upper left corner
//                                                { 0, 2, 'L', 294},
//                                                { 1, 1, 'L', 295},
//                                                { 0, 1, 'L', 296},
//                                                { 1, 0, 'L', 297},
                                                  { 0, 0, 'L', 298},

//                                                { 1, 4, 'Z', 309},  // if columns of cliff tiles get larger by 2 at lower end
//                                                { 0, 4, 'Z', 310},
//                                                { 1, 3, 'Z', 311},
//                                                { 0, 3, 'Z', 312},
                                                  { 1, 2, 'Z', 313},
//                                                { 0, 2, 'Z', 314},
                                                  { 1, 1, 'Z', 315},
                                                  { 0, 1, 'Z', 316},
                                                  { 1, 0, 'Z', 317},
                                                  { 0, 0, 'Z', 318},

//                                                { 1, 4, 'z', 319},  // if columns of cliff tiles get smaller by 2 at lower end
//                                                { 0, 4, 'z', 320},
//                                                { 1, 3, 'z', 321},
//                                                { 0, 3, 'z', 322},
//                                                { 1, 2, 'z', 323},
                                                  { 0, 2, 'z', 324},
                                                  { 1, 1, 'z', 325},
                                                  { 0, 1, 'z', 326},
                                                  { 1, 0, 'z', 327},
                                                  { 0, 0, 'z', 328},

//                                                { 1, 3, 'S', 348}, // if columns of cliff tiles get larger by 1 at lower end
//                                                { 0, 3, 'S', 349},
//                                                { 1, 2, 'S', 350},
//                                                { 0, 2, 'S', 351},
                                                  { 1, 1, 'S', 352},
//                                                { 0, 1, 'S', 353},
                                                  { 1, 0, 'S', 354},
                                                  { 0, 0, 'S', 355},

//                                                { 1, 3, 's', 356}, // if columns of cliff tiles get smaller by 1 at lower end
//                                                { 0, 3, 's', 357},
//                                                { 1, 2, 's', 358},
//                                                { 0, 2, 's', 359},
//                                                { 1, 1, 's', 360},
                                                  { 0, 1, 's', 361},
                                                  { 1, 0, 's', 362},
                                                  { 0, 0, 's', 363},

                                                  { 0, 2, '/', 333}, // if columns of cliff tiles get larger by 1 at upper end (only 5 tiles)
                                                  { 1, 1, '/', 334},
                                                  { 0, 1, '/', 335},
                                                  { 1, 0, '/', 336},
                                                  { 0, 0, '/', 337},

                                                  { 1, 2, '\\', 338}, // if columns of cliff tiles get smaller  by 1 at upper end
                                                  { 0, 2, '\\', 339},
                                                  { 1, 1, '\\', 340},
                                                  { 0, 1, '\\', 341},
                                                  { 1, 0, '\\', 342},
                                                  { 0, 0, '\\', 343},

                                                  { 1, 1, 'U', 231},  // Gate
                                                  { 0, 1, 'U', 232},
                                                  { 1, 0, 'U', 233},
                                                  { 0, 0, 'U', 234},

                                                  { 0, 0, '"', 263},  // grass, green (manually placed)
                                                  { 0, 0, '\'', 266},  // grass, green to yellow (manually placed)
                                                  { 0, 0, 'v', 259},  // red grass (manually placed)
                                                  { 0, 0, 'y', 267},  // yellow grass (manually placed)

                                                  { 0, 0, '1', 268}, // yellow grass -- "conditional" objects are last in this list
                                                  { 0, 0, '0', 263}, // grass -- "conditional" objects are last in this list
                                                  { 0, 0, '.', 266}, // grass -- "conditional" objects are last in this list
                                                 };
// to parse the asciiart map (shadows)
//#define SHADOWMAP_AASHAPE_MAX 72
//#define SHADOWMAP_AASHAPE_MAX_CLIFFCORNER 28
//#define SHADOWMAP_AASHAPE_MAX 77
//#define SHADOWMAP_AASHAPE_MAX_CLIFFCORNER 33
#define SHADOWMAP_AASHAPE_MAX 82
#define SHADOWMAP_AASHAPE_MAX_CLIFFCORNER 38
int ShadowAAShapes[SHADOWMAP_AASHAPE_MAX][5] = {{ 0, 0, 'C', 'c', 244}, // conifer, important shadow tiles
                                                { 0, -1, 'C', 'c', 247},

                                                { 0, 0, 'F', 'f', 499}, // conifer, small, important shadow tiles
                                                { 0, -1, 'F', 'f', 501},

                                                { 1, 0, 'B', 'b', 237},  // broadleaf, important shadow tiles
                                                { 0, 0, 'B', 'b', 238},
                                                { 1, -1, 'B', 'b', 240},
                                                { 0, -1, 'B', 'b', 241},

                                                { 1, 0, 'T', 't', 475},  // broadleaf, small, important shadow tiles
                                                { 0, 0, 'T', 't', 476},
                                                { 1, -1, 'T', 't', 477},
                                                { 0, -1, 'T', 't', 478},

                                                { 0, 0, 'H', 'h', 254},  // menhir
                                                { -1, 0, 'H', 'h', 255},

                                                { 0, 0, 'P', 'p', 412},  // palisades
                                                { 0, -1, 'P', 'p', 427},
                                                { -1, 0, 'P', 'p', 418},
                                                { -1, -1, 'P', 'p', 438},

                                                { 1, 0, 'C', 'c', 243},  // conifer, small shadow tiles (skipped if layers are full)
                                                { -1, 0, 'C', 'c', 245},
                                                { 1, -1, 'C', 'c', 246},
                                                { -1, -1, 'C', 'c', 248},

                                                { 1, 0, 'F', 'f', 498},  // conifer, small, small shadow tiles (skipped if layers are full)
                                                { 1, -1, 'F', 'f', 500},
                                                { -1, -1, 'F', 'f', 502},

                                                { 2, 0, 'T', 't', 474},  // broadleaf, small shadow tiles (skipped if layers are full)

                                                { 2, 0, 'B', 'b', 236},  // broadleaf, small, small shadow tiles (skipped if layers are full)
                                                { -1, 0, 'B', 'b', 239},
                                                { -1, -1, 'B', 'b', 242},

                                                { 1, 0, 'G', 'g', 256},  // boulder
                                                { 0, 0, 'G', 'g', 257},
                                                { -1, 0, 'G', 'g', 258},

                                                { 0, 0, 'R', 'R', 364}, // cliff, corner 1
                                                { 0, -1, 'R', 'R', 365},

                                                { 0, 0, 'L', 'L', 366}, // cliff, corner 2
                                                { -1, 0, 'L', 'L', 367},
//                                              { 0, -1, 'L', 'L', 368},
                                                { -1, -1, 'L', 'L', 369},

                                                { -1, 1, '>', '>', 383}, // cliff, right side of special "upper right corner" row (CLIFVEG)
                                                { -2, 1, '>', '>', 384},

                                                { -1, 2, ')', '}', 381}, // cliff, right side of normal row (CLIFVEG)
                                                { -2, 2, ')', '}', 382},
                                                { 0, 1, 'l', 'l', 381}, // cliff, corner 3
                                                { -1, 1, 'l', 'l', 382},

                                                { -1, 2, 'J', 'j', 381}, // cliff, right side of normal row (terrain version)
                                                { -2, 2, 'J', 'j', 382},

                                                { 1, 0, '[', 'm', 395},  // cliff, lower left corner (CLIFVEG)
                                                { 0, 0, '[', 'm', 396},
                                                { 1, -1, '[', 'm', 397},
                                                { 0, -1, '[', 'm', 398},

                                                { -1, 2, ']', ']', 401},  // cliff, lower right corner (CLIFVEG)
                                                { -2, 2, ']', ']', 402},
                                                { -1, 1, ']', ']', 403},
                                                { -2, 1, ']', ']', 404},
                                                { 0, 0, ']', ']', 405},
                                                { -1, 0, ']', ']', 406},
                                                { -2, 0, ']', ']', 407},
                                                { 0, -1, ']', ']', 408},
                                                { -1, -1, ']', ']', 409},
                                                { -2, -1, ']', ']', 410},

                                                { 1, 2, 'Z', 'Z', 370}, // if columns of cliff tiles get larger by 2 at lower end
                                                { 1, 1, 'Z', 'Z', 371},
                                                { 1, 0, 'Z', 'Z', 372},
                                                { 0, 0, 'Z', 'Z', 373},
                                                { 0, -1, 'Z', 'Z', 374},

                                                { 0, 2, 'z', 'z', 375}, //                               smaller by 2 at lower end
                                                { 0, 1, 'z', 'z', 376},
                                                { 1, 0, 'z', 'z', 377},
                                                { 0, 0, 'z', 'z', 378},
                                                { 1, -1, 'z', 'z', 379},
                                                { 0, -1, 'z', 'z', 380},

                                                { 1, 1, 'S', 'S', 385}, // if columns of cliff tiles get larger by 1 at lower end
                                                { 1, 0, 'S', 'S', 386},
                                                { 0, 0, 'S', 'S', 387},
                                                { 1, -1, 'S', 'S', 388},
                                                { 0, -1, 'S', 'S', 389},
                                                { 0, 1, 's', 's', 390}, //                               smaller by 1 at lower end
                                                { 1, 0, 's', 's', 391},
                                                { 0, 0, 's', 's', 392},
                                                { 1, -1, 's', 's', 393},
                                                { 0, -1, 's', 's', 394},

                                                { 0, 0, '!', '|', 399}, // cliff, lower side of normal column (CLIFVEG)
                                                { 0, -1, '!', '|', 400}};


class GameMapLayer : public QGraphicsItem
{
    int layer;
    const GameGraphicsObjects *grobjs;

public:
    GameMapLayer(int layer_, const GameGraphicsObjects *grobjs_, QGraphicsItem *parent = 0)
        : QGraphicsItem(parent), layer(layer_), grobjs(grobjs_)
    {
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    }

    QRectF boundingRect() const
    {
        return QRectF(0, 0, RPG_MAP_WIDTH * TILE_SIZE, RPG_MAP_HEIGHT * TILE_SIZE);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
    {
        Q_UNUSED(widget)

        int x1 = std::max(0, int(option->exposedRect.left()) / TILE_SIZE);
        int x2 = std::min(RPG_MAP_WIDTH, int(option->exposedRect.right()) / TILE_SIZE + 1);
        int y1 = std::max(0, int(option->exposedRect.top()) / TILE_SIZE);
        int y2 = std::min(RPG_MAP_HEIGHT, int(option->exposedRect.bottom()) / TILE_SIZE + 1);

        // a visible tile near the upper left corner of the screen
        // (not used -- position jumping around erratically)
//      pmon_mapview_ul_col = (x1 * 9 + x2) / 10 + 1;
//      pmon_mapview_ul_row = (y1 * 9 + y2) / 10 + 1;

        // allow offset for some tiles without popping
        if (Display_dbg_allow_tile_offset)
        {
            if (x1 > 0) x1--;
            if (y1 > 0) y1--;
            if (x2 < RPG_MAP_WIDTH - 1) x2++;
            if (y2 < RPG_MAP_HEIGHT - 1) y2++;
        }

        for (int y = y1; y < y2; y++)
            for (int x = x1; x < x2; x++)
            {
                // better GUI -- more map tiles
                // insert shadows
                if ((layer > 0) && (layer <= SHADOW_LAYERS))
                {
                    int stile = 0;
                    int stile1 = 0;
                    int stile2 = 0;
                    int stile3 = 0;

                    // parse asciiart map
                    if (Displaycache_gamemapgood[y][x] < SHADOW_LAYERS + 1)
                    {
                        Displaycache_gamemapgood[y][x] = SHADOW_LAYERS + 1;

                        if ((SHADOW_LAYERS > 1) && (layer > 1)) break;

                        bool is_cliffcorner = false;
                        bool is_palisade = false;
                        for (int m = 0; m < SHADOWMAP_AASHAPE_MAX; m++)
                        {
                            int u = x + ShadowAAShapes[m][0];
                            int v = y + ShadowAAShapes[m][1];

                            // AsciiArtMap array bounds
                            if ((u < 0) || (v < 0) || (u >= RPG_MAP_WIDTH + 4) || (v >= RPG_MAP_HEIGHT + 4))
                               continue;

                            if ((is_cliffcorner) && (m >= SHADOWMAP_AASHAPE_MAX_CLIFFCORNER))
                                break;

                            if ((AsciiArtMap[v][u] == ShadowAAShapes[m][2]) || (AsciiArtMap[v][u] == ShadowAAShapes[m][3]))
                            {
                                // cache data for all shadow layers in 1 pass (at layer 1)
                                // todo: do this with non-shadow tiles too
                                stile = ShadowAAShapes[m][4];

                                // palisade shadows need custom logic
                                if (((stile == 427) || (stile == 418) || (stile == 438)     || (stile == 412)) &&
                                        (x > 0) && (y > 0) && (y < RPG_MAP_HEIGHT - 1))
                                {
                                    if (is_palisade)
                                    {
                                        continue; // only 1 palisade shadow per tile
                                    }
                                    else
                                    {
                                        is_palisade = true;
                                    }

                                    char terrain_W = AsciiArtMap[y][x - 1];
                                    char terrain_SW = AsciiArtMap[y + 1][x - 1];
                                    char terrain_NW = AsciiArtMap[y - 1][x - 1];
                                    if (stile == 427)
                                    {
                                        if ((terrain_W == 'P') || (terrain_W == 'p') || (terrain_SW == 'P') || (terrain_SW == 'p'))
                                            stile = 413;
                                        else if ((terrain_NW == 'P') || (terrain_NW == 'p'))
                                             stile = 432;
                                    }
                                    else if (stile == 418)
                                    {
                                        if ((terrain_NW == 'P') || (terrain_NW == 'p'))
                                            stile = 421;
                                    }
                                }

                                if ((stile <= 0) || (stile >= NUM_TILE_IDS))
                                    continue;

                                if (!stile1)
                                {
                                    stile1 = stile;
                                    Displaycache_gamemap[y][x][1] = stile;
                                }
                                else if ((!stile2) && (SHADOW_LAYERS >= 2))
                                {
                                    stile2 = stile;
                                    Displaycache_gamemap[y][x][2] = stile;
                                }
                                else if ((!stile3) && (SHADOW_LAYERS >= 3))
                                {
                                    stile3 = stile;
                                    Displaycache_gamemap[y][x][3] = stile;
                                }
                                else
                                {
                                    continue;
                                }

                                painter->setOpacity(0.4);
                                painter->drawPixmap(x * TILE_SIZE, y * TILE_SIZE, grobjs->tiles[stile]);
                                painter->setOpacity(1);

                                // shadows of 1 of the cliff corners need custom logic
                                if ((AsciiArtMap[v][u] == 'L') || (AsciiArtMap[v][u] == 'R') || (AsciiArtMap[v][u] == '>'))
                                    is_cliffcorner = true;
                            }
                        }
                        Display_dbg_maprepaint_cachemisses++;
                    }
                    else
                    {
                        stile = Displaycache_gamemap[y][x][layer];

                        if (stile)
                        {
                            Display_dbg_maprepaint_cachehits++;

                            painter->setOpacity(0.4);
                            painter->drawPixmap(x * TILE_SIZE, y * TILE_SIZE, grobjs->tiles[stile]);
                            painter->setOpacity(1);
                        }
                    }

                    continue; // it's a shadow layer
                }

                int tile = 0;
                int grassoffs_x = 0;
                int grassoffs_y = 0;

                if (Displaycache_gamemapgood[y][x] < layer+1)
                {
                    int l = layer - SHADOW_LAYERS > 0 ? layer - SHADOW_LAYERS : 0;
                    int l_free = 1;

                    if (!layer)
                    {
                        char terrain = AsciiArtMap[y][x];
                        char terrain_N = (y > 0) ? AsciiArtMap[y - 1][x] : '0';
                        char terrain_W = (x > 0) ? AsciiArtMap[y][x - 1] : '0';
                        char terrain_E = AsciiArtMap[y][x + 1];
                        char terrain_S = AsciiArtMap[y + 1][x];
                        char terrain_S2 = AsciiArtMap[y + 2][x];

                        // gate on cobblestone (or on something else)
                        if (terrain == 'U')
                        {
                            terrain = terrain_W;
                        }

                        // tiles converted to terrain
                        // cliff, lower left corner
                        if (terrain_S == 'm') tile = 75;
                        else if (terrain_S2 == 'm') tile = 92;

                        // cliff, lower right corner
                        else if (terrain_S == ']') tile = 71;
                        else if (AsciiArtMap[y + 2][x] == ']') tile = 69;

                        // cliff, lower end of normal column (2 versions)
                        else if (terrain_S == '!') tile = 105;
                        else if (AsciiArtMap[y + 2][x] == '!') tile = 103;
                        else if (terrain_S == '|') tile = 109;
                        else if (AsciiArtMap[y + 2][x] == '|') tile = 107;

                        // cliff, left/right end of normal line (alternative terrain version)
                        else if (terrain_S2 == 'j') tile = 95;
                        else if (terrain_S2 == 'J') tile = 177;
                        else if (terrain_S2 == 'i') tile = 97;
                        else if (terrain_S2 == 'I') tile = 203;

                        // cliff, "concave" upper right corner
                        else if (terrain_S2 == 'R') tile = 287;
                        else if ((x >= 1) && (AsciiArtMap[y + 2][x - 1] == 'R')) tile = 288;
                        else if ((x >= 1) && (AsciiArtMap[y + 1][x - 1] == 'R')) tile = 290;

                        // cliff, "concave" upper left corner
                        else if (AsciiArtMap[y + 2][x + 1] == 'L') tile = 293;
                        else if (AsciiArtMap[y + 2][x] == 'L') tile = 294;
                        else if (AsciiArtMap[y + 1][x + 1] == 'L') tile = 295;
                        else if (AsciiArtMap[y + 1][x] == 'L') tile = 296;
                        else if (AsciiArtMap[y][x + 1] == 'L') tile = 297;

                        // if columns of cliff tiles get larger by 2 at lower end
                        else if (AsciiArtMap[y + 4][x + 1] == 'Z') tile = 309;
                        else if (AsciiArtMap[y + 4][x] == 'Z') tile = 310;
                        else if (AsciiArtMap[y + 3][x + 1] == 'Z') tile = 311;
                        else if (AsciiArtMap[y + 3][x] == 'Z') tile = 312;
                        else if (AsciiArtMap[y + 2][x] == 'Z') tile = 314;

                        // if columns of cliff tiles get smaller by 2 at lower end
                        else if (AsciiArtMap[y + 4][x + 1] == 'z') tile = 319;
                        else if (AsciiArtMap[y + 4][x] == 'z') tile = 320;
                        else if (AsciiArtMap[y + 3][x + 1] == 'z') tile = 321;
                        else if (AsciiArtMap[y + 3][x] == 'z') tile = 322;
                        else if (AsciiArtMap[y + 2][x + 1] == 'z') tile = 323;

                        // if columns of cliff tiles get larger by 1 at lower end
                        else if (AsciiArtMap[y + 3][x + 1] == 'S') tile = 348;
                        else if (AsciiArtMap[y + 3][x] == 'S') tile = 349;
                        else if (AsciiArtMap[y + 2][x + 1] == 'S') tile = 350;
                        else if (AsciiArtMap[y + 2][x] == 'S') tile = 351;
                        else if (terrain_S == 'S')  tile = 353; // special

                        // if columns of cliff tiles get smaller by 1 at lower end
                        else if (AsciiArtMap[y + 3][x + 1] == 's') tile = 356;
                        else if (AsciiArtMap[y + 3][x] == 's') tile = 357;
                        else if (AsciiArtMap[y + 2][x + 1] == 's') tile = 358;
                        else if (AsciiArtMap[y + 2][x] == 's') tile = 359;
                        else if (AsciiArtMap[y + 1][x + 1] == 's') tile = 360;

                        // water
                        else if (terrain == 'w')
                        {
                            tile = 299;
                        }
                        else if (terrain == 'W')
                        {
                            if (y % 2) tile = x % 2 ? 329 : 330;
                            else tile = x % 2 ? 331 : 332;
                        }

                        else if (terrain == ';') tile = 68; // sand
                        else if (terrain == ':') tile = 205; // sand
                        else if (terrain == ',') tile = 204; // sand
                        else if (terrain == 'v')             // red grass on sand
                        {
                            tile = 205;
                        }
                        else if (terrain == 'o') tile = 31; // cobblestone
                        else if (terrain == 'O') tile = 32; // cobblestone
                        else if (terrain == 'q') tile = 33; // cobblestone
                        else if (terrain == 'Q') tile = 37; // cobblestone
                        else if (terrain == '8') tile = 38; // cobblestone
                        else if (terrain == '9')            // special cobblestone, small vertical road
                        {
                            if (terrain_W == '9') tile = 34;
                            else tile = 30;
                        }
                        else if (terrain == '6')            // special cobblestone, small horizontal road
                        {
                            if (terrain_N == '6') tile = 35;
                            else tile = 28;
                        }

                        else if (terrain == '.')
                        {
                            tile = 1;

                            char terrain_SE = (y < RPG_MAP_HEIGHT - 1) && (x < RPG_MAP_WIDTH - 1) ? AsciiArtMap[y + 1][x + 1] : '0';
                            char terrain_NE = (y > 0) && (x < RPG_MAP_WIDTH - 1) ? AsciiArtMap[y - 1][x + 1] : '0';
                            char terrain_NW = (y > 0) && (x > 0) ? AsciiArtMap[y - 1][x - 1] : '0';
                            char terrain_SW = (y < RPG_MAP_HEIGHT - 1) && (x > 0) ? AsciiArtMap[y + 1][x - 1] : '0';

                            if (ASCIIART_IS_COBBLESTONE(terrain_S))
                            {
                                if (ASCIIART_IS_COBBLESTONE(terrain_W))
                                    tile = 39;
                                else if (ASCIIART_IS_COBBLESTONE(terrain_E))
                                    tile = 36;
                                else
                                    tile = 28;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_N))
                            {
                                if (ASCIIART_IS_COBBLESTONE(terrain_W))
                                    tile = 31; // fixme
                                else if (ASCIIART_IS_COBBLESTONE(terrain_E))
                                    tile = 31; // fixme
                                else
                                    tile = 35;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_W))
                            {
                                tile = 34;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_E))
                            {
                                tile = 30;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_SE))
                            {
                                tile = 27;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_NE))
                            {
                                tile = 54;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_NW))
                            {
                                tile = 55;
                            }
                            else if (ASCIIART_IS_COBBLESTONE(terrain_SW))
                            {
                                tile = 29;
                            }
                        }

                        // tree on cliff
                        // rocks on cliff
                        // and also cliff side tiles that don't have normal terrain as background
                        else if ((ASCIIART_IS_TREE(terrain)) || (ASCIIART_IS_ROCK(terrain)))
                        {
                            if ((ASCIIART_IS_CLIFFSAND(terrain_S)) || (terrain_S == 'v'))
                                tile = 68;                                                      // also sand
                        }
                        // cliff on cliff
                        // cliff on water
                        else if ((ASCIIART_IS_CLIFFBASE(terrain)) || (terrain == 'L') || (terrain == 'R') || (terrain == '#') || (terrain == 'S') || (terrain == 's' || (terrain == 'Z') || (terrain == 'z')))
                        {
                            if ((terrain_S == ';') || (terrain_S == ':') || (terrain_S == ',')) // sand
                                tile = 68;                                                      // also sand
                            else if (terrain_S == 'w')
                                tile = 299;
                            else if (terrain_S == 'W')
                            {
                                if (y % 2) tile = x % 2 ? 329 : 330;
                                else tile = x % 2 ? 331 : 332;
                            }

                        }
                        // cliff side tiles are painted at 1 cliff height (2 rows) vertical offset
                        else if (ASCIIART_IS_CLIFFSIDE_NEW(terrain))
                        {
                            if (tile == 0) tile = 68;                                           // change default terrain tile from grass to sand
                        }

                        if (tile == 0)
                        {
                            bool dirt_S = (terrain_S == '.');
                            bool dirt_N = (terrain_N == '.');
                            bool dirt_E = (terrain_E == '.');
                            bool dirt_W = (terrain_W == '.');
                            bool dirt_SE = ((y < RPG_MAP_HEIGHT - 1) && (x < RPG_MAP_WIDTH - 1) && (AsciiArtMap[y + 1][x + 1] == '.'));
                            bool dirt_NE = ((y > 0) && (x < RPG_MAP_WIDTH - 1) && (AsciiArtMap[y - 1][x + 1] == '.'));
                            bool dirt_NW = ((y > 0) && (x > 0) && (AsciiArtMap[y - 1][x - 1] == '.'));
                            bool dirt_SW = ((y < RPG_MAP_HEIGHT - 1) && (x > 0) && (AsciiArtMap[y + 1][x - 1] == '.'));

                            if (dirt_S)
                            {
                                if (dirt_W)
                                {
                                    if (dirt_NE) tile = 1;
                                    else tile = 20;
                                }
                                else if (dirt_E)
                                {
                                    if (dirt_NW) tile = 1;
                                    else tile = 26;
                                }
                                else
                                {
                                    if (dirt_N) tile = 1;
                                    else if (dirt_NW) tile = 20;   // 3/4 dirt SW
                                    else if (dirt_NE) tile = 26;   // 3/4 dirt SE
                                    else tile = 4;                 // 1/2 dirt S
                                }
                            }
                            else if (dirt_N)
                            {
                                if (dirt_W)
                                {
                                    if (dirt_SE) tile = 1;
                                    else if (dirt_NE || dirt_SW) tile = 15; // or tile = 19;   3/4 dirt NW
                                    else tile = 19;                                         // 3/4 dirt NW
                                }
                                else if (dirt_E)
                                {
                                    if (dirt_SW) tile = 1;
                                    else if (dirt_NW || dirt_SE) tile = 14; // or tile = 23;   3/4 dirt NE
                                    else tile = 23;                                         // 3/4 dirt NE
                                }
                                else
                                {
                                    if (dirt_S) tile = 1;
                                    else if (dirt_SW) tile = 15; // 3/4 dirt NW
                                    else if (dirt_SE) tile = 14; // 3/4 dirt NE
                                    else tile = 21;              // 1/2 dirt N
                                }
                            }
                            else if (dirt_W)
                            {
                                if (dirt_NE) tile = 19;      //  3/4 dirt NW
                                else if (dirt_SE) tile = 20; //  3/4 dirt SW
                                else if (dirt_E) tile = 1;
                                else tile = 10;              //  1/2 dirt W
                            }
                            else if (dirt_E)
                            {
                                if (dirt_NW) tile = 23;      //  3/4 dirt NE
                                else if (dirt_SW) tile = 26; //  3/4 dirt SE
                                else if (dirt_W) tile = 1;
                                else tile = 9;               //  1/2 dirt E
                            }
                            else if (dirt_SE)
                            {
                                tile = 6; // 1/4 dirt SE
                            }
                            else if (dirt_NE)
                            {
                                tile = 25; // 1/4 dirt NE
                            }
                            else if (dirt_NW)
                            {
                                tile = 24; // 1/4 dirt NW
                            }
                            else if (dirt_SW)
                            {
                                tile = 5; // 1/4 dirt SW
                            }
                            else
                            {
                                bool sand_S = ASCIIART_IS_CLIFFSAND(terrain_S);
                                bool sand_N = ASCIIART_IS_CLIFFSAND(terrain_N);
                                bool sand_E = ASCIIART_IS_CLIFFSAND(terrain_E);
                                bool sand_W = ASCIIART_IS_CLIFFSAND(terrain_W);
                                bool sand_SE = (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y + 1][x + 1]));
                                bool sand_NE = ((y > 0) && (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y - 1][x + 1])));
                                bool sand_NW = ((y > 0) && (x > 0) && (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y - 1][x - 1])));
                                bool sand_SW = ((x > 0) && (ASCIIART_IS_CLIFFSAND(AsciiArtMap[y + 1][x - 1])));

                                if (sand_S)
                                {
                                    if (sand_W)
                                    {
                                        if (sand_NE) tile = 68;  // sand
                                        else tile = 450;
                                    }
                                    else if (sand_E)
                                    {
                                        if (sand_NW) tile = 68;  // sand
                                        else tile = 449;
                                    }
                                    else
                                    {
                                        if (sand_N) tile = 68;  // sand
                                        else if (sand_NW) tile = 450;   // 3/4 sand SW
                                        else if (sand_NE) tile = 449;   // 3/4 sand SE
                                        else tile = 442;                // 1/2 sand S
                                    }
                                }
                                else if (sand_N)
                                {
                                    if (sand_W)
                                    {
                                        if (sand_SE) tile = 68;  // sand
                                        else tile = 452;         // 3/4 sand NW
                                    }
                                    else if (sand_E)
                                    {
                                        if (sand_SW) tile = 68;  // sand
                                        else tile = 451;         // 3/4 sand NE
                                    }
                                    else
                                    {
                                        if (sand_S) tile = 68;  // sand
                                        else if (sand_SW) tile = 452; // 3/4 sand NW
                                        else if (sand_SE) tile = 451; // 3/4 sand NE
                                        else tile = 447;              // 1/2 sand N
                                    }
                                }
                                else if (sand_W)
                                {
                                    if (sand_NE) tile = 452;      // 3/4 sand NW
                                    else if (sand_SE) tile = 450; // 3/4 sand SW
                                    else if (sand_E) tile = 68;   // sand
                                    else tile = 445;              // 1/2 sand W
                                }
                                else if (sand_E)
                                {
                                    if (sand_NW) tile = 451;      // 3/4 sand NE
                                    else if (sand_SW) tile = 449; // 3/4 sand SE
                                    else if (sand_W) tile = 68;   // sand
                                    else tile = 444;              // 1/2 sand E
                                }
                                else if (sand_SE)
                                {
                                    tile = 441; // 1/4 sand SE
                                }
                                else if (sand_NE)
                                {
                                    tile = 446; // 1/4 sand NE
                                }
                                else if (sand_NW)
                                {
                                    tile = 448; // 1/4 sand NW
                                }
                                else if (sand_SW)
                                {
                                    tile = 443; // 1/4 sand SW
                                }
                            }
                        }
                    }

                    // insert mapobjects from asciiart map (that can cast shadows)
                    if (l)
                    {
                        int off_min = -1;
                        int off_mid = -1;
                        int off_max = -1;
                        int tile_min = 0;
                        int tile_mid = 0;
                        int tile_max = 0;
                        int m_max = SHADOWMAP_AAOBJECT_MAX_NO_GRASS;

                        // insert grass if desired (and possible)
                        if (AsciiArtTileCount[y][x] < 3) // MAP_LAYERS - 1 + SHADOW_EXTRALAYERS  == 3
                        if (Display_dbg_obstacle_marker)
                        {
                            // if we need yellow grass as marker for unwalkable tiles
//                            if (AsciiArtMap[y][x] == '1')
                            if (ObstacleMap[y][x] == 1)
                            {
                                bool need_grass = true;

                                if ((x > 0) && (y > 0) && (x < RPG_MAP_WIDTH - 1) && (y < RPG_MAP_HEIGHT - 2)) // adjacent tile + 2 tiles south ok
                                {
                                    // skip if either hidden behind trees/cliffs or if this tile would be unwalkable anyway
                                    // (still needed because AsciiArtTileCount only counts trees and rocks, not cliffs)
                                    char c_east = AsciiArtMap[y][x + 1];
                                    char c_west = AsciiArtMap[y][x - 1];
//                                    char c_north = AsciiArtMap[y - 1][x];
                                    char c_se = AsciiArtMap[y + 1][x + 1];
                                    char c_south = AsciiArtMap[y + 1][x];
                                    char c_south2 = AsciiArtMap[y + 2][x];
                                    if ((c_east == 'C') || (c_east == 'c')) need_grass = false;
                                    if ((c_east == 'B') || (c_east == 'b') || (c_se == 'B') || (c_se == 'b')) need_grass = false;
                                    if ((c_south == '<') || (c_south == '>') || (c_south2 == '<') || (c_south2 == '>')) need_grass = false;
                                    if ((c_south == '!') || (c_south == '|') || (c_south2 == '!') || (c_south2 == '|')) need_grass = false;

                                    // skip on some tiles
                                    if ((ASCIIART_IS_CLIFFSIDE(c_east)) || (ASCIIART_IS_CLIFFSIDE(c_west)))
                                        need_grass = false;
                                }

                                if (need_grass)
                                {
                                    // skip if all adjacent tiles are unreachable
                                    need_grass = false;
                                    for (int v = y - 1; v <= y + 1; v++)
                                    {
                                        for (int u = x - 1; u <= x + 1; u++)
                                        {
                                            if ((u < 0) || (v < 0) || (u >= RPG_MAP_WIDTH) || (v >= RPG_MAP_HEIGHT)) // if (!(IsInsideMap((u, v))))
                                                continue;
                                            if ((u == x) && (v == y))
                                                continue;
                                            if (ObstacleMap[v][u] == 0)
//                                            if (Distance_To_POI[POIINDEX_CENTER][v][u] >= 0)
                                            {
                                                need_grass = true;
                                                break;
                                            }
                                        }
                                        if (need_grass) break;
                                    }
                                }

                                if (need_grass) m_max = SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS;
                            }
                        }

                        // sort and insert mapobjects from asciiart map into 1 of 3 possible layers
                        for (int m = 0; m < m_max; m++)
                        {
                            int x_offs = ShadowAAObjects[m][0];   int u = x + x_offs;
                            int y_offs = ShadowAAObjects[m][1];   int v = y + y_offs;

                            // need 2 additional lines (2 tiles offset for cliffs because of their "height")
                            if ((u < 0) || (v < 0) || (u >= RPG_MAP_WIDTH) || (v >= RPG_MAP_HEIGHT + 2))
                                continue;

                            if ((AsciiArtMap[v][u] == ShadowAAObjects[m][2]) &&
                                (Displaycache_gamemap[y][x][0] != ShadowAAObjects[m][3]))
                            {
                                int off = y_offs * 10 + x_offs;
                                if (!tile_min)
                                {
                                    tile_min = ShadowAAObjects[m][3];
                                    off_min = off;
                                }
                                else if (off < off_min) // lower offset == farther away == lower layer
                                {
                                    if (tile_mid)
                                    {
                                        tile_max = tile_mid;
                                        off_max = off_mid;
                                    }
                                    tile_mid = tile_min;
                                    off_mid = off_min;
                                    tile_min = ShadowAAObjects[m][3];
                                    off_min = off;
                                }
                                else if (!tile_mid)
                                {
                                    tile_mid = ShadowAAObjects[m][3];
                                    off_mid = off;
                                }
                                else if (off < off_mid)
                                {
                                    tile_max = tile_mid;
                                    off_max = off_mid;
                                    tile_mid = ShadowAAObjects[m][3];
                                    off_mid = off;
                                }
                                else
                                {
                                    tile_max = ShadowAAObjects[m][3];
                                    off_max = off;
                                }
                            }
                        }


                        if (l == l_free)
                        {
                            if (tile_min) tile = tile_min;
                        }
                        else if (l == l_free + 1)
                        {
                            if ((tile_mid) && (tile_mid != tile_min)) tile = tile_mid;
                        }
                        else if (l == l_free + 2)
                        {
                            if ((tile_max) && (tile_mid != tile_max)) tile = tile_max;
                        }
                    }

                    if (TILE_IS_GRASS(tile))
                    {
                        // 2 versions of red grass
                        if (tile == 259)
                            if (Display_xorshift128plus() & 1)
                                tile = 262;
                        // 2 versions of yellow grass
                        if (tile == 267)
                            if (Display_xorshift128plus() & 1)
                                tile = 268;

                        if ((AsciiArtMap[y][x] == '"') || (AsciiArtMap[y][x] == '\'') || (AsciiArtMap[y][x] == 'v') || (AsciiArtMap[y][x] == 'y'))
                        {
                            Display_go_idx++;
                            if ((Display_go_idx >= 7) || (Display_go_idx < 0)) Display_go_idx = 0;
                            grassoffs_x = Display_go_x[Display_go_idx];
                            grassoffs_y = Display_go_y[Display_go_idx];
                            Displaycache_grassoffs_x[y][x][layer] = grassoffs_x;
                            Displaycache_grassoffs_y[y][x][layer] = grassoffs_y;
                        }
                    }
                    else
                    {
                         Displaycache_grassoffs_x[y][x][layer] = grassoffs_x = 0;
                         Displaycache_grassoffs_y[y][x][layer] = grassoffs_y = 0;
                    }

                    Displaycache_gamemapgood[y][x] = layer+1;
                    if (!Displaycache_gamemap[y][x][layer]) Displaycache_gamemap[y][x][layer] = tile;
                    Display_dbg_maprepaint_cachemisses++;
                }
                else
                {
                    tile = Displaycache_gamemap[y][x][layer];
                    grassoffs_x = Displaycache_grassoffs_x[y][x][layer];
                    grassoffs_y = Displaycache_grassoffs_y[y][x][layer];

                    Display_dbg_maprepaint_cachehits++;
                }


                // Tile 0 denotes grass in layer 0 and empty cell in other layers
                if (!tile && layer)
                    continue;

                float tile_opacity = 1.0;
                if ((tile == RPG_TILE_TPGLOW) || (tile == RPG_TILE_TPGLOW_SMALL) || (tile == RPG_TILE_TPGLOW_TINY)) tile_opacity = 0.65;
                else if ((tile >= 299) && (tile <= 303)) tile_opacity = 0.78; // water
                else if ((tile >= 329) && (tile <= 332)) tile_opacity = 0.78; // blue water

                if (tile_opacity < 0.99) painter->setOpacity(tile_opacity);
                painter->drawPixmap(x * TILE_SIZE + grassoffs_x, y * TILE_SIZE + grassoffs_y, grobjs->tiles[tile]);
                if (tile_opacity < 0.99) painter->setOpacity(1.0);
            }

//       printf("repaint gamemap: cache hits %d misses %d\n", Display_dbg_maprepaint_cachehits, Display_dbg_maprepaint_cachemisses);
    }
};

GameMapView::GameMapView(QWidget *parent)
    : QGraphicsView(parent),
      grobjs(new GameGraphicsObjects()),
      zoomFactor(1.0),
      playerPath(NULL), queuedPlayerPath(NULL), banks(),
      panning(false), use_cross_cursor(false), scheduledZoom(1.0)
{
    scene = new QGraphicsScene(this);

    scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    scene->setBspTreeDepth(15);

    setScene(scene);
    setSceneRect(0, 0, RPG_MAP_WIDTH * TILE_SIZE, RPG_MAP_HEIGHT * TILE_SIZE);
    centerOn(MAP_WIDTH * TILE_SIZE / 2, MAP_HEIGHT * TILE_SIZE / 2);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    gameMapCache = new GameMapCache(scene, grobjs);

    setOptimizationFlags(QGraphicsView::DontSavePainterState);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

    setBackgroundBrush(QColor(128, 128, 128));

    defaultRenderHints = renderHints();

    animZoom = new QTimeLine(350, this);
    animZoom->setUpdateInterval(20);
    connect(animZoom, SIGNAL(valueChanged(qreal)), SLOT(scalingTime(qreal)));
    connect(animZoom, SIGNAL(finished()), SLOT(scalingFinished()));

    // Draw map
    // better GUI -- more map tiles
    for (int k = 0; k < MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS; k++)
    {
        GameMapLayer *layer = new GameMapLayer(k, grobjs);
        layer->setZValue(k * 1e8);
        scene->addItem(layer);
    }

    crown = scene->addPixmap(grobjs->crown_sprite);
    crown->hide();
    crown->setOffset(CROWN_START_X * TILE_SIZE, CROWN_START_Y * TILE_SIZE);
    crown->setZValue(0.3);
}

GameMapView::~GameMapView ()
{
  BOOST_FOREACH (QGraphicsRectItem* b, banks)
    {
      scene->removeItem (b);
      delete b;
    }

  delete gameMapCache;
  delete grobjs;
}

struct CharacterEntry
{
    QString name;
    unsigned char color;

    // better GUI -- icons
    int icon_a1; // gems and storage
    int icon_d1;
    int icon_d2;

    const CharacterState *state;
};


// pending tx monitor -- helper
static int pmon_CoordStep(int x, int target)
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
static int pmon_CoordUpd(int u, int v, int du, int dv, int from_u, int from_v)
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
int pmon_CoordHelper_x = 0;
int pmon_CoordHelper_y = 0;
// for "dead man switch" path
static bool pmon_CoordHelper_no_banks(int current_x, int current_y, int from_x, int from_y, int target_x, int target_y, int steps)
{
    int new_c_x;
    int new_c_y;

    int dx = target_x - from_x;
    int dy = target_y - from_y;

    for (int n; n < steps; n++)
    {
        if (abs(dx) > abs(dy))
        {
            new_c_x = pmon_CoordStep(current_x, target_x);
            new_c_y = pmon_CoordUpd(new_c_x, current_y, dx, dy, from_x, from_y);
        }
        else
        {
            new_c_y = pmon_CoordStep(current_y, target_y);
            new_c_x = pmon_CoordUpd(new_c_y, current_x, dy, dx, from_y, from_x);
        }

        // no LOS                              avoid banks
        if ((!IsWalkable(new_c_x, new_c_y)) || (AI_coinmap_copy[new_c_y][new_c_x] == -1))
        {
            pmon_CoordHelper_x = current_x;
            pmon_CoordHelper_y = current_y;
            return false;
        }

        current_x = new_c_x;
        current_y = new_c_y;
        if ((current_x == target_x) && (current_y == target_y))
            break;
    }

    pmon_CoordHelper_x = current_x;
    pmon_CoordHelper_y = current_y;
    return true;
}
static bool pmon_CoordHelper(int current_x, int current_y, int from_x, int from_y, int target_x, int target_y, int steps, bool noclip)
{
    int new_c_x;
    int new_c_y;

    int dx = target_x - from_x;
    int dy = target_y - from_y;

    for (int n; n < steps; n++)
    {
        if (abs(dx) > abs(dy))
        {
            new_c_x = pmon_CoordStep(current_x, target_x);
            new_c_y = pmon_CoordUpd(new_c_x, current_y, dx, dy, from_x, from_y);
        }
        else
        {
            new_c_y = pmon_CoordStep(current_y, target_y);
            new_c_x = pmon_CoordUpd(new_c_y, current_x, dy, dx, from_y, from_x);
        }

        // no LOS
        if ((!IsWalkable(new_c_x, new_c_y)) && (!noclip))
        {
            pmon_CoordHelper_x = current_x;
            pmon_CoordHelper_y = current_y;
            return false;
        }

        current_x = new_c_x;
        current_y = new_c_y;
        if ((current_x == target_x) && (current_y == target_y))
            break;
    }

    pmon_CoordHelper_x = current_x;
    pmon_CoordHelper_y = current_y;
    return true;
}
static int pmon_DistanceHelper(int x1, int y1, int x2, int y2, bool heuristic)
{
    int dx = abs(x1 - x2);
    int dy = abs(y1 - y2);
    int d = dx > dy ? dx : dy;
    if (heuristic)
    {
        if (((x1 >= 245) && (x1 <= 256) && (y1 >= 244) && (y1 <= 255)) && // inside of inner palisades
            ((x2 <= 243) || (x2 >= 258) || (y2 <= 242) || (y2 >= 257)))   // outside
            if (d < y2 - 243 + 8)
                d = y2 - 243 + 8;
    }

    return d;
}


void GameMapView::updateGameMap(const GameState &gameState)
{
    if (playerPath)
    {
        scene->removeItem(playerPath);
        delete playerPath;
        playerPath = NULL;
    }
    if (queuedPlayerPath)
    {
        scene->removeItem(queuedPlayerPath);
        delete queuedPlayerPath;
        queuedPlayerPath = NULL;
    }


    // for FORK_TIMESAVE -- visualize player spawns
    // note: Formerly, the SpawnMap was calculated in init.cpp, after graphics initialization.
    //       We could now move player spawn visualization code to GameMapView::GameMapView, but it would make the system less flexible.
    visualize_nHeight = gameState.nHeight;
    if (!visualize_spawn_done)
    {
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++)
            {
                if (SpawnMap[y][x] & SPAWNMAPFLAG_PLAYER)
                {
                    scene->addRect(x * TILE_SIZE, y * TILE_SIZE,
                        TILE_SIZE, TILE_SIZE,
                        visualize_spawn_pen, QColor(255, 255, 0, 40));

                    if (!visualize_spawn_done) visualize_spawn_done = true;
                }
            }
    }


    /* Update the banks.  */
    // better GUI -- banks
    const int bankOpacity = 10;
    int bank_xpos[75];
    int bank_ypos[75];
    int bank_timeleft[75];
    int bank_idx = 0;

    bool tmp_warn_blocktime = ((pmon_block_age > pmon_config_warn_stalled) && (pmon_block_age % 2));
    bool tmp_warn_poison = ((!tmp_warn_blocktime) && (gameState.nHeight - gameState.nDisasterHeight < pmon_config_warn_disaster) && (pmon_block_age % 2));
    bool tmp_zoomed_out = ((!tmp_warn_blocktime) && (!tmp_warn_poison) && (zoomFactor < (double)pmon_config_zoom/(double)100));
    int tmp_red = tmp_zoomed_out ? 255 : (tmp_warn_blocktime ? 228 : 0);
    int tmp_green = tmp_zoomed_out ? 255 : (tmp_warn_poison ? 228 : 0);
    int tmp_blue = tmp_zoomed_out ? 255 : (tmp_warn_blocktime ? 120 : 0);


    BOOST_FOREACH (QGraphicsRectItem* b, banks)
      {
        scene->removeItem (b);
        delete b;
      }
    banks.clear ();
    BOOST_FOREACH (const PAIRTYPE(Coord, unsigned)& b, gameState.banks)
      {

        // better GUI -- steal some rectangles from banks to highlight my hunter positions
        int tmp_rect_idx = bank_idx;
        int tmp_rect_x = b.first.x;
        int tmp_rect_y = b.first.y;
        int tmp_opacity = bankOpacity;
        if ((tmp_zoomed_out || tmp_warn_blocktime || tmp_warn_poison) && (tmp_rect_idx < PMON_MY_MAX) && (pmon_my_idx[tmp_rect_idx] >= 0) && (pmon_my_idx[tmp_rect_idx] < PMON_ALL_MAX))
        {
            tmp_rect_x = pmon_all_x[pmon_my_idx[tmp_rect_idx]];
            tmp_rect_y = pmon_all_y[pmon_my_idx[tmp_rect_idx]];
            tmp_opacity = 255;
        }
        QGraphicsRectItem* r
         = scene->addRect (TILE_SIZE * tmp_rect_x, TILE_SIZE * tmp_rect_y,
                           TILE_SIZE, TILE_SIZE,
                           Qt::NoPen, QColor (tmp_red, tmp_green, tmp_blue, tmp_opacity));
        if (bank_idx < 75)
        {
            bank_xpos[bank_idx] = b.first.x;
            bank_ypos[bank_idx] = b.first.y;
            bank_timeleft[bank_idx] = b.second;
            bank_idx++;

            // for "dead man switch" path
            if (IsInsideMap(b.first.x, b.first.y))
                AI_coinmap_copy[b.first.y][b.first.x] = -1;
        }


        banks.push_back (r);
      }

    gameMapCache->StartCachedScene();
    BOOST_FOREACH(const PAIRTYPE(Coord, LootInfo) &loot, gameState.loot)
        gameMapCache->PlaceCoin(loot.first, loot.second.nAmount);
    BOOST_FOREACH(const Coord &h, gameState.hearts)
        gameMapCache->PlaceHeart(h);


    // pending tx monitor -- reset name list
    pmon_all_count = 0;
    for (int m = 0; m < PMON_MY_MAX; m++)
    {
        pmon_my_idx[m] = -1;
    }

#ifdef PERMANENT_LUGGAGE
    // gems and storage
    if (gameState.gemSpawnState == GEM_SPAWNED)
    {
        gem_visualonly_state = GEM_SPAWNED;
        gem_visualonly_x = gameState.gemSpawnPos.x;
        gem_visualonly_y = gameState.gemSpawnPos.y;
    }
#ifdef AUX_AUCTION_BOT
    if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_WAITING_BLUE) // if still alive and waiting for sell order that would match ours
        pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_UNKNOWN; // unknown if alive (possibly killed since last tick)
    else if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_UNKNOWN)
        pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_RED; // dead
#endif
#endif

    // for "dead man switch" path
    if (pmon_config_defence & 16)
        pmon_config_defence -= 16;

    // Sort by coordinate bottom-up, so the stacking (multiple players on tile) looks correct
    std::multimap<Coord, CharacterEntry> sortedPlayers;

    for (std::map<PlayerID, PlayerState>::const_iterator mi = gameState.players.begin(); mi != gameState.players.end(); mi++)
    {
        const PlayerState &pl = mi->second;
        for (std::map<int, CharacterState>::const_iterator mi2 = pl.characters.begin(); mi2 != pl.characters.end(); mi2++)
        {
            const CharacterState &characterState = mi2->second;
            const Coord &coord = characterState.coord;
            CharacterEntry entry;
            CharacterID chid(mi->first, mi2->first);
            entry.name = QString::fromStdString(chid.ToString());
            if (mi2->first == 0) // Main character ("the general") has a star before the name
                entry.name = QString::fromUtf8("\u2605") + entry.name;
            if (chid == gameState.crownHolder)
                entry.name += QString::fromUtf8(" \u265B");

            // for FORK_TIMESAVE -- show protected/spectator state
            if (characterState.stay_in_spawn_area != CHARACTER_MODE_NORMAL)
            {
                entry.name += QString::fromStdString(" (");
                entry.name += QString::number(characterState.stay_in_spawn_area);
                if (VISUALIZE_TIMESAVE_IN_EFFECT(visualize_nHeight))
                {
                    if (CHARACTER_IN_SPECTATOR_MODE(characterState.stay_in_spawn_area))
                        entry.name += QString::fromStdString(", spectator)");
                    else if (CHARACTER_HAS_SPAWN_PROTECTION(characterState.stay_in_spawn_area))
                        entry.name += QString::fromStdString(", protected)");
                    else
                        entry.name += QString::fromStdString(")");
                }
                else
                    entry.name += QString::fromStdString(")");
            }

            // gems and storage
            entry.icon_d1 = RPG_ICON_EMPTY;
            entry.icon_d2 = RPG_ICON_EMPTY;
            entry.icon_a1 = 0;
            if (((gem_visualonly_state == GEM_UNKNOWN_HUNTER) || (gem_visualonly_state == GEM_ININVENTORY)) &&
                (gem_cache_winner_name == chid.ToString()))
            {
                gem_visualonly_state = GEM_ININVENTORY;
                entry.icon_a1 = 453;
            }
#ifdef PERMANENT_LUGGAGE
            int64 tmp_in_purse = characterState.rpg_gems_in_purse;
#ifdef RPG_OUTFIT_ITEMS
            unsigned char tmp_outfit = tmp_in_purse % 10;
            tmp_in_purse -= tmp_outfit;
#endif
            if (pl.playerflags & PLAYER_SUSPEND)
                entry.name += QString::fromStdString(" (recently transferred)");

            if (tmp_in_purse > 0)
            {
                if (tmp_in_purse >= 100000000)
                    entry.icon_a1 = 453;
                entry.name += QString::fromStdString(" ");
                entry.name += QString::fromStdString(FormatMoney(tmp_in_purse));
                entry.name += QString::fromStdString("gems");
            }
#endif

#ifdef AUX_AUCTION_BOT
            // if unknown if alive
            bool tmp_auction_auto_on_duty = false;
            if (pmon_config_auction_auto_name == chid.ToString())
            {
                tmp_auction_auto_on_duty = true;
                if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_UNKNOWN)
                {
                    pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_WAITING_BLUE; // still alive and waiting for sell order that would match ours
                }
            }
#endif

            // pending tx monitor -- info text
            const Coord &pmon_from = characterState.from;
            if (((pmon_state == PMONSTATE_CONSOLE) || (pmon_state == PMONSTATE_RUN)) &&
                (pmon_all_count < PMON_ALL_MAX))
            {
                pmon_all_names[pmon_all_count] = chid.ToString();
                pmon_all_x[pmon_all_count] = coord.x;
                pmon_all_y[pmon_all_count] = coord.y;
                pmon_all_color[pmon_all_count] = pl.color;

                if (VISUALIZE_TIMESAVE_IN_EFFECT(visualize_nHeight))
                {
                    if (CHARACTER_IN_SPECTATOR_MODE(characterState.stay_in_spawn_area))
                        pmon_all_invulnerability[pmon_all_count] = 2;
                    else if (CHARACTER_HAS_SPAWN_PROTECTION(characterState.stay_in_spawn_area))
                        pmon_all_invulnerability[pmon_all_count] = 1;
                    else
                        pmon_all_invulnerability[pmon_all_count] = 0;

                    if (pl.value > 20000000000)
                        entry.icon_d1 = RPG_ICON_HUC_BANDIT400;
                    else if (pl.value > 10000000000)
                        entry.icon_d1 = RPG_ICON_HUC_BANDIT;
                }
                else
                {
                    if (pl.value > 40000000000)
                        entry.icon_d1 = RPG_ICON_HUC_BANDIT400;
                    else if (pl.value > 20000000000)
                        entry.icon_d1 = RPG_ICON_HUC_BANDIT;
                }
#ifdef AUX_AUCTION_BOT
                if ((tmp_auction_auto_on_duty) && (pmon_config_auction_auto_stateicon > 0) && (pmon_config_auction_auto_stateicon < NUM_TILE_IDS))
                    entry.icon_d2 = pmon_config_auction_auto_stateicon;
#endif

                entry.name += QString::fromStdString(" ");
                entry.name += QString::number(coord.x);
                entry.name += QString::fromStdString(",");
                entry.name += QString::number(coord.y);

                // pending waypoints/destruct
                int pending_tx_idx = -1;
                int wp_age = pmon_all_tx_age[pmon_all_count] = 0;

                for (int k = 0; k < pmon_tx_count; k++)
                {
                    if (chid.ToString() == pmon_tx_names[k])
                    {
                        pending_tx_idx = k;
                        wp_age = pmon_all_tx_age[pmon_all_count] = pmon_tx_age[k];

                        break;
                    }
                }

                bool will_move = false;
                Coord target;

                // hit+run system
                bool will_confirm = false;
                if (wp_age >= pmon_config_confirm)
                    will_confirm = true;
                pmon_all_wp1_x[pmon_all_count] = pmon_all_wp1_y[pmon_all_count] = -1;
                pmon_all_wp_unconfirmed_x[pmon_all_count] = pmon_all_wp_unconfirmed_y[pmon_all_count] = -1;
                if (!(characterState.waypoints.empty()))
                {
                    Coord cdest = characterState.waypoints.front();
                    pmon_all_wpdest_x[pmon_all_count] = cdest.x;
                    pmon_all_wpdest_y[pmon_all_count] = cdest.y;
                }
                else
                {
                    pmon_all_wpdest_x[pmon_all_count] = pmon_all_wpdest_y[pmon_all_count] = -1;
                }

                if (!(characterState.waypoints.empty()))
                {
                    Coord new_c;
                    target = characterState.waypoints.back();

                    // hit+run system
                    pmon_all_wp1_x[pmon_all_count] = target.x;
                    pmon_all_wp1_y[pmon_all_count] = target.y;

                    int dx = target.x - pmon_from.x;
                    int dy = target.y - pmon_from.y;

                    if (abs(dx) > abs(dy))
                    {
                        new_c.x = pmon_CoordStep(coord.x, target.x);
                        new_c.y = pmon_CoordUpd(new_c.x, coord.y, dx, dy, pmon_from.x, pmon_from.y);
                    }
                    else
                    {
                        new_c.y = pmon_CoordStep(coord.y, target.y);
                        new_c.x = pmon_CoordUpd(new_c.y, coord.x, dy, dx, pmon_from.y, pmon_from.x);
                    }
                    pmon_all_next_x[pmon_all_count] = new_c.x;
                    pmon_all_next_y[pmon_all_count] = new_c.y;
                    will_move = true;
                }
                else
                {
                    pmon_all_next_x[pmon_all_count] = coord.x;
                    pmon_all_next_y[pmon_all_count] = coord.y;
                }

                if (will_move)
                {
                    entry.name += QString::fromStdString("->");
                    entry.name += QString::number(pmon_all_next_x[pmon_all_count]);
                    entry.name += QString::fromStdString(",");
                    entry.name += QString::number(pmon_all_next_y[pmon_all_count]);
                }

                if (!(characterState.waypoints.empty()))
                {
                    entry.name += QString::fromStdString(" wp:");
                    entry.name += QString::number(target.x);
                    entry.name += QString::fromStdString(",");
                    entry.name += QString::number(target.y);
                }

                // is this one of my players?
                pmon_all_cache_isinmylist[pmon_all_count] = false;
                for (int m = 0; m < PMON_MY_MAX; m++)
                {
                    if (chid.ToString() == pmon_my_names[m])
                    {
                        pmon_all_cache_isinmylist[pmon_all_count] = true;
                        pmon_my_idx[m] = pmon_all_count;

                        // if one of my hunters has alarm triggered
                        bool tmp_alarm = false;
                        for (int m2 = 0; m2 < PMON_MY_MAX; m2++)
                        {
                            if (pmon_my_alarm_state[m2])
                            {
                                if (!tmp_alarm)
                                {
                                    tmp_alarm = true;
                                    entry.name += QString::fromStdString(" ALARM:");
                                }
                                else
                                {
                                    entry.name += QString::fromStdString(",");
                                }
                                if (pmon_my_alarm_state[m2] == 2) // several hostiles
                                    entry.name += QString::fromStdString("***");
                                else if (pmon_my_alarm_state[m2] == 6) // acoustic alarm finished
                                    entry.name += QString::fromStdString("*");
                                entry.name += QString::fromStdString(pmon_my_names[m2]);
                                if (pmon_my_alarm_state[m2] == 2)
                                    entry.name += QString::fromStdString("***");
                                else if (pmon_my_alarm_state[m2] == 6)
                                    entry.name += QString::fromStdString("*");
                                entry.name += QString::number(pmon_my_foe_dist[m2]);
                                entry.name += QString::fromStdString("/");
                                entry.name += QString::number(pmon_my_alarm_dist[m2]);
                            }
                        }

                        // find a (directly reachable) bank
                        pmon_my_bankdist[m] = 10000;
                        if ((pending_tx_idx == -1) &&
                            (pmon_my_foecontact_age[m] == 0) &&
                            ((pmon_my_foe_dist[m] > pmon_my_alarm_dist[m]) || (pmon_my_foe_dist[m] >= 5)))
                        {
                          for (int b = 0; b < 75; b++)
                          {
                            int dx = (abs(bank_xpos[b] - coord.x));
                            int dy = (abs(bank_ypos[b] - coord.y));
                            int d = dx > dy ? dx : dy;
                            if ((d > 0) && (bank_timeleft[b] > d + 3) &&
                                (d <= pmon_config_bank_notice) && (d < pmon_my_bankdist[m]))
                            {

                                Coord tmp_bank_coord;
                                tmp_bank_coord.x = bank_xpos[b];
                                tmp_bank_coord.y = bank_ypos[b];
//                              // see CheckLinearPath
                                CharacterState tmp;
                                tmp.from = tmp.coord = coord;
                                tmp.waypoints.push_back(tmp_bank_coord);
                                while (!tmp.waypoints.empty())
                                    tmp.MoveTowardsWaypoint();
                                if(tmp.coord == tmp_bank_coord)
                                {
                                    CharacterState tmp2;
                                    Coord coord2;
                                    coord2.x = pmon_all_next_x[pmon_all_count];
                                    coord2.y = pmon_all_next_y[pmon_all_count];
                                    tmp2.from = tmp2.coord = coord2;
                                    tmp2.waypoints.push_back(tmp_bank_coord);
                                    while (!tmp2.waypoints.empty())
                                        tmp2.MoveTowardsWaypoint();
                                    if(tmp2.coord == tmp_bank_coord)
                                    {
                                    pmon_my_bankdist[m] = d;
                                    pmon_my_bank_x[m] = bank_xpos[b];
                                    pmon_my_bank_y[m] = bank_ypos[b];
                                    }
                                }
                            }
                          }
                        }

                        // check for pending tx (to determine idle status)
                        bool tmp_has_pending_tx = false;
                        bool tmp_is_banking = gameState.IsBank(coord);
                        if ((characterState.waypoints.empty()) || (pmon_out_of_wp_idx == m))
                        {
                            for (int k2 = 0; k2 < pmon_tx_count; k2++)
                            {
                                if (pmon_tx_names[k2] == pmon_my_names[m])
                                {
                                    tmp_has_pending_tx = true;
                                    break;
                                }
                            }
                        }

                        // if this hunter is idle, or "longest idle"
                        if ((characterState.waypoints.empty()) && (!tmp_has_pending_tx) && (!tmp_is_banking))
                        {
                            pmon_my_idlecount[m]++;

                            if (pmon_out_of_wp_idx <= -1)
                                pmon_out_of_wp_idx = m;
                            else if ((pmon_out_of_wp_idx < PMON_MY_MAX) && (pmon_my_idlecount[m] > pmon_my_idlecount[pmon_out_of_wp_idx]))
                                pmon_out_of_wp_idx = m;
                        }
                        else
                        {
                            pmon_my_idlecount[m] = 0;

                            if (pmon_out_of_wp_idx == m)
                                pmon_out_of_wp_idx = -1;
                        }

                        // for "dead man switch" path
#define AI_BLOCKS_TILL_PATH_UPDATE ((pmon_config_defence & 8)?10:15)
                        if (tmp_has_pending_tx)
                            if (pmon_my_idle_chronon[m] < gameState.nHeight + AI_BLOCKS_TILL_PATH_UPDATE)
                                pmon_my_idle_chronon[m] = gameState.nHeight + AI_BLOCKS_TILL_PATH_UPDATE;

                        // notice heavy loot and nearby bank
                        //
                        // currently banking
                        if (gameState.IsBank(coord))
                            pmon_my_bankstate[m] = BANKSTATE_ONMYWAY;
                        // already over carrying limit (crown holder)
                        else if ((pmon_my_bankstate[m] != BANKSTATE_ONMYWAY) && (characterState.loot.nAmount > 10000000000))
                            pmon_my_bankstate[m] = BANKSTATE_NOLIMIT;
                        // notice if full even if no bank is around
                        else if ((pmon_my_bankstate[m] != BANKSTATE_ONMYWAY) && (characterState.loot.nAmount == 10000000000))
                            pmon_my_bankstate[m] = BANKSTATE_FULL;
                        // reset if no loot (just stepped off a bank tile)
                        else if ((pmon_my_bankstate[m] != BANKSTATE_NORMAL) && (characterState.loot.nAmount == 0))
                            pmon_my_bankstate[m] = BANKSTATE_NORMAL;
                        // player did something after notification
                        else if (((pmon_my_bankstate[m] == BANKSTATE_FULL) || (pmon_my_bankstate[m] == BANKSTATE_NOTIFY)) && (pending_tx_idx >= 0))
                            pmon_my_bankstate[m] = BANKSTATE_ONMYWAY;

                        if ( (pmon_my_bankstate[m] != BANKSTATE_ONMYWAY) && (pending_tx_idx == -1) && (pmon_my_bankdist[m] <= pmon_config_bank_notice) &&
                             ((pmon_config_afk_leave) || (characterState.loot.nAmount / 100000000 >= pmon_config_loot_notice)) )
                        {
                            if ((pmon_my_bankstate[m] == BANKSTATE_NORMAL) || (pmon_my_bankstate[m] == BANKSTATE_FULL))
                                pmon_my_bankstate[m] = ((characterState.loot.nAmount < 10000000000) ? BANKSTATE_NOTIFY : BANKSTATE_FULL);

                            bool tmp_on_my_way = false;
                            if ( (!characterState.waypoints.empty()) &&
                                ((gameState.IsBank(characterState.waypoints.back())) || (gameState.IsBank(characterState.waypoints.front()))) )
                                tmp_on_my_way = true;

                            if (tmp_on_my_way)
                            {
                                pmon_my_bankstate[m] = BANKSTATE_ONMYWAY;
                            }
                            else
                            {
                                if (pmon_need_bank_idx <= -1)
                                    pmon_need_bank_idx = m;
                                else if ((pmon_need_bank_idx < PMON_MY_MAX) && (pmon_my_bankdist[m] < pmon_my_bankdist[pmon_need_bank_idx]))
                                    pmon_need_bank_idx = m;
                            }

                            if ((pmon_need_bank_idx == m) && (pmon_my_bankstate[m] != BANKSTATE_ONMYWAY) && (pmon_my_bankdist[m] <= pmon_config_bank_notice) &&
                                (pmon_my_bank_x[m] >= 0) && (pmon_my_bank_y[m] >= 0))
                            if (pmon_config_afk_leave)
                            {
                                pmon_name_update(m, pmon_my_bank_x[m], pmon_my_bank_y[m]);
                                pmon_my_bankstate[m] = BANKSTATE_ONMYWAY;
                            }
                        }

                        if ((pmon_my_bankstate[m] == BANKSTATE_NORMAL) || (pmon_my_bankstate[m] == BANKSTATE_ONMYWAY))
                        {
                            if (pmon_need_bank_idx == m)
                                pmon_need_bank_idx = -1;
                        }

#ifdef AUX_AUCTION_BOT
                        if ((auction_auto_actual_totalcoins + (pmon_config_auction_auto_size / 10000 * pmon_config_auction_auto_price / 10000)) > pmon_config_auction_auto_coinmax)
                            pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_BLUE; // stopped, session limit reached
                        else if ((tmp_auction_auto_on_duty) && ((pmon_my_foecontact_age[m] > 0) || (pmon_my_foe_dist[m] <= pmon_my_alarm_dist[m])))
                            pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_RED; // stopped, enemy nearby
                        else if (gameState.nHeight - gameState.nDisasterHeight < pmon_config_warn_disaster)
                            pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_WHITE; // stopped, blockchain problem

                        if ((pmon_config_auction_auto_coinmax > 0) && (tmp_auction_auto_on_duty) && (!pmon_config_afk_leave) && (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_GO_YELLOW)) // if go!
                        {
                            if (pending_tx_idx == -1)
                            {
                                if (pmon_name_update(m, -1, -1))
                                    pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_WAIT2_GREEN; // chat msg sent
                                else
                                    pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_WHITE; // error
                            }
                        }
#endif

                        // longest idle time in minutes
                        if (pmon_out_of_wp_idx >= 0)
                        {
#ifdef WIN32
                            // note: "Segoe UI Symbol" has u231B and many other unicode characters on windows7 (and all newer versions?)
                            entry.name += QString::fromUtf8(" \u2603"); // snowman
#else
                            entry.name += QString::fromUtf8(" \u231B"); // hourglass
#endif
                            if (pmon_out_of_wp_idx < PMON_MY_MAX)
                            {
                                entry.name += QString::number(pmon_my_idlecount[pmon_out_of_wp_idx] * pmon_go / 60);
                                entry.name += QString::fromStdString("min:");
                                entry.name += QString::fromStdString(pmon_my_names[pmon_out_of_wp_idx]);
                            }
                        }
                        if ((pmon_need_bank_idx >= 0) && (pmon_need_bank_idx < PMON_MY_MAX))
                        {
                            if (pmon_my_bankstate[pmon_need_bank_idx] == BANKSTATE_NOTIFY)
                                entry.name += QString::fromStdString(" Bank:");
                            else if (pmon_my_bankstate[pmon_need_bank_idx] == BANKSTATE_FULL)
                                entry.name += QString::fromStdString(" Full:");

                            entry.name += QString::fromStdString(pmon_my_names[pmon_need_bank_idx]);

                            if (pmon_my_bankdist[pmon_need_bank_idx] <= 15)
                            {
                                 entry.name += QString::fromStdString(" d=");
                                 entry.name += QString::number(pmon_my_bankdist[pmon_need_bank_idx]);
                            }
                        }
                        else if ((!tmp_alarm) && (pmon_out_of_wp_idx < 0))
                        {
                            entry.name += QString::fromStdString(" (OK)");
                        }

                        if (!pmon_noisy)
                            entry.name += QString::fromStdString(" (silent)");

                        // indicate if a hunter is currently ignored by the banking logic
                        if (pmon_my_bankstate[m] == BANKSTATE_ONMYWAY)
                        {
                            entry.name += QString::fromStdString(" [On my way]");
                        }
                        else if (pmon_my_bankstate[m] == BANKSTATE_FULL)
                        {
                            entry.name += QString::fromStdString(" [Full]");
                        }
                        else if (pmon_my_idle_chronon[m] > gameState.nHeight)
                        {
                            entry.name += QString::fromStdString(" [Idle ");
                            entry.name += QString::number(pmon_my_idle_chronon[m] - gameState.nHeight);
                            entry.name += QString::fromStdString("]");
                        }

                        if ((pmon_config_afk_leave) && (pmon_my_bankstate[m] != BANKSTATE_ONMYWAY))
                        {
                            entry.name += QString::fromStdString(" Leaving:");
                            entry.name += QString::number(pmon_config_bank_notice);
                            entry.name += QString::fromStdString("/");
                            entry.name += QString::number(pmon_config_afk_leave);
                        }

                        // hit+run system
                        if (!(pmon_my_new_wps[m].empty()))
                        {
                            // will be set to 1 or 2 if cornered (relative to our hit+run point) by >=1 enemies
                            pmon_my_tactical_sitch[m] = 0;
                        }

                        if (pmon_my_foecontact_age[m])
                        {
                            entry.name += QString::fromStdString(" CONTACT*");
                            entry.name += QString::number(pmon_my_foecontact_age[m]);

                            if (pmon_config_defence)
                            {
                                entry.name += QString::fromStdString("/");
                                entry.name += QString::number(pmon_config_hold);
                            }
                        }
                    }
                }

                // value of pending tx
                if (pending_tx_idx >= 0)
                {
                    entry.name += QString::fromStdString(" tx*");
                    entry.name += QString::number(wp_age);

                    if (pmon_config_defence)
                    {
                        entry.name += QString::fromStdString("/");
                        entry.name += QString::number(pmon_config_confirm);
                    }

                    entry.name += QString::fromStdString(" ");
                    entry.name += QString::fromStdString(pmon_tx_values[pending_tx_idx]);
                }

                pmon_all_count++;
            }


            entry.color = pl.color;
            entry.state = &characterState;
            sortedPlayers.insert(std::make_pair(Coord(-coord.x, -coord.y), entry));
        }
    }


    // pending tx monitor --  if enemy is nearby
    if (true)
    {
      for (int m = 0; m < PMON_MY_MAX; m++)
      {
        if (pmon_my_foecontact_age[m] < 0)
        {
            pmon_my_foecontact_age[m]++;  // cooldown
            continue;
        }

        if (pmon_state == PMONSTATE_SHUTDOWN)
        {
            // clear variables for my hunters
            pmon_my_foecontact_age[m] = 0;
            pmon_my_alarm_state[m] = 0;
            pmon_my_idlecount[m] = 0;
            pmon_my_bankdist[m] = 0;
            pmon_my_bankstate[m] = 0;
//            pmon_my_new_wps[m].clear();
            pmon_my_tactical_sitch[m] = 0;
            pmon_my_movecount[m] = 0;

            if (m == 0)
            {
                pmon_out_of_wp_idx = -1;
                pmon_need_bank_idx = -1;
            }

            continue;
        }

        bool tmp_trigger_alarm = false;
        bool tmp_trigger_multi_alarm = false;
        bool enemy_in_range = false;
        int my_alarm_range = pmon_my_alarm_dist[m];
        if (pmon_my_alarm_state[m])
            my_alarm_range += 2;

        pmon_my_foe_dist[m] = 10000;

        int my_idx = pmon_my_idx[m];
        if ((my_idx < 0) || (pmon_all_invulnerability[my_idx] > 0))  // not alive or not in danger
        {
            // clear variables for my hunters
            pmon_my_foecontact_age[m] = 0;
            pmon_my_alarm_state[m] = 0;
            pmon_my_idlecount[m] = 0;
            pmon_my_bankdist[m] = 0;
            pmon_my_bankstate[m] = 0;
            if (my_idx < 0) pmon_my_new_wps[m].clear(); // only if not alive
            pmon_my_tactical_sitch[m] = 0;
            pmon_my_movecount[m] = 0;

            if (pmon_out_of_wp_idx == m) pmon_out_of_wp_idx = -1;
            if (pmon_need_bank_idx == m) pmon_need_bank_idx = -1;

            // for "dead man switch" path
            if (my_idx < 0)
                pmon_my_idle_chronon[m] = 0; // clear if not alive
            if (pmon_config_defence & 8)
            {
                if (my_idx < 0) continue; // only if not alive

                // delete me
                if (pmon_my_idlecount[m] < 4)
                    pmon_my_idlecount[m] = 4;
            }
            else

            {
                continue;
            }
        }
        int my_enemy_tx_age = -1;

        int my_next_x = pmon_all_next_x[my_idx];
        int my_next_y = pmon_all_next_y[my_idx];

        int my_x = pmon_all_x[my_idx];
        int my_y = pmon_all_y[my_idx];

        // hit+run system
        bool have_hit_and_run_point = false;
        bool enemy_is_adjacent = false;
        bool enemy_on_top_of_us = false;
        int my_dist_to_nearest_neutral = 10000;
        int my_hit_and_run_point_x = 0;
        int my_hit_and_run_point_y = 0;
        int my_enemy_in_range_x = 0;
        int my_enemy_in_range_y = 0;
        int my_enemy_in_range_next_x = 0;
        int my_enemy_in_range_next_y = 0;
        if (!(pmon_my_new_wps[m].empty()))
        {
            Coord c1 = pmon_my_new_wps[m].back();
            have_hit_and_run_point = true;
            my_hit_and_run_point_x = c1.x;
            my_hit_and_run_point_y = c1.y;
        }

        for (int k_all = 0; k_all < pmon_all_count; k_all++)
        {
            if (k_all == my_idx) continue; // that's me
            if (pmon_all_cache_isinmylist[k_all]) continue; // one of my players
            if (pmon_all_invulnerability[k_all] >= 2) continue; // ignore spectators completely

// only if in danger
if (pmon_all_invulnerability[my_idx] == 0)  // for "dead man switch" path
{
            // hit+run system
            int dtn = pmon_DistanceHelper(my_x, my_y, pmon_all_x[k_all], pmon_all_y[k_all], false);
            if (dtn < my_dist_to_nearest_neutral) my_dist_to_nearest_neutral = dtn;

            if (pmon_all_color[my_idx] == pmon_all_color[k_all]) continue; // same team

            if ((pmon_all_invulnerability[k_all] == 0) || (pmon_all_tx_age[k_all] > 0)) // don't attack if foe has spawn invuln. and no pending tx
            if ((abs(my_next_x - pmon_all_next_x[k_all]) <= 1) && (abs(my_next_y - pmon_all_next_y[k_all]) <= 1))
            {
                // if foe has spawn invuln. and pending tx, attack right now
                if ((pmon_all_invulnerability[k_all] > 0) && (pmon_all_tx_age[k_all] > 0))
                    pmon_my_foecontact_age[m] = pmon_config_hold - 1; // will be incremented if (enemy_in_range) is true

                enemy_in_range = true;
                my_enemy_tx_age = pmon_all_tx_age[k_all];

                // hit+run system
                my_enemy_in_range_x = pmon_all_x[k_all];
                my_enemy_in_range_y = pmon_all_y[k_all];
                int my_dist_to_eir = pmon_DistanceHelper(my_x, my_y, my_enemy_in_range_x, my_enemy_in_range_y, false);

                if (my_dist_to_eir <= 1)
                {
                    enemy_is_adjacent = true;
                    if (my_dist_to_eir == 0)
                        enemy_on_top_of_us = true;
                    printf("hit+run system: player #%d %s dist to enemy in range %d\n", m, pmon_my_names[m].c_str(), my_dist_to_eir);
                }
                my_enemy_in_range_next_x = pmon_all_next_x[k_all];
                my_enemy_in_range_next_y = pmon_all_next_y[k_all];
                if (have_hit_and_run_point)
                {
                    pmon_CoordHelper(my_x, my_y, my_x, my_y, my_hit_and_run_point_x, my_hit_and_run_point_y, 1, true);
                    int d1 = pmon_DistanceHelper(my_x,  my_y, my_enemy_in_range_next_x, my_enemy_in_range_next_y, false);
                    int d2 = pmon_DistanceHelper(pmon_CoordHelper_x, pmon_CoordHelper_y, my_enemy_in_range_next_x, my_enemy_in_range_next_y, false);
                    if (d2 <= d1)
                    {
                        if (pmon_my_tactical_sitch[m] < 2)
                            pmon_my_tactical_sitch[m] = 2; // cornered (by this enemy)
                    }
                }
            }
}

            int fdx = abs(my_x - pmon_all_x[k_all]);
            int fdy = abs(my_y - pmon_all_y[k_all]);
            int tmp_foe_dist = fdx > fdy ? fdx : fdy;

            if ((tmp_trigger_alarm) && (my_alarm_range) && (tmp_foe_dist <= my_alarm_range))
            {
                tmp_trigger_multi_alarm = true;
            }

            if (tmp_foe_dist < pmon_my_foe_dist[m])
            {
                pmon_my_foe_dist[m] = tmp_foe_dist;
                if ((my_alarm_range) && (tmp_foe_dist <= my_alarm_range))
                {
                    tmp_trigger_alarm = true;

// only if in danger
if (pmon_all_invulnerability[my_idx] == 0)  // for "dead man switch" path
{
                    // hit+run system
                    int tmp_foe_x = pmon_all_next_x[k_all];
                    int tmp_foe_y = pmon_all_next_y[k_all];
                    if (have_hit_and_run_point)
                    {
                        pmon_CoordHelper(my_x, my_y, my_x, my_y, my_hit_and_run_point_x, my_hit_and_run_point_y, 1, true);
                        int d1 = pmon_DistanceHelper(my_x,  my_y, tmp_foe_x, tmp_foe_y, false);
                        int d2 = pmon_DistanceHelper(pmon_CoordHelper_x, pmon_CoordHelper_y, tmp_foe_x, tmp_foe_y, false);
                        if (d2 <= d1)
                        {
                            if (pmon_my_tactical_sitch[m] < 1)
                                pmon_my_tactical_sitch[m] = 1; // possibly cornered (by this enemy, if they come closer)
                        }
                    }
}
else
{
    tmp_trigger_alarm = false;
    tmp_trigger_multi_alarm = false;
}
                }
            }
        }

        // 1...normal alert
        // 2...multi alert
        // 5...normal alert, acoustic alarm finished
        // 6...multi alert, acoustic alarm finished
        if (tmp_trigger_alarm)
        {
            if (tmp_trigger_multi_alarm)
            {
                if (pmon_my_alarm_state[m] != 6) pmon_my_alarm_state[m] = 2;
            }
            else
            {
                if (pmon_my_alarm_state[m] == 6) pmon_my_alarm_state[m] = 5;
                if (pmon_my_alarm_state[m] != 5) pmon_my_alarm_state[m] = 1;
            }
        }
        else
        {
            pmon_my_alarm_state[m] = 0;
        }

        if (enemy_in_range) pmon_my_foecontact_age[m]++;
        else pmon_my_foecontact_age[m] = 0;


        //
        // hit+run system
        //
        if (enemy_on_top_of_us)
        {
            // attack right now
            pmon_my_foecontact_age[m] = pmon_config_hold;
        }
        else if ((pmon_my_foecontact_age[m] >= 1))
        {
            // attack right now
            if (enemy_is_adjacent)
                pmon_my_foecontact_age[m] = pmon_config_hold;
            else if ( (!(pmon_my_new_wps[m].empty())) && (pmon_my_tactical_sitch[m] < 2) )
                pmon_my_foecontact_age[m] = pmon_config_hold;
        }

        if (pmon_my_foecontact_age[m] >= pmon_config_hold)
        if ( (pmon_config_defence & 2) ||                                             // prediction test
             ((pmon_config_defence) && (!(my_enemy_tx_age >= pmon_config_confirm))) ) // normal version (no prediction test)
        {
            bool tx_good = pmon_name_update(m, -1, -1);

            if (tx_good)
            {
                if (!(pmon_my_new_wps[m].empty()))
                {
                    Coord c1;
                    c1 = pmon_my_new_wps[m].back();

                    int ir8 = (Display_xorshift128plus() % 8);
                    for (int id = ir8; id < ir8 + 8; id++)
                    {
                        if (id < 24)
                        if (IsWalkable(c1.x + pmon_24dirs_clockwise_x[id], c1.y + pmon_24dirs_clockwise_y[id]))
                        {
                            c1.x += pmon_24dirs_clockwise_x[id];
                            c1.y += pmon_24dirs_clockwise_y[id];
                            break;
                        }
                    }

                    pmon_my_new_wps[m].clear();
                    pmon_my_new_wps[m].push_back(c1);
                }

                pmon_my_foecontact_age[m] = -5; // cooldown
                break;
            }
            else
            {
                pmon_my_foecontact_age[m] = pmon_config_hold - 1; // try again in 5 seconds
            }
        }

        // grabbing coins
        pmon_my_movecount[m] = 0;
#define AI_MAX_FARM_DIST 12
        // for "dead man switch" path
//#define AI_MAX_FARM_MOVES 75
#define AI_MAX_FARM_MOVES ((pmon_config_defence & 8)?15:75)
        if ((my_idx > 0) && (my_idx < PMON_MY_MAX))
        {
//            printf("harvest test: player #%d %s idx %d next %d %d dest %d %d\n", m, pmon_my_names[m].c_str(), my_idx, my_next_x, my_next_y, pmon_all_wpdest_x[my_idx], pmon_all_wpdest_y[my_idx]);
            if ((pmon_my_idle_chronon[m] < gameState.nHeight + AI_BLOCKS_TILL_PATH_UPDATE - 2) || (pmon_config_defence & 8))
            if (my_next_x == pmon_all_wpdest_x[my_idx])
            if (my_next_y == pmon_all_wpdest_y[my_idx])
            if (SpawnMap[my_next_y][my_next_x] == 2)
                pmon_my_idle_chronon[m] = gameState.nHeight - 1;
        }

        if ((pmon_config_defence & 4) &&
            (gameState.nHeight > pmon_my_idle_chronon[m]))
        {

         // for "dead man switch" path
         if ( (pmon_go) && (!(pmon_config_defence & 16)) )
         {
//        if ((pmon_go) && (gameState.nHeight > gem_log_height)) // try once per block, if tx monitor is on
//        if (pmon_my_idlecount[m] > 3) // in pmon ticks

          if ((pmon_config_defence & 8) ||
              (pmon_my_bankstate[m] == BANKSTATE_NORMAL) || (pmon_my_bankstate[m] == BANKSTATE_NOTIFY) || (pmon_my_bankstate[m] == BANKSTATE_NOLIMIT))
          {
           if ((pmon_my_foe_dist[m] > pmon_my_alarm_dist[m]) && (pmon_my_foe_dist[m] >= 5))
           {
//            for (int vy = my_y - AI_MAX_FARM_DIST; vy <= my_y + AI_MAX_FARM_DIST; vy++)
//            {
//              for (int vx = my_x - AI_MAX_FARM_DIST; vx <= my_x + AI_MAX_FARM_DIST; vx++)
//              {
//                if (IsInsideMap(vx, vy))
//                    printf("%d ", AI_coinmap_copy[vy][vx] > 0 ? 1 : 0);
//              }
//              printf("\n");
//            }

            int old_x = my_x;
            int old_y = my_y;
            for (int nh = 0; nh < AI_MAX_FARM_MOVES; nh++)
            {
              int dmin = 10000;
              int dmin2 = 10000;
              int best_x = 0;
              int best_y = 0;
              if (pmon_my_movecount[m] > AI_MAX_FARM_MOVES - 1)
              {
                  printf("harvest test: bad movecount %d\n", pmon_my_movecount[m]);
                  break;
              }

              if (pmon_my_movecount[m] > 0)
              {
                  old_x = pmon_my_moves_x[m][pmon_my_movecount[m] - 1];
                  old_y = pmon_my_moves_y[m][pmon_my_movecount[m] - 1];
              }
              if (!IsInsideMap(old_x, old_y))
              {
                  printf("harvest test: bad coors %d %d\n", old_x, old_y);
                  break;
              }
              for (int vy = old_y - AI_MAX_FARM_DIST; vy <= old_y + AI_MAX_FARM_DIST; vy++)
              for (int vx = old_x - AI_MAX_FARM_DIST; vx <= old_x + AI_MAX_FARM_DIST; vx++)
              {
                if (IsInsideMap(vx, vy))
                {
                  int d = pmon_DistanceHelper(old_x, old_y, vx, vy, false); // to prev. tile
                  int d2 = pmon_DistanceHelper(my_x, my_y, vx, vy, false); // to current tile
                  if ( (d > 0) && (AI_coinmap_copy[vy][vx] > 0) &&
                          ((d < dmin) || ((d == dmin) && (d2 < dmin2))) )
                  {
                      // for "dead man switch" path
                      if (pmon_CoordHelper_no_banks(old_x, old_y, old_x, old_y, vx, vy, AI_MAX_FARM_DIST))
                      {
                          dmin = d;
                          best_x = vx;
                          best_y = vy;
                      }
/*                    // old version
                      // see CheckLinearPath
                      Coord coord;
                      Coord variant_coord;
                      coord.x = old_x;
                      coord.y = old_y;
                      variant_coord.x = vx;
                      variant_coord.y = vy;
                      CharacterState tmp;
                      tmp.from = tmp.coord = coord;
                      tmp.waypoints.push_back(variant_coord);
                      while (!tmp.waypoints.empty())
                      tmp.MoveTowardsWaypoint();
                      if(tmp.coord == variant_coord)
                      {
                          dmin = d;
                          best_x = vx;
                          best_y = vy;
                      }
*/
                  }
                }
              }

              // for "dead man switch" path
              if ( /* (pmon_config_defence & 8) && */
                  ((dmin >= 10000) || (nh == AI_MAX_FARM_MOVES-1))) // Go to spawn strip
              {
                  dmin = 10000;
                  for (int vy = old_y - AI_MAX_FARM_DIST; vy <= old_y + AI_MAX_FARM_DIST; vy++)
                  for (int vx = old_x - AI_MAX_FARM_DIST; vx <= old_x + AI_MAX_FARM_DIST; vx++)
                  {
                    if (IsInsideMap(vx, vy))
                    {
                      int d = pmon_DistanceHelper(old_x, old_y, vx, vy, false); // to prev. tile
                      if ( (d > 0) && (SpawnMap[vy][vx] == 2) && (d < dmin) )
                      {
                          // see CheckLinearPath
                          Coord coord;
                          Coord variant_coord;
                          coord.x = old_x;
                          coord.y = old_y;
                          variant_coord.x = vx;
                          variant_coord.y = vy;
                          CharacterState tmp;
                          tmp.from = tmp.coord = coord;
                          tmp.waypoints.push_back(variant_coord);
                          while (!tmp.waypoints.empty())
                          tmp.MoveTowardsWaypoint();
                          if(tmp.coord == variant_coord)
                          {
                              dmin = d;
                              best_x = vx;
                              best_y = vy;
                          }
                      }
                    }
                  }
                  if (dmin < 10000)
                  {
                      pmon_my_moves_x[m][pmon_my_movecount[m]] = best_x;
                      pmon_my_moves_y[m][pmon_my_movecount[m]] = best_y;
                      pmon_my_movecount[m]++;
                      break;
                  }
              }
              else
              // old version
                  if ((dmin < 10000) && (pmon_my_movecount[m] < AI_MAX_FARM_MOVES))
              {
                  pmon_my_moves_x[m][pmon_my_movecount[m]] = best_x;
                  pmon_my_moves_y[m][pmon_my_movecount[m]] = best_y;
                  pmon_my_movecount[m]++;
                  AI_coinmap_copy[best_y][best_x] = 0; // dibs
              }
              else
              {
                  break;
              }
            }
            printf("harvest test: player #%d %s found %d tiles: \n", m, pmon_my_names[m].c_str(), pmon_my_movecount[m]);
//            if (pmon_my_movecount[m] >= (my_dist_to_nearest_neutral <= 5 ? 1 : 3)) // found 3 coins to pick up (or just 1 if facing annoying competition)
            if (pmon_my_movecount[m] >= 2) // found 1 coins to pick up and 1 bank tile
            {
//              for (int nh = 0; nh < pmon_my_movecount[m]; nh++)
//                  printf("%d,%d ", pmon_my_moves_x[m][nh], pmon_my_moves_y[m][nh]);
//              printf("\n");

              pmon_name_update(m, -1, -1);

              // be lazy only in case of no competition
              pmon_my_idle_chronon[m] = gameState.nHeight + AI_BLOCKS_TILL_PATH_UPDATE;
              if (pmon_config_defence & 8)
                  pmon_my_idle_chronon[m] = gameState.nHeight + (my_dist_to_nearest_neutral < AI_BLOCKS_TILL_PATH_UPDATE ? my_dist_to_nearest_neutral : AI_BLOCKS_TILL_PATH_UPDATE);

              // for "dead man switch" path
              pmon_config_defence |= 16;
            }
           }
           else
           {
//             printf("harvest test: player #%d %s skipped, busy\n", m, pmon_my_names[m].c_str());
           }
          }
          else
          {
              printf("harvest test: player #%d %s skipped, waiting for processing\n", m, pmon_my_names[m].c_str());
          }
         }
         else
         {
             printf("harvest test: player #%d %s skipped, full\n", m, pmon_my_names[m].c_str());
         }
        }
        else
        {
            printf("harvest test: player #%d %s skipped, nearby enemy\n", m, pmon_my_names[m].c_str());
        }

      }
    }

    if (pmon_config_afk_leave > pmon_config_bank_notice)
        pmon_config_bank_notice++;

    if (pmon_state == PMONSTATE_SHUTDOWN)
    {
#ifdef AUX_DEBUG_DUMP_GAMEMAP
        FILE *fp;
        fp = fopen("generatedgamemap.txt", "w");
        if (fp != NULL)
        {
            for (int z = 0; z < Game::MAP_LAYERS + SHADOW_LAYERS + SHADOW_EXTRALAYERS; z++)
            {
                fprintf(fp, "    // Layer %d\n", z);
                fprintf(fp, "    {\n");
                // visually larger map
//              for (int y = 0; y < RPG_MAP_HEIGHT; y++)
//                for (int x = 0; x < RPG_MAP_WIDTH; x++)
                // normal 502*502 map
                for (int y = 0; y < Game::MAP_HEIGHT; y++)
                  for (int x = 0; x < Game::MAP_WIDTH; x++)
                  {
                      if (x == 0) fprintf(fp, "        {");
                      if (x < Game::MAP_WIDTH - 1) fprintf(fp, "%d,", Displaycache_gamemap[y][x][z]);
                      else fprintf(fp, "%d},\n", Displaycache_gamemap[y][x][z]);
                  }
                fprintf(fp, "    },\n");
            }
            fclose(fp);
        }
        MilliSleep(20);
#endif

        pmon_state = PMONSTATE_STOPPED;
    }

    // windows stability bug workaround
#ifdef PMON_DEBUG_WIN32_GUI
    if ((pmon_go > 0) && (pmon_config_dbg_sleep & PMON_DBGWIN_LOG))
    {
        printf("ThreadSocketHandler2, loops %d (%d/s)  ", pmon_config_dbg_loops, pmon_config_dbg_loops/pmon_go);

        int w = pmon_dbg_which_thread;
        if (w == 1)
            printf("T.SocketHandler2 active, max wait count %d %d %d\n", pmon_dbg_waitcount_t0, pmon_dbg_waitcount_t1, pmon_dbg_waitcount_t2);
        else if (w == 2)
            printf("T.OpenConnections2 active, max wait count %d %d %d\n", pmon_dbg_waitcount_t0, pmon_dbg_waitcount_t1, pmon_dbg_waitcount_t2);
        else if (w == 4)
            printf("T.MessageHandler2 active, max wait count %d %d %d\n", pmon_dbg_waitcount_t0, pmon_dbg_waitcount_t1, pmon_dbg_waitcount_t2);
        else if (w)
            printf("Threads #0, #1, #2: ERROR %d, max wait count %d %d %d\n", w, pmon_dbg_waitcount_t0, pmon_dbg_waitcount_t1, pmon_dbg_waitcount_t2);
        else
            printf("Threads #0, #1, #2: sleeping, max wait count %d %d %d\n", pmon_dbg_waitcount_t0, pmon_dbg_waitcount_t1, pmon_dbg_waitcount_t2);

        pmon_config_dbg_loops = pmon_dbg_waitcount_t0 = pmon_dbg_waitcount_t1 = pmon_dbg_waitcount_t2 = 0;
    }
#endif

    if ((pmon_go) && (gameState.nHeight > gem_log_height))
    {
        gem_log_height = gameState.nHeight;
#ifdef PERMANENT_LUGGAGE
        // list all players that own a storage vault
        int count = 0;
        int64 count_volume = 0;

        int64 zhunt_info_available = 0;

        FILE *fp;
        fp = fopen("adventurers.txt", "w");
        if (fp != NULL)
        {
            fprintf(fp, "\n Inventory (chronon %7d, %s)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet");
            fprintf(fp, " ------------------------------------\n\n");
#ifdef RPG_OUTFIT_ITEMS
#ifdef AUX_STORAGE_ZHUNT
            fprintf(fp, "                                                                                   (raw coin amount)\n");
            fprintf(fp, "                                          hunter                         conjured           magic                                       found\n");
            fprintf(fp, "storage vault key                           name      gems    outfit     creature  order    number     age    position   life  magicka  gems  state state2\n");
#else
            fprintf(fp, "                                          hunter\n");
            fprintf(fp, "storage vault key                           name      gems    outfit\n");
#endif
#else
            fprintf(fp, "                                          hunter\n");
            fprintf(fp, "storage vault key                           name      gems\n");
#endif
            fprintf(fp, "\n");

            BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
            {
              int64 tmp_volume = st.second.nGems;

#ifdef AUX_STORAGE_ZHUNT_TAGTEST
              if (st.first == AUX_ZHUNT_TESTADDRESS(fTestNet))
              {
                  zhunt_info_available = tmp_volume + st.second.ex_trade_profitloss + (100 * AUX_COIN);
              }
#endif

#ifdef RPG_OUTFIT_ITEMS
              unsigned char tmp_outfit = st.second.item_outfit;
              std::string s = "-";
              if (tmp_outfit == 1) s = "mage";
              else if (tmp_outfit == 2) s = "fighter";
              else if (tmp_outfit == 4) s = "rogue";

#ifdef AUX_STORAGE_ZHUNT
              std::string s_zhunt = "-";
              if (gameState.nHeight < st.second.zhunt_chronon + ZHUNT_MAX_LIFETIME)
              {
                  if (st.second.zhunt_order.length() >= 8)
                  {
                      s_zhunt = st.second.zhunt_order.substr(0,4) + "." + st.second.zhunt_order.substr(4);
                      if (st.second.ai_state2 & ZHUNT_STATE2_TOOHOT) s_zhunt = "burning " + s_zhunt;
                      if (st.second.ai_life == 0) s_zhunt = "dead    " + s_zhunt;
                      else if (s_zhunt[0] == '4') s_zhunt = "lemure  " + s_zhunt;
                      else s_zhunt = "zombie  " + s_zhunt;
                  }
              }
              else if (st.second.zhunt_chronon > 0)
              {
                  s_zhunt = "expired";
              }
              if (s_zhunt.length() > 1)
              {
                  int tmp_age = st.second.zhunt_death_chronon ? (int)st.second.zhunt_death_chronon - (int)st.second.zhunt_chronon : gameState.nHeight - (int)st.second.zhunt_chronon;
                  fprintf(fp, "%s    %10s    %6s    %7s      %-17s 5501   %7d    %3d,%-3d    %3d    %3d    %5s  %3d  %3d\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_volume).c_str(), s.c_str(),  s_zhunt.c_str(), tmp_age, st.second.ai_coord.x, st.second.ai_coord.y, st.second.ai_life, st.second.ai_magicka, FormatMoney(st.second.zhunt_found_gems).c_str(), st.second.ai_state, st.second.ai_state2);
              }
              else
              {
                  fprintf(fp, "%s    %10s    %6s    %7s      %s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_volume).c_str(), s.c_str(),  s_zhunt.c_str());
              }
#else
              fprintf(fp, "%s    %10s    %6s    %s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_volume).c_str(), s.c_str());
#endif

#else
              fprintf(fp, "%s    %10s    %6s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_volume).c_str());
#endif
              count++;
              count_volume += tmp_volume;
            }
            fprintf(fp, "\n");
            fprintf(fp, "                                           total\n");
            fprintf(fp, "\n");
            fprintf(fp, "                                           %4d     %6s\n", count, FormatMoney(count_volume).c_str());
#ifdef AUX_STORAGE_ZHUNT_TAGTEST
            if (gameState.nHeight >= AUX_MINHEIGHT_ZHUNT(fTestNet))
            {
                fprintf(fp, "\n");
                fprintf(fp, "->console command to instantly create a vault, or buy gems:\n");
                fprintf(fp, "sendtoaddress %s <coin amount> comment1 comment2 \"GEM <huntercoinaddress>\"\n", AUX_ZHUNT_TESTADDRESS(fTestNet));
//                fprintf(fp, "%s gems available, GEM:HUC rate %s, fee for new vault %s coins\n", FormatMoney(zhunt_info_available).c_str(), FormatMoney(gameState.auction_settle_price).c_str(), FormatMoney(gameState.auction_settle_price / AUX_COIN * GEM_ONETIME_STORAGE_FEE).c_str());
                fprintf(fp, "%s gems available, GEM:HUC rate %s, fee for new vault 0.02 gems\n", FormatMoney(zhunt_info_available).c_str(), FormatMoney(gameState.auction_settle_price).c_str());
                fprintf(fp, "\n");
                fprintf(fp, "->send coins to yourself to conjure a creature:\n");
                fprintf(fp, "sendtoaddress <storage vault key> <raw coin amount>\n");
                fprintf(fp, "fee per creature 0.04 gems\n");
                fprintf(fp, "\n");
            }
#endif
            fclose(fp);
        }
        MilliSleep(20);

#ifdef AUX_STORAGE_VOTING
        if (gameState.nHeight >= AUX_MINHEIGHT_VOTING(fTestNet))
        {
            fp = fopen("adv_motion.txt", "w");
            if (fp != NULL)
            {
                int64 count_c[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                int64 count_g[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                int tmp_blocks = (gameState.nHeight % AUX_VOTING_INTERVAL(fTestNet));
                int tmp_tag_official = gameState.nHeight - tmp_blocks + AUX_VOTING_INTERVAL(fTestNet) - 25; // xxx9975 (testnet: xxx975)

                if (pmon_config_vote_tally)
                    tmp_tag_official = pmon_config_vote_tally;

                fprintf(fp, "\n Votes (chronon %7d, %s)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet");
                fprintf(fp, " --------------------------------\n\n");
                fprintf(fp, "                                          hunter             coins           motion\n");
                fprintf(fp, "storage vault key                           name     gems    (*1k)   vote    id-tag   close\n");
                fprintf(fp, "\n");

                BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
                {
                    if (st.second.vote_raw_amount > 0)
                    {
                        int64 tmp_volume = st.second.nGems;
                        int64 tmp_kcoins = (st.second.vote_raw_amount / 100000000 / 1000);
                        int tmp_vote = int((st.second.vote_raw_amount / 10000000) % 10);
                        int tmp_tag = int(st.second.vote_raw_amount % 10000000);

                        fprintf(fp, "%s    %10s    %5s    %5d    #%d    %7d  %7d\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_volume).c_str(), int(tmp_kcoins), tmp_vote, tmp_tag, tmp_tag);
#ifdef AUX_STORAGE_VERSION2
                        if (st.second.vote_comment.length() > 0)
                            fprintf(fp, "\n  %s: \"%s\"\n\n", st.second.huntername.c_str(), st.second.vote_comment.c_str());
#endif
                        if (tmp_tag == tmp_tag_official)
                        {
                            count_c[tmp_vote] += tmp_kcoins;
                            count_g[tmp_vote] += tmp_volume;
                        }
                    }
                }

                fprintf(fp, "\n Vote tally (chronon %7d)\n", tmp_tag_official);
                fprintf(fp, " ----------------------------\n\n");
                fprintf(fp, "                                                             coins\n");
                fprintf(fp, "vote                                                 gems    (*1k)\n\n");
                for (int iv = 0; iv < 10;iv++)
                {
                    if ((count_g[iv]) || (count_c[iv]))
                        fprintf(fp, "#%d                                                  %5s    %5d\n", iv, FormatMoney(count_g[iv]).c_str(), int(count_c[iv]));
                }

                fclose(fp);
            }
            MilliSleep(20);
        }
#endif

#ifdef PERMANENT_LUGGAGE_AUCTION
        fp = fopen("auction.txt", "w");
        if (fp != NULL)
        {
          if (gameState.nHeight < AUX_MINHEIGHT_FEED(fTestNet))
          {
            fprintf(fp, "Closed until block %d\n", AUX_MINHEIGHT_FEED(fTestNet));
          }
          else
          {
            bool w = false;
            if (gameState.nHeight < AUX_MINHEIGHT_GEMHUC_SETTLEMENT(fTestNet))
            {
                if (gameState.upgrade_test != gameState.nHeight * 2)
                    w = true;
            }
            else if (gameState.nHeight < AUX_MINHEIGHT_ZHUNT(fTestNet))
            {
                if (gameState.upgrade_test != gameState.nHeight * 3)
                    w = true;
            }
            else
            {
                if (gameState.upgrade_test != gameState.nHeight * 4)
                    w = true;
            }
            if (w)
            {
              fprintf(fp, "GAMESTATE OUT OF SYNC: !!! DON'T USE THIS AUCTION PAGE !!!\n");
              fprintf(fp, "Either the gamestate was used with an old version of this client\n");
              fprintf(fp, "or the alarm (only for auction, different from 'network alert') was triggered\n");
              fprintf(fp, "or this client version itself is too old.\n");
            }

            fprintf(fp, "\n Continuous dutch auction (chronon %7d, %s, next down tick in %2d)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet", AUCTION_DUTCHAUCTION_INTERVAL - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
            fprintf(fp, " -------------------------------------------------------------------------\n\n");
            fprintf(fp, "                                     hunter                 ask\n");
            fprintf(fp, "storage vault key                    name        gems       price    chronon   flags\n");
            fprintf(fp, "\n");

            // sorted order book
            int bs_count = 0;
            int64 bs1_max = 0;
            int64 bs2_max = 0;
            int64 bs2_min = AUX_COIN * AUX_COIN;
            int bs_idx = 0;

            BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
            {
                // settlement in coins
                // (need AUX_STORAGE_VERSION4 to compile)
                if ((st.second.auction_ask_price > 0) || (st.second.auction_proceeds_remain > 0))
                {
                    int64 tmp_total = st.second.auction_proceeds_total;
                    int64 tmp_done = tmp_total - st.second.auction_proceeds_remain;

                    // sorted order book                     v-- can write 2 lines at once
                    if (bs_count < SORTED_ORDER_BOOK_LINES - 2)
                    {
                        if (st.second.auction_proceeds_remain <= 0)
                        {
                            sprintf(Displaycache_book[bs_count], "%s   %-10s %5s at %9s   %d    %s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.auction_ask_size).c_str(), FormatMoney(st.second.auction_ask_price).c_str(), st.second.auction_ask_chronon, st.second.auction_flags & AUCTIONFLAG_ASK_GTC ? "good-til-canceled" : "auction");
                        }
                        else
                        {
                            sprintf(Displaycache_book[bs_count], "%s   %-10s                      %d    processing %s/%s HUC\n", st.first.c_str(), st.second.huntername.c_str(), st.second.auction_ask_chronon, FormatMoney(tmp_done).c_str(), FormatMoney(tmp_total).c_str());
                            Displaycache_book_sort1[bs_count] = 1; // must be >0 for sorting
                            Displaycache_book_sort2[bs_count] = st.second.auction_ask_chronon;
                            Displaycache_book_done[bs_count] = false;
                            bs_count++;
                            sprintf(Displaycache_book[bs_count], "%s   %-10s %5s at %9s   %d    auction\n", "                liquidity fund for", st.second.huntername.c_str(), FormatMoney(st.second.auction_ask_size).c_str(), FormatMoney(st.second.auction_ask_price).c_str(), st.second.auction_ask_chronon);
                        }
                        Displaycache_book_sort1[bs_count] = st.second.auction_ask_price;
                        Displaycache_book_sort2[bs_count] = st.second.auction_ask_chronon;
                        Displaycache_book_done[bs_count] = false;
                        bs_count++;
                    }
                    else
                    {
                        if (st.second.auction_proceeds_remain <= 0)
                        {
                            fprintf(fp, "%s   %-10s %5s at %9s   %d    %s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.auction_ask_size).c_str(), FormatMoney(st.second.auction_ask_price).c_str(), st.second.auction_ask_chronon, st.second.auction_flags & AUCTIONFLAG_ASK_GTC ? "good-til-canceled" : "auction");
                        }
                        else
                        {
                            fprintf(fp, "%s   %-10s                      %d    processing %s/%s HUC\n", st.first.c_str(), st.second.huntername.c_str(), st.second.auction_ask_chronon, FormatMoney(tmp_done).c_str(), FormatMoney(tmp_total).c_str());
                            fprintf(fp, "%s   %-10s %5s at %9s   %d    auction\n", "                liquidity fund for", st.second.huntername.c_str(), FormatMoney(st.second.auction_ask_size).c_str(), FormatMoney(st.second.auction_ask_price).c_str(), st.second.auction_ask_chronon);
                        }
                    }
                }
            }
            // sorted order book
            if (bs_count > 0)
            for (int j = 0; j < bs_count; j++)
            {
                bs1_max = 0;
                for (int i = 0; i < bs_count; i++)
                {
                  if (!Displaycache_book_done[i])
                  {
                      if (Displaycache_book_sort1[i] > bs1_max) // higher prices go first
                      {
                          bs_idx = i;
                          bs1_max = Displaycache_book_sort1[i];
                          bs2_max = Displaycache_book_sort2[i];
                      }
                      //                   ...and anything else equal, later list element goes first --v
                      else if ((Displaycache_book_sort1[i] == bs1_max) && (Displaycache_book_sort2[i] >= bs2_max)) // later timestamp goes first...
                      {
                          bs_idx = i;
                          bs2_max = Displaycache_book_sort2[i];
                      }
                  }
                }
                fprintf(fp, "%s", Displaycache_book[bs_idx]);
                Displaycache_book_done[bs_idx] = true;
            }
            bs_count = 0;

            if (auctioncache_bestask_price > 0)
            {
                // liquidity reward
                int64 tmp_r = 0;
                if (auctioncache_bestask_chronon < gameState.nHeight - GEM_RESET_INTERVAL(fTestNet))
                {
                    tmp_r = gameState.liquidity_reward_remaining;
                    if (auctioncache_bestask_size < tmp_r) tmp_r = auctioncache_bestask_size;
                    tmp_r /= 10;
                    if (auctioncache_bestask_price > gameState.auction_settle_price) tmp_r /= 5;
                    tmp_r -= (tmp_r % 1000000);
                }
                if (tmp_r)
                {
                    fprintf(fp, "\n");
                    fprintf(fp, "best ask, good for liquidity reward:            %5s\n", FormatMoney(tmp_r).c_str());
                }
                else
                {
                    fprintf(fp, "\nbest ask\n");
                }

                fprintf(fp, "%s              %5s at %9s   %d\n", auctioncache_bestask_key.c_str(), FormatMoney(auctioncache_bestask_size).c_str(), FormatMoney(auctioncache_bestask_price).c_str(), auctioncache_bestask_chronon);
            }

            fprintf(fp, "\n\n");
            fprintf(fp, "                                                 gems       price    chronon\n\n");
            if (gameState.auction_last_price > 0)
            {
                fprintf(fp, "last trade                                               %9s   %d\n", FormatMoney(gameState.auction_last_price).c_str(), (int)gameState.auction_last_chronon);
            }
            if (gameState.auction_settle_price > 0)
            {
                fprintf(fp, "settlement, auction start price minimum:                 %9s   %d\n", FormatMoney(gameState.auction_settle_price).c_str(), gameState.nHeight - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
                if (gameState.auction_settle_conservative > 0)
                {
                    fprintf(fp, "settlement, less or equal than last price:               %9s   %d\n", FormatMoney(gameState.auction_settle_conservative).c_str(), gameState.nHeight - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
                    // wouldn't be "feeless" if not the same price
                    fprintf(fp, "settlement, for liquidity fund:                          %9s   %d\n", FormatMoney(gameState.auction_settle_conservative).c_str(), gameState.nHeight - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
                    if (gameState.liquidity_reward_remaining > 0)
                    {
                        fprintf(fp, "                liquidity fund (total):          %5s\n", FormatMoney(gameState.liquidity_reward_remaining).c_str());
                    }
                }
//                fprintf(fp, "->chat message to sell minimum size at auction start price minimum:\n");
//                fprintf(fp, "GEM:HUC set ask %s at %s\n", FormatMoney(AUCTION_MIN_SIZE).c_str(), FormatMoney(gameState.auction_settle_price).c_str());
            }
            fprintf(fp, "\n");

            if (auctioncache_bid_price > 0)
            {
                fprintf(fp, "\n\nactive bid\n");
                fprintf(fp, "                                     hunter\n");
                fprintf(fp, "status                               name        gems\n");
                fprintf(fp, "\n");
                if(gameState.auction_last_chronon >= auctioncache_bid_chronon)
                {
                    fprintf(fp, "done                                 %-10s %5s at %9s\n", auctioncache_bid_name.c_str(), FormatMoney(auctioncache_bid_size).c_str(), FormatMoney(auctioncache_bid_price).c_str());
                }
                else if (gameState.nHeight >= auctioncache_bid_chronon + AUCTION_BID_PRIORITY_TIMEOUT - 5)
                {
                    fprintf(fp, "waiting for timeout %-7d          %-10s %5s at %9s\n", auctioncache_bid_chronon + AUCTION_BID_PRIORITY_TIMEOUT, auctioncache_bid_name.c_str(), FormatMoney(auctioncache_bid_size).c_str(), FormatMoney(auctioncache_bid_price).c_str());
                }
#ifdef AUX_AUCTION_BOT
                else if ((pmon_config_auction_auto_coinmax > 0) &&
                         (auctioncache_bid_name == pmon_config_auction_auto_name))
                {
                    int64 tmp_auto_coins = auctioncache_bid_size / 10000 * auctioncache_bid_price / 10000;
                    fprintf(fp, "auto mode, timeout %-7d           %-10s %5s at %9s\n", auctioncache_bid_chronon + AUCTION_BID_PRIORITY_TIMEOUT, auctioncache_bid_name.c_str(), FormatMoney(auctioncache_bid_size).c_str(), FormatMoney(auctioncache_bid_price).c_str());
                    if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_WAIT2_GREEN) // if chat msg sent
                    {
                        pmon_sendtoaddress(auctioncache_bestask_key, tmp_auto_coins);
                        auction_auto_actual_totalcoins += tmp_auto_coins;

                        pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_DONE_GREEN; // done
                        fprintf(fp, "\nauto mode, sending coins... %s coins spent, session limit is %s", FormatMoney(tmp_auto_coins).c_str(), FormatMoney(pmon_config_auction_auto_coinmax).c_str());
                    }
                    else
                    {
                        fprintf(fp, "\nauto mode, waiting... %s coins spent, session limit is %s", FormatMoney(tmp_auto_coins).c_str(), FormatMoney(pmon_config_auction_auto_coinmax).c_str());
                    }
//                  fprintf(fp, "sendto4ddress %s %s\n", auctioncache_bestask_key.c_str(), FormatMoney(tmp_auto_coins).c_str());
                }
#endif
                else
                {
                    fprintf(fp, "timeout %-7d                      %-10s %5s at %9s\n", auctioncache_bid_chronon + AUCTION_BID_PRIORITY_TIMEOUT, auctioncache_bid_name.c_str(), FormatMoney(auctioncache_bid_size).c_str(), FormatMoney(auctioncache_bid_price).c_str());
                    fprintf(fp, "\n");
                    fprintf(fp, "->console command to buy in manual mode:\n");
                    fprintf(fp, "->gems will be transferred to the address of hunter %s if the transaction confirms until timeout\n", auctioncache_bid_name.c_str());
                    fprintf(fp, "sendtoaddress %s %s\n", auctioncache_bestask_key.c_str(), FormatMoney(auctioncache_bid_size / 10000 * auctioncache_bid_price / 10000).c_str());
                }
            }
            else if (auctioncache_bestask_price > 0)
            {
//                fprintf(fp, "\n\n");
//                fprintf(fp, "->chat message to buy (size and price of best ask):\n");
//                fprintf(fp, "GEM:HUC set bid %s at %s\n", FormatMoney(auctioncache_bestask_size).c_str(), FormatMoney(auctioncache_bestask_price).c_str());

#ifdef AUX_AUCTION_BOT
                if ((pmon_config_auction_auto_coinmax > 0) &&                                // if enabled at all
                    (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_WAITING_BLUE) && // if still alive and waiting for sell order that would match ours
                    (pmon_config_auction_auto_price >= auctioncache_bestask_price))
                {
                    auction_auto_actual_price = auctioncache_bestask_price;
                    auction_auto_actual_amount = pmon_config_auction_auto_size;
                    if (pmon_config_auction_auto_size < auctioncache_bestask_size)
                        auction_auto_actual_amount = pmon_config_auction_auto_size;
                    else
                        auction_auto_actual_amount = auctioncache_bestask_size;

                    pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_GO_YELLOW; // go!
                }
#endif
            }
#ifdef AUX_AUCTION_BOT
//          if ((pmon_config_auction_auto_size > 0) && (pmon_config_auction_auto_price > 0))
            {
                std::string s = "";
                if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_WAITING_BLUE) s = "yes, waiting";
                else if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_STOPPED_WHITE) s = "no (block times/disaster)";
                else if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_STOPPED_RED) s = "no (hunter dead/enemy nearby)";
                else if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_STOPPED_BLUE) s = (pmon_config_auction_auto_coinmax > 0) ? "no (session limit)" : "no (not enabled)";
                else if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_GO_YELLOW) s = "yes, go!";
                else if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_WAIT2_GREEN) s = "yes, msg sent";
                else if (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_DONE_GREEN) s = "no (done -- restart tx monitor to continue)";

                fprintf(fp, "\n");
                fprintf(fp, "this client's auction bot            hunter                 bid                coins\n");
                fprintf(fp, "hidden bid                           name        gems       price              spent/session max    active\n");
                fprintf(fp, "\n");

                fprintf(fp, "                                     %-10s %5s at %9s         %9s/%-9s      %s\n", pmon_config_auction_auto_name.c_str(), FormatMoney(pmon_config_auction_auto_size).c_str(), FormatMoney(pmon_config_auction_auto_price).c_str(), FormatMoney(auction_auto_actual_totalcoins).c_str(), FormatMoney(pmon_config_auction_auto_coinmax).c_str(), s.c_str());

            }
#endif

            if (gameState.auction_settle_price > 0)
            {
                fprintf(fp, "\n\n");
                fprintf(fp, "->chat message to sell minimum size at auction start price minimum:\n");
                fprintf(fp, "GEM:HUC set ask %s at %s\n", FormatMoney(AUCTION_MIN_SIZE).c_str(), FormatMoney(gameState.auction_settle_price).c_str());
                fprintf(fp, "->without automatic downtick:\n");
                fprintf(fp, "GEM:HUC GTC ask %s at %s\n", FormatMoney(AUCTION_MIN_SIZE).c_str(), FormatMoney(gameState.auction_settle_price).c_str());
            }
            if (auctioncache_bid_price <= 0) // no active bid (i.e. best ask reserved for one hunter)
            {
                if (auctioncache_bestask_price > 0) // but there is a best ask
                {
                    fprintf(fp, "\n");
                    fprintf(fp, "->chat message to buy manually (size and price of best ask):\n");
                    fprintf(fp, "GEM:HUC set bid %s at %s\n", FormatMoney(auctioncache_bestask_size).c_str(), FormatMoney(auctioncache_bestask_price).c_str());
                    if (auctioncache_bestask_size > AUX_COIN)
                    {
                        fprintf(fp, "->lines in names.txt to buy automatically (1 gem per trade):\n");
                        fprintf(fp, "config:auctionbot_trade_size %s\n", FormatMoney(AUX_COIN).c_str());
                    }
                    else
                    {
                        fprintf(fp, "->lines in names.txt to buy automatically:\n");
                        fprintf(fp, "config:auctionbot_trade_size %s\n", FormatMoney(auctioncache_bestask_size).c_str());
                    }
                    fprintf(fp, "config:auctionbot_trade_price %s\n", FormatMoney(auctioncache_bestask_price).c_str());
                    fprintf(fp, "config:auctionbot_limit_coins %s\n", FormatMoney(auctioncache_bestask_price / AUX_COIN * auctioncache_bestask_size).c_str());
                }
            }
            // settlement in coins
            if (gameState.auction_settle_conservative > 0)
            {
                fprintf(fp, "\n");

                // delete me
                if (gameState.nHeight < AUX_MINHEIGHT_GEMHUC_SETTLEMENT(fTestNet))
                    fprintf(fp, "*** enabled after chronon %d ***\n", AUX_MINHEIGHT_GEMHUC_SETTLEMENT(fTestNet));

                fprintf(fp, "->example chat message to request settlement of 1 gem at a fixed rate of %s HUC per gem\n", FormatMoney(gameState.auction_settle_conservative).c_str());
                fprintf(fp, "GEM:HUC set ask 1.0 at settlement\n");
            }


            int tmp_oldexp_chronon = gameState.nHeight - (gameState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet));
            if (feedcache_status == FEEDCACHE_EXPIRY)
                tmp_oldexp_chronon = gameState.nHeight - AUX_EXPIRY_INTERVAL(fTestNet);
            int tmp_newexp_chronon = tmp_oldexp_chronon + AUX_EXPIRY_INTERVAL(fTestNet);
            fprintf(fp, "\n\n\n");
            fprintf(fp, " HUC:USD price feed (cached state: %7s)\n", feedcache_status == FEEDCACHE_EXPIRY ? "expiry" : feedcache_status == FEEDCACHE_NORMAL ? "normal" : "unknown");
            fprintf(fp, " ------------------------------------------\n\n");

            fprintf    (fp, "                                     hunter                  updated                   reward\n");
            if (gameState.nHeight < tmp_oldexp_chronon + 50)
            {
                fprintf(fp, "storage vault key                    name         HUC:USD    chronon    weight         (payable %d for prev. feed)\n", tmp_oldexp_chronon + 50);
            }
            else
            {
                fprintf(fp, "storage vault key                    name         HUC:USD    chronon    weight         (payout for prev. feed finished)\n");
            }
            fprintf(fp, "\n");
            BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
            {
                int64 tmp_volume = st.second.nGems;
                int tmp_chronon = st.second.feed_chronon;
                if ((tmp_volume > 0) && (st.second.feed_price > 0) && (tmp_chronon > tmp_oldexp_chronon - AUX_EXPIRY_INTERVAL(fTestNet)))
                {
                    std::string s = (st.second.vaultflags & VAULTFLAG_FEED_REWARD) ? "yes" : "   ";
//                    if (tmp_chronon > tmp_oldexp_chronon)
                        fprintf(fp, "%s   %-10s   %-7s   %7d     %6s         %s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.feed_price).c_str(), tmp_chronon, (tmp_chronon > tmp_oldexp_chronon) ? FormatMoney(tmp_volume).c_str() : " stale", s.c_str());
//                    else if (st.second.vaultflags & VAULTFLAG_FEED_REWARD)
//                        fprintf(fp, "%s   %-10s   %-7s   %7d                    yes, payable %d\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.feed_price).c_str(), tmp_chronon, tmp_oldexp_chronon + 50);
//                    else if (tmp_chronon > tmp_oldexp_chronon - AUX_EXPIRY_INTERVAL(fTestNet))
//                        fprintf(fp, "%s   %-10s   %-7s   %7d     stale\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.feed_price).c_str(), tmp_chronon);
                }
            }
            // quota is in coins, not sats
            int tmp_dividend = gameState.feed_reward_dividend;
            int tmp_divisor = gameState.feed_reward_divisor;
            fprintf(fp, "\n");
            fprintf(fp, "median feed (previous)                            %-7s   %7d                    prev. quota: %d/%d\n", FormatMoney(gameState.feed_prevexp_price).c_str(), tmp_oldexp_chronon, tmp_dividend, tmp_divisor);
            fprintf(fp, "median feed (pending)                             %-7s   %7d                    reward fund: %s\n", FormatMoney(gameState.feed_nextexp_price).c_str(), tmp_newexp_chronon, FormatMoney(gameState.feed_reward_remaining).c_str());
            fprintf(fp, "    participation                                                       %6s/%-6s\n", FormatMoney(feedcache_volume_participation).c_str(), FormatMoney(feedcache_volume_total).c_str());
            fprintf(fp, "    higher than median                                                  %6s\n", FormatMoney(feedcache_volume_bull).c_str());
            fprintf(fp, "    at median                                                           %6s\n", FormatMoney(feedcache_volume_neutral).c_str());
            fprintf(fp, "    lower than median                                                   %6s\n", FormatMoney(feedcache_volume_bear).c_str());
            fprintf(fp, "\n\n");

            fprintf(fp, "->example chat message to feed HUC price:\n");
            fprintf(fp, "HUC:USD feed price %s\n\n\n", FormatMoney(gameState.feed_nextexp_price).c_str());

            // only used for expiry blocks
//          fprintf(fp, "cached reward: %s\n", FormatMoney(feedcache_volume_reward).c_str());

#ifdef PERMANENT_LUGGAGE_TALLY
            fprintf(fp, "\nliquidity reward fund      %9s\n", FormatMoney(gameState.liquidity_reward_remaining).c_str());
            int tmp_intervals = (gameState.nHeight - AUX_MINHEIGHT_FEED(fTestNet)) / GEM_RESET_INTERVAL(fTestNet);
            int64 tmp_gems_me = gameState.gemSpawnState == 1 ? GEM_NORMAL_VALUE : 0;
            int64 tmp_gems_mm = (int64)(GEM_NORMAL_VALUE * tmp_intervals) * 7 / 3 + 700000000;
            tmp_gems_mm -= (tmp_gems_mm % 1000000);
            int64 tmp_gems_ot = (int64)(GEM_NORMAL_VALUE * tmp_intervals) + 300000000;
            fprintf(fp, "messenger                  %9s\n", FormatMoney(tmp_gems_me).c_str());
            fprintf(fp, "reserved for market maker  %9s\n", FormatMoney(tmp_gems_mm).c_str());
            fprintf(fp, "reserved for other NPCs    %9s\n", FormatMoney(tmp_gems_ot).c_str());

            fprintf(fp, "\nheld by players            %9s\n", FormatMoney(count_volume).c_str());
            fprintf(fp, "unspawned                  %9s, including fees payed by players\n", FormatMoney(42000000000000 - gameState.feed_reward_remaining - gameState.liquidity_reward_remaining - tmp_gems_me - tmp_gems_mm - tmp_gems_ot - count_volume).c_str());
#endif
          }
          fclose(fp);
        }
        MilliSleep(20);

#ifdef AUX_STORAGE_VERSION2
        // CRD test
        if (gameState.nHeight >= AUX_MINHEIGHT_TRADE(fTestNet) - 3000)
        {
            // fixme: if it depends on feedcache_status, we can't print out anything if (feedcache_status==0)
            int tmp_oldexp_chronon = gameState.nHeight - (gameState.nHeight % AUX_EXPIRY_INTERVAL(fTestNet));
            if (feedcache_status == FEEDCACHE_EXPIRY)
                tmp_oldexp_chronon = gameState.nHeight - AUX_EXPIRY_INTERVAL(fTestNet);
            int tmp_newexp_chronon = tmp_oldexp_chronon + AUX_EXPIRY_INTERVAL(fTestNet);
#ifdef TRADE_OUTPUT_HTML
            fp = fopen("adv_chrono.html", "w");
            if (fp != NULL)
            {
                fprintf(fp, "<!doctype html>\n");
                fprintf(fp, "<html>\n");
                fprintf(fp, "<head>\n");
                fprintf(fp, "<meta charset=\"utf-8\">\n");
                fprintf(fp, "<meta http-equiv=\"refresh\" content=\"6\" > <!-- refresh every 6 seconds -->\n");
                fprintf(fp, "<title>chronoDollar order book & stats</title>\n");
                fprintf(fp, "<style>\n");
                fprintf(fp, "body {\n");
                fprintf(fp, "        color: white;\n");
                fprintf(fp, "        background-color: #111111;\n");
                fprintf(fp, "</style>\n");
                fprintf(fp, "</head>\n");
                fprintf(fp, "<body>\n");
                fprintf(fp, "<pre>\n");
#else
            fp = fopen("adv_chrono.txt", "w");
            if (fp != NULL)
            {
#endif
                if (gameState.nHeight < AUX_MINHEIGHT_TRADE(fTestNet))
                {
                  fprintf(fp, "Closed until block %d\n", AUX_MINHEIGHT_TRADE(fTestNet));
                }

                fprintf(fp, "\n CRD:GEM open orders (chronon %7d, %s)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet");
                fprintf(fp, " ----------------------------------------------\n\n");
                fprintf(fp, "                                     hunter       ask       ask       order       gems at risk   additional\n");
                fprintf(fp, "storage vault key                    name         size      price     chronon     if filled      P/L if filled   flags\n");
                fprintf(fp, "\n");

                // sorted order book
                int bs_count = 0;
                int64 bs1_max = 0;
                int64 bs2_max = 0;
                int64 bs2_min = AUX_COIN * AUX_COIN;
                int bs_idx = 0;

                BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
                {
                    int64 tmp_ask_size = st.second.ex_order_size_ask;
                    if (tmp_ask_size > 0)
                    {
                        int64 tmp_ask_price = st.second.ex_order_price_ask;
                        int tmp_ask_chronon = st.second.ex_order_chronon_ask;
                        int64 tmp_position_size = st.second.ex_position_size;
                        int64 tmp_position_price = st.second.ex_position_price;

                        // if we get a fill, this will be our (additional) profit or loss
                        int64 pl_a = pl_when_ask_filled(tmp_ask_price, tmp_position_size, tmp_position_price, gameState.crd_prevexp_price * 3);
                        // can go to strike price
                        int64 risk_askorder = risk_after_ask_filled(tmp_ask_size, tmp_ask_price, tmp_position_size, gameState.crd_prevexp_price * 3, st.second.ex_order_flags);

                        std::string s = "";
                        int tmp_orderflags = st.second.ex_order_flags;
                        if (tmp_orderflags & ORDERFLAG_ASK_SETTLE) s += " rollover";
                        if (tmp_orderflags & ORDERFLAG_ASK_INVALID) s += " *no funds*";
                        else if (tmp_orderflags & ORDERFLAG_ASK_ACTIVE) s += " ok";

                        // sorted order book
                        if (bs_count < SORTED_ORDER_BOOK_LINES - 1)
                        {
#ifdef TRADE_OUTPUT_HTML
                            if (tmp_orderflags & (ORDERFLAG_ASK_SETTLE | ORDERFLAG_ASK_INVALID))
                                sprintf(Displaycache_book[bs_count], "<font color=gray>%s   %-10s %6s at %7s     %7d     %10s         %6s     %-11s</font>\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_ask_size).c_str(), FormatMoney(tmp_ask_price).c_str(), tmp_ask_chronon, FormatMoney(risk_askorder).c_str(), FormatMoney(pl_a).c_str(), s.c_str());
                            else
#endif
                                sprintf(Displaycache_book[bs_count], "%s   %-10s %6s at %7s     %7d     %10s         %6s     %-11s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_ask_size).c_str(), FormatMoney(tmp_ask_price).c_str(), tmp_ask_chronon, FormatMoney(risk_askorder).c_str(), FormatMoney(pl_a).c_str(), s.c_str());
                            Displaycache_book_sort1[bs_count] = tmp_ask_price;
                            Displaycache_book_sort2[bs_count] = tmp_ask_chronon;
                            Displaycache_book_done[bs_count] = false;
                            bs_count++;
                        }
                        else
                        {
                            fprintf(fp, "%s   %-10s %6s at %7s     %7d     %10s         %6s     %-11s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_ask_size).c_str(), FormatMoney(tmp_ask_price).c_str(), tmp_ask_chronon, FormatMoney(risk_askorder).c_str(), FormatMoney(pl_a).c_str(), s.c_str());
                        }
                    }
                }
                // sorted order book
                if (bs_count > 0)
                for (int j = 0; j < bs_count; j++)
                {
                  bs1_max = 0;
                  for (int i = 0; i < bs_count; i++)
                  {
                    if (!Displaycache_book_done[i])
                    {
                        if (Displaycache_book_sort1[i] > bs1_max) // higher prices go first
                        {
                            bs_idx = i;
                            bs1_max = Displaycache_book_sort1[i];
                            bs2_max = Displaycache_book_sort2[i];
                        }
                        else if ((Displaycache_book_sort1[i] == bs1_max) && (Displaycache_book_sort2[i] >= bs2_max)) // later timestamp goes first
                        {
                            bs_idx = i;
                            bs2_max = Displaycache_book_sort2[i];
                        }
                    }
                  }
                  fprintf(fp, "%s", Displaycache_book[bs_idx]);
                  Displaycache_book_done[bs_idx] = true;
                }
                bs_count = 0;


                fprintf(fp, "\n");
                if (tradecache_bestask_price > 0)
                {
                    fprintf(fp, "best ask full size = %6s                     %6s at %7s     %7d\n\n", FormatMoney(tradecache_bestask_fullsize).c_str(), FormatMoney(tradecache_bestask_size).c_str(), FormatMoney(tradecache_bestask_price).c_str(), tradecache_bestask_chronon);
                }

                std::string sp = tradecache_is_print ? "matching orders, " : "                 ";
                if(gameState.crd_last_size)
                    fprintf(fp, "%s        last trade:            %6s at %7s     %7d\n\n", sp.c_str(), FormatMoney(gameState.crd_last_size).c_str(), FormatMoney(gameState.crd_last_price).c_str(), gameState.crd_last_chronon);

                if (tradecache_bestbid_price > 0)
                {
                    fprintf(fp, "best bid full size = %6s                     %6s at %7s     %7d\n\n", FormatMoney(tradecache_bestbid_fullsize).c_str(), FormatMoney(tradecache_bestbid_size).c_str(), FormatMoney(tradecache_bestbid_price).c_str(), tradecache_bestbid_chronon);
                }
                fprintf(fp, "                                     hunter       bid       bid       order       gems at risk   additional\n");
                fprintf(fp, "storage vault key                    name         size      price     chronon     if filled      P/L if filled   flags\n");
                fprintf(fp, "\n");
                BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
                {
                  int64 tmp_bid_size = st.second.ex_order_size_bid;
                  if (tmp_bid_size > 0)
                  {
                      int64 tmp_bid_price = st.second.ex_order_price_bid;
                      int tmp_bid_chronon = st.second.ex_order_chronon_bid;
                      int64 tmp_position_size = st.second.ex_position_size;
                      int64 tmp_position_price = st.second.ex_position_price;

                      // if we get a fill, this will be our (additional) profit or loss
                      int64 pl_b = pl_when_bid_filled(tmp_bid_price, tmp_position_size, tmp_position_price);
                      // can drop to 0
                      int64 risk_bidorder = risk_after_bid_filled(tmp_bid_size, tmp_bid_price, tmp_position_size, st.second.ex_order_flags);

                      std::string s = "";
                      int tmp_orderflags = st.second.ex_order_flags;
                      if (tmp_orderflags & ORDERFLAG_BID_SETTLE) s += " rollover";
                      if (tmp_orderflags & ORDERFLAG_BID_INVALID) s += " *no funds*";
                      else if (tmp_orderflags & ORDERFLAG_BID_ACTIVE) s += " ok";

                      // sorted order book
                      if (bs_count < SORTED_ORDER_BOOK_LINES - 1)
                      {
#ifdef TRADE_OUTPUT_HTML
                          if (tmp_orderflags & (ORDERFLAG_ASK_SETTLE | ORDERFLAG_ASK_INVALID))
                              sprintf(Displaycache_book[bs_count], "<font color=gray>%s   %-10s %6s at %7s     %7d     %10s         %6s     %-11s</font>\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_bid_size).c_str(), FormatMoney(tmp_bid_price).c_str(), tmp_bid_chronon, FormatMoney(risk_bidorder).c_str(), FormatMoney(pl_b).c_str(), s.c_str());
                          else
#endif
                              sprintf(Displaycache_book[bs_count], "%s   %-10s %6s at %7s     %7d     %10s         %6s     %-11s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_bid_size).c_str(), FormatMoney(tmp_bid_price).c_str(), tmp_bid_chronon, FormatMoney(risk_bidorder).c_str(), FormatMoney(pl_b).c_str(), s.c_str());
                          Displaycache_book_sort1[bs_count] = tmp_bid_price;
                          Displaycache_book_sort2[bs_count] = tmp_bid_chronon;
                          Displaycache_book_done[bs_count] = false;
                          bs_count++;
                      }
                      else
                      {
                          fprintf(fp, "%s   %-10s %6s at %7s     %7d     %10s         %6s     %-11s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_bid_size).c_str(), FormatMoney(tmp_bid_price).c_str(), tmp_bid_chronon, FormatMoney(risk_bidorder).c_str(), FormatMoney(pl_b).c_str(), s.c_str());
                      }
                  }
                }

                // sorted order book
                if (bs_count > 0)
                for (int j = 0; j < bs_count; j++)
                {
                  bs1_max = 0;
                  for (int i = 0; i < bs_count; i++)
                  {
                    if (!Displaycache_book_done[i])
                    {
                        if (Displaycache_book_sort1[i] > bs1_max) // higher prices go first
                        {
                            bs_idx = i;
                            bs1_max = Displaycache_book_sort1[i];
                            bs2_max = Displaycache_book_sort2[i];
                        }
                        else if ((Displaycache_book_sort1[i] == bs1_max) && (Displaycache_book_sort2[i] < bs2_min)) // earlier timestamp goes first
                        {
                            bs_idx = i;
                            bs2_min = Displaycache_book_sort2[i];
                        }
                    }
                  }
                  fprintf(fp, "%s", Displaycache_book[bs_idx]);
                  Displaycache_book_done[bs_idx] = true;
                }

                fprintf(fp, "\n\n CRD:GEM settlement (chronon %7d, %s, first regular expiration: %7d)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet", AUX_MINHEIGHT_SETTLE(fTestNet));
                fprintf(fp, " --------------------------------------------------------------------------------\n\n");

                if (gameState.auction_settle_price == 0)
                {
                     printf("trade test: ERROR: gameState.auction_settle_price == 0\n");
                }
                else if (gameState.feed_nextexp_price == 0)
                {
                     printf("trade test: ERROR: gameState.feed_nextexp_price == 0\n");
                }
                else if (gameState.crd_prevexp_price > 0)
                {
                    int64 tmp_settlement = (((AUX_COIN * AUX_COIN) / gameState.auction_settle_conservative) * AUX_COIN) / gameState.feed_nextexp_price;
                    tmp_settlement = tradecache_pricetick_down(tradecache_pricetick_up(tmp_settlement)); // snap to grid

                    fprintf(fp, "                                                                      expiry\n");
                    fprintf(fp, "                                                 settlement price     chronon    covered call strike\n\n");
                    fprintf(fp, "previous                                                    %-7s   %7d    %-7s\n", FormatMoney(gameState.crd_prevexp_price).c_str(), tmp_oldexp_chronon, FormatMoney(gameState.crd_prevexp_price * 3).c_str());
                    fprintf(fp, "pending                                                     %-7s   %7d    %-7s\n", FormatMoney(tmp_settlement).c_str(), tmp_newexp_chronon, FormatMoney(tmp_settlement * 3).c_str());
                    if ((gameState.nHeight >= AUX_MINHEIGHT_MM_AI_UPGRADE(fTestNet)) && (tradecache_crd_nextexp_mm_adjusted > 0))
                    {
                    fprintf(fp, "pending (adjusted for market maker)                         %-7s\n", FormatMoney(tradecache_crd_nextexp_mm_adjusted).c_str());
                    }

                    fprintf(fp, "\n\n Order examples (copy and submit into fully synced Huntercoin client)\n");
                    fprintf(fp, " --------------------------------------------------------------------\n\n");

                    fprintf(fp, "->message to buy 1 chronoDollar (and sell 1 covered call)\n");
#ifdef TRADE_OUTPUT_HTML
                    fprintf(fp, "<button id=\"b1\" onclick=\"copyToClipboard(document.getElementById('b1').innerHTML)\">");
                    fprintf(fp, "CRD:GEM set bid 1 at %s", FormatMoney(tmp_settlement).c_str());
                    fprintf(fp, "</button>");
                    fprintf(fp, "\n\n");
                    fprintf(fp, "->message to sell 1 chronoDollar (and buy 1 covered call)\n");
                    fprintf(fp, "<button id=\"b2\" onclick=\"copyToClipboard(document.getElementById('b2').innerHTML)\">");
                    fprintf(fp, "CRD:GEM set ask 1 at %s", FormatMoney(tmp_settlement).c_str());
                    fprintf(fp, "</button>");
                    fprintf(fp, "\n\n");
                    fprintf(fp, "->messages to order rollover of an 1 chronoDollar (long or short) position\n");
                    fprintf(fp, "<button id=\"b3\" onclick=\"copyToClipboard(document.getElementById('b3').innerHTML)\">");
                    fprintf(fp, "CRD:GEM set bid 1 at settlement");
                    fprintf(fp, "</button>");
                    fprintf(fp, "\n");
                    fprintf(fp, "<button id=\"b4\" onclick=\"copyToClipboard(document.getElementById('b4').innerHTML)\">");
                    fprintf(fp, "CRD:GEM set ask 1 at settlement");
                    fprintf(fp, "</button>");
#else
                    fprintf(fp, "CRD:GEM set bid 1 at %s", FormatMoney(tmp_settlement).c_str());
                    fprintf(fp, "\n\n");
                    fprintf(fp, "->message to sell 1 chronoDollar (and buy 1 covered call)\n");
                    fprintf(fp, "CRD:GEM set ask 1 at %s", FormatMoney(tmp_settlement).c_str());
                    fprintf(fp, "\n\n");
                    fprintf(fp, "->messages to order rollover of an 1 chronoDollar (long or short) position\n");
                    fprintf(fp, "CRD:GEM set bid 1 at settlement");
                    fprintf(fp, "\n");
                    fprintf(fp, "CRD:GEM set ask 1 at settlement");
#endif
                    fprintf(fp, "\n\n");
                    fprintf(fp, "notes: - all orders and bitassets are specific to a hunter and its player address\n");
                    fprintf(fp, "       - format for huntercore-qt console, huntercoin-qt console, and all daemon versions:\n");
                    fprintf(fp, "         name_update my_hunter_name {\"msg\":\"my_message\"}\n");
                    fprintf(fp, "       - minimum size: 1 chronoDollar\n");
#ifdef TRADE_OUTPUT_HTML
                    fprintf(fp, "<script>function copyToClipboard(text) { window.prompt(\"Copy to clipboard: Ctrl+C\", text); }</script>");
#endif
                }

                fprintf(fp, "\n\n CRD:GEM trader positions (chronon %7d, %s)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet");
                fprintf(fp, " ---------------------------------------------------\n\n");
                fprintf(fp, "                                                                                 immature\n");
                fprintf(fp, "                                     hunter               chronoDollar   trade   gems from  gems, not     long    bid    bid      ask     ask     short\n");
                fprintf(fp, "storage vault key                    name          gems   position       price   trade P/L   at risk      risk    size   price    price   size    risk    flags\n");
                fprintf(fp, "\n");
                BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
                {
                  if ((st.second.ex_order_size_bid != 0) || (st.second.ex_order_size_ask != 0) ||
                      (st.second.ex_position_size != 0) || (st.second.ex_trade_profitloss != 0) ||
                      (st.second.nGems < 0))
                  {
                      int64 tmp_bid_price = st.second.ex_order_price_bid;
                      int64 tmp_bid_size = st.second.ex_order_size_bid;
                      int64 tmp_ask_price = st.second.ex_order_price_ask;
                      int64 tmp_ask_size = st.second.ex_order_size_ask;

                      int tmp_order_flags = st.second.ex_order_flags;
                      int64 tmp_position_size = st.second.ex_position_size;
                      int64 tmp_position_price = st.second.ex_position_price;

                      // if we get a fill, this will be our (additional) profit or loss
                      // (assume extreme values in case of no order)
                      int64 pl_a = pl_when_ask_filled(tmp_ask_price, tmp_position_size, tmp_position_price, gameState.crd_prevexp_price * 3);
                      int64 pl_b = pl_when_bid_filled(tmp_bid_price, tmp_position_size, tmp_position_price);
                      int64 pl = pl_b < pl_a ? pl_b : pl_a;

                      // can drop to 0
                      int64 risk_bidorder = risk_after_bid_filled(tmp_bid_size, tmp_bid_price, tmp_position_size, tmp_order_flags);
                      // can go to strike price
                      int64 risk_askorder = risk_after_ask_filled(tmp_ask_size, tmp_ask_price, tmp_position_size, gameState.crd_prevexp_price * 3, tmp_order_flags);

                      int64 not_at_risk = pl + st.second.nGems - (risk_bidorder > risk_askorder ? risk_bidorder : risk_askorder);
                      // ignoring "unsettled profits"
                      if (st.second.ex_trade_profitloss < 0) not_at_risk += st.second.ex_trade_profitloss;
                      // if collateral is about to be sold for coins
                      if (st.second.auction_ask_size > 0) not_at_risk -= st.second.auction_ask_size;

                      std::string s = "";
                      if (tmp_order_flags & (ORDERFLAG_BID_SETTLE|ORDERFLAG_BID_INVALID|ORDERFLAG_BID_ACTIVE)) s += " bid:";
                      if (tmp_order_flags & ORDERFLAG_BID_SETTLE) s += " rollover";
                      if (tmp_order_flags & ORDERFLAG_BID_INVALID) s += " *no funds*";
                      else if (tmp_order_flags & ORDERFLAG_BID_ACTIVE) s += " ok";
                      if (tmp_order_flags & (ORDERFLAG_ASK_SETTLE|ORDERFLAG_ASK_INVALID|ORDERFLAG_ASK_ACTIVE)) s += " ask:";
                      if (tmp_order_flags & ORDERFLAG_ASK_SETTLE) s += " rollover";
                      if (tmp_order_flags & ORDERFLAG_ASK_INVALID) s += " *no funds*";
                      else if (tmp_order_flags & ORDERFLAG_ASK_ACTIVE) s += " ok";

                      // not rounded ----------------------------v-----v
                      fprintf(fp, "%s   %-10s %8s    %9s   %6s  %8s   %8s     %6s  %6s  %6s  %6s  %6s  %6s   %s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.nGems).c_str(), FormatMoney(tmp_position_size).c_str(), FormatMoney(tmp_position_price).c_str(), FormatMoney(st.second.ex_trade_profitloss).c_str(), FormatMoney(not_at_risk).c_str(),
                              FormatMoney(risk_bidorder).c_str(), FormatMoney(tmp_bid_size).c_str(), FormatMoney(tmp_bid_price).c_str(), FormatMoney(tmp_ask_price).c_str(), FormatMoney(tmp_ask_size).c_str(), FormatMoney(risk_askorder).c_str(), s.c_str());
                  }
                }

                // market maker
                if (gameState.nHeight >= AUX_MINHEIGHT_TRADE(fTestNet))
                {
                    fprintf(fp, "\n\n HUC:CRD market maker order limits vote\n");
                    fprintf(fp, " --------------------------------------\n\n");
                    fprintf(fp, "                                     hunter\n");
                    fprintf(fp, "storage vault key                    name         max bid   min ask    updated     weight\n");
                    fprintf(fp, "\n");
                    BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
                    {
                        int64 tmp_max_bid = 0;
                        int64 tmp_min_ask = 0;
                        MM_ORDERLIMIT_UNPACK(st.second.ex_vote_mm_limits, tmp_max_bid, tmp_min_ask);
                        if ((tmp_max_bid > 0) && (tmp_min_ask > 0))
                        {
                            int64 tmp_volume = st.second.nGems;
                            int tmp_chronon = st.second.ex_vote_mm_chronon;
                            fprintf(fp, "%s   %-10s   %-7s   %-7s   %7d     %6s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_max_bid).c_str(), FormatMoney(tmp_min_ask).c_str(), tmp_chronon, FormatMoney(tmp_volume).c_str());
                        }
                    }
                    int64 tmp_max_bid = 0;
                    int64 tmp_min_ask = 0;
                    MM_ORDERLIMIT_UNPACK(gameState.crd_mm_orderlimits, tmp_max_bid, tmp_min_ask);

                    fprintf(fp, "\n");
                    fprintf(fp, "->example message to vote MM bid/ask limits:\n");
                    fprintf(fp, "CRD:GEM vote MM max bid %s min ask %s\n", FormatMoney(tmp_max_bid).c_str(), FormatMoney(tmp_min_ask).c_str());
                    fprintf(fp, "\n");
                    fprintf(fp, "median vote                                       max bid   min ask    updated\n\n");
                    fprintf(fp, "current                                           %-7s   %-7s    %d\n", FormatMoney(tmp_max_bid).c_str(), FormatMoney(tmp_min_ask).c_str(), gameState.nHeight);
                    fprintf(fp, "\n");
                    fprintf(fp, "cached volume (max bid): total %s, participation %s, higher than median %s, at median %s, lower than median %s\n", FormatMoney(mmlimitcache_volume_total).c_str(), FormatMoney(mmlimitcache_volume_participation).c_str(), FormatMoney(mmmaxbidcache_volume_bull).c_str(), FormatMoney(mmmaxbidcache_volume_neutral).c_str(), FormatMoney(mmmaxbidcache_volume_bear).c_str());
                    fprintf(fp, "cached volume (min ask): total %s, participation %s, higher than median %s, at median %s, lower than median %s\n", FormatMoney(mmlimitcache_volume_total).c_str(), FormatMoney(mmlimitcache_volume_participation).c_str(), FormatMoney(mmminaskcache_volume_bull).c_str(), FormatMoney(mmminaskcache_volume_neutral).c_str(), FormatMoney(mmminaskcache_volume_bear).c_str());
                    fprintf(fp, "\n");
                }
#ifdef TRADE_OUTPUT_HTML
                fprintf(fp, "</pre>\n");
                fprintf(fp, "</body>\n");
                fprintf(fp, "</html>\n");
#endif
                fclose(fp);
            }
            MilliSleep(20);
        }
#endif

#endif
#endif
    }


    Coord prev_coord;
    int offs = -1;
    BOOST_FOREACH(const PAIRTYPE(Coord, CharacterEntry) &data, sortedPlayers)
    {
        const QString &playerName = data.second.name;
        const CharacterState &characterState = *data.second.state;
        const Coord &coord = characterState.coord;

        if (offs >= 0 && coord == prev_coord)
            offs++;
        else
        {
            prev_coord = coord;
            offs = 0;
        }

        int x = coord.x * TILE_SIZE + offs;
        int y = coord.y * TILE_SIZE + offs * 2;


        // better GUI -- better player sprites
        int color_attack1 = data.second.icon_a1 ==  453 ? 453 : RPG_ICON_EMPTY; // gems and storage

#ifdef AUX_AUCTION_BOT
        int color_defense1 = ((data.second.icon_d1 > 0) && (data.second.icon_d1 < NUM_TILE_IDS)) ? data.second.icon_d1 : RPG_ICON_EMPTY;
        int color_defense2 = ((data.second.icon_d2 > 0) && (data.second.icon_d2 < NUM_TILE_IDS)) ? data.second.icon_d2 : RPG_ICON_EMPTY;
#else
        int color_defense1 = data.second.icon_d1 ==  RPG_ICON_HUC_BANDIT ? RPG_ICON_HUC_BANDIT : RPG_ICON_EMPTY;
        int color_defense2 = data.second.icon_d2 ==  RPG_ICON_HUC_BANDIT ? RPG_ICON_HUC_BANDIT : RPG_ICON_EMPTY;
#endif

        unsigned char tmp_color = data.second.color;
#ifdef PERMANENT_LUGGAGE
#ifdef RPG_OUTFIT_ITEMS
        if (characterState.rpg_gems_in_purse & 1)
        {
            // "mage" sprite
            if (tmp_color == 0) tmp_color = 33;
            else if (tmp_color == 1) tmp_color = 34;
            else if (tmp_color == 2) tmp_color = 35;
            else if (tmp_color == 3) tmp_color = 36;
        }
        else if (characterState.rpg_gems_in_purse & 2)
        {
            // "knight" sprite
            if (tmp_color == 0) tmp_color = 23;
            else if (tmp_color == 1) tmp_color = 15;
            else if (tmp_color == 2) tmp_color = 24;
            else if (tmp_color == 3) tmp_color = 14;
        }
        else if (characterState.rpg_gems_in_purse & 4)
        {
            // "rogue" sprite
            if (tmp_color == 0) tmp_color = 6;
            else if (tmp_color == 1) tmp_color = 7;
            else if (tmp_color == 2) tmp_color = 8;
            else if (tmp_color == 3) tmp_color = 9;
        }
#endif
#endif
        gameMapCache->AddPlayer(playerName, x, y, 1 + offs, tmp_color, color_attack1, color_defense1, color_defense2, characterState.dir, characterState.loot.nAmount);
//        gameMapCache->AddPlayer(playerName, x, y, 1 + offs, data.second.color, color_attack1, color_defense1, color_defense2, characterState.dir, characterState.loot.nAmount);
    }


    //
    // hit+run system
    //
    for (int m = 0; m < PMON_MY_MAX; m++)
    {
        if ((pmon_my_idx[m] < 0) || (pmon_my_idx[m] >= PMON_ALL_MAX)) continue;
//        int my_idx = pmon_my_idx[m];
//        int my_x = pmon_all_x[my_idx];
//        int my_y = pmon_all_y[my_idx];

        if (!(pmon_my_new_wps[m].empty()))
        {
            Coord c1;
            int dn = 2;
            c1 = pmon_my_new_wps[m].back();

            QString tmp_name = QString::fromStdString("\n\n          ");
            tmp_name += QString::fromStdString(pmon_my_names[m]);

            if (pmon_my_tactical_sitch[m] == 0)
            {
                tmp_name += QString::fromStdString(" hit+run");
            }
            else if (pmon_my_tactical_sitch[m] == 1)
            {
                tmp_name += QString::fromStdString(" cornered?");
                dn = 8;
            }
            else
            {
                tmp_name += QString::fromStdString(" punch through");
                dn = 6;
            }
            gameMapCache->AddPlayer(tmp_name, TILE_SIZE * c1.x, TILE_SIZE * c1.y, 1 + 0, 32, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, dn, 0);
        }
    }

    // show first waypoint if desired
    if (pmon_config_show_wps & 2)
    for (int k_all = 0; k_all < pmon_all_count; k_all++)
    {
        if (pmon_all_cache_isinmylist[k_all]) continue; // one of my players

        bool uwp = false;
        int tmp_x = pmon_all_x[k_all];
        int tmp_y = pmon_all_y[k_all];
        int tmp_next_x = pmon_all_next_x[k_all];
        int tmp_next_y = pmon_all_next_y[k_all];
        int tmp_wp_x = pmon_all_wp1_x[k_all];
        int tmp_wp_y = pmon_all_wp1_y[k_all];
        if (IsInsideMap(pmon_all_wp_unconfirmed_x[k_all], pmon_all_wp_unconfirmed_y[k_all]))
        {
            tmp_wp_x = pmon_all_wp_unconfirmed_x[k_all];
            tmp_wp_y = pmon_all_wp_unconfirmed_y[k_all];
            uwp = true;
        }
        if ((tmp_wp_x < 0) || (tmp_wp_y < 0)) continue; // no waypoint at all

        if ((tmp_next_x == tmp_x) && (tmp_next_y == tmp_y)) continue; // (if unconirmed wp has same coors as the hunter)

        int dn = 0;
        if (tmp_next_x > tmp_x)
        {
            if (tmp_next_y > tmp_y) dn = 3;
            else if (tmp_next_y == tmp_y) dn = 6;
            else dn = 9;
        }
        else if (tmp_next_x == tmp_x)
        {
            if (tmp_next_y > tmp_y) dn = 2;
            else if (tmp_next_y < tmp_y) dn = 8;
        }
        else
        {
            if (tmp_next_y > tmp_y) dn = 1;
            else if (tmp_next_y == tmp_y) dn = 4;
            else dn = 7;
        }
        if (dn == 0) continue; // no valid dir (if unconirmed wp has same coors as the hunter)

        QString tmp_name = QString::fromStdString("\n");
        tmp_name += QString::fromStdString(pmon_all_names[k_all]);
        if (uwp)
            tmp_name += QString::fromStdString("'s wp (predicted):");
        else
            tmp_name += QString::fromStdString("'s wp:");
        tmp_name += QString::number(tmp_wp_x);
        tmp_name += QString::fromStdString(",");
        tmp_name += QString::number(tmp_wp_y);
        gameMapCache->AddPlayer(tmp_name, TILE_SIZE * tmp_wp_x, TILE_SIZE * tmp_wp_y, 1 + 0, 32, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, dn, 0);

        // show next position if desired
        if (pmon_config_show_wps & 1)
        if ((tmp_next_x != tmp_wp_x) || (tmp_next_y != tmp_wp_y))
        {
            tmp_name = QString::fromStdString("\n");
            tmp_name += QString::fromStdString(pmon_all_names[k_all]);
            tmp_name += QString::fromStdString(":");
            tmp_name += QString::number(tmp_next_x);
            tmp_name += QString::fromStdString(",");
            tmp_name += QString::number(tmp_next_y);
            gameMapCache->AddPlayer(tmp_name, TILE_SIZE * tmp_next_x, TILE_SIZE * tmp_next_y, 1 + 0, 32, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, dn, 0);
        }

        // show final waypoint if desired
        if (pmon_config_show_wps & 4)
        {
            int tmp_wpdest_x = pmon_all_wpdest_x[k_all];
            int tmp_wpdest_y = pmon_all_wpdest_y[k_all];
            if ((tmp_wpdest_x != tmp_wp_x) || (tmp_wpdest_y != tmp_wp_y))
            if ((tmp_wpdest_x != tmp_next_x) || (tmp_wpdest_y != tmp_next_y))
            if (IsInsideMap(tmp_wpdest_x, tmp_wpdest_y))
            {
                tmp_name = QString::fromStdString("\n");
                tmp_name += QString::fromStdString(pmon_all_names[k_all]);
                tmp_name += QString::fromStdString("'s dest.");
                tmp_name += QString::number(tmp_wpdest_x);
                tmp_name += QString::fromStdString(",");
                tmp_name += QString::number(tmp_wpdest_y);
                gameMapCache->AddPlayer(tmp_name, TILE_SIZE * tmp_wpdest_x, TILE_SIZE * tmp_wpdest_y, 1 + 0, 32, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 2, 0);
            }
        }
    }


    // better GUI -- banks
    // note: players need unique names
    for (int m = 0; m < bank_idx; m++)
    {
        QString tmp_name = QString::fromStdString("bank");
        tmp_name += QString::number(m);
        tmp_name += QString::fromStdString(":");
        tmp_name += QString::number(bank_timeleft[m]);
        tmp_name += QString::fromStdString(" ");
        for (int tl = 0; tl < bank_timeleft[m]; tl++)
            tmp_name += QString::fromStdString("|");

//        int bs = 13;
//        if (m % 7 == 1) bs = 10;
//        else if (m % 7 == 2) bs = 11;
        int bs = 10;
        if (m % 7 >= 3) bs = 11;
        int bd = (m % 9) + 1;
        if (bd == 5) bd = 2;
        gameMapCache->AddPlayer(tmp_name, TILE_SIZE * bank_xpos[m], TILE_SIZE * bank_ypos[m], 1 + 0, bs, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, bd, 0);
    }

// don't display in safemode, the client side prediction is no longer reliable
#ifdef PERMANENT_LUGGAGE
    // gems and storage
    if (gem_visualonly_state == GEM_SPAWNED)
    {
        gameMapCache->AddPlayer("Tia'tha '1 soul gem here, for free'", TILE_SIZE * gem_visualonly_x, TILE_SIZE * gem_visualonly_y, 1 + 0, 20, 453, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 2, 0);
    }
#endif

#ifdef AUX_STORAGE_ZHUNT
    if (gem_visualonly_state == GEM_SPAWNED)
    {
        gameMapCache->AddPlayer("Tia's ghost '1 soul gem here, for free'", TILE_SIZE * ZHUNT_GEM_SPOINT_X, TILE_SIZE * ZHUNT_GEM_SPOINT_Y, 1 + 0, 20, 453, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 6, 0);
    }
    if (gameState.zhunt_gemSpawnState == GEM_SPAWNED)
    {
        gameMapCache->AddPlayer("Arvi'ius '4 soul gems here, for free'", TILE_SIZE * ZHUNT_GEM_SPOINT2_X, TILE_SIZE * ZHUNT_GEM_SPOINT2_Y, 1 + 0, 25, 453, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 6, 0);
    }
#endif

#ifdef PERMANENT_LUGGAGE
#ifndef RPG_OUTFIT_NPCS
    else
    {
        // "legacy version"
        QString qs = QString::fromStdString("Tia'tha 'next gem at ");
        qs += QString::number(gameState.nHeight - (gameState.nHeight % GEM_RESET_INTERVAL(fTestNet)) + GEM_RESET_INTERVAL(fTestNet));
        qs += QString::fromStdString("'");
        gameMapCache->AddPlayer(qs, TILE_SIZE * 128, TILE_SIZE * 486, 1 + 0, 20, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 2, 0);
    }
#endif
#ifdef RPG_OUTFIT_NPCS
    for (int tmp_npc = 0; tmp_npc < RPG_NUM_NPCS; tmp_npc++)
    {
        int tmp_interval = fTestNet ? rpg_interval_tnet[tmp_npc] : rpg_interval[tmp_npc];
        int tmp_timeshift = fTestNet ? rpg_timeshift_tnet[tmp_npc] : rpg_timeshift[tmp_npc];
        int tmp_finished = fTestNet ? rpg_finished_tnet[tmp_npc] : rpg_finished[tmp_npc];
        int tmp_step = 0;
        int tmp_pos = 0;

        if (tmp_npc == 5)
        {
            if (gem_visualonly_state == GEM_SPAWNED)
               continue;

            int t0 = gameState.nHeight - (gameState.nHeight % GEM_RESET_INTERVAL(fTestNet)) + GEM_RESET_INTERVAL(fTestNet);
            t0 -= 12; // -= RPG_PATH_LEN
            tmp_step = gameState.nHeight - t0;
            if (tmp_step < 0)
            {
                QString qs = QString::fromStdString("Tia'tha 'next gem at ");
                qs += QString::number(gameState.nHeight - (gameState.nHeight % GEM_RESET_INTERVAL(fTestNet)) + GEM_RESET_INTERVAL(fTestNet));
                qs += QString::fromStdString("'");
                gameMapCache->AddPlayer(qs, TILE_SIZE * 128, TILE_SIZE * 486, 1 + 0, 20, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 2, 0);

                continue;
            }
        }
        else
        {
            tmp_step = (gameState.nHeight + tmp_timeshift) % tmp_interval;
        }

        tmp_pos = tmp_step >= 0 ? tmp_step : 0;
        if (tmp_pos >= RPG_PATH_LEN) tmp_pos = RPG_PATH_LEN - 1;

        if (tmp_step >= tmp_finished) tmp_pos = 0;
        QString tmp_name = QString::fromStdString(rpg_npc_name[tmp_npc]);
        if (tmp_pos == RPG_PATH_LEN - 1)
        {
            if (tmp_npc == 0) tmp_name += QString::fromStdString(" 'mage outfit here, for free'");
            else if (tmp_npc == 1) tmp_name += QString::fromStdString(" 'fighter outfit here, for free'");
            else if (tmp_npc == 2) tmp_name += QString::fromStdString(" 'rogue outfit here, for free'");
        }

#ifdef RPG_OUTFIT_DEBUG
            tmp_name += QString::fromStdString(" step:");
            tmp_name += QString::number(tmp_step);
#endif

        int xn = rpg_path_x[tmp_npc][tmp_pos];
        int yn = rpg_path_y[tmp_npc][tmp_pos];
        int dn = rpg_path_d[tmp_npc][tmp_pos];
        gameMapCache->AddPlayer(tmp_name, TILE_SIZE * xn, TILE_SIZE * yn, 1 + 0, rpg_sprite[tmp_npc], RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, dn, 0);
    }
#endif

#ifdef AUX_STORAGE_ZHUNT
    if ((gameState.nHeight >= AUX_MINHEIGHT_ZHUNT(fTestNet)) &&
        (!(pmon_config_show_wps & 16)))
    {
      BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
      {
        if ((st.second.zhunt_chronon > 0) && (gameState.nHeight < st.second.zhunt_chronon + ZHUNT_MAX_LIFETIME))
        {
            int xn = st.second.ai_coord.x;
            int yn = st.second.ai_coord.y;
            if (IsInsideMap(xn, yn))
            {
                if ((st.second.zhunt_order.length() >= 8) && (st.second.ai_life == 0))
                {
                    std::string s_zhunt = st.second.zhunt_order;
                    QString tmp_name = QString::fromStdString(st.second.huntername);

                    if (s_zhunt[0] == '4') tmp_name += QString::fromStdString("'s lemure");
                    else tmp_name += QString::fromStdString("'s zombie");

                    if (st.second.ai_state & ZHUNT_STATE_WRONGPLACE)
                    {
                        tmp_name += QString::fromStdString(" was in the wrong place");
                    }
                    else if (st.second.ai_state2 & ZHUNT_STATE2_OUTOFTIME)
                    {
                        tmp_name += QString::fromStdString(" starved to death");
                    }
                    else if (st.second.ai_state2 & ZHUNT_STATE2_NOWCOLD)
                    {
                        tmp_name += QString::fromStdString(" was incinerated");
                    }
                    else
                    {
                        tmp_name += QString::fromStdString(" died");
                    }

                    gameMapCache->AddPlayer(tmp_name, TILE_SIZE * xn, TILE_SIZE * yn, 1 + 0, 31, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 1, 0);
                }
                else if ((st.second.zhunt_order.length() >= 8) && (st.second.ai_life > 0))
                {
                    std::string s_zhunt = st.second.zhunt_order;
                    QString tmp_name = QString::fromStdString(st.second.huntername);

                    if ((st.second.ai_dir >= 0) && (st.second.ai_dir <= 7))
                    {
                        if (s_zhunt[0] == '4')
                        {
                            int dn = pmon_24spritedirs_clockwise[st.second.ai_dir];

                            // individual attack range
                            int tmp_myrange = st.second.zhunt_order[3] - '0';
                            if (tmp_myrange > ZHUNT_MAX_ATTACK_RANGE) tmp_myrange = ZHUNT_MAX_ATTACK_RANGE;

                            tmp_name += QString::fromStdString("'s lemure ");
                            tmp_name += QString::number(xn);
                            tmp_name += QString::fromStdString(",");
                            tmp_name += QString::number(yn);

                            tmp_name += QString::fromStdString(" [");
                            tmp_name += QString::number(tmp_myrange);
                            tmp_name += QString::fromStdString("] l:");
                            tmp_name += QString::number(st.second.ai_life);
                            tmp_name += QString::fromStdString(" m:");
                            tmp_name += QString::number(st.second.ai_magicka);

                            if (st.second.ai_state & ZHUNT_STATE_FIREBALL)
                                tmp_name += QString::fromStdString(" 'Burn!'");
                            if (st.second.ai_state & ZHUNT_STATE_DIBS)
                                tmp_name += QString::fromStdString(" 'Dibs on the gem!'");
#ifdef AUX_STORAGE_ZHUNT_INFIGHT
                            if (st.second.ai_state & ZHUNT_STATE_ZAP)
                                tmp_name += QString::fromStdString(" 'Take that!'");
                            if (st.second.ai_state & ZHUNT_STATE_HOT)
                            {
                                QString tmp_name2 = QString::fromStdString("\n          ");
                                tmp_name2 += QString::fromStdString(st.second.huntername);
                                tmp_name2 += QString::fromStdString(" takes ");
                                tmp_name2 += QString::number(gameState.zhunt_RNG / 2);
                                tmp_name2 += QString::fromStdString(" damage");
                                gameMapCache->AddPlayer(tmp_name2, TILE_SIZE * xn, TILE_SIZE * yn, 1 + 1, 30, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 3, 0);
                            }
#endif
                            gameMapCache->AddPlayer(tmp_name, TILE_SIZE * xn, TILE_SIZE * yn, 1 + 0, 29, RPG_ICON_FIRE, RPG_ICON_EMPTY, RPG_ICON_EMPTY, dn, 0);
                        }
                        else // if (s_zhunt[0] == '3')
                        {
                            int dn = pmon_24spritedirs_clockwise[st.second.ai_dir];

                            int tmp_myblinkrange = st.second.zhunt_order[3] - '0';
                            int tmp_myfreezerange = st.second.zhunt_order[4] - '0';

                            tmp_name += QString::fromStdString("'s zombie ");
                            tmp_name += QString::number(xn);
                            tmp_name += QString::fromStdString(",");
                            tmp_name += QString::number(yn);

                            tmp_name += QString::fromStdString(" [");
                            tmp_name += QString::number(tmp_myblinkrange);
                            tmp_name += QString::fromStdString("/");
                            tmp_name += QString::number(tmp_myfreezerange);
                            tmp_name += QString::fromStdString("]");
                            int dist = zhunt_distancemap[yn][xn];
                            if (dist > 0)
                            {
                                tmp_name += QString::fromStdString(" d:");
                                tmp_name += QString::number(zhunt_distancemap[yn][xn]);
                            }

                            if (st.second.ai_state & ZHUNT_STATE_WAIT)
                                tmp_name += QString::fromStdString(" waiting..");
                            if (st.second.ai_state & ZHUNT_STATE_BLINK)
                                tmp_name += QString::fromStdString(" 'Blink!'");
                            if (st.second.ai_state & ZHUNT_STATE_DIBS)
                                tmp_name += QString::fromStdString(" 'Dibs on the gem!'");

                            if (st.second.ai_state2 & ZHUNT_STATE2_TOOHOT)
                                gameMapCache->AddPlayer(tmp_name, TILE_SIZE * xn, TILE_SIZE * yn, 1 + 1, 30, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 1, 0);
                            else
                                gameMapCache->AddPlayer(tmp_name, TILE_SIZE * xn, TILE_SIZE * yn, 1 + 0, 12, RPG_ICON_EMPTY, RPG_ICON_WORD_RECALL, RPG_ICON_EMPTY, dn, 0);
                        }
                    }
                }
            }
        }
      }
    }
#endif

#endif


    gameMapCache->EndCachedScene();

    if (!gameState.crownHolder.player.empty())
        crown->hide();
    else
    {
        crown->show();
        crown->setOffset(gameState.crownPos.x * TILE_SIZE, gameState.crownPos.y * TILE_SIZE);
    }

    //scene->invalidate();
    repaint(rect());
}

static void DrawPath(const std::vector<Coord> &coords, QPainterPath &path)
{
    if (coords.empty())
        return;

    for (int i = 0; i < coords.size(); i++)
    {
        QPointF p((coords[i].x + 0.5) * TILE_SIZE, (coords[i].y + 0.5) * TILE_SIZE);
        if (i == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
}

void GameMapView::SelectPlayer(const QString &name, const GameState &state, QueuedMoves &queuedMoves)
{
    // Clear old path
    DeselectPlayer();

    if (name.isEmpty())
        return;

    QPainterPath path, queuedPath;

    std::map<PlayerID, PlayerState>::const_iterator mi = state.players.find(name.toStdString());
    if (mi == state.players.end())
        return;

    BOOST_FOREACH(const PAIRTYPE(int, CharacterState) &pc, mi->second.characters)
    {
        int i = pc.first;
        const CharacterState &ch = pc.second;

        DrawPath(ch.DumpPath(), path);

        std::vector<Coord> *p = UpdateQueuedPath(ch, queuedMoves, CharacterID(mi->first, i));
        if (p)
        {
            std::vector<Coord> wp = PathToCharacterWaypoints(*p);
            DrawPath(ch.DumpPath(&wp), queuedPath);
        }
    }
    if (!path.isEmpty())
    {
        playerPath = scene->addPath(path, grobjs->magenta_pen);
        playerPath->setZValue(1e9 + 1);
    }
    if (!queuedPath.isEmpty())
    {
        queuedPlayerPath = scene->addPath(queuedPath, grobjs->gray_pen);
        queuedPlayerPath->setZValue(1e9 + 2);
    }

    use_cross_cursor = true;
    if (!panning)
        setCursor(Qt::CrossCursor);
}

void GameMapView::CenterMapOnCharacter(const Game::CharacterState &state)
{
    centerOn((state.coord.x + 0.5) * TILE_SIZE, (state.coord.y + 0.5) * TILE_SIZE);
}

void GameMapView::DeselectPlayer()
{
    if (playerPath || queuedPlayerPath)
    {
        if (playerPath)
        {
            scene->removeItem(playerPath);
            delete playerPath;
            playerPath = NULL;
        }
        if (queuedPlayerPath)
        {
            scene->removeItem(queuedPlayerPath);
            delete queuedPlayerPath;
            queuedPlayerPath = NULL;
        }
        //scene->invalidate();
        repaint(rect());
    }
    use_cross_cursor = false;
    if (!panning)
        setCursor(Qt::ArrowCursor);
}

const static double MIN_ZOOM = 0.1;
const static double MAX_ZOOM = 2.0;

void GameMapView::mousePressEvent(QMouseEvent *event)
{   
    if (event->button() == Qt::LeftButton)
    {
        QPoint p = mapToScene(event->pos()).toPoint();
        int x = p.x() / TILE_SIZE;
        int y = p.y() / TILE_SIZE;
        if (IsInsideMap(x, y))
            emit tileClicked(x, y, event->modifiers().testFlag( Qt::ControlModifier ));
    }
    else if (event->button() == Qt::RightButton)
    {
        panning = true;
        setCursor(Qt::ClosedHandCursor);
        pan_pos = event->pos();
    }
    else if (event->button() == Qt::MiddleButton)
    {
        // pending tx monitor -- middle mouse button
        if (event->modifiers().testFlag( Qt::ControlModifier ))
        {
            pmon_noisy = pmon_noisy ? false : true;

            if (pmon_noisy)
                for (int m = 0; m < PMON_MY_MAX; m++)
                {
                    if (pmon_my_alarm_state[m] == 1)
                         pmon_my_alarm_state[m] = 5;
                    else if (pmon_my_alarm_state[m] == 2)
                         pmon_my_alarm_state[m] = 6;
                }
        }
        else if ( ! (event->modifiers().testFlag( Qt::ShiftModifier )) )
        {
            if (pmon_state == PMONSTATE_STOPPED)
                pmon_state = PMONSTATE_START;
            else
                pmon_state = PMONSTATE_SHUTDOWN;
        }
        else
        {
            QPoint p = mapToScene(event->pos()).toPoint();
            zoomReset();
            centerOn(p);
        }
    }
    event->accept();
}

void GameMapView::mouseReleaseEvent(QMouseEvent *event)
{   
    if (event->button() == Qt::RightButton)
    {
        panning = false;
        setCursor(use_cross_cursor ? Qt::CrossCursor : Qt::ArrowCursor);
    }
    event->accept();
}

void GameMapView::mouseMoveEvent(QMouseEvent *event)
{
    if (panning)
    {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + pan_pos.x() - event->pos().x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + pan_pos.y() - event->pos().y());
        pan_pos = event->pos();
    }
    event->accept();
}

void GameMapView::wheelEvent(QWheelEvent *event)
{
    double delta = event->delta() / 120.0;

    // If user moved the wheel in another direction, we reset previously scheduled scalings
    if ((scheduledZoom > zoomFactor && delta < 0) || (scheduledZoom < zoomFactor && delta > 0))
        scheduledZoom = zoomFactor;

    scheduledZoom *= std::pow(1.2, delta);
    oldZoom = zoomFactor;

    animZoom->stop();
    if (scheduledZoom != zoomFactor)
        animZoom->start();

    event->accept();
}

void GameMapView::zoomIn()
{
    if (scheduledZoom < zoomFactor)
        scheduledZoom = zoomFactor;
    scheduledZoom *= 1.2;
    oldZoom = zoomFactor;

    animZoom->stop();
    if (scheduledZoom != zoomFactor)
        animZoom->start();
}

void GameMapView::zoomOut()
{
    if (scheduledZoom > zoomFactor)
        scheduledZoom = zoomFactor;
    scheduledZoom /= 1.2;
    oldZoom = zoomFactor;

    animZoom->stop();
    if (scheduledZoom != zoomFactor)
        animZoom->start();
}

void GameMapView::zoomReset()
{
    animZoom->stop();
    oldZoom = zoomFactor = scheduledZoom = 1.0;

    resetTransform();
    setRenderHints(defaultRenderHints);
}

void GameMapView::scalingTime(qreal t)
{
    if (t > 0.999)
        zoomFactor = scheduledZoom;
    else
        zoomFactor = oldZoom * (1.0 - t) + scheduledZoom * t;
        //zoomFactor = std::exp(std::log(oldZoom) * (1.0 - t) + std::log(scheduledZoom) * t);

    if (zoomFactor > MAX_ZOOM)
        zoomFactor = MAX_ZOOM;
    else if (zoomFactor < MIN_ZOOM)
        zoomFactor = MIN_ZOOM;

    resetTransform();
    scale(zoomFactor, zoomFactor);

    if (zoomFactor < 0.999)
        setRenderHints(defaultRenderHints | QPainter::SmoothPixmapTransform);
    else
        setRenderHints(defaultRenderHints);
}

void GameMapView::scalingFinished()
{
    // This may be redundant, if QTimeLine ensures that last frame is always procesed
    scalingTime(1.0);
}
