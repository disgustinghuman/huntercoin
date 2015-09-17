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
    QPixmap player_sprite[Game::NUM_TEAM_COLORS+1][10];

    QPixmap coin_sprite, heart_sprite, crown_sprite;
    QPixmap tiles[NUM_TILE_IDS];

    // better GUI -- more player sprites
    QBrush player_text_brush[Game::NUM_TEAM_COLORS+1];

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
        player_text_brush[4] = QBrush(QColor(255, 255, 255));

        // better GUI -- more player sprites
        for (int i = 0; i < Game::NUM_TEAM_COLORS+1; i++)

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
// to parse the old hardcoded gamemap (shadows)
/*
  in hardcoded gamemap:

  boulder   broadleaf, dark   mirrored      broadleaf, bright   mirrored

  212        117,118,119      130,131,132   122,123,124        125,126,127
             133,134,135      148,149,150   138,139,160        161,144,145
             151,152,153      168,169,170   156,157,173        174,164,165

            conifer, dark   mirrored      conifer, bright   mirrored
             120,121         128,129       140, 141          142,143
             136,137         146,147       158, 159          162,163
             154,155         166,167       171, 172          175,176
*/
bool Display_dbg_use_gamemap_tiles = true;
bool Display_dbg_allow_tile_offset = false;

int Display_dbg_maprepaint_cachemisses = 0;
int Display_dbg_maprepaint_cachehits = 0;

int Shadowmap[SHADOW_SHAPES][5] = {
                      { 0, 0, 135, 160, 235}, // trees
                      { 0, 0, 151, 156, 236},
                      { 0, 0, 152, 157, 237},
                      { 0, 0, 153, 173, 238},
                      { -1, 0, 153, 173, 239},
                      { 1, -1, 153, 173, 240},
                      { 0, -1, 153, 173, 241},
                      { -1, -1, 153, 173, 242},
                      { 1, 0, 155, 172, 243},
                      { 0, 0, 155, 172, 244},
                      { -1, 0, 155, 172, 245},
                      { 1, -1, 155, 172, 246},
                      { 0, -1, 155, 172, 247},
                      { -1, -1, 155, 172, 248},
                      { 1, 0, 212, 249, 256}, // boulder
                      { 0, 0, 212, 249, 257},
                      { -1, 0, 212, 249, 258}};

// note: old gamemap has only 235 tiles
int Shadowunrotate[NUM_TILE_IDS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                           10, 11, 12, 13,14, 15, 16, 17, 18, 19,
                           20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                           30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                           40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                           50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                           60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                           70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
                           80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
                           90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
                           100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
                           110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
                           120, 121, 122, 123, 124, 122, 123, 124, 120, 121,
                           117, 118, 119, 133, 134, 135, 136, 137, 138, 139,
                           140, 141, 140, 141, 139, 160, 136, 137, 133, 134,
                           135, 151, 152, 153, 154, 155, 156, 157, 158, 159,
                           160, 138, 158, 159, 157, 173, 154, 155, 151, 152,
                           153, 171, 172, 173, 156, 171, 172, 177, 178, 179,
                           180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
                           190, 191, 192, 193, 194, 195, 196, 197, 198, 199,
                           200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
                           210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
                           220, 221, 222, 223, 224, 225, 226, 227, 228, 229,
                           230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
                           240, 241, 242, 243, 244, 245, 246, 247, 248, 249,
                           250, 251, 252, 253, 254, 255, 256, 257, 258, 259,
                           260, 261, 262, 263, 264, 265, 266, 267, 268, 269,
                           270, 271, 272, 273, 274, 275, 276, 277, 278, 279,
                           280, 281, 282, 283, 284, 285, 286, 287, 288, 289,
                           290, 291, 292, 293, 294, 295, 296, 297, 298, 299};



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
        return QRectF(0, 0, MAP_WIDTH * TILE_SIZE, MAP_HEIGHT * TILE_SIZE);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
    {
        Q_UNUSED(widget)

        int x1 = std::max(0, int(option->exposedRect.left()) / TILE_SIZE);
        int x2 = std::min(MAP_WIDTH, int(option->exposedRect.right()) / TILE_SIZE + 1);
        int y1 = std::max(0, int(option->exposedRect.top()) / TILE_SIZE);
        int y2 = std::min(MAP_HEIGHT, int(option->exposedRect.bottom()) / TILE_SIZE + 1);

        // allow offset for some tiles without popping
        if (Display_dbg_allow_tile_offset)
        {
            if (x1 > 0) x1--;
            if (y1 > 0) y1--;
            if (x2 < MAP_WIDTH - 1) x2++;
            if (y2 < MAP_HEIGHT - 1) y2++;
        }

        for (int y = y1; y < y2; y++)
            for (int x = x1; x < x2; x++)
            {
                // better GUI -- more map tiles
                // insert shadows
                if ((layer > 0) && (layer <= SHADOW_LAYERS))
                {
                    if ((SHADOW_LAYERS == 2) && (layer == 2)) break;

                    // parse old hardcoded gamemap
                    if (Display_dbg_use_gamemap_tiles)
                    {
                        // palisades
                        int stile = 0;
                        if ((x > 0) && (y > 0))
                        {
                            int gm1 = GameMap[1][y][x];
                            int gm2 = GameMap[2][y][x];
                            int gm1l = GameMap[1][y][x - 1];
                            int gm2l = GameMap[2][y][x - 1];
                            int gm1u = GameMap[1][y - 1][x];
                            int gm2u = GameMap[2][y - 1][x];
                            int gm1ul = GameMap[1][y - 1][x - 1];
                            int gm2ul = GameMap[2][y - 1][x - 1];

                            if ((gm1 == 115) || (gm1 == 116) || (gm1 == 191) || (gm1 == 192) ||
                                (gm2 == 115) || (gm2 == 116) || (gm2 == 191) || (gm2 == 192))
                            {
                                stile = 412;
                            }
                            else
                            {
                                if ((gm1u == 115) || (gm1u == 116) || (gm1u == 191) || (gm1u == 192) ||
                                     (gm2u == 115) || (gm2u == 116) || (gm2u == 191) || (gm2u == 192))
                                {
                                    stile = 427;
                                    if ((gm1l == 115) || (gm1l == 116) || (gm1l == 191) || (gm1l == 192) ||
                                         (gm1l == 113) || (gm1l == 114) || (gm1l == 189) || (gm1l == 190) ||
                                         (gm2l == 115) || (gm2l == 116) || (gm2l == 191) || (gm2l == 192) ||
                                         (gm2l == 113) || (gm2l == 114) || (gm2l == 189) || (gm2l == 190))
                                    {
                                        stile = 413;
                                    }
                                    else if ((gm1ul == 115) || (gm1ul == 116) || (gm1ul == 191) || (gm1ul == 192) ||
                                             (gm2ul == 115) || (gm2ul == 116) || (gm2ul == 191) || (gm2ul == 192))
                                    {
                                            stile = 432;
                                    }
                                }
                            }

                            if (!stile)
                            {
                                if ((gm1l == 115) || (gm1l == 116) || (gm1l == 191) || (gm1l == 192) ||
                                     (gm1l == 113) || (gm1l == 114) || (gm1l == 189) || (gm1l == 190) ||
                                     (gm2l == 115) || (gm2l == 116) || (gm2l == 191) || (gm2l == 192) ||
                                     (gm2l == 113) || (gm2l == 114) || (gm2l == 189) || (gm2l == 190))
                                {
                                    stile = 418;
                                    if ((gm1ul == 115) || (gm1ul == 116) || (gm1ul == 191) || (gm1ul == 192) ||
                                         (gm1ul == 113) || (gm1ul == 114) || (gm1ul == 189) || (gm1ul == 190) ||
                                         (gm2ul == 115) || (gm2ul == 116) || (gm2ul == 191) || (gm2ul == 192) ||
                                         (gm2ul == 113) || (gm2ul == 114) || (gm2ul == 189) || (gm2ul == 190))
                                        stile = 421;
                                }
                            }

                            if (!stile)
                            {
                                if ((gm1ul == 115) || (gm1ul == 116) || (gm1ul == 191) || (gm1ul == 192) ||
                                     (gm2ul == 115) || (gm2ul == 116) || (gm2ul == 191) || (gm2ul == 192))
                                    stile = 438;
                            }
                        }

                        if (stile)
                        {
                            painter->setOpacity(0.4);
                            painter->drawPixmap(x * TILE_SIZE, y * TILE_SIZE, grobjs->tiles[stile]);
                            painter->setOpacity(1);
                            continue;
                        }

                        // trees and rocks
                        for (int m = 0; m < SHADOW_SHAPES; m++)
                        {
                            int u = x + Shadowmap[m][0];
                            int v = y + Shadowmap[m][1];
                            if ((u < 0) || (v < 0) || (u >= MAP_WIDTH) || (v >= MAP_HEIGHT)) // if (!(IsInsideMap((u, v))))
                                continue;

                            if ((Shadowunrotate[GameMap[2][v][u]] == Shadowmap[m][2]) || (Shadowunrotate[GameMap[1][v][u]] == Shadowmap[m][3]) ||
                                (Shadowunrotate[GameMap[2][v][u]] == Shadowmap[m][3]) || (Shadowunrotate[GameMap[1][v][u]] == Shadowmap[m][2]))
                            {
                                painter->setOpacity(0.4);
                                painter->drawPixmap(x * TILE_SIZE, y * TILE_SIZE, grobjs->tiles[Shadowmap[m][4]]);
                                painter->setOpacity(1);
                                continue; // delete me
                            }
                        }
                    }
                    continue; // it's a shadow layer
                }

                int tile = 0;

                int l = layer - SHADOW_LAYERS > 0 ? layer - SHADOW_LAYERS : 0;

                tile = (l < MAP_LAYERS) ? Shadowunrotate[GameMap[l][y][x]] : 0; // MAP_LAYERS == 3

                // try to fix grass/dirt transition
                if (!layer)
                {
                    char terrain = AsciiArtMap[y][x];
                    if ( ((tile >= 3) && (tile <= 26)) ||
                         ((tile >= 41) && (tile <= 46)) ||
                         ((tile >= 48) && (tile <= 53)) ||
                         ((tile >= 56) && (tile <= 67)))
                    {
                        if (terrain == '0')
                            tile = 0;
                        else
                            tile = 1;
                    }

                    if (tile == 0)
                    {
                        bool dirt_S = ((y < MAP_HEIGHT - 1) && (AsciiArtMap[y + 1][x] == '.'));
                        bool dirt_N = ((y > 0) && (AsciiArtMap[y - 1][x] == '.'));
                        bool dirt_E = ((x < MAP_WIDTH - 1) && (AsciiArtMap[y][x + 1] == '.'));
                        bool dirt_W = ((x > 0) && (AsciiArtMap[y][x - 1] == '.'));
                        bool dirt_SE = ((y < MAP_HEIGHT - 1) && (x < MAP_WIDTH - 1) && (AsciiArtMap[y + 1][x + 1] == '.'));
                        bool dirt_NE = ((y > 0) && (x < MAP_WIDTH - 1) && (AsciiArtMap[y - 1][x + 1] == '.'));
                        bool dirt_NW = ((y > 0) && (x > 0) && (AsciiArtMap[y - 1][x - 1] == '.'));
                        bool dirt_SW = ((y < MAP_HEIGHT - 1) && (x > 0) && (AsciiArtMap[y + 1][x - 1] == '.'));
                        if (dirt_S)
                        {
                            if (dirt_W)
                            {
                                if (dirt_NE) tile = 1;
                                else tile = 20; // 3/4 dirt SW  (17 looks wrong)
                            }
                            else if (dirt_E)
                            {
                                if (dirt_NW) tile = 1;
                                else tile = 26; //  3/4 dirt SE  (18 looks wrong)
                            }
                            else
                            {
                                if (dirt_N) tile = 1;
                                else if (dirt_NW) tile = 20;   // 3/4 dirt SW
                                else if (dirt_NE) tile = 26;   //  3/4 dirt SE
                                else tile = 4;                 // 1/2 dirt S
                            }
                        }
                        else if (dirt_N)
                        {
                            if (dirt_W)
                            {
                                if (dirt_SE) tile = 1;
                                else if (dirt_NE || dirt_SW) tile = 15; // or tile = 19;   3/4 dirt NW
                                else tile = 19;  // 3/4 dirt NW            or tile = 16;   1/2 dirt NW
                            }
                            else if (dirt_E)
                            {
                                if (dirt_SW) tile = 1;
                                else if (dirt_NW || dirt_SE) tile = 14; // or tile = 23;   3/4 dirt NE
                                else tile = 23;   // 3/4 dirt NE           or tile = 13;   1/2 dirt NE
                            }
                            else
                            {
                                if (dirt_S) tile = 1;
                                else if (dirt_SW) tile = 15; // 3/4 dirt NW
                                else if (dirt_SE) tile = 14; //  3/4 dirt NE
                                else tile = 21; //  1/2 dirt N
                            }
                        }
                        else if (dirt_W)
                        {
                            if (dirt_NE) tile = 19;      //  3/4 dirt NW
                            else if (dirt_SE) tile = 20; //  3/4 dirt SW
                            else if (dirt_E) tile = 1;
                            else tile = 10; //  1/2 dirt W
                        }
                        else if (dirt_E)
                        {
                            if (dirt_NW) tile = 23;      //  3/4 dirt NE
                            else if (dirt_SW) tile = 26; //  3/4 dirt SE
                            else if (dirt_W) tile = 1;
                            else tile = 9; //  1/2 dirt E
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
                    }
                }


                // Tile 0 denotes grass in layer 0 and empty cell in other layers
                if (!tile && layer)
                    continue;

                painter->drawPixmap(x * TILE_SIZE, y * TILE_SIZE, grobjs->tiles[tile]);
            }
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
    setSceneRect(0, 0, MAP_WIDTH * TILE_SIZE, MAP_HEIGHT * TILE_SIZE);
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


    BOOST_FOREACH (QGraphicsRectItem* b, banks)
      {
        scene->removeItem (b);
        delete b;
      }
    banks.clear ();
    BOOST_FOREACH (const PAIRTYPE(Coord, unsigned)& b, gameState.banks)
      {
        QGraphicsRectItem* r
          = scene->addRect (TILE_SIZE * b.first.x, TILE_SIZE * b.first.y,
                            TILE_SIZE, TILE_SIZE,
                            Qt::NoPen, QColor (255, 255, 255, bankOpacity));


        // better GUI -- banks
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


            // pending tx monitor -- info text
            const Coord &wmon_from = characterState.from;
            if (pmon_all_count >= PMON_ALL_MAX)
            {
                entry.name += QString::fromStdString(" off,ERROR");
            }
            else if (pmon_stop)
            {
//                entry.name += QString::fromStdString(" off");
            }
            else if ((!pmon_stop) && (pmon_all_count < PMON_ALL_MAX))
            {

                pmon_all_names[pmon_all_count] = chid.ToString();
                pmon_all_x[pmon_all_count] = coord.x;
                pmon_all_y[pmon_all_count] = coord.y;
                pmon_all_color[pmon_all_count] = pl.color;

                entry.icon_d1 = 0;
                entry.icon_d2 = 0;
                if (pl.value > 40000000000)
                    entry.icon_d2 = 411;
//                    entry.name += QString::fromUtf8("\u2620"); // Unicode Character 'SKULL AND CROSSBONES'
                if (pl.value > 20000000000)
                    entry.icon_d1 = 411;
//                    entry.name += QString::fromUtf8("\u2620 ");
                else
                    entry.name += QString::fromStdString(" ");

                entry.name += QString::fromStdString(" ");
                entry.name += QString::number(coord.x);
                entry.name += QString::fromStdString(",");
                entry.name += QString::number(coord.y);

                // pending waypoints/destruct
                int pending_tx_idx = -1;
                int wp_age = 0;

                for (int k = 0; k < pmon_tx_count; k++)
                {
                    if (chid.ToString() == pmon_tx_names[k])
                    {
                        pending_tx_idx = k;
                        wp_age = pmon_tx_age[k];

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
                            }
                        }

                        // check for pending tx (to determine idle status)
                        bool tmp_has_pending_tx = false;
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

                        if ((characterState.waypoints.empty()) && (!tmp_has_pending_tx))
                        {
                            if (pmon_out_of_wp_idx == -1)
                                pmon_out_of_wp_idx = m;
                        }
                        else if (pmon_out_of_wp_idx == m)
                        {
                            pmon_out_of_wp_idx = -1;
                        }

                        if (pmon_out_of_wp_idx >= 0)
                        {
                            entry.name += QString::fromStdString(" Idle:");
                            if (pmon_out_of_wp_idx < PMON_MY_MAX)
                                entry.name += QString::fromStdString(pmon_my_names[pmon_out_of_wp_idx]);
                        }
                        else if (!tmp_alarm)
                        {
                            entry.name += QString::fromStdString(" (OK)");
                        }

                        if (!pmon_noisy)
                            entry.name += QString::fromStdString(" (silent)");

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

        if (pmon_stop)
        {
            pmon_my_foecontact_age[m] = 0;
            pmon_my_alarm_state[m] = 0;

            continue;
        }

        bool tmp_trigger_alarm = false;
        bool enemy_in_range = false;
        int my_alarm_range = pmon_my_alarm_dist[m];

        int my_idx = pmon_my_idx[m];
        if (my_idx < 0)  // not alive
        {
            pmon_my_alarm_state[m] = 0;

            if (pmon_out_of_wp_idx == m) pmon_out_of_wp_idx = -1;

            continue;
        }

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
                enemy_in_range = true;

            if ((my_alarm_range) && (abs(my_x - pmon_all_x[k_all]) <= my_alarm_range) && (abs(my_y - pmon_all_y[k_all]) <= my_alarm_range))
            {
                tmp_trigger_alarm = true;
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
        int color_attack1 = RPG_ICON_EMPTY;
        int color_defense1 = data.second.icon_d1 ==  RGP_ICON_HUC_BANDIT ? RGP_ICON_HUC_BANDIT : RPG_ICON_EMPTY;
        int color_defense2 = data.second.icon_d2 ==  RGP_ICON_HUC_BANDIT ? RGP_ICON_HUC_BANDIT : RPG_ICON_EMPTY;
        gameMapCache->AddPlayer(playerName, x, y, 1 + offs, data.second.color, color_attack1, color_defense1, color_defense2, characterState.dir, characterState.loot.nAmount);
    }


    // better GUI -- banks
    // note: players need unique names
    for (int m = 0; m < bank_idx; m++)
    {
        QString tmp_name = QString::number(m);
        tmp_name += QString::fromStdString(":");
        tmp_name += QString::number(bank_timeleft[m]);
        tmp_name += QString::fromStdString(" ");
        for (int tl = 0; tl < bank_timeleft[m]; tl++)
            tmp_name += QString::fromStdString("|");

        gameMapCache->AddPlayer(tmp_name, TILE_SIZE * bank_xpos[m], TILE_SIZE * bank_ypos[m], 1 + 0, 4, RPG_ICON_EMPTY, RPG_ICON_EMPTY, RPG_ICON_EMPTY, 3, 0);
    }


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
        }
        else if ( ! (event->modifiers().testFlag( Qt::ShiftModifier )) )
        {
            pmon_stop = true;
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
