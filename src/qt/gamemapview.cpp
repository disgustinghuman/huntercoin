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
        player_text_brush[1] = QBrush(QColor(255, 80, 80));
        player_text_brush[2] = QBrush(QColor(100, 255, 100));
        player_text_brush[3] = QBrush(QColor(0, 170, 255));

        // better GUI -- more player sprites
        player_text_brush[4] = QBrush(QColor(255, 255, 255)); // NPCs
        player_text_brush[5] = QBrush(QColor(255, 255, 255));

        player_text_brush[6] = QBrush(QColor(255, 255, 100)); // yellow
        player_text_brush[7] = QBrush(QColor(255, 80, 80));   // red
        player_text_brush[8] = QBrush(QColor(100, 255, 100)); // green
        player_text_brush[9] = QBrush(QColor(0, 170, 255));   // blue

        player_text_brush[10] = QBrush(QColor(255, 255, 255)); // NPCs
        player_text_brush[11] = QBrush(QColor(255, 255, 255));
        player_text_brush[12] = QBrush(QColor(255, 255, 255));
        player_text_brush[13] = QBrush(QColor(255, 255, 255));

        player_text_brush[14] = QBrush(QColor(0, 170, 255)); // blue
        player_text_brush[15] = QBrush(QColor(255, 80, 80)); // red

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
        player_text_brush[34] = QBrush(QColor(255, 80, 80));   // red
        player_text_brush[35] = QBrush(QColor(100, 255, 100)); // green
        player_text_brush[36] = QBrush(QColor(0, 170, 255));   // blue

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


            // better GUI -- better player sprites
            color_a1 = color_a1_;
            color_d1 = color_d1_;
            color_d2 = color_d2_;
            shadow_sprite1 = scene->addPixmap(grobjs->tiles[260]);
            shadow_sprite1->setOffset(x, y);
            shadow_sprite1->setZValue(z_order);
            shadow_sprite1->setOpacity(0.4);
            shadow_sprite2 = scene->addPixmap(grobjs->tiles[261]);
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
#define SHADOWMAP_AAOBJECT_MAX 129
#define SHADOWMAP_AAOBJECT_MAX_ONLY_YELLOW_GRASS 127
#define SHADOWMAP_AAOBJECT_MAX_NO_GRASS 126
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

                                                  { 0, 0, '1', 268}, // yellow grass -- "conditional" objects are last in this list
                                                  { 0, 0, '0', 263}, // grass -- "conditional" objects are last in this list
                                                  { 0, 0, '.', 266}, // grass -- "conditional" objects are last in this list
                                                 };
// to parse the asciiart map (shadows)
#define SHADOWMAP_AASHAPE_MAX 72
#define SHADOWMAP_AASHAPE_MAX_CLIFFCORNER 28
int ShadowAAShapes[SHADOWMAP_AASHAPE_MAX][5] = {{ 0, 0, 'C', 'c', 244}, // conifer, important shadow tiles
                                                { 0, -1, 'C', 'c', 247},

                                                { 1, 0, 'B', 'b', 237},  // broadleaf, important shadow tiles
                                                { 0, 0, 'B', 'b', 238},
                                                { 1, -1, 'B', 'b', 240},
                                                { 0, -1, 'B', 'b', 241},

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

                                                { 2, 0, 'B', 'b', 236},  // broadleaf, small shadow tiles (skipped if layers are full)
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
                        if (tile == 259)
                            if (Display_xorshift128plus() & 1)
                                tile = 262;

                        if ((AsciiArtMap[y][x] == '"') || (AsciiArtMap[y][x] == '\'') || (AsciiArtMap[y][x] == 'v'))
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
static int wmon_CoordStep(int x, int target)
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
static int wmon_CoordUpd(int u, int v, int du, int dv, int from_u, int from_v)
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
            const Coord &wmon_from = characterState.from;
            if (((pmon_state == PMONSTATE_CONSOLE) || (pmon_state == PMONSTATE_RUN)) &&
                (pmon_all_count < PMON_ALL_MAX))
            {
                pmon_all_names[pmon_all_count] = chid.ToString();
                pmon_all_x[pmon_all_count] = coord.x;
                pmon_all_y[pmon_all_count] = coord.y;
                pmon_all_color[pmon_all_count] = pl.color;

//                entry.icon_d1 = 0;
//                entry.icon_d2 = 0;
                if (pl.value > 40000000000)
                    entry.icon_d1 = RPG_ICON_HUC_BANDIT400;
                else if (pl.value > 20000000000)
                    entry.icon_d1 = RPG_ICON_HUC_BANDIT;
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

                if (!(characterState.waypoints.empty()))
                {
                    Coord new_c;
                    target = characterState.waypoints.back();


                    int dx = target.x - wmon_from.x;
                    int dy = target.y - wmon_from.y;

                    if (abs(dx) > abs(dy))
                    {
                        new_c.x = wmon_CoordStep(coord.x, target.x);
                        new_c.y = wmon_CoordUpd(new_c.x, coord.y, dx, dy, wmon_from.x, wmon_from.y);
                    }
                    else
                    {
                        new_c.y = wmon_CoordStep(coord.y, target.y);
                        new_c.x = wmon_CoordUpd(new_c.y, coord.x, dy, dx, wmon_from.y, wmon_from.x);
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
                                    entry.name += QString::fromStdString(" *ALARM*:");
                                }
                                else
                                {
                                    entry.name += QString::fromStdString(",");
                                }
                                entry.name += QString::fromStdString(pmon_my_names[m2]);
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


                        // notice heavy loot and nearby bank
                        if (gameState.IsBank(coord))
                            pmon_my_bankstate[m] = 3;
                        // reset to 0 if just stepped off a bank tile
                        else if ((pmon_my_bankstate[m] != 0) && (characterState.loot.nAmount == 0))
                            pmon_my_bankstate[m] = 0;
                        // player did something after notification
                        else if ((pmon_my_bankstate[m] >= 1) && (pmon_my_bankstate[m] <= 2) && (pending_tx_idx >= 0))
                            pmon_my_bankstate[m] = 3;

                        if ( (pmon_my_bankstate[m] != 3) && (pending_tx_idx == -1) && (pmon_my_bankdist[m] <= pmon_config_bank_notice) &&
                             ((pmon_config_afk_leave) || (characterState.loot.nAmount / 100000000 >= pmon_config_loot_notice)) )
                        {
                            if (pmon_my_bankstate[m] < 2)
                                pmon_my_bankstate[m] = ((characterState.loot.nAmount < 10000000000) ? 2 : 1);

                            bool tmp_on_my_way = false;
                            if ( (!characterState.waypoints.empty()) &&
                                ((gameState.IsBank(characterState.waypoints.back())) || (gameState.IsBank(characterState.waypoints.front()))) )
                                tmp_on_my_way = true;

                            if (tmp_on_my_way)
                            {
                                pmon_my_bankstate[m] = 3;
                            }
                            else
                            {
                                if (pmon_need_bank_idx <= -1)
                                    pmon_need_bank_idx = m;
                                else if ((pmon_need_bank_idx < PMON_MY_MAX) && (pmon_my_bankdist[m] < pmon_my_bankdist[pmon_need_bank_idx]))
                                    pmon_need_bank_idx = m;
                            }

                            if ((pmon_need_bank_idx == m) && (pmon_my_bankstate[m] != 3) && (pmon_my_bankdist[m] <= pmon_config_bank_notice) &&
                                (pmon_my_bank_x[m] >= 0) && (pmon_my_bank_y[m] >= 0))
                            if (pmon_config_afk_leave)
                            {
                                pmon_name_update(m, pmon_my_bank_x[m], pmon_my_bank_y[m]);
                                pmon_my_bankstate[m] = 3;
                            }
                        }

                        if ((pmon_my_bankstate[m] == 0) || (pmon_my_bankstate[m] == 3))
                        {
                            if (pmon_need_bank_idx == m)
                                pmon_need_bank_idx = -1;
                        }

#ifdef AUX_AUCTION_BOT
                        if ((tmp_auction_auto_on_duty) && ((pmon_my_foecontact_age[m] > 0) || (pmon_my_foe_dist[m] <= pmon_my_alarm_dist[m])))
                            pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_RED; // stopped, enemy nearby
                        else if (gameState.nHeight - gameState.nDisasterHeight < pmon_config_warn_disaster)
                            pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_WHITE; // stopped, blockchain problem
                        else if ((auction_auto_actual_totalcoins + (pmon_config_auction_auto_size / 10000 * pmon_config_auction_auto_price / 10000)) > pmon_config_auction_auto_coinmax)
                            pmon_config_auction_auto_stateicon = RPG_ICON_ABSTATE_STOPPED_BLUE; // stopped, session limit reached

                        if ((pmon_config_auction_auto_coinmax > 0) && (tmp_auction_auto_on_duty) && (!pmon_config_afk_leave) && (pmon_config_auction_auto_stateicon == RPG_ICON_ABSTATE_GO_YELLOW)) // if go!
                        {
//                          if ((pending_tx_idx == -1) &&
//                              (pmon_my_foecontact_age[m] == 0) &&
//                              (pmon_my_foe_dist[m] > pmon_my_alarm_dist[m]))
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
                        else if ((pmon_need_bank_idx >= 0) && (pmon_need_bank_idx < PMON_MY_MAX))
                        {
                            if (pmon_my_bankstate[pmon_need_bank_idx] == 2)
                                entry.name += QString::fromStdString(" Bank:");
                            else if (pmon_my_bankstate[pmon_need_bank_idx] == 1)
                                entry.name += QString::fromStdString(" Full:");

                            entry.name += QString::fromStdString(pmon_my_names[pmon_need_bank_idx]);

                            if (pmon_my_bankdist[pmon_need_bank_idx] <= 15)
                            {
                                 entry.name += QString::fromStdString(" d=");
                                 entry.name += QString::number(pmon_my_bankdist[pmon_need_bank_idx]);
                            }
                        }
                        else if (!tmp_alarm)
                        {
                            entry.name += QString::fromStdString(" (OK)");
                        }

                        if (!pmon_noisy)
                            entry.name += QString::fromStdString(" (silent)");

                        if ((pmon_config_afk_leave) && (pmon_my_bankstate[m] != 3))
                        {
                            entry.name += QString::fromStdString(" Leaving:");
                            entry.name += QString::number(pmon_config_bank_notice);
                            entry.name += QString::fromStdString("/");
                            entry.name += QString::number(pmon_config_afk_leave);
                        }

                        if (pmon_my_foecontact_age[m])
                        {
                            entry.name += QString::fromStdString(" CONTACT*");
                            entry.name += QString::number(pmon_my_foecontact_age[m]);
                        }
                    }
                }

                // value of pending tx
                if (pending_tx_idx >= 0)
                {
                    entry.name += QString::fromStdString(" tx*");
                    entry.name += QString::number(wp_age);

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
        if (pmon_state == PMONSTATE_SHUTDOWN)
        {
            pmon_my_foecontact_age[m] = 0;
            pmon_my_alarm_state[m] = 0;
            pmon_my_idlecount[m] = 0;

            pmon_my_bankdist[m] = 0;
            pmon_my_bankstate[m] = 0;

            continue;
        }

        bool tmp_trigger_alarm = false;
        bool enemy_in_range = false;
        int my_alarm_range = pmon_my_alarm_dist[m];
        if (pmon_my_alarm_state[m])
            my_alarm_range += 2;

        int my_idx = pmon_my_idx[m];
        if (my_idx < 0)  // not alive
        {
            pmon_my_alarm_state[m] = 0;
            pmon_my_idlecount[m] = 0;
            pmon_my_bankdist[m] = 0;

            if (pmon_out_of_wp_idx == m) pmon_out_of_wp_idx = -1;
            if (pmon_need_bank_idx == m) pmon_need_bank_idx = -1;

            continue;
        }
        int my_enemy_tx_age = -1;
        pmon_my_foe_dist[m] = 10000;

        int my_next_x = pmon_all_next_x[my_idx];
        int my_next_y = pmon_all_next_y[my_idx];

        int my_x = pmon_all_x[my_idx];
        int my_y = pmon_all_y[my_idx];

        for (int k_all = 0; k_all < pmon_all_count; k_all++)
        {
            if (k_all == my_idx) continue; // that's me
            if (pmon_all_color[my_idx] == pmon_all_color[k_all]) continue; // same team
            if (pmon_all_cache_isinmylist[k_all]) continue; // one of my players

            if ((abs(my_next_x - pmon_all_next_x[k_all]) <= 1) && (abs(my_next_y - pmon_all_next_y[k_all]) <= 1))
            {
                enemy_in_range = true;
                my_enemy_tx_age = pmon_all_tx_age[k_all];
            }

//            if ((my_alarm_range) && (abs(my_x - pmon_all_x[k_all]) <= my_alarm_range) && (abs(my_y - pmon_all_y[k_all]) <= my_alarm_range))
//            {
//                tmp_trigger_alarm = true;
//            }
            int fdx = abs(my_x - pmon_all_x[k_all]);
            int fdy = abs(my_y - pmon_all_y[k_all]);
            int tmp_foe_dist = fdx > fdy ? fdx : fdy;
            if (tmp_foe_dist < pmon_my_foe_dist[m])
            {
                pmon_my_foe_dist[m] = tmp_foe_dist;
                if ((my_alarm_range) && (tmp_foe_dist <= my_alarm_range))
                {
                    tmp_trigger_alarm = true;
                }
            }
        }

        if (tmp_trigger_alarm)
        {
            if (!pmon_my_alarm_state[m]) pmon_my_alarm_state[m] = 1;
        }
        else
        {
            pmon_my_alarm_state[m] = 0;
        }

        if (enemy_in_range) pmon_my_foecontact_age[m]++;
        else pmon_my_foecontact_age[m] = 0;

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
                      else if (x == Game::MAP_WIDTH - 1) fprintf(fp, "%d},\n", Displaycache_gamemap[y][x][z]);
                      else fprintf(fp, "%d,", Displaycache_gamemap[y][x][z]);
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

        FILE *fp;
        fp = fopen("adventurers.txt", "w");
        if (fp != NULL)
        {
            fprintf(fp, "\n Inventory (chronon %7d, %s)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet");
            fprintf(fp, " ------------------------------------\n\n");
#ifdef RPG_OUTFIT_ITEMS
            fprintf(fp, "                                          hunter\n");
            fprintf(fp, "storage vault key                           name      gems    outfit\n");
#else
            fprintf(fp, "                                          hunter\n");
            fprintf(fp, "storage vault key                           name      gems\n");
#endif
            fprintf(fp, "\n");

            BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
            {
              int64 tmp_volume = st.second.nGems;
#ifdef RPG_OUTFIT_ITEMS
              unsigned char tmp_outfit = st.second.item_outfit;
              std::string s = "-";
              if (tmp_outfit == 1) s = "mage";
              else if (tmp_outfit == 2) s = "fighter";
              else if (tmp_outfit == 4) s = "rogue";

              fprintf(fp, "%s    %10s    %6s    %s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_volume).c_str(), s.c_str());
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
            if (gameState.upgrade_test != gameState.nHeight * 2)
            {
              fprintf(fp, "GAMESTATE OUT OF SYNC: !!! DON'T USE THIS AUCTION PAGE !!!\n");
              fprintf(fp, "Either the gamestate was used with an old version of this client\n");
              fprintf(fp, "or the alarm (only for auction, different from 'network alert') was triggered\n");
              fprintf(fp, "or this client version itself is too old.\n");
            }

            fprintf(fp, "\n Continuous dutch auction (chronon %7d, %s, next down tick in %2d)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet", AUCTION_DUTCHAUCTION_INTERVAL - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
            fprintf(fp, " -------------------------------------------------------------------------\n\n");
            fprintf(fp, "                                     hunter                 ask\n");
            fprintf(fp, "storage vault key                    name        gems       price    chronon\n");
            fprintf(fp, "\n");
            BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
            {
              if (st.second.auction_ask_price > 0)
                  fprintf(fp, "%s   %-10s %5s at %9s   %d\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.auction_ask_size).c_str(), FormatMoney(st.second.auction_ask_price).c_str(), st.second.auction_ask_chronon);
            }
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
                    fprintf(fp, "best ask, good for liquidity rebate:            %5s\n", FormatMoney(tmp_r).c_str());
                    fprintf(fp, "    total fund for liquidity rebate:            %5s\n", FormatMoney(gameState.liquidity_reward_remaining).c_str());
                }
                else
                {
                    fprintf(fp, "\nbest ask\n");
                }

                fprintf(fp, "%s              %5s at %9s   %d\n", auctioncache_bestask_key.c_str(), FormatMoney(auctioncache_bestask_size).c_str(), FormatMoney(auctioncache_bestask_price).c_str(), auctioncache_bestask_chronon);
            }
            if (gameState.auction_last_price > 0)
            {
                fprintf(fp, "\n");
                fprintf(fp, "last price                                               %9s   %d\n", FormatMoney(gameState.auction_last_price).c_str(), (int)gameState.auction_last_chronon);
            }
            if (gameState.auction_settle_price > 0)
            {
                fprintf(fp, "\n");
                fprintf(fp, "settlement price (auction start price minimum)           %9s   %d\n", FormatMoney(gameState.auction_settle_price).c_str(), gameState.nHeight - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
                fprintf(fp, "\n");
                if (gameState.auction_settle_conservative > 0)
                {
                    fprintf(fp, "settlement price 2 (less or equal than last price)       %9s   %d\n", FormatMoney(gameState.auction_settle_conservative).c_str(), gameState.nHeight - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
                    fprintf(fp, "\n");
                }
//                fprintf(fp, "->chat message to sell minimum size at auction start price minimum:\n");
//                fprintf(fp, "GEM:HUC set ask %s at %s\n", FormatMoney(AUCTION_MIN_SIZE).c_str(), FormatMoney(gameState.auction_settle_price).c_str());
            }

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
                    fprintf(fp, "->gems will be transferred to the address of hunter %s if the transaction confirms until timeout)\n", auctioncache_bid_name.c_str());
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
            }
            if (auctioncache_bid_price <= 0) // no active bid (i.e. best ask reserved for one hunter)
            {
                if (auctioncache_bestask_price > 0) // but there is a best ask
                {
                    fprintf(fp, "\n");
                    fprintf(fp, "->chat message to buy (size and price of best ask):\n");
                    fprintf(fp, "GEM:HUC set bid %s at %s\n", FormatMoney(auctioncache_bestask_size).c_str(), FormatMoney(auctioncache_bestask_price).c_str());
                }
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

            fp = fopen("adv_chrono.txt", "w");
            if (fp != NULL)
            {
                if (gameState.nHeight < AUX_MINHEIGHT_TRADE(fTestNet))
                {
                  fprintf(fp, "Closed until block %d\n", AUX_MINHEIGHT_TRADE(fTestNet));
                }

                fprintf(fp, "\n CRD:GEM open orders (chronon %7d, %s)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet");
                fprintf(fp, " ----------------------------------------------\n\n");
                fprintf(fp, "                                     hunter       ask       ask       order    chronoDollar   gems at risk   additional\n");
                fprintf(fp, "storage vault key                    name         size      price     chronon      position   if filled      P/L if filled   flags\n");
                fprintf(fp, "\n");
                BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
                {
                    int64 tmp_ask_size = st.second.ex_order_size_ask;
                    if (tmp_ask_size > 0)
                    {
                        int64 tmp_ask_price = st.second.ex_order_price_ask;
                        int tmp_ask_chronon = st.second.ex_order_chronon_ask;
                        int64 tmp_position_size = st.second.ex_position_size;
                        int64 tmp_position_price = st.second.ex_position_price;
                        int64 pl_a = (tmp_position_size / AUX_COIN) * (tmp_ask_price - tmp_position_price);
                        int64 risk_askorder = ((-tmp_position_size + tmp_ask_size) / AUX_COIN) * (gameState.crd_prevexp_price * 3 - tmp_ask_price);
                        if (risk_askorder < 0) risk_askorder = 0;
                        // in case above division has rounded it down (which is ok for risk_bidorder)
                        if (risk_askorder) risk_askorder = tradecache_pricetick_up(risk_askorder);

                        std::string s = "";
                        int tmp_orderflags = st.second.ex_order_flags;
                        if (tmp_orderflags & ORDERFLAG_ASK_SETTLE) s += " rollover";
                        if (tmp_orderflags & ORDERFLAG_ASK_INVALID) s += " *no funds*";
                        else if (tmp_orderflags & ORDERFLAG_ASK_ACTIVE) s += " ok";

                        fprintf(fp, "%s   %-10s %6s at %7s     %7d       %7s   %10s         %6s     %-11s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_ask_size).c_str(), FormatMoney(tmp_ask_price).c_str(), tmp_ask_chronon, FormatMoney(tmp_position_size).c_str(), FormatMoney(risk_askorder).c_str(), FormatMoney(pl_a).c_str(), s.c_str());
                    }
                }
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
                fprintf(fp, "                                     hunter       bid       bid       order    chronoDollar   gems at risk   additional\n");
                fprintf(fp, "storage vault key                    name         size      price     chronon      position   if filled      P/L if filled   flags\n");
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
                      int64 pl_b = (tmp_position_size / AUX_COIN) * (tmp_bid_price - tmp_position_price);
                      int64 risk_bidorder = ((tmp_position_size + tmp_bid_size) / AUX_COIN) * tmp_bid_price;
                      if (risk_bidorder < 0) risk_bidorder = 0;

                      std::string s = "";
                      int tmp_orderflags = st.second.ex_order_flags;
                      if (tmp_orderflags & ORDERFLAG_BID_SETTLE) s += " rollover";
                      if (tmp_orderflags & ORDERFLAG_BID_INVALID) s += " *no funds*";
                      else if (tmp_orderflags & ORDERFLAG_BID_ACTIVE) s += " ok";

                      fprintf(fp, "%s   %-10s %6s at %7s     %7d       %7s   %10s         %6s     %-11s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_bid_size).c_str(), FormatMoney(tmp_bid_price).c_str(), tmp_bid_chronon, FormatMoney(tmp_position_size).c_str(), FormatMoney(risk_bidorder).c_str(), FormatMoney(pl_b).c_str(), s.c_str());
                  }
                }
                fprintf(fp, "\n\n");

                fprintf(fp, "\n CRD:GEM trader positions (chronon %7d, %s)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet", AUCTION_DUTCHAUCTION_INTERVAL - (gameState.nHeight % AUCTION_DUTCHAUCTION_INTERVAL));
                fprintf(fp, " ---------------------------------------------------\n\n");
                fprintf(fp, "                                     hunter             chronoDollar   trade   trade    net worth after fill  bid    ask\n");
                fprintf(fp, "storage vault key                    name        gems   position       price   P/L      (worst case)          size   size\n");
                fprintf(fp, "\n");
                BOOST_FOREACH(const PAIRTYPE(const std::string, StorageVault) &st, gameState.vault)
                {
                  if ((st.second.ex_order_size_bid != 0) || (st.second.ex_order_size_ask != 0) ||
                      (st.second.ex_position_size != 0) || (st.second.ex_trade_profitloss != 0))
                  {
                      int64 tmp_position_size = st.second.ex_position_size;
                      int64 tmp_position_price = st.second.ex_position_price;
                      int64 tmp_ask_price = st.second.ex_order_price_ask;
                      int64 tmp_bid_price = st.second.ex_order_price_bid;
                      int64 pl_a = 0;
                      int64 pl_b = 0;
                      if (tmp_ask_price) pl_a = (tmp_position_size / AUX_COIN) * (tmp_ask_price - tmp_position_price);
                      if (tmp_bid_price) pl_b = (tmp_position_size / AUX_COIN) * (tmp_bid_price - tmp_position_price);
                      int64 pl = pl_b < pl_a ? pl_b : pl_a;
                      //   net worth ignoring "unsettled profits"
                      int64 nw = st.second.ex_trade_profitloss < 0 ? pl + st.second.nGems + st.second.ex_trade_profitloss : pl + st.second.nGems;
                      // if collateral is about to be sold for coins
                      if (st.second.auction_ask_size > 0) nw -= st.second.auction_ask_size;

                      //                                                 not rounded
                      fprintf(fp, "%s   %-10s %6s    %9s   %6s  %6s         %8s       %6s %6s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(st.second.nGems).c_str(), FormatMoney(st.second.ex_position_size).c_str(), FormatMoney(st.second.ex_position_price).c_str(), FormatMoney(st.second.ex_trade_profitloss).c_str(), FormatMoney(nw).c_str(),
                              FormatMoney(st.second.ex_order_size_bid).c_str(), FormatMoney(st.second.ex_order_size_ask).c_str());
                  }
                }


                fprintf(fp, "\n\n CRD:GEM settlement (chronon %7d, %s, first regular settlement: %7d)\n", gameState.nHeight, fTestNet ? "testnet" : "mainnet", AUX_MINHEIGHT_SETTLE(fTestNet));
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
//                    int64 tmp_settlement = (((AUX_COIN * AUX_COIN) / gameState.auction_settle_price) * AUX_COIN) / gameState.feed_nextexp_price;
                    int64 tmp_settlement = (((AUX_COIN * AUX_COIN) / gameState.auction_settle_conservative) * AUX_COIN) / gameState.feed_nextexp_price;
                    tmp_settlement = tradecache_pricetick_down(tradecache_pricetick_up(tmp_settlement)); // snap to grid

                    fprintf(fp, "                                      settlement price       chronon    covered call strike\n\n");
                    fprintf(fp, "previous                                          %-7s    %7d    %-7s\n", FormatMoney(gameState.crd_prevexp_price).c_str(), tmp_oldexp_chronon, FormatMoney(gameState.crd_prevexp_price * 3).c_str());
                    fprintf(fp, "pending                                           %-7s    %7d    %-7s\n", FormatMoney(tmp_settlement).c_str(), tmp_newexp_chronon, FormatMoney(tmp_settlement * 3).c_str());
                    if ((gameState.nHeight >= AUX_MINHEIGHT_MM_AI_UPGRADE(fTestNet)) && (tradecache_crd_nextexp_mm_adjusted > 0))
                    {
                    fprintf(fp, "pending (adjusted for market maker)               %-7s\n", FormatMoney(tradecache_crd_nextexp_mm_adjusted).c_str());
                    }
                    fprintf(fp, "\n");
                    fprintf(fp, "->example chat message to buy 1 chronoDollar (and sell 1 covered call)\n");
                    fprintf(fp, "CRD:GEM set bid 1 at %s\n", FormatMoney(tmp_settlement).c_str());
                    fprintf(fp, "\n");
                    fprintf(fp, "->example chat message to sell 1 chronoDollar (and buy 1 covered call)\n");
                    fprintf(fp, "CRD:GEM set ask 1 at %s\n", FormatMoney(tmp_settlement).c_str());
                    fprintf(fp, "\n");
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
                            int tmp_chronon = st.second.ex_reserve1;
                            fprintf(fp, "%s   %-10s   %-7s   %-7s   %7d     %6s\n", st.first.c_str(), st.second.huntername.c_str(), FormatMoney(tmp_max_bid).c_str(), FormatMoney(tmp_min_ask).c_str(), tmp_chronon, FormatMoney(tmp_volume).c_str());
                        }
                    }
                    int64 tmp_max_bid = 0;
                    int64 tmp_min_ask = 0;
                    MM_ORDERLIMIT_UNPACK(gameState.crd_mm_orderlimits, tmp_max_bid, tmp_min_ask);

                    fprintf(fp, "\n");
                    fprintf(fp, "->example chat message to vote MM bid/ask limits:\n");
                    fprintf(fp, "CRD:GEM vote MM max bid %s min ask %s\n", FormatMoney(tmp_max_bid).c_str(), FormatMoney(tmp_min_ask).c_str());
                    fprintf(fp, "\n");
                    fprintf(fp, "median vote                                       max bid   min ask    updated\n\n");
                    fprintf(fp, "current                                           %-7s   %-7s    %d\n", FormatMoney(tmp_max_bid).c_str(), FormatMoney(tmp_min_ask).c_str(), gameState.nHeight);
                    fprintf(fp, "\n");
                    fprintf(fp, "cached volume (max bid): total %s, participation %s, higher than median %s, at median %s, lower than median %s\n", FormatMoney(mmlimitcache_volume_total).c_str(), FormatMoney(mmlimitcache_volume_participation).c_str(), FormatMoney(mmmaxbidcache_volume_bull).c_str(), FormatMoney(mmmaxbidcache_volume_neutral).c_str(), FormatMoney(mmmaxbidcache_volume_bear).c_str());
                    fprintf(fp, "cached volume (min ask): total %s, participation %s, higher than median %s, at median %s, lower than median %s\n", FormatMoney(mmlimitcache_volume_total).c_str(), FormatMoney(mmlimitcache_volume_participation).c_str(), FormatMoney(mmminaskcache_volume_bull).c_str(), FormatMoney(mmminaskcache_volume_neutral).c_str(), FormatMoney(mmminaskcache_volume_bear).c_str());
                    fprintf(fp, "\n");
                }

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

        int bs = 13;
        if (m % 7 == 1) bs = 10;
        else if (m % 7 == 2) bs = 11;
        int bd = (m % 9) + 1;
        if (bd == 5) bd = 2;
        gameMapCache->AddPlayer(tmp_name, TILE_SIZE * bank_xpos[m], TILE_SIZE * bank_ypos[m], 1 + 0, bs, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, bd, 0);
    }
    // gems and storage
    if (gem_visualonly_state == GEM_SPAWNED)
    {
        gameMapCache->AddPlayer("Tia'tha '1 soul gem here, for free", TILE_SIZE * gem_visualonly_x, TILE_SIZE * gem_visualonly_y, 1 + 0, 20, 453, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 2, 0);
    }
#ifdef PERMANENT_LUGGAGE
#ifndef RPG_OUTFIT_NPCS
    else
    {
        // "legacy version"
        QString qs = QString::fromStdString("Tia'tha 'next gem at ");
        qs += QString::number(gameState.nHeight - (gameState.nHeight % GEM_RESET_INTERVAL(fTestNet)) + GEM_RESET_INTERVAL(fTestNet));
        qs += QString::fromStdString("'");
        gameMapCache->AddPlayer(qs, TILE_SIZE * 128, TILE_SIZE * 486, 1 + 0, 5, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 2, 0);
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
                gameMapCache->AddPlayer(qs, TILE_SIZE * 128, TILE_SIZE * 486, 1 + 0, 5, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 2, 0);

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
                    if (pmon_my_alarm_state[m] == 1)
                         pmon_my_alarm_state[m] = 2;
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
