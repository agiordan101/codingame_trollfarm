#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>
#include <queue>
#include <cstdlib>
#include <chrono>
#include <variant>
#include <unordered_map>
#include <cstring>

/*
    Macro MCTS algorithms :

        MacroAction :
            - Une macro action est une séquence d'actions primitives.
            - Exemple : Troll X MOVE à un tree, pour ensuite HARVEST jusqu'a ce qu'il soit plein ou que l'arbre n'ait plus de fruits
            - C'est un nombre de tours défini
                Macro actions possibles (Sont suivis par une action primitive) :
                - Troll X MOVE à un tree, pour ensuite CHOP tout l'arbre
                - Troll X MOVE à un tree PLUM, pour ensuite HARVEST jusqu'a ce qu'il soit plein ou que l'arbre n'ait plus de fruits
                - Troll X MOVE à un tree LEMON, pour ensuite HARVEST jusqu'a ce qu'il soit plein ou que l'arbre n'ait plus de fruits
                - Troll X MOVE à un tree APPLE, pour ensuite HARVEST jusqu'a ce qu'il soit plein ou que l'arbre n'ait plus de fruits
                - Troll X MOVE à un tree BANANA, pour ensuite HARVEST jusqu'a ce qu'il soit plein ou que l'arbre n'ait plus de fruits
                - Troll X MOVE à une mine, pour ensuite MINE jusqu'a ce qu'il soit plein
                - Troll X MOVE au shack allié, pour ensuite PICK un fruit
                - Troll X MOVE au shack allié, pour ensuite DROP ce qu'il carry
                - Troll X MOVE à une cell particuliere, pour ensuite PLANT
                Actions primitives restantes :
                - Train un troll avec des stats données

            MacroActionSet :
                Un MacroActionSet est une liste de MacroAction, une par troll.
                Lorsqu'on applique une MacroActionSet au State pour générer un nouveau state:
                    Des actions primitives sont générées pour chaque troll puis éxécutées simultanément à chaque tour, jusqu'à ce qu'une MacroAction soit finie.

            MacroMCTS :
                Chaque nodes possède une liste de MacroActionSet possibles, qui sont les combinaisons des MacroActions possible pour chaque troll.
                Lorsqu'un child est généré à la fin d'un macroaction, doit-on continuer aveuglément les MacroActions des autres trolls ou tout regérerer pour tous les trolls ?
                    1. Continuer aveuglément les MacroActions des autres trolls :
                        - Avantage : Moins de combinatoire : On a que N MacroAction comme enfant de la Node
                        - Inconvénient : Des éléments pourraient avoir changé, qui ferait prendre une décision différente.
                    2. Tout régénérer pour tous les trolls :
                        - Avantage : On prend en compte les changements de l'environnement pour tous les trolls à chaque décision
                        - Inconvénient : Plus de combinatoire : On a N^M MacroActionSet comme enfant de la Node, avec M le nombre de trolls

    - Prochain algorithme :
        Un HMCTS-OP avec un graph MAXQ.

    Engine :
        C'est pas grave que l'engine ne simule pas exactement le comportement du jeu.
        Certains comportements de l'adversaire sont trop aléatoires pour que simuler le jeu précisément par rapport à ses actions n'apporte pas grand chose,
            mis à part de la complexité et du temps perdu.
        Exemple : Pas besoin d'ancitiper que planter 2 arbre différents en même temps sur la même case ne résulte en rien.
        Par contre : Simuler que 2 trolls coupent le même arbre en même temps et la répartition du bois est important

    Next steps :
        - Vraiment simuler les body blocks ...
            - Vrai simulation de toutes les combinaisons à depth = 1 ?
            - Vrai simulation des body blocks dans l'engine
        - Facteur d 'agressivité en fonction de la distance avec le shack adverse ?
        - Simuler les chops de l'adversaire pour mieux anticiper les récoltes de bois: Voler les récoltes de bois de l'adversaire
    
    Optimisations :
        5. macroTurnCount for MOVE_AND_CHOP simulates chops in a loop
        trollfarm_v4.cpp:1280-1325

        Called for every action in every applyMacroActions call. The chop simulation runs until tree dies (up to 30 iterations).

        Fix: Closed-form for the common case (no growth interleave because cooldown > chopTurns): chopTurns = ceil(health / chopPower). Only fall back to the loop simulation when cooldown might tick to 0 during chopping.

        7. vector allocations in hot paths
        applyHarvest/applyChop: vector<vector<int>> byTree(trees.size()) allocated per call, plus budgets, active inside loops.
        selectJointAction: admissible, untried, chosenSoFar all allocated per troll per iteration — this fires thousands of times.
        Fix: Use thread-local/static scratch buffers and .clear() (capacity is preserved).

        8. unordered_map<uint64_t, Node*> for children
        trollfarm_v4.cpp:1395

        Hash map allocations are expensive. With ≤8 trolls and small action counts per troll, the joint space is sparse but lookups are frequent.

        Fix: Either keep but use robin_hood / flat hashmap, or for small node degrees use a vector<pair<key, Node*>> with linear scan (often faster up to ~16 entries).

        9. state copy on every node expand
        trollfarm_v4.cpp:1444

        child->state = node->state copies all trees + trolls + grid (the grid is 11×22 chars = 242 bytes, negligible, but trees vector copy + trolls reallocate).

        Fix: Use small-vector / inline storage for trees and trolls, or pool the inner allocations.
*/

using namespace std;

// =====================================================
// POSITION
// =====================================================

class Position
{
public:
    int x = -1;
    int y = -1;
};

// =====================================================
// ACTION
// =====================================================

class State;
class Troll;

class Action
{
public:
    enum Category
    {
        PRIMITIVE,
        MACRO
    };

    enum Type
    {
        // Primitive types
        MOVE,
        HARVEST,
        PLANT,
        CHOP,
        PICK,
        DROP,
        TRAIN,
        MINE,
        // Macro-only types
        MOVE_AND_HARVEST_ONCE,
        MOVE_AND_PLANT,
        MOVE_AND_CHOP,
        MOVE_AND_PICK,
        MOVE_AND_DROP,
        MOVE_AND_MINE_ONCE
    };

    Category category;
    Type type;
    // Every action begins unfinished. Primitives flip to true the turn they execute.
    // Macros flip themselves via findNextPrimitiveAction when their final step is reached.
    bool macroTaskFinished = false;
    int trollid = 0;
    int playerid = 0;
    int x = 0, y = 0;
    string resource;
    int moveSpeed = 0, carryCapacity = 0, harvestPower = 0, chopPower = 0;

    int macroStepsTaken = 0;

    // Primitive factories
    static Action move(int trollid, int playerid, int x, int y) { return Action(PRIMITIVE, MOVE, trollid, playerid, x, y); }
    static Action harvest(int trollid, int playerid) { return Action(PRIMITIVE, HARVEST, trollid, playerid); }
    static Action plant(int trollid, int playerid, const string &res) { return Action(PRIMITIVE, PLANT, trollid, playerid, 0, 0, res); }
    static Action chop(int trollid, int playerid) { return Action(PRIMITIVE, CHOP, trollid, playerid); }
    static Action pick(int trollid, int playerid, const string &res) { return Action(PRIMITIVE, PICK, trollid, playerid, 0, 0, res); }
    static Action drop(int trollid, int playerid) { return Action(PRIMITIVE, DROP, trollid, playerid); }
    static Action train(int playerid, int ms, int cc, int hp, int cp) { return Action(PRIMITIVE, TRAIN, 0, playerid, 0, 0, "", ms, cc, hp, cp); }
    static Action mine(int trollid, int playerid) { return Action(PRIMITIVE, MINE, trollid, playerid); }

    // Macro factories
    static Action moveAndHarvest(int trollid, int playerid, int x, int y) { return Action(MACRO, MOVE_AND_HARVEST_ONCE, trollid, playerid, x, y); }
    static Action moveAndChop(int trollid, int playerid, int x, int y) { return Action(MACRO, MOVE_AND_CHOP, trollid, playerid, x, y); }
    static Action moveAndPlant(int trollid, int playerid, int x, int y, const string &res) { return Action(MACRO, MOVE_AND_PLANT, trollid, playerid, x, y, res); }
    static Action moveAndPick(int trollid, int playerid, int x, int y, const string &res) { return Action(MACRO, MOVE_AND_PICK, trollid, playerid, x, y, res); }
    static Action moveAndDrop(int trollid, int playerid, int x, int y) { return Action(MACRO, MOVE_AND_DROP, trollid, playerid, x, y); }
    static Action moveAndMine(int trollid, int playerid, int x, int y) { return Action(MACRO, MOVE_AND_MINE_ONCE, trollid, playerid, x, y); }

    // Defined out-of-line after State is complete.
    // Primitives: set macroTaskFinished and return *this.
    // Macros: move toward (x,y); execute terminal primitive when arrived.
    Action findNextPrimitiveAction(const State &s);

    // Turns until this action finishes given the current state.
    // Primitives: 1. Macros: 1 (terminal primitive) + ceil(bfs_dist / movementSpeed).
    int macroTurnCount(const State &s) const;

    Action terminalPrimitive() const;

    string toString() const
    {
        switch (type)
        {
        case MOVE:
            return "MOVE " + to_string(trollid) + " " + to_string(x) + " " + to_string(y);
        case HARVEST:
            return "HARVEST " + to_string(trollid);
        case PLANT:
            return "PLANT " + to_string(trollid) + " " + resource;
        case CHOP:
            return "CHOP " + to_string(trollid);
        case PICK:
            return "PICK " + to_string(trollid) + " " + resource;
        case DROP:
            return "DROP " + to_string(trollid);
        case TRAIN:
            return "TRAIN " + to_string(moveSpeed) + " " + to_string(carryCapacity) + " " + to_string(harvestPower) + " " + to_string(chopPower);
        case MINE:
            return "MINE " + to_string(trollid);
        case MOVE_AND_HARVEST_ONCE:
            return "MOVE_AND_HARVEST " + to_string(trollid) + " " + to_string(x) + " " + to_string(y);
        case MOVE_AND_CHOP:
            return "MOVE_AND_CHOP " + to_string(trollid) + " " + to_string(x) + " " + to_string(y);
        case MOVE_AND_PLANT:
            return "MOVE_AND_PLANT " + to_string(trollid) + " " + to_string(x) + " " + to_string(y) + " " + resource;
        case MOVE_AND_PICK:
            return "MOVE_AND_PICK " + to_string(trollid) + " " + to_string(x) + " " + to_string(y) + " " + resource;
        case MOVE_AND_DROP:
            return "MOVE_AND_DROP " + to_string(trollid) + " " + to_string(x) + " " + to_string(y);
        case MOVE_AND_MINE_ONCE:
            return "MOVE_AND_MINE " + to_string(trollid) + " " + to_string(x) + " " + to_string(y);
        default:
            return "";
        }
    }

private:
    Action(Category cat, Type t, int trollid = 0, int playerid = 0, int x = 0, int y = 0, string res = "", int ms = 0, int cc = 0, int hp = 0, int cp = 0)
        : category(cat), type(t), trollid(trollid), playerid(playerid), x(x), y(y), resource(res), moveSpeed(ms), carryCapacity(cc), harvestPower(hp), chopPower(cp) {}

    // Out-of-line (need complete State/Troll)
    const Troll *findTrollInState(const State &s) const;
    Action moveToward(const State &s, const Troll &t, int targetX, int targetY) const;
};

class ActionSet
{
public:
    vector<Action> actions;

    void add(Action a) { actions.push_back(std::move(a)); }

    string toString() const
    {
        string res;
        for (const Action &a : actions)
            res += a.toString() + "; ";
        return res;
    }
};

// =====================================================
// GLOBAL HELPERS
// =====================================================

int manhattan(int x1, int y1, int x2, int y2)
{
    return abs(x1 - x2) + abs(y1 - y2);
}

// =====================================================
// LOOKUP TABLES
// =====================================================

constexpr int MAX_MAP_HEIGHT = 11;
constexpr int MAX_MAP_WIDTH = 2 * MAX_MAP_HEIGHT;

int bfs_dist_lookup[MAX_MAP_HEIGHT][MAX_MAP_WIDTH][MAX_MAP_HEIGHT][MAX_MAP_WIDTH];

// For each (from, to), a bitmask of cardinal directions that step onto a
// cell exactly 1 closer (BFS-wise) to `to`. Direction encoding matches the
// dxs/dys arrays below: bit 0=+x, 1=-x, 2=+y, 3=-y.
// Lets moveToward walk greedily in O(movementSpeed) instead of scanning the
// whole grid.
static constexpr int STEP_DXS[4] = {1, -1, 0, 0};
static constexpr int STEP_DYS[4] = {0, 0, 1, -1};
uint8_t nextStepMask[MAX_MAP_HEIGHT][MAX_MAP_WIDTH][MAX_MAP_HEIGHT][MAX_MAP_WIDTH];

static vector<Position> ironMines;
static bool nearWaterLookup[MAX_MAP_HEIGHT][MAX_MAP_WIDTH];

// Per-player caches keyed off the fixed shack position. Initialized lazily
// once the shacks are known (first parseTrolls). The shack never moves, so
// these stay valid for the whole game.
static vector<Position> shackAdjCells[2];             // walkable cells adjacent to shack (PICK/DROP target)
static vector<Position> plantableCellsAroundShack[2]; // walkable cells with BFS dist ≤ 2 of shack
static vector<Position> mineAdjCells;                 // walkable cells adjacent to any iron mine
static bool shackCachesReady = false;

bool isCellWalkable(char c)
{
    return c == '.';
}

void buildMineAdjCells(int w, int h, const char g[][MAX_MAP_WIDTH])
{
    mineAdjCells.clear();
    constexpr int dxs[4] = {1, -1, 0, 0};
    constexpr int dys[4] = {0, 0, 1, -1};
    // Dedup via a flat seen[] grid so a cell touching two mines only appears once.
    bool seen[MAX_MAP_HEIGHT][MAX_MAP_WIDTH] = {};
    for (const Position &mine : ironMines)
    {
        for (int k = 0; k < 4; k++)
        {
            int nx = mine.x + dxs[k];
            int ny = mine.y + dys[k];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                continue;
            if (!isCellWalkable(g[ny][nx]))
                continue;
            if (seen[ny][nx])
                continue;
            seen[ny][nx] = true;
            Position p;
            p.x = nx;
            p.y = ny;
            mineAdjCells.push_back(p);
        }
    }
}

void buildShackCaches(int w, int h, const char g[][MAX_MAP_WIDTH],
                      int myX, int myY, int enX, int enY)
{
    constexpr int dxs[4] = {1, -1, 0, 0};
    constexpr int dys[4] = {0, 0, 1, -1};
    int shackX[2] = {myX, enX};
    int shackY[2] = {myY, enY};
    for (int p = 0; p < 2; p++)
    {
        shackAdjCells[p].clear();
        plantableCellsAroundShack[p].clear();

        // Walkable cells directly adjacent to the shack — PICK/DROP move target.
        for (int k = 0; k < 4; k++)
        {
            int nx = shackX[p] + dxs[k];
            int ny = shackY[p] + dys[k];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                continue;
            if (!isCellWalkable(g[ny][nx]))
                continue;
            Position cell;
            cell.x = nx;
            cell.y = ny;
            shackAdjCells[p].push_back(cell);
        }

        // Walkable cells within BFS dist ≤ 2 of the shack — plant target candidates.
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                if (!isCellWalkable(g[y][x]))
                    continue;
                int d = bfs_dist_lookup[shackY[p]][shackX[p]][y][x];
                if (d < 0 || d > 2)
                    continue;
                Position cell;
                cell.x = x;
                cell.y = y;
                plantableCellsAroundShack[p].push_back(cell);
            }
    }
    shackCachesReady = true;
}

void buildNearWaterLookup(int w, int h, const char g[][MAX_MAP_WIDTH])
{
    constexpr int dxs[4] = {1, -1, 0, 0};
    constexpr int dys[4] = {0, 0, 1, -1};
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            bool near = false;
            for (int k = 0; k < 4; k++)
            {
                int nx = x + dxs[k];
                int ny = y + dys[k];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                    continue;
                if (g[ny][nx] == '~')
                {
                    near = true;
                    break;
                }
            }
            nearWaterLookup[y][x] = near;
        }
    }
}

void buildBfsLookup(int w, int h, const char g[][MAX_MAP_WIDTH])
{
    for (int sy = 0; sy < h; sy++)
        for (int sx = 0; sx < w; sx++)
            for (int ty = 0; ty < h; ty++)
                for (int tx = 0; tx < w; tx++)
                    bfs_dist_lookup[sy][sx][ty][tx] = -1;

    int dxs[4] = {1, -1, 0, 0};
    int dys[4] = {0, 0, 1, -1};

    for (int sy = 0; sy < h; sy++)
    {
        for (int sx = 0; sx < w; sx++)
        {
            // If the cell is not walkable and isn't a shack, we won't be able to stand on it so we can skip BFS from it
            if (!isCellWalkable(g[sy][sx]) && g[sy][sx] != '0' && g[sy][sx] != '1')
                continue;

            bfs_dist_lookup[sy][sx][sy][sx] = 0;
            queue<pair<int, int>> q;
            q.push({sx, sy});

            while (!q.empty())
            {
                auto [x, y] = q.front();
                q.pop();
                int d = bfs_dist_lookup[sy][sx][y][x];

                for (int k = 0; k < 4; k++)
                {
                    int nx = x + dxs[k];
                    int ny = y + dys[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                        continue;
                    if (!isCellWalkable(g[ny][nx]))
                        continue;
                    if (bfs_dist_lookup[sy][sx][ny][nx] != -1)
                        continue;
                    bfs_dist_lookup[sy][sx][ny][nx] = d + 1;
                    q.push({nx, ny});
                }
            }
        }
    }

    // Derive nextStepMask from the completed BFS table.
    for (int sy = 0; sy < h; sy++)
        for (int sx = 0; sx < w; sx++)
            for (int ty = 0; ty < h; ty++)
                for (int tx = 0; tx < w; tx++)
                {
                    uint8_t mask = 0;
                    int d = bfs_dist_lookup[sy][sx][ty][tx];
                    if (d > 0)
                    {
                        for (int k = 0; k < 4; k++)
                        {
                            int nx = sx + STEP_DXS[k];
                            int ny = sy + STEP_DYS[k];
                            if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                                continue;
                            if (bfs_dist_lookup[ny][nx][ty][tx] == d - 1)
                                mask |= (uint8_t)(1 << k);
                        }
                    }
                    nextStepMask[sy][sx][ty][tx] = mask;
                }
}

void displayBfsDistsFrom(int h, int w, int sx, int sy)
{
    cerr << "BFS distances from (" << sx << "," << sy << "):" << endl;
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            int d = bfs_dist_lookup[sy][sx][y][x];
            if (d < 0)
                cerr << " ##";
            else if (d < 10)
                cerr << "  " << d;
            else
                cerr << " " << d;
        }
        cerr << endl;
    }
}

// =====================================================
// TREE
// =====================================================

class Tree
{
public:
    string type;
    int x, y;
    int size;
    int health;
    int fruits;
    int cooldown;

    void grow(bool nearWater)
    {
        if (cooldown > 0)
        {
            cooldown--;
            return;
        }

        if (size < 4)
        {
            int oldMax = healthFromSize(type, size);
            int newMax = healthFromSize(type, size + 1);
            size++;
            health += (newMax - oldMax);
        }
        else if (fruits < 3)
        {
            fruits++;
        }

        cooldown = cooldownFromType(type, nearWater);
    }

    static int healthFromSize(const string &type, int size)
    {
        // Indexed by size 1..4
        static constexpr int PLUM_LEMON[5] = {0, 6, 8, 10, 12};
        static constexpr int APPLE[5] = {0, 11, 14, 17, 20};
        static constexpr int BANANA[5] = {0, 3, 4, 5, 6};

        if (type == "PLUM" || type == "LEMON")
            return PLUM_LEMON[size];
        if (type == "APPLE")
            return APPLE[size];
        if (type == "BANANA")
            return BANANA[size];
        return 0;
    }

    static int cooldownFromType(const string &type, bool nearWater)
    {
        if (nearWater)
        {
            if (type == "PLUM")
                return 3;
            if (type == "LEMON")
                return 3;
            if (type == "APPLE")
                return 2;
            if (type == "BANANA")
                return 4;
        }
        else
        {
            if (type == "PLUM")
                return 8;
            if (type == "LEMON")
                return 8;
            if (type == "APPLE")
                return 9;
            if (type == "BANANA")
                return 6;
        }
        return 0;
    }
};

// =====================================================
// SHACK
// =====================================================

class Shack
{
public:
    int x = -1;
    int y = -1;
    int plum, lemon, apple, banana, iron, wood;

    int *fruitField(const string &type)
    {
        if (type == "PLUM")
            return &plum;
        if (type == "LEMON")
            return &lemon;
        if (type == "APPLE")
            return &apple;
        if (type == "BANANA")
            return &banana;
        return nullptr;
    }
};

// =====================================================
// TROLL
// =====================================================

enum TrollTask
{
    CHOPPERWARRIOR,
    CHOPPERSCALER,
};

class Troll
{
public:
    int id;
    int player;

    int x, y;

    int movementSpeed;
    int carryCapacity;
    int harvestPower;
    int chopPower;

    int carryPlum;
    int carryLemon;
    int carryApple;
    int carryBanana;
    int carryIron;
    int carryWood;

    int carried() const
    {
        return carryPlum + carryLemon + carryApple +
               carryBanana + carryIron + carryWood;
    }

    bool isCarrying() const
    {
        return carried() > 0;
    }

    bool canCarry() const
    {
        return carried() < carryCapacity;
    }

    int remainingCarry() const
    {
        return carryCapacity - carried();
    }

    int *fruitField(const string &type)
    {
        if (type == "PLUM")
            return &carryPlum;
        if (type == "LEMON")
            return &carryLemon;
        if (type == "APPLE")
            return &carryApple;
        if (type == "BANANA")
            return &carryBanana;
        return nullptr;
    }
};

// =====================================================
// STATE
// =====================================================

constexpr int MAX_TROLL_ID = 11;
constexpr int MAX_TROLLS_TOTAL = 12;

class State
{
public:
    int turn = 1;
    int w = 0, h = 0;
    char grid[MAX_MAP_HEIGHT][MAX_MAP_WIDTH];
    Shack myShack;
    Shack enemyShack;
    vector<Tree> trees;

    // Single owning storage for all trolls. `trolls` / `enemyTrolls` hold
    // non-owning pointers into `allTrolls` for fast per-player iteration.
    // Invariants:
    //   - troll.id == its index in allTrolls (parsing pushes in id order;
    //     applyTrain assigns nextId = allTrolls.size()).
    //   - pointers in trolls/enemyTrolls stay valid as long as allTrolls
    //     doesn't reallocate. We reserve MAX_TROLLS_TOTAL up-front to ensure that.
    vector<Troll> allTrolls;
    vector<Troll *> trolls;
    vector<Troll *> enemyTrolls;

    // Index map: treeAtCell[y][x] = index in `trees`, or -1 if no tree.
    // Kept in sync with mutations (parse, plant, chop kills). Lets findTreeIndex
    // be O(1) instead of an O(N) linear scan.
    int16_t treeAtCell[MAX_MAP_HEIGHT][MAX_MAP_WIDTH];

    State()
    {
        allTrolls.reserve(MAX_TROLLS_TOTAL);
        for (int y = 0; y < MAX_MAP_HEIGHT; y++)
            for (int x = 0; x < MAX_MAP_WIDTH; x++)
                treeAtCell[y][x] = -1;
    }

    State(const State &o) { *this = o; }

    State &operator=(const State &o)
    {
        if (this == &o)
            return *this;
        turn = o.turn;
        w = o.w;
        h = o.h;
        memcpy(grid, o.grid, sizeof(grid));
        myShack = o.myShack;
        enemyShack = o.enemyShack;
        trees = o.trees;
        allTrolls = o.allTrolls;
        if (allTrolls.capacity() < MAX_TROLLS_TOTAL)
            allTrolls.reserve(MAX_TROLLS_TOTAL);
        memcpy(treeAtCell, o.treeAtCell, sizeof(treeAtCell));
        rebuildTrollPointers();
        return *this;
    }

    void rebuildTreeIndex()
    {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                treeAtCell[y][x] = -1;
        for (int i = 0; i < (int)trees.size(); i++)
            treeAtCell[trees[i].y][trees[i].x] = (int16_t)i;
    }

    void rebuildTrollPointers()
    {
        trolls.clear();
        enemyTrolls.clear();
        for (Troll &t : allTrolls)
        {
            if (t.player == 0)
                trolls.push_back(&t);
            else
                enemyTrolls.push_back(&t);
        }
    }

private:
    vector<Action> generateTrollMacroActions(const Troll &t, const Shack &shack, const vector<Troll *> &allies) const
    {
        vector<Action> actions;

        if (turn < 200 && t.harvestPower > 0 && t.canCarry())
        {
            // MOVE_AND_HARVEST_ONCE: closest tree of each fruit type that has fruits
            const string fruitTypes[4] = {"PLUM", "LEMON", "APPLE", "BANANA"};
            for (const string &ft : fruitTypes)
            {
                int bestDist = -1;
                const Tree *best = nullptr;
                for (const auto &tr : trees)
                {
                    if (tr.type != ft || tr.fruits <= 0)
                        continue;
                    int d = bfs_dist_lookup[shack.y][shack.x][tr.y][tr.x];
                    if (d < 0)
                        continue;
                    if (bestDist == -1 || d < bestDist)
                    {
                        bestDist = d;
                        best = &tr;
                    }
                }
                if (best)
                    actions.push_back(Action::moveAndHarvest(t.id, t.player, best->x, best->y));
            }
        }

        // MOVE_AND_CHOP: N closest trees to shack, where N = troll count
        if (turn > 200 && t.chopPower > 0 && t.canCarry())
        {
            int N = (int)allies.size();
            vector<pair<int, int>> byDist; // (dist from shack, tree index)
            for (int i = 0; i < (int)trees.size(); i++)
            {
                int d = bfs_dist_lookup[shack.y][shack.x][trees[i].y][trees[i].x];
                if (d >= 0)
                    byDist.push_back({d, i});
            }
            sort(byDist.begin(), byDist.end());
            for (int k = 0; k < min(N, (int)byDist.size()); k++)
            {
                const Tree &tr = trees[byDist[k].second];
                actions.push_back(Action::moveAndChop(t.id, t.player, tr.x, tr.y));
            }
        }

        // TODO: When there isn't enough tree to chop, planting for immediate wood is a good idea
        if (turn < 200 && (t.carryPlum > 0 || t.carryLemon > 0 || t.carryApple > 0 || t.carryBanana > 0))
        {
            for (const Position &cell : plantableCellsAroundShack[t.player])
            {
                if (treeAtCell[cell.y][cell.x] >= 0)
                    continue;
                if (t.carryPlum > 0)
                    actions.push_back(Action::moveAndPlant(t.id, t.player, cell.x, cell.y, "PLUM"));
                if (t.carryLemon > 0)
                    actions.push_back(Action::moveAndPlant(t.id, t.player, cell.x, cell.y, "LEMON"));
                if (t.carryApple > 0)
                    actions.push_back(Action::moveAndPlant(t.id, t.player, cell.x, cell.y, "APPLE"));
                if (t.carryBanana > 0)
                    actions.push_back(Action::moveAndPlant(t.id, t.player, cell.x, cell.y, "BANANA"));
            }
        }

        // MOVE_AND_PICK / MOVE_AND_DROP: target is the closest walkable cell adjacent
        // to the shack (shack cell itself is not walkable).
        bool canPick = !t.isCarrying() && (shack.plum > 0 || shack.lemon > 0 || shack.apple > 0 || shack.banana > 0);
        bool canDrop = t.isCarrying();
        if (canPick || canDrop)
        {
            int shackAdjX = -1, shackAdjY = -1, shackAdjDist = -1;
            for (const Position &cell : shackAdjCells[t.player])
            {
                int d = bfs_dist_lookup[t.y][t.x][cell.y][cell.x];
                if (d >= 0 && (shackAdjDist < 0 || d < shackAdjDist))
                {
                    shackAdjDist = d;
                    shackAdjX = cell.x;
                    shackAdjY = cell.y;
                }
            }

            // COuld be worth to generate the move on shack directly and adpat the engine to choose the best path
            // If the currently choosen cell has an ally, the move is blocked ...
            if (shackAdjX >= 0)
            {
                if (canPick)
                {
                    if (shack.plum > 0)
                        actions.push_back(Action::moveAndPick(t.id, t.player, shackAdjX, shackAdjY, "PLUM"));
                    if (shack.lemon > 0)
                        actions.push_back(Action::moveAndPick(t.id, t.player, shackAdjX, shackAdjY, "LEMON"));
                    if (shack.apple > 0)
                        actions.push_back(Action::moveAndPick(t.id, t.player, shackAdjX, shackAdjY, "APPLE"));
                    if (shack.banana > 0)
                        actions.push_back(Action::moveAndPick(t.id, t.player, shackAdjX, shackAdjY, "BANANA"));
                }

                if (canDrop)
                    actions.push_back(Action::moveAndDrop(t.id, t.player, shackAdjX, shackAdjY));
            }
        }

        // MOVE_AND_MINE_ONCE: closest reachable walkable cell adjacent to any mine
        if (turn < 200 && t.chopPower > 0 && t.canCarry())
        {
            int bestDist = -1;
            int bestX = -1, bestY = -1;
            for (const Position &cell : mineAdjCells)
            {
                int d = bfs_dist_lookup[t.y][t.x][cell.y][cell.x];
                if (d < 0)
                    continue;
                if (bestDist == -1 || d < bestDist)
                {
                    bestDist = d;
                    bestX = cell.x;
                    bestY = cell.y;
                }
            }
            if (bestX != -1)
                actions.push_back(Action::moveAndMine(t.id, t.player, bestX, bestY));
        }

        return actions;
    }

public:
    // =====================================================
    // ACTION GENERATION
    // =====================================================

    // Returns one candidate action list per troll (no cartesian product — DUCT-friendly).
    // Trolls busy on a macro inherited from `base` get a single-action list (the kept macro).
    // Also reports whether TRAIN is affordable; the caller appends TRAIN at apply time.
    void generatePerTrollMacroActions(int player,
                                      const ActionSet &base,
                                      vector<vector<Action>> &outPerTroll,
                                      bool &outCanTrain) const
    {
        const vector<Troll *> &playerTrolls = (player == 0) ? trolls : enemyTrolls;
        const Shack &shack = (player == 0) ? myShack : enemyShack;

        outPerTroll.clear();
        outPerTroll.reserve(playerTrolls.size());

        for (const Troll *tp : playerTrolls)
        {
            const Troll &t = *tp;
            const Action *kept = nullptr;
            for (const Action &a : base.actions)
            {
                if (a.trollid == t.id && !a.macroTaskFinished)
                {
                    kept = &a;
                    break;
                }
            }

            if (kept)
            {
                outPerTroll.push_back({*kept});
                continue;
            }

            vector<Action> trollActions = generateTrollMacroActions(t, shack, playerTrolls);
            if (trollActions.empty())
            {
                cerr << "No actions available for troll " << t.id << " at turn " << turn << endl;
                exit(0);
            }

            outPerTroll.push_back(move(trollActions));
        }

        int n = (int)playerTrolls.size();
        outCanTrain = shack.plum >= n + 4 &&
                      shack.lemon >= n + 4 &&
                      shack.apple >= n + 4 &&
                      shack.iron >= n + 4;
    }

    // =====================================================
    // ACTION APPLICATION
    // =====================================================

    void applyMacroActions(ActionSet &set)
    {
        if (set.actions.size() == 0)
        {
            cerr << "applyMacroActions: Applying empty ActionSet !!!" << endl;
            exit(0);
        }

        // Pre-check: a MOVE_AND_CHOP whose target tree is already gone finishes
        // immediately without burning a turn. Caller will regenerate actions
        // for those trolls on the next call.
        bool anyFinishedEarly = false;
        for (Action &a : set.actions)
        {
            if (a.type != Action::MOVE_AND_CHOP)
                continue;
            bool treeAlive = false;
            for (const Tree &tree : trees)
                if (tree.x == a.x && tree.y == a.y && tree.health > 0)
                {
                    treeAlive = true;
                    break;
                }
            if (!treeAlive)
            {
                a.macroTaskFinished = true;
                anyFinishedEarly = true;
            }
        }
        if (anyFinishedEarly)
            return;

        // Per-action moveTurns (for arrival turn detection) and totalTurns.
        // T = number of game turns to advance before at least one action finishes.
        int N = (int)set.actions.size();
        vector<int> moveTurnsArr(N, 0);
        vector<int> totalTurnsArr(N, 1);
        for (int i = 0; i < N; i++)
        {
            Action &a = set.actions[i];
            if (a.macroTaskFinished)
            {
                cerr << "Macro action " << a.toString() << " is already finished before using it !!! " << turn << endl;
                exit(0);
            }
            if (a.category != Action::PRIMITIVE)
            {
                Troll *tr = getTrollById(a.trollid);
                if (tr && !(tr->x == a.x && tr->y == a.y))
                {
                    int d = bfs_dist_lookup[tr->y][tr->x][a.y][a.x];
                    if (d >= 0)
                        moveTurnsArr[i] = (d + tr->movementSpeed - 1) / tr->movementSpeed;
                }
            }
            totalTurnsArr[i] = a.macroTurnCount(*this);
        }
        int T = *min_element(totalTurnsArr.begin(), totalTurnsArr.end());

        // Helper: returns true iff the tree at (ax, ay) is still alive.
        auto treeAliveAt = [&](int ax, int ay)
        {
            for (const Tree &tree : trees)
                if (tree.x == ax && tree.y == ay && tree.health > 0)
                    return true;
            return false;
        };

        // Turn 1: real moves via findNextPrimitiveAction (BFS-based moveToward).
        // At-target macros also emit their terminal primitive here.
        vector<Action> primitiveActions;
        primitiveActions.reserve(N);
        for (Action &a : set.actions)
            primitiveActions.push_back(a.findNextPrimitiveAction(*this));
        applyActions(primitiveActions);

        // Post-turn-1: at-target MOVE_AND_CHOPs already emitted a chop; mark
        // finished if the tree died from it.
        for (int i = 0; i < N; i++)
        {
            Action &a = set.actions[i];
            if (a.macroTaskFinished || a.type != Action::MOVE_AND_CHOP)
                continue;
            if (moveTurnsArr[i] == 0 && !treeAliveAt(a.x, a.y))
                a.macroTaskFinished = true;
        }

        // If any action finished on turn 1, we're done (matches original).
        for (const Action &a : set.actions)
            if (a.macroTaskFinished)
                return;

        // Turns 2..T: keep advancing. Each turn, each unfinished macro emits a
        // primitive based on its phase:
        //   - in transit (tn <= moveTurns): no primitive (move-phase abstraction)
        //   - arrival turn (tn == moveTurns + 1): teleport + emit terminal
        //   - chop continuation (tn > moveTurns + 1, MOVE_AND_CHOP): emit chop
        // The loop always runs exactly T-1 iterations so we advance T turns
        // total (1 from turn-1 applyActions + T-1 from this loop). Empty
        // primitive sets are fine: applyActions still grows trees + ticks turn.
        for (int tn = 2; tn <= T; tn++)
        {
            vector<Action> primitives;
            for (int i = 0; i < N; i++)
            {
                Action &a = set.actions[i];
                if (a.macroTaskFinished)
                    continue;
                int mt = moveTurnsArr[i];
                if (tn == mt + 1)
                {
                    // Arrival turn: teleport, emit terminal. Non-chop macros
                    // finish here; MOVE_AND_CHOP defers to tree-death check.
                    Troll *tr = getTrollById(a.trollid);
                    if (tr)
                    {
                        tr->x = a.x;
                        tr->y = a.y;
                    }
                    primitives.push_back(a.terminalPrimitive());
                    if (a.type != Action::MOVE_AND_CHOP)
                        a.macroTaskFinished = true;
                }
                else if (tn > mt + 1 && a.type == Action::MOVE_AND_CHOP)
                {
                    primitives.push_back(Action::chop(a.trollid, a.playerid));
                }
                // else still in transit: no primitive this turn
            }
            applyActions(primitives);

            // Post-turn: MOVE_AND_CHOPs that just chopped finish if tree died.
            for (int i = 0; i < N; i++)
            {
                Action &a = set.actions[i];
                if (a.macroTaskFinished || a.type != Action::MOVE_AND_CHOP)
                    continue;
                if (tn < moveTurnsArr[i] + 1)
                    continue; // hadn't started chopping yet
                if (!treeAliveAt(a.x, a.y))
                    a.macroTaskFinished = true;
            }
        }

        // Optimistic teleport: any unfinished macro whose troll never reached
        // its arrival turn within this call (mt + 1 > T) gets teleported to
        // its target. Matches the original turn-T behavior: the next
        // applyMacroActions call sees the troll already at-target. For
        // MOVE_AND_CHOP this means subsequent chops happen at mt=0.
        for (int i = 0; i < N; i++)
        {
            Action &a = set.actions[i];
            if (a.macroTaskFinished)
                continue;
            Troll *tr = getTrollById(a.trollid);
            if (tr && !(tr->x == a.x && tr->y == a.y))
            {
                tr->x = a.x;
                tr->y = a.y;
            }
        }
    }

    void applyActions(const vector<Action> &actions)
    {
        // 1. MOVE
        for (const Action &a : actions)
            if (a.type == Action::MOVE)
                applyMove(a);

        // 2. HARVEST (simultaneous, fruit sharing)
        applyHarvest(actions);

        // 3. PLANT
        for (const Action &a : actions)
            if (a.type == Action::PLANT)
                applyPlant(a);

        // 4. CHOP (simultaneous, wood sharing)
        applyChop(actions);

        // 5. PICK
        for (const Action &a : actions)
            if (a.type == Action::PICK)
                applyPick(a);

        // 6. TRAIN
        for (const Action &a : actions)
            if (a.type == Action::TRAIN)
                applyTrain(a);

        // 7. DROP
        for (const Action &a : actions)
            if (a.type == Action::DROP)
                applyDrop(a);

        // 8. MINE
        for (const Action &a : actions)
            if (a.type == Action::MINE)
                applyMine(a);

        // 9. Trees grow
        updateTrees();
        turn++;
    }

    bool isNearWater(int x, int y) const
    {
        return nearWaterLookup[y][x];
    }

    Troll *getTrollById(int id) { return &allTrolls[id]; }
    const Troll *getTrollById(int id) const { return &allTrolls[id]; }

private:
    int findTreeIndex(int x, int y) const
    {
        return treeAtCell[y][x];
    }

    void applyMove(const Action &a)
    {
        Troll *t = getTrollById(a.trollid);
        if (!t)
            return;

        const vector<Troll *> &allies = (t->player == 0) ? trolls : enemyTrolls;
        for (const Troll *ally : allies)
            if (ally->id != t->id && ally->x == a.x && ally->y == a.y)
                return;

        t->x = a.x;
        t->y = a.y;
    }

    void applyPlant(const Action &a)
    {
        Troll *t = getTrollById(a.trollid);
        if (!t)
            return;
        int *fruit = t->fruitField(a.resource);
        if (!fruit || *fruit <= 0)
            return;
        (*fruit)--;

        Tree tree;
        tree.type = a.resource;
        tree.x = t->x;
        tree.y = t->y;
        tree.size = 1;
        tree.health = Tree::healthFromSize(a.resource, 1);
        tree.fruits = 0;
        tree.cooldown = Tree::cooldownFromType(a.resource, isNearWater(t->x, t->y));

        int tx = t->x, ty = t->y;
        trees.push_back(move(tree));
        treeAtCell[ty][tx] = (int16_t)(trees.size() - 1);
    }

    void applyPick(const Action &a)
    {
        Troll *t = getTrollById(a.trollid);
        if (!t || !t->canCarry())
            return;
        Shack &shack = (t->player == 0) ? myShack : enemyShack;
        int *shackFruit = shack.fruitField(a.resource);
        int *trollFruit = t->fruitField(a.resource);
        if (!shackFruit || !trollFruit || *shackFruit <= 0)
            return;
        (*shackFruit)--;
        (*trollFruit)++;
    }

    void applyDrop(const Action &a)
    {
        Troll *t = getTrollById(a.trollid);
        if (!t)
            return;
        Shack &shack = (t->player == 0) ? myShack : enemyShack;
        shack.plum += t->carryPlum;
        t->carryPlum = 0;
        shack.lemon += t->carryLemon;
        t->carryLemon = 0;
        shack.apple += t->carryApple;
        t->carryApple = 0;
        shack.banana += t->carryBanana;
        t->carryBanana = 0;
        shack.iron += t->carryIron;
        t->carryIron = 0;
        shack.wood += t->carryWood;
        t->carryWood = 0;
    }

    void applyMine(const Action &a)
    {
        Troll *t = getTrollById(a.trollid);
        if (!t)
            return;
        int amount = min(t->chopPower, t->remainingCarry());
        if (amount > 0)
            t->carryIron += amount;
    }

    void applyTrain(const Action &a)
    {
        // Should check if shack has enough resources because a troll can PICK items from the shack in the same turn
        int player = a.playerid;
        const vector<Troll *> &teamTrolls = (player == 0) ? trolls : enemyTrolls;
        Shack &shack = (player == 0) ? myShack : enemyShack;

        int n = (int)teamTrolls.size();
        int costPlum = n + a.moveSpeed * a.moveSpeed;
        int costLemon = n + a.carryCapacity * a.carryCapacity;
        int costApple = n + a.harvestPower * a.harvestPower;
        int costIron = n + a.chopPower * a.chopPower;

        if (shack.plum < costPlum || shack.lemon < costLemon ||
            shack.apple < costApple || shack.iron < costIron)
            return;

        shack.plum -= costPlum;
        shack.lemon -= costLemon;
        shack.apple -= costApple;
        shack.iron -= costIron;

        int nextId = 0;
        for (const Troll *t : trolls)
            nextId = max(nextId, t->id);
        for (const Troll *t : enemyTrolls)
            nextId = max(nextId, t->id);
        nextId++;

        if ((int)allTrolls.size() >= MAX_TROLLS_TOTAL)
        {
            cerr << "allTrolls capacity exceeded — increase MAX_TROLLS_TOTAL" << endl;
            return;
        }

        Troll nt;
        nt.id = nextId;
        nt.player = player;
        nt.x = shack.x;
        nt.y = shack.y;
        nt.movementSpeed = a.moveSpeed;
        nt.carryCapacity = a.carryCapacity;
        nt.harvestPower = a.harvestPower;
        nt.chopPower = a.chopPower;
        nt.carryPlum = nt.carryLemon = nt.carryApple = 0;
        nt.carryBanana = nt.carryIron = nt.carryWood = 0;
        allTrolls.push_back(move(nt));
        rebuildTrollPointers();
    }

    void applyHarvest(const vector<Action> &actions)
    {
        // Bucket harvest actions by the tree they target (same cell as the troll)
        vector<vector<int>> byTree(trees.size());
        for (const Action &a : actions)
        {
            if (a.type != Action::HARVEST)
                continue;
            Troll *t = getTrollById(a.trollid);
            if (!t)
                continue;
            int idx = findTreeIndex(t->x, t->y);
            if (idx >= 0)
                byTree[idx].push_back(t->id);
        }

        for (int idx = 0; idx < (int)trees.size(); idx++)
        {
            Tree &tree = trees[idx];
            vector<int> &trollIds = byTree[idx];
            if (trollIds.empty() || tree.fruits <= 0)
                continue;

            // Each troll may harvest up to harvestPower fruits this turn
            vector<int> budgets;
            for (int id : trollIds)
                budgets.push_back(getTrollById(id)->harvestPower);

            // Distribute one fruit per active troll per round until exhausted
            while (tree.fruits > 0)
            {
                // Active = trolls who still have harvest budget and carry capacity
                vector<int> active;
                for (int i = 0; i < (int)trollIds.size(); i++)
                {
                    Troll *t = getTrollById(trollIds[i]);
                    if (budgets[i] > 0 && t->canCarry())
                        active.push_back(i);
                }
                if (active.empty())
                    break;

                // Last-fruit duplication: demand > supply -> everyone still gets one
                if ((int)active.size() > tree.fruits)
                {
                    for (int i : active)
                    {
                        Troll *t = getTrollById(trollIds[i]);
                        int *f = t->fruitField(tree.type);
                        if (f)
                            (*f)++;
                        budgets[i]--;
                    }
                    tree.fruits = 0;
                }
                else
                {
                    // Normal round: each active troll grabs one fruit
                    for (int i : active)
                    {
                        Troll *t = getTrollById(trollIds[i]);
                        int *f = t->fruitField(tree.type);
                        if (f)
                            (*f)++;
                        budgets[i]--;
                    }
                    tree.fruits -= (int)active.size();
                }
            }
        }
    }

    void applyChop(const vector<Action> &actions)
    {
        // Bucket chop actions by the tree they target (same cell as the troll)
        vector<vector<int>> byTree(trees.size());
        for (const Action &a : actions)
        {
            if (a.type != Action::CHOP)
                continue;
            Troll *t = getTrollById(a.trollid);
            if (!t)
                continue;
            int idx = findTreeIndex(t->x, t->y);
            if (idx >= 0)
                byTree[idx].push_back(t->id);
        }

        vector<bool> killed(trees.size(), false);
        for (int idx = 0; idx < (int)trees.size(); idx++)
        {
            Tree &tree = trees[idx];
            vector<int> &trollIds = byTree[idx];
            if (trollIds.empty())
                continue;

            // All choppers hit simultaneously: sum their chopPower into the tree
            int totalDmg = 0;
            for (int id : trollIds)
                totalDmg += getTrollById(id)->chopPower;
            tree.health -= totalDmg;

            // Tree survives this turn -> no wood drop
            if (tree.health > 0)
                continue;

            killed[idx] = true;

            // Tree dies: distribute tree.size wood among choppers
            int wood = tree.size;

            // Each chopper can collect up to its remaining carry capacity
            vector<int> budgets;
            for (int id : trollIds)
                budgets.push_back(getTrollById(id)->remainingCarry());

            // Distribute one wood per active troll per round until exhausted
            while (wood > 0)
            {
                // Active = choppers who still have free carry capacity
                vector<int> active;
                for (int i = 0; i < (int)trollIds.size(); i++)
                    if (budgets[i] > 0)
                        active.push_back(i);
                if (active.empty())
                    break;

                // Last-wood duplication: demand > supply -> everyone still gets one
                if ((int)active.size() > wood)
                {
                    for (int i : active)
                    {
                        getTrollById(trollIds[i])->carryWood++;
                        budgets[i]--;
                    }
                    wood = 0;
                }
                else
                {
                    // Normal round: each active troll grabs one wood
                    for (int i : active)
                    {
                        getTrollById(trollIds[i])->carryWood++;
                        budgets[i]--;
                    }
                    wood -= (int)active.size();
                }
            }
        }

        // Remove killed trees (back to front to keep indices valid)
        bool anyKilled = false;
        for (int i = (int)trees.size() - 1; i >= 0; i--)
            if (killed[i])
            {
                trees.erase(trees.begin() + i);
                anyKilled = true;
            }
        if (anyKilled)
            rebuildTreeIndex();
    }

    void updateTrees()
    {
        for (Tree &tree : trees)
            tree.grow(isNearWater(tree.x, tree.y));
    }
};

// =====================================================
// ACTION — out-of-line methods (need complete State/Troll)
// =====================================================

const Troll *Action::findTrollInState(const State &s) const
{
    return s.getTrollById(trollid);
}

// Returns Action::move toward (targetX, targetY) picking the reachable cell
// within t.movementSpeed that minimises remaining BFS distance to the target.
// Skips ally-occupied cells. If no improving free cell exists, the troll stays
// put — picking a worsening cell would cause A→E→A oscillation because the
// BFS would just pull the troll back next turn. Repeated stalls are bounded by
// MAX_MACRO_TURNS in findNextPrimitiveAction, so MCTS sees the bad outcome.
Action Action::moveToward(const State &s, const Troll &t, int targetX, int targetY) const
{
    const vector<Troll *> &allies = (t.player == 0) ? s.trolls : s.enemyTrolls;

    auto cellOccupied = [&](int cx, int cy)
    {
        for (const Troll *ally : allies)
            if (ally->id != t.id && ally->x == cx && ally->y == cy)
                return true;
        return false;
    };

    int x = t.x, y = t.y;
    for (int step = 0; step < t.movementSpeed; step++)
    {
        if (x == targetX && y == targetY)
            break;
        uint8_t mask = nextStepMask[y][x][targetY][targetX];
        if (mask == 0)
            break;
        int chosen = -1;
        for (int k = 0; k < 4; k++)
        {
            if (!(mask & (1u << k)))
                continue;
            int nx = x + STEP_DXS[k];
            int ny = y + STEP_DYS[k];
            if (cellOccupied(nx, ny))
                continue;
            chosen = k;
            break;
        }
        if (chosen < 0)
            break;
        x += STEP_DXS[chosen];
        y += STEP_DYS[chosen];
    }

    return Action::move(trollid, playerid, x, y);
}

Action Action::findNextPrimitiveAction(const State &s)
{
    macroStepsTaken++;

    if (category == PRIMITIVE)
    {
        macroTaskFinished = true;
        return *this;
    }

    // Defensive cap: a macro that hasn't reached its target after MAX_MACRO_TURNS
    // applyMacroActions calls is forced to finish. Prevents infinite loops if
    // pathing gets permanently blocked (e.g. mutually-blocking trolls beyond
    // moveToward's heuristic). Sized to accommodate a MOVE_AND_CHOP chunked
    // 1 chop per call against a max-health tree (~20 HP) with chopPower 1.
    constexpr int MAX_MACRO_TURNS = 40;
    if (macroStepsTaken >= MAX_MACRO_TURNS)
    {
        cerr << "Macro action " << toString() << " forced to finish after reaching turn limit of " << MAX_MACRO_TURNS << endl;
        macroTaskFinished = true;
        return *this;
    }

    // Move toward destination
    const Troll *t = findTrollInState(s);
    if (t && !(t->x == x && t->y == y))
    {
        return moveToward(s, *t, x, y);
    }

    // MOVE_AND_CHOP: keep chopping the tree until it dies. macroTaskFinished
    // is set only once the tree is gone; otherwise we emit another chop next
    // call (troll already at target -> macroTurnCount returns 1).
    if (type == MOVE_AND_CHOP)
    {
        bool treeAlive = false;
        for (const Tree &tree : s.trees)
            if (tree.x == x && tree.y == y && tree.health > 0)
            {
                treeAlive = true;
                break;
            }
        if (!treeAlive)
        {
            macroTaskFinished = true;
            return *this;
        }
        return Action::chop(trollid, playerid);
    }

    // Execute terminal primitive only once arrived
    macroTaskFinished = true;
    return terminalPrimitive();
}

Action Action::terminalPrimitive() const
{
    switch (type)
    {
    case MOVE_AND_HARVEST_ONCE:
        return Action::harvest(trollid, playerid);
    case MOVE_AND_PLANT:
        return Action::plant(trollid, playerid, resource);
    case MOVE_AND_CHOP:
        return Action::chop(trollid, playerid);
    case MOVE_AND_PICK:
        return Action::pick(trollid, playerid, resource);
    case MOVE_AND_DROP:
        return Action::drop(trollid, playerid);
    case MOVE_AND_MINE_ONCE:
        return Action::mine(trollid, playerid);
    default:
        return *this;
    }
}

int Action::macroTurnCount(const State &s) const
{
    if (category == PRIMITIVE)
        return 1;

    const Troll *t = findTrollInState(s);
    if (!t)
        return 1;

    int moveTurns = 0;
    if (!(t->x == x && t->y == y))
    {
        int d = bfs_dist_lookup[t->y][t->x][y][x];
        if (d < 0)
            return 1;
        moveTurns = (d + t->movementSpeed - 1) / t->movementSpeed;
    }

    if (type != MOVE_AND_CHOP)
        return moveTurns + 1;

    // MOVE_AND_CHOP: chopping the tree may take several turns. Simulate the
    // tree's growth during travel and the chop/grow interleave until it dies
    // (chop happens before grow within applyActions, matching the real order).
    const Tree *targetTree = nullptr;
    for (const Tree &tr : s.trees)
        if (tr.x == x && tr.y == y)
        {
            targetTree = &tr;
            break;
        }
    if (!targetTree)
        return moveTurns; // tree gone, nothing to chop

    if (t->chopPower <= 0)
        return moveTurns + 1; // shouldn't happen, defensive

    Tree sim = *targetTree;
    bool nearWater = s.isNearWater(x, y);

    auto growOnce = [&]()
    {
        if (sim.cooldown > 0)
        {
            sim.cooldown--;
            return;
        }
        if (sim.size < 4)
        {
            int oldMax = Tree::healthFromSize(sim.type, sim.size);
            int newMax = Tree::healthFromSize(sim.type, sim.size + 1);
            sim.size++;
            sim.health += (newMax - oldMax);
            sim.cooldown = Tree::cooldownFromType(sim.type, nearWater);
        }
        else if (sim.fruits < 3)
        {
            sim.fruits++;
            sim.cooldown = Tree::cooldownFromType(sim.type, nearWater);
        }
    };

    for (int i = 0; i < moveTurns; i++)
        growOnce();

    constexpr int CHOP_CAP = 30;
    int chopTurns = 0;
    while (sim.health > 0 && chopTurns < CHOP_CAP)
    {
        sim.health -= t->chopPower;
        chopTurns++;
        if (sim.health <= 0)
            break;
        growOnce();
    }

    return moveTurns + chopTurns;
}

// =====================================================
// MCTS
// =====================================================

constexpr int MAX_NODES = 100000;
constexpr int MAX_TROLLS_PER_PLAYER = 8;

// One bandit slot per (troll, candidate-action) pair.
struct TrollActionStat
{
    int visits = 0;
    float score = 0.0f;
};

// DUCT node: instead of enumerating the cartesian product of per-troll macros
// as children, we keep per-troll bandits and lazily allocate child nodes keyed
// by the joint action (one action index per troll).
struct Node
{
    State state;
    // Unfinished macro actions inherited from parent's chosen joint action.
    // Trolls busy on a macro will appear in perTrollActions as a single-action list.
    ActionSet base;

    bool initialized = false;
    bool canTrain = false;
    int player = 0;

    vector<vector<Action>> perTrollActions;        // [trollIdx][actionIdx]
    vector<vector<TrollActionStat>> perTrollStats; // [trollIdx][actionIdx]

    // children[jointKey] = child node reached by applying that joint.
    // jointKey packs each troll's chosen actionIdx into 8 bits.
    unordered_map<uint64_t, Node *> children;

    int visits = 0;
    float score = 0.0f;
};

Node nodePool[MAX_NODES];
int nodeCount = 0;
int g_rootTurn = 0;

Node *allocNode()
{
    if (nodeCount >= MAX_NODES - 1)
    {
        cerr << "Node pool exhausted!" << endl;
        exit(1);
    }

    Node *n = &nodePool[nodeCount++];

    n->base.actions.clear();
    n->initialized = false;
    n->canTrain = false;
    n->player = 0;
    n->perTrollActions.clear();
    n->perTrollStats.clear();
    n->children.clear();
    n->visits = 0;
    n->score = 0.0f;

    return n;
}

void resetNodePool() { nodeCount = 0; }

float mapRange(float value, float inputMin, float inputMax, float outputMin, float outputMax)
{
    float t = (value - inputMin) / (inputMax - inputMin);
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return t * (outputMax - outputMin) + outputMin;
};

// Placeholder heuristic: resource differential. Replace with a domain-specific
// evaluation when ready (tree counts, troll positioning, carried fruit, etc.).
float heuristic(const State &s)
{
    constexpr int CHOPPING_PHASE_START = 200;

    float myGamePts = s.myShack.plum + s.myShack.lemon + s.myShack.apple +
                      s.myShack.banana + 4 * s.myShack.wood;
    float enGamePts = s.enemyShack.plum + s.enemyShack.lemon + s.enemyShack.apple +
                      s.enemyShack.banana + 4 * s.enemyShack.wood;

    // Compute game points
    float myRes = myGamePts;
    float enRes = enGamePts;

    bool chopPhase = s.turn >= CHOPPING_PHASE_START;

    for (const Troll *t : s.trolls)
    {
        // Having a troll is always good
        myRes += 50;

        if (!chopPhase)
            myRes += 0.25f * (t->carryPlum + t->carryLemon + t->carryApple + t->carryBanana + t->carryIron) +
                     (1.0f + 2.0f * chopPhase) * t->carryWood;
    }
    for (const Troll *t : s.enemyTrolls)
    {
        // Having a troll is always good
        enRes += 50;

        if (!chopPhase)
            enRes += 0.25f * (t->carryPlum + t->carryLemon + t->carryApple + t->carryBanana + t->carryIron) +
                     (1.0f + 2.0f * chopPhase) * t->carryWood;
    }

    // Encourage balanced gathering of plum/lemon/apple/iron toward next troll training.
    // Cost = (trollCount + 4) of each. Bonus scales linearly up to 25 when fully ready.
    if (!chopPhase)
    {
        int statNumber = 2;
        int myTrollCount = (int)s.trolls.size();
        int enTrollCount = (int)s.enemyTrolls.size();

        float myRessourceCost = myTrollCount + statNumber * statNumber;
        float enRessourceCost = enTrollCount + statNumber * statNumber;

        float myTrainReady = (min((float)s.myShack.plum, myRessourceCost) + min((float)s.myShack.lemon, myRessourceCost) +
                              min((float)s.myShack.apple, myRessourceCost) + min((float)s.myShack.iron, myRessourceCost)) /
                             (4.0f * myRessourceCost);
        float enTrainReady = (min((float)s.enemyShack.plum, enRessourceCost) + min((float)s.enemyShack.lemon, enRessourceCost) +
                              min((float)s.enemyShack.apple, enRessourceCost) + min((float)s.enemyShack.iron, enRessourceCost)) /
                             (4.0f * enRessourceCost);

        myRes += 40.0f * myTrainReady;
        enRes += 40.0f * enTrainReady;
    }

    // Each type of fruit produces a bonus from its two closest trees to each shack.
    // Banana is ignored because it's not used for training. It should be chopped for wood
    constexpr int NUM_FRUITS = 3;
    constexpr int TOP_K = 2;
    const string fruitTypes[NUM_FRUITS] = {"PLUM", "LEMON", "APPLE"};
    float bestMy[NUM_FRUITS][TOP_K] = {};
    float bestEn[NUM_FRUITS][TOP_K] = {};

    auto insertTopK = [](float (&top)[TOP_K], float score)
    {
        if (score <= top[TOP_K - 1])
            return;
        int k = TOP_K - 1;
        while (k > 0 && score > top[k - 1])
        {
            top[k] = top[k - 1];
            k--;
        }
        top[k] = score;
    };

    // Single pass over trees: contributes both the fruit-tree top-K bonus (when
    // not in chop phase) and the chop-progress reward (when in chop phase).
    for (const auto &tree : s.trees)
    {
        int dMy = bfs_dist_lookup[s.myShack.y][s.myShack.x][tree.y][tree.x];
        int dEn = bfs_dist_lookup[s.enemyShack.y][s.enemyShack.x][tree.y][tree.x];
        if (dMy <= 0 || dEn <= 0)
            continue;

        if (!chopPhase)
        {
            int idx = -1;
            for (int i = 0; i < NUM_FRUITS; i++)
                if (tree.type == fruitTypes[i])
                {
                    idx = i;
                    break;
                }
            if (idx >= 0)
            {
                float treeScore = nearWaterLookup[tree.y][tree.x] ? 60.0f : 40.0f;
                insertTopK(bestMy[idx], treeScore / (float)dMy);
                insertTopK(bestEn[idx], treeScore / (float)dEn);
            }
        }
        else
        {
            int maxHealth = Tree::healthFromSize(tree.type, tree.size);
            float progressScore = (float)(maxHealth - tree.health) / (float)maxHealth;
            myRes += progressScore / (float)dMy;
            enRes += progressScore / (float)dEn;
        }
    }

    for (int i = 0; i < NUM_FRUITS; i++)
        for (int k = 0; k < TOP_K; k++)
        {
            myRes += bestMy[i][k];
            enRes += bestEn[i][k];
        }

    // Enemy score reliability decays with simulation depth: we don't simulate
    // opponent moves, so the further ahead we look, the less accurate enRes is.
    int turnsElapsed = max(0, s.turn - g_rootTurn);
    enRes += turnsElapsed * 0.05;

    constexpr float SCALE = 1000.0f;
    if (myRes > SCALE)
        cerr << "Warning: myRes " << myRes << " exceeds heuristic scale of " << SCALE << endl;

    return max(-1.0f, min(1.0f, (myRes - enRes) / SCALE));
}

// constexpr float UCT_C = 1.5;
constexpr float UCT_C = 1.41421356f;

// Two macro actions conflict if they would race for the same spot/resource.
// PICK/DROP at the shack are intentionally allowed to coexist.
static bool isTargetedMacro(const Action &a)
{
    if (a.category != Action::MACRO)
        return false;
    switch (a.type)
    {
    case Action::MOVE_AND_HARVEST_ONCE:
    case Action::MOVE_AND_CHOP:
    case Action::MOVE_AND_PLANT:
    case Action::MOVE_AND_MINE_ONCE:
        return true;
    default:
        return false;
    }
}

static bool conflictsWith(const Action &a, const Action &b)
{
    if (!isTargetedMacro(a) || !isTargetedMacro(b))
        return false;
    return a.x == b.x && a.y == b.y;
}

// Lazily build per-troll action lists and bandit slots on first visit.
void initializeNode(Node *node)
{
    if (node->initialized)
        return;
    node->state.generatePerTrollMacroActions(node->player, node->base,
                                             node->perTrollActions, node->canTrain);

    node->perTrollStats.resize(node->perTrollActions.size());
    for (int i = 0; i < (int)node->perTrollActions.size(); i++)
        node->perTrollStats[i].assign(node->perTrollActions[i].size(), {});

    node->initialized = true;
}

// DUCT selection: independently pick an action per troll, masking out actions
// whose target collides with what an earlier troll has already chosen this iter.
// Untried (visits==0) actions are picked first (random); otherwise UCB1 over
// the troll's bandit using the parent's total visit count as logN.
// Returns false only if some troll has no admissible action (extremely rare —
// happens only if every candidate conflicts with prior picks).
static bool selectJointAction(Node *node, vector<int> &outIdx, uint64_t &outKey)
{
    int M = (int)node->perTrollActions.size();
    outIdx.assign(M, -1);
    outKey = 0;

    float logN = node->visits > 0 ? logf((float)node->visits) : 0.0f;

    vector<int> chosenSoFar;
    chosenSoFar.reserve(M);

    for (int t = 0; t < M; t++)
    {
        const auto &actions = node->perTrollActions[t];
        const auto &stats = node->perTrollStats[t];
        int K = (int)actions.size();

        // Build admissible set: actions that don't conflict with any previously
        // chosen troll's pick this iteration.
        vector<int> admissible;
        admissible.reserve(K);
        for (int i = 0; i < K; i++)
        {
            bool ok = true;
            for (int prev : chosenSoFar)
            {
                int prevTroll = prev >> 16;
                int prevIdx = prev & 0xFFFF;
                if (conflictsWith(actions[i], node->perTrollActions[prevTroll][prevIdx]))
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
                admissible.push_back(i);
        }

        // Fallback: if every candidate conflicts (rare), allow all so we still
        // produce a joint action — let the heuristic absorb the wasted move.
        if (admissible.empty())
            for (int i = 0; i < K; i++)
                admissible.push_back(i);

        // Prefer untried actions first (random tiebreak).
        vector<int> untried;
        for (int i : admissible)
            if (stats[i].visits == 0)
                untried.push_back(i);

        int pick;
        if (!untried.empty())
        {
            pick = untried[rand() % untried.size()];
        }
        else
        {
            int best = admissible[0];
            float bestUct = -1e30f;
            for (int i : admissible)
            {
                float exploit = stats[i].score / (float)stats[i].visits;
                float explore = UCT_C * sqrtf(logN / (float)stats[i].visits);
                float uct = exploit + explore;
                if (uct > bestUct)
                {
                    bestUct = uct;
                    best = i;
                }
            }
            pick = best;
        }

        outIdx[t] = pick;
        chosenSoFar.push_back((t << 16) | pick);
        outKey |= ((uint64_t)(pick & 0xFF)) << (t * 8);
    }
    return true;
}

// Build the full ActionSet (per-troll picks + optional TRAIN) for application.
static ActionSet jointToActionSet(Node *node, const vector<int> &idx)
{
    ActionSet set;
    set.actions.reserve(node->perTrollActions.size() + (node->canTrain ? 1 : 0));
    for (int t = 0; t < (int)idx.size(); t++)
        set.actions.push_back(node->perTrollActions[t][idx[t]]);
    if (node->canTrain)
        set.actions.push_back(Action::train(node->player, 2, 2, 2, 2));
    return set;
}

// Apply the joint action and return (or create) the corresponding child node.
Node *expand(Node *node, const vector<int> &idx, uint64_t key)
{
    auto it = node->children.find(key);
    if (it != node->children.end())
        return it->second;

    Node *child = allocNode();
    child->state = node->state;
    child->player = node->player;

    ActionSet applied = jointToActionSet(node, idx);
    child->state.applyMacroActions(applied);

    for (const Action &a : applied.actions)
        if (!a.macroTaskFinished)
            child->base.actions.push_back(a);

    node->children[key] = child;
    return child;
}

float mcts(Node *node, int depth = 0)
{
    if (depth > 100)
    {
        cerr << "[MCTS] deep depth=" << depth << " nodes=" << nodeCount << endl;
        exit(0);
    }

    if (!node->initialized)
        initializeNode(node);

    vector<int> idx;
    uint64_t key;
    selectJointAction(node, idx, key);

    auto it = node->children.find(key);
    bool isNewChild = (it == node->children.end());

    Node *childNode = expand(node, idx, key);
    float childValue;

    if (isNewChild)
    {
        childValue = heuristic(childNode->state);
        childNode->visits++;
        childNode->score += childValue;
    }
    else
    {
        childValue = mcts(childNode, depth + 1);
    }

    // Per-troll bandit backprop.
    for (int t = 0; t < (int)idx.size(); t++)
    {
        node->perTrollStats[t][idx[t]].visits++;
        node->perTrollStats[t][idx[t]].score += childValue;
    }
    node->visits++;
    node->score += childValue;

    return childValue;
}

// For each troll, return the most-visited candidate action (with score tiebreak).
static vector<int> getBestPerTrollIndices(Node *node, bool log = false)
{
    vector<int> best(node->perTrollActions.size(), 0);
    for (int t = 0; t < (int)node->perTrollActions.size(); t++)
    {
        const auto &stats = node->perTrollStats[t];
        int bestIdx = 0;
        int bestVisits = -1;
        float bestScore = -1e30f;
        for (int i = 0; i < (int)stats.size(); i++)
        {
            float avg = stats[i].visits > 0 ? stats[i].score / stats[i].visits : -1e30f;
            if (stats[i].visits > bestVisits ||
                (stats[i].visits == bestVisits && avg > bestScore))
            {
                bestVisits = stats[i].visits;
                bestScore = avg;
                bestIdx = i;
            }
        }
        best[t] = bestIdx;

        if (log)
        {
            Troll &troll = *node->state.trolls[t];
            cerr << "Troll " << troll.id << " bandit:";
            for (int i = 0; i < (int)stats.size(); i++)
            {
                float avg = stats[i].visits > 0 ? stats[i].score / stats[i].visits : 0.0f;
                cerr << " | " << node->perTrollActions[t][i].toString()
                     << " v=" << stats[i].visits << " q=" << avg << endl;
            }
        }
    }
    return best;
}

vector<Action> runMCTS(const State &rootState)
{
    g_rootTurn = rootState.turn;
    resetNodePool();
    Node *root = allocNode();
    root->state = rootState;
    root->player = 0;

    auto start = chrono::steady_clock::now();
    int iters = 0;
    while (true)
    {
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                           chrono::steady_clock::now() - start)
                           .count();
        if (elapsed >= 40)
            break;
        if (nodeCount >= MAX_NODES - 64)
            break;

        mcts(root);
        iters++;
    }

    cerr << "[MCTS] iters=" << iters << " nodes=" << nodeCount << endl;

    if (!root->initialized)
        initializeNode(root);

    vector<int> bestIdx = getBestPerTrollIndices(root, true);

    // Resolve final per-troll picks against each other to avoid a conflicting
    // joint output: if troll t's choice collides with an earlier troll's, fall
    // back to its next-best non-conflicting candidate.
    for (int t = 1; t < (int)bestIdx.size(); t++)
    {
        const Action &a = root->perTrollActions[t][bestIdx[t]];
        bool conflict = false;
        for (int p = 0; p < t; p++)
            if (conflictsWith(a, root->perTrollActions[p][bestIdx[p]]))
            {
                conflict = true;
                break;
            }
        if (!conflict)
            continue;

        const auto &stats = root->perTrollStats[t];
        int alt = -1;
        int altVisits = -1;
        float altScore = -1e30f;
        for (int i = 0; i < (int)stats.size(); i++)
        {
            if (i == bestIdx[t])
                continue;
            const Action &cand = root->perTrollActions[t][i];
            bool bad = false;
            for (int p = 0; p < t; p++)
                if (conflictsWith(cand, root->perTrollActions[p][bestIdx[p]]))
                {
                    bad = true;
                    break;
                }
            if (bad)
                continue;
            float avg = stats[i].visits > 0 ? stats[i].score / stats[i].visits : -1e30f;
            if (stats[i].visits > altVisits ||
                (stats[i].visits == altVisits && avg > altScore))
            {
                altVisits = stats[i].visits;
                altScore = avg;
                alt = i;
            }
        }
        if (alt >= 0)
            bestIdx[t] = alt;
    }

    ActionSet best = jointToActionSet(root, bestIdx);
    return best.actions;
}

// =====================================================
// PARSING
// =====================================================

void parseMap(int w, int h, State &state)
{
    for (int y = 0; y < h; y++)
    {
        string row;
        getline(cin, row);

        for (int x = 0; x < w; x++)
        {
            state.grid[y][x] = row[x];

            if (row[x] == '+')
            {
                Position p;
                p.x = x;
                p.y = y;
                ironMines.push_back(p);
            }
        }
    }
}

void parseResources(State &state)
{
    auto &me = state.myShack;
    auto &enemy = state.enemyShack;
    cin >> me.plum >> me.lemon >> me.apple >> me.banana >> me.iron >> me.wood;
    cin >> enemy.plum >> enemy.lemon >> enemy.apple >> enemy.banana >> enemy.iron >> enemy.wood;
}

void parseTrees(State &state)
{
    int n;
    cin >> n;

    state.trees.clear();
    state.trees.resize(n);
    for (auto &t : state.trees)
        cin >> t.type >> t.x >> t.y >> t.size >> t.health >> t.fruits >> t.cooldown;
    state.rebuildTreeIndex();
}

void parseTrolls(State &state)
{
    int n;
    cin >> n;

    state.allTrolls.clear();
    if (state.allTrolls.capacity() < MAX_TROLLS_TOTAL)
        state.allTrolls.reserve(MAX_TROLLS_TOTAL);

    for (int i = 0; i < n; i++)
    {
        Troll t;

        cin >> t.id >> t.player >> t.x >> t.y >> t.movementSpeed >> t.carryCapacity >> t.harvestPower >> t.chopPower >> t.carryPlum >> t.carryLemon >> t.carryApple >> t.carryBanana >> t.carryIron >> t.carryWood;

        if (t.player == 0 && state.myShack.x == -1)
        {
            state.myShack.x = t.x;
            state.myShack.y = t.y;
        }
        else if (t.player != 0 && state.enemyShack.x == -1)
        {
            state.enemyShack.x = t.x;
            state.enemyShack.y = t.y;
        }

        state.allTrolls.push_back(t);
    }

    state.rebuildTrollPointers();
}

// =====================================================
// ACTION OUTPUT
// =====================================================

void displayActions(vector<Action> &actions, const State &state)
{
    for (int i = 0; i < (int)actions.size(); i++)
    {
        Action primitive = actions[i].findNextPrimitiveAction(state);
        cout << primitive.toString();
        if (i + 1 < (int)actions.size())
            cout << ";";
    }
    cout << endl;
}

// =====================================================
// MAIN
// =====================================================

int main()
{

    int w, h;
    cin >> w >> h;
    cin.ignore();

    State state;
    state.w = w;
    state.h = h;
    state.myShack.x = -1;
    state.enemyShack.x = -1;

    parseMap(w, h, state);
    buildBfsLookup(w, h, state.grid);
    buildNearWaterLookup(w, h, state.grid);
    buildMineAdjCells(w, h, state.grid);

    State prevState;

    int turn = 0;
    while (true)
    {
        prevState = state;

        parseResources(state);
        parseTrees(state);
        parseTrolls(state);
        state.turn = turn;

        // Shack positions are only known after the first parseTrolls (shack
        // sits where the starting troll spawns). Build the per-player caches
        // once they're available.
        if (!shackCachesReady && state.myShack.x >= 0 && state.enemyShack.x >= 0)
            buildShackCaches(w, h, state.grid,
                             state.myShack.x, state.myShack.y,
                             state.enemyShack.x, state.enemyShack.y);

        vector<Action> actions = runMCTS(state);

        displayActions(actions, state);
        turn++;
    }
}
