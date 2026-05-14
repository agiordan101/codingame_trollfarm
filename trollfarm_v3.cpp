#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>
#include <queue>
#include <cstdlib>
#include <chrono>

/*
    3

    Game plan :
        - Create static BFS lookup table at first turn. Ouput move based on troll speed and BFS distance to target.
        - At all times, if an enemy is chopping a tree and I can reach it before he finishes, do it (when at least one slot is empty)
        - Harvest to generate Troll until turn 200 arrives
            Only trolls stats : (2, 2, 1, 2)
        - After turn 200, CHOP all trees
        - Generate a map for plant. Normalize between 0 and 0.5 using best cell score as 0.5 and score=0 as 0.
            Weight all cells based on the distance between shack. The closer shacks are, less we should plant
            When trolls have any fruits, plant according to the probability of the cell.
            if unplantable
                0
            else
                + d to enemy shack
                - d to my shack
                * 1.2 if next to water
        - When trolls count = 3 -> Plant on all cells with d<=2 ??
        - Do NOT plant trees if shacks distance is <= 5


    Algorithms :
        - Faire un MCTS classique avec une heuristic.
        - Faire un macro-MCTS en ajoutant des macro actions. Soit un séquence défini de k actions qui termine sur un état à t+k tours
            Macro actions possibles (Sont suivis par une action primitive) :
            - Troll X MOVE à un tree, pour ensuite CHOP tout l'arbre
            - Troll X MOVE à un tree, pour ensuite HARVEST jusqu'a ce qu'il soit plein ou que l'arbre n'ait plus de fruits
            - Troll X MOVE à une mine, pour ensuite MINE jusqu'a ce qu'il soit plein
            - Troll X MOVE au shack allié, pour ensuite PICK un fruit
            - Troll X MOVE au shack allié, pour ensuite DROP ce qu'il carry
            - Troll X MOVE à une cell particuliere, pour ensuite PLANT
            Actions primitives restantes :
            - Train un troll avec des stats données

        - Tester un HMCTS-OP avec le graph MAXQ suivant :

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

struct Action
{
    enum Type
    {
        MOVE,
        HARVEST,
        PLANT,
        CHOP,
        PICK,
        DROP,
        TRAIN,
        MINE
    };
    Type type;
    int id = 0;
    int x = 0, y = 0;
    string resource;
    int moveSpeed = 0, carryCapacity = 0, harvestPower = 0, chopPower = 0;

    static Action move(int id, int x, int y) { return {MOVE, id, x, y}; }
    static Action harvest(int id) { return {HARVEST, id}; }
    static Action plant(int id, const string &res) { return {PLANT, id, 0, 0, res}; }
    static Action chop(int id) { return {CHOP, id}; }
    static Action pick(int id, const string &res) { return {PICK, id, 0, 0, res}; }
    static Action drop(int id) { return {DROP, id}; }
    static Action train(int player, int ms, int cc, int hp, int cp) { return {TRAIN, player, 0, 0, "", ms, cc, hp, cp}; }
    static Action mine(int id) { return {MINE, id}; }

    string toString() const
    {
        switch (type)
        {
        case MOVE:
            return "MOVE " + to_string(id) + " " + to_string(x) + " " + to_string(y);
        case HARVEST:
            return "HARVEST " + to_string(id);
        case PLANT:
            return "PLANT " + to_string(id) + " " + resource;
        case CHOP:
            return "CHOP " + to_string(id);
        case PICK:
            return "PICK " + to_string(id) + " " + resource;
        case DROP:
            return "DROP " + to_string(id);
        case TRAIN:
            return "TRAIN " + to_string(moveSpeed) + " " + to_string(carryCapacity) + " " + to_string(harvestPower) + " " + to_string(chopPower);
        case MINE:
            return "MINE " + to_string(id);
        }
        return "";
    }
};

// =====================================================
// GLOBAL HELPERS
// =====================================================

int manhattan(int x1, int y1, int x2, int y2)
{
    return abs(x1 - x2) + abs(y1 - y2);
}

void emitAction(vector<Action> &actions, Action action)
{
    cerr << "[ACTION] " << action.toString() << endl;
    actions.push_back(action);
}

// =====================================================
// LOOKUP TABLES
// =====================================================

constexpr int MAX_MAP_HEIGHT = 11;
constexpr int MAX_MAP_WIDTH = 2 * MAX_MAP_HEIGHT;

int bfs_dist_lookup[MAX_MAP_HEIGHT][MAX_MAP_WIDTH][MAX_MAP_HEIGHT][MAX_MAP_WIDTH];

static vector<Position> ironMines;

bool isCellWalkable(char c)
{
    return c == '.';
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
            if (!isCellWalkable(g[sy][sx]) && g[sy][sx] != '0' && g[sy][sx] != '2')
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
        else
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

        if (size < 1 || size > 4)
            return 0;
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

    TrollTask task = CHOPPERSCALER;

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

    void handleReturn(const Shack &shack,
                      vector<Action> &actions) const
    {
        if (manhattan(x, y, shack.x, shack.y) <= 1)
            emitAction(actions, Action::drop(id));
        else
            emitAction(actions, Action::move(id, shack.x, shack.y));
    }
};

// True when (src_x,src_y) and (dst_x,dst_y) are aligned (same row or same column) and
// any cell on the straight line between them — endpoints included — is
// occupied by an ally troll other than selfId.
bool allyOnStraightPath(int src_x, int src_y, int dst_x, int dst_y,
                        const vector<Troll> &trolls, int selfId)
{
    if (src_x != dst_x && src_y != dst_y)
        return false;

    int dx = (src_x == dst_x) ? 0 : (dst_x > src_x ? 1 : -1);
    int dy = (src_y == dst_y) ? 0 : (dst_y > src_y ? 1 : -1);

    int cx = src_x, cy = src_y;
    while (true)
    {
        for (const auto &t : trolls)
        {
            if (t.id == selfId)
                continue;
            if (t.x == cx && t.y == cy)
                return true;
        }

        cx += dx;
        cy += dy;
        if (cx == dst_x && cy == dst_y)
            break;
    }
    return false;
}

// Among cells at BFS distance == speed from (src_x,src_y), excluding cells occupied
// by another ally troll, return the one with the smallest BFS distance to
// (dst_x,dst_y). Returns {-1,-1} when no such cell exists.
Position pickMoveTarget(int src_x, int src_y, int dst_x, int dst_y, int speed,
                        const vector<Troll> &trolls, int selfId,
                        int w, int h)
{
    Position best;
    best.x = -1;
    best.y = -1;
    int bestDist = 1 << 30;

    // displayBfsDistsFrom(h, w, src_x, src_y);
    // displayBfsDistsFrom(h, w, dst_x, dst_y);

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            // Unreachable or too far from current position
            if (bfs_dist_lookup[src_y][src_x][y][x] < 1 || bfs_dist_lookup[src_y][src_x][y][x] > speed)
                continue;

            // Cell can't reach the destination if it's a shack
            // So we check the opposite
            int d = bfs_dist_lookup[dst_y][dst_x][y][x];
            if (d < 0)
                continue;

            // Skip cells occupied by an ally
            bool occupiedByAlly = false;
            for (const auto &t : trolls)
                if (t.id != selfId && t.x == x && t.y == y)
                {
                    occupiedByAlly = true;
                    break;
                }
            if (occupiedByAlly)
                continue;

            if (allyOnStraightPath(src_x, src_y, x, y, trolls, selfId))
                continue;

            if (d < bestDist)
            {
                bestDist = d;
                best.x = x;
                best.y = y;
            }
        }
    }

    return best;
}

bool generateBestTrollStats(
    const vector<Troll> &existing,
    const Shack &myShack,
    vector<Action> &actions)
{
    int n = (int)existing.size();

    int bestMs = 1;
    for (int ms = 3; ms >= 1; ms--)
    {
        int cost = n + ms * ms;
        if (myShack.plum >= cost)
        {
            bestMs = ms;
            break;
        }
    }

    int bestCc = 1;
    for (int cc = 3; cc >= 1; cc--)
    {
        int cost = n + cc * cc;
        if (myShack.lemon >= cost)
        {
            bestCc = cc;
            break;
        }
    }

    int bestHp = 1;
    for (int hp = 3; hp >= 1; hp--)
    {
        int cost = n + hp * hp;
        if (myShack.apple >= cost)
        {
            bestHp = hp;
            break;
        }
    }

    int bestCp = 0;
    for (int cp = 3; cp >= 1; cp--)
    {
        int cost = n + cp * cp;

        if (myShack.iron < cost)
            continue;

        bestCp = cp;
        break;
    }

    if (bestMs == 0 || bestCc == 0 || (bestHp == 0 && bestCp == 0))
        return false;

    emitAction(actions, Action::train(0, bestMs, bestCc, bestHp, bestCp));
    return true;
}

void assignTrollTasks(vector<Troll> &trolls)
{
    int bestScore = -1;
    int bestIdx = 0;

    for (int i = 0; i < (int)trolls.size(); i++)
    {
        const Troll &t = trolls[i];
        int score = t.chopPower * 2 + t.movementSpeed + t.carryCapacity;
        if (score > bestScore)
        {
            bestScore = score;
            bestIdx = i;
        }

        trolls[i].task = CHOPPERSCALER;
    }

    trolls[bestIdx].task = CHOPPERWARRIOR;
    cerr << "Troll " << trolls[bestIdx].id << " is the chopping warrior with score " << bestScore << endl;
}

// =====================================================
// STATE
// =====================================================

class State
{
public:
    int w = 0, h = 0;
    char grid[MAX_MAP_HEIGHT][MAX_MAP_WIDTH];
    Shack myShack;
    Shack enemyShack;
    vector<Tree> trees;
    vector<Troll> trolls;
    vector<Troll> enemyTrolls;

private:
    void generateMoveActions(const Troll &t, const vector<Troll> &allies, vector<Action> &actions) const
    {
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                int d = bfs_dist_lookup[t.y][t.x][y][x];
                if (d < 1 || d > t.movementSpeed)
                    continue;

                bool occupied = false;
                for (const auto &ally : allies)
                {
                    if (ally.id != t.id && ally.x == x && ally.y == y)
                    {
                        occupied = true;
                        break;
                    }
                }
                if (!occupied)
                    actions.push_back(Action::move(t.id, x, y));
            }
        }
    }

    void generateTrollActions(const Troll &t, const Shack &shack, const vector<Troll> &allies, vector<Action> &actions) const
    {
        // MOVE: Walkable cells in move speed range, with no ally on it
        generateMoveActions(t, allies, actions);

        // MINE: troll is on an iron mine
        for (const auto &mine : ironMines)
        {
            if (mine.x == t.x && mine.y == t.y)
            {
                if (t.chopPower > 0)
                    actions.push_back(Action::mine(t.id));
                break;
            }
        }

        // CHOP: on tree
        const Tree *onTree = nullptr;
        for (const auto &tr : trees)
        {
            if (tr.x == t.x && tr.y == t.y)
            {
                onTree = &tr;

                // No need for carrying capacity left: We could want to chop tree to deny it
                if (t.chopPower > 0)
                    actions.push_back(Action::chop(t.id));

                break;
            }
        }

        if (onTree)
        {
            // HARVEST: on tree with fruits and carry capacity available
            if (t.harvestPower > 0 && onTree->fruits > 0)
                actions.push_back(Action::harvest(t.id));
        }
        else
        {
            // PLANT: on grass with no tree under, for any fruit type carried
            if (t.carryPlum > 0)
                actions.push_back(Action::plant(t.id, "PLUM"));
            if (t.carryLemon > 0)
                actions.push_back(Action::plant(t.id, "LEMON"));
            if (t.carryApple > 0)
                actions.push_back(Action::plant(t.id, "APPLE"));
            if (t.carryBanana > 0)
                actions.push_back(Action::plant(t.id, "BANANA"));
        }

        if (manhattan(t.x, t.y, shack.x, shack.y) <= 1)
        {
            if (t.canCarry())
            {
                // PICK: when adjacent to own shack, carry capacity available and fruit is available in shack
                if (shack.plum > 0)
                    actions.push_back(Action::pick(t.id, "PLUM"));
                if (shack.lemon > 0)
                    actions.push_back(Action::pick(t.id, "LEMON"));
                if (shack.apple > 0)
                    actions.push_back(Action::pick(t.id, "APPLE"));
                if (shack.banana > 0)
                    actions.push_back(Action::pick(t.id, "BANANA"));
            }

            // DROP: adjacent to own shack, with something to drop
            if (t.isCarrying())
                actions.push_back(Action::drop(t.id));
        }
    }

public:
    vector<Action> generatePossibleActions(int player) const
    {
        const vector<Troll> &playerTrolls = (player == 0) ? trolls : enemyTrolls;
        const Shack &shack = (player == 0) ? myShack : enemyShack;

        vector<Action> actions;
        for (const Troll &t : playerTrolls)
            generateTrollActions(t, shack, playerTrolls, actions);

        // TRAIN: fixed stats (2, 2, 1, 2)
        int n = (int)playerTrolls.size();
        if (shack.plum >= n + 4 &&
            shack.lemon >= n + 4 &&
            shack.apple >= n + 1 &&
            shack.iron >= n + 4)
            actions.push_back(Action::train(player, 2, 2, 1, 2));

        return actions;
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
    }

private:
    Troll *findTrollById(int id)
    {
        for (auto &t : trolls)
            if (t.id == id)
                return &t;
        for (auto &t : enemyTrolls)
            if (t.id == id)
                return &t;
        return nullptr;
    }

    int findTreeIndex(int x, int y) const
    {
        for (int i = 0; i < (int)trees.size(); i++)
            if (trees[i].x == x && trees[i].y == y)
                return i;
        return -1;
    }

    void applyMove(const Action &a)
    {
        Troll *t = findTrollById(a.id);
        if (!t)
            return;

        const vector<Troll> &allies = (t->player == 0) ? trolls : enemyTrolls;
        for (const Troll &ally : allies)
            if (ally.id != t->id && ally.x == a.x && ally.y == a.y)
                return;

        t->x = a.x;
        t->y = a.y;
    }

    void applyPlant(const Action &a)
    {
        Troll *t = findTrollById(a.id);
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

        trees.push_back(tree);
    }

    void applyPick(const Action &a)
    {
        Troll *t = findTrollById(a.id);
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
        Troll *t = findTrollById(a.id);
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
        Troll *t = findTrollById(a.id);
        if (!t)
            return;
        int amount = min(t->chopPower, t->remainingCarry());
        if (amount > 0)
            t->carryIron += amount;
    }

    void applyTrain(const Action &a)
    {
        // Should check if shack has enough resources because a troll can PICK items from the shack in the same turn
        int player = a.id;
        vector<Troll> &teamTrolls = (player == 0) ? trolls : enemyTrolls;
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
        for (const auto &t : trolls)
            nextId = max(nextId, t.id);
        for (const auto &t : enemyTrolls)
            nextId = max(nextId, t.id);
        nextId++;

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
        teamTrolls.push_back(nt);
    }

    void applyHarvest(const vector<Action> &actions)
    {
        // Bucket harvest actions by the tree they target (same cell as the troll)
        vector<vector<int>> byTree(trees.size());
        for (const Action &a : actions)
        {
            if (a.type != Action::HARVEST)
                continue;
            Troll *t = findTrollById(a.id);
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
                budgets.push_back(findTrollById(id)->harvestPower);

            // Distribute one fruit per active troll per round until exhausted
            while (tree.fruits > 0)
            {
                // Active = trolls who still have harvest budget and carry capacity
                vector<int> active;
                for (int i = 0; i < (int)trollIds.size(); i++)
                {
                    Troll *t = findTrollById(trollIds[i]);
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
                        Troll *t = findTrollById(trollIds[i]);
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
                        Troll *t = findTrollById(trollIds[i]);
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
            Troll *t = findTrollById(a.id);
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
                totalDmg += findTrollById(id)->chopPower;
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
                budgets.push_back(findTrollById(id)->remainingCarry());

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
                        findTrollById(trollIds[i])->carryWood++;
                        budgets[i]--;
                    }
                    wood = 0;
                }
                else
                {
                    // Normal round: each active troll grabs one wood
                    for (int i : active)
                    {
                        findTrollById(trollIds[i])->carryWood++;
                        budgets[i]--;
                    }
                    wood -= (int)active.size();
                }
            }
        }

        // Remove killed trees (back to front to keep indices valid)
        for (int i = (int)trees.size() - 1; i >= 0; i--)
            if (killed[i])
                trees.erase(trees.begin() + i);
    }

    bool isNearWater(int x, int y) const
    {
        constexpr int dxs[4] = {1, -1, 0, 0};
        constexpr int dys[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++)
        {
            int nx = x + dxs[k];
            int ny = y + dys[k];
            if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                continue;
            if (grid[ny][nx] == '~')
                return true;
        }
        return false;
    }

    void updateTrees()
    {
        for (Tree &tree : trees)
            tree.grow(isNearWater(tree.x, tree.y));
    }
};

// =====================================================
// MAP HELPERS
// =====================================================

const Tree *bestEnemyTree(
    const vector<Troll> &trolls,
    const Troll &t,
    const Shack &enemyShack,
    const vector<Tree> &trees)
{
    const Tree *best = nullptr;
    int bestDist = 1e9;

    for (const auto &tr : trees)
    {
        // Verify an ally isn't chopping it already
        if (any_of(trolls.begin(), trolls.end(),
                   [&](const Troll &ally)
                   { return ally.x == tr.x && ally.y == tr.y && ally.id != t.id; }))
        {
            cerr << "Skipping tree at " << tr.x << " " << tr.y << " because an ally is already chopping it" << endl;
            continue;
        }

        int d = manhattan(enemyShack.x, enemyShack.y, tr.x, tr.y);
        if (d < bestDist)
        {
            bestDist = d;
            best = &tr;
        }
    }

    // cerr << "Closest tree of type " << type << " is at " << (best ? to_string(best->x) + " " + to_string(best->y) : "none") << endl;
    return best;
}

// =====================================================
// CHOPPER WARRIOR
// =====================================================

bool chopEnemyTrees(
    const vector<Troll> &trolls,
    const Troll &t,
    const Shack &myShack,
    const Shack &enemyShack,
    const vector<Tree> &trees,
    vector<Action> &actions)
{
    cerr << "chopEnemyTrees(): Troll " << t.id << "[" << t.task << "] should chop enemy tree with capacity of " << t.carried() << "/" << t.carryCapacity << endl;

    if (!t.canCarry())
    {
        t.handleReturn(myShack, actions);
        return true;
    }

    const Tree *tr = bestEnemyTree(trolls, t, enemyShack, trees);
    if (!tr)
        return false;

    if (t.x == tr->x && t.y == tr->y)
    {
        emitAction(actions, Action::chop(t.id));
    }
    else
    {
        emitAction(actions, Action::move(t.id, tr->x, tr->y));
    }

    return true;
}

// =====================================================
// CHOPPING SCALING
// =====================================================

string pickableFruit(const Shack &shack)
{
    if (shack.banana > 0)
        return "BANANA";
    if (shack.apple > 0)
        return "APPLE";
    if (shack.plum > 0)
        return "PLUM";
    if (shack.lemon > 0)
        return "LEMON";
    return "";
}

bool pickFruitFromShack(
    const Troll &t,
    const Shack &myShack,
    vector<Action> &actions)
{
    string fruitType = pickableFruit(myShack);
    if (fruitType.empty())
        return false;

    if (manhattan(t.x, t.y, myShack.x, myShack.y) <= 1)
        emitAction(actions, Action::pick(t.id, fruitType));
    else
        emitAction(actions, Action::move(t.id, myShack.x, myShack.y));
    return true;
}

bool isOnTree(const Troll &t, const vector<Tree> &trees)
{
    for (const auto &tr : trees)
        if (tr.x == t.x && tr.y == t.y)
            return true;
    return false;
}

// True if troll's current cell is a valid planting spot:
// within d=1..3 of myShack, not on either shack, and no tree already there.
bool isPlantable(const Troll &t, const Shack &myShack, const Shack &enemyShack, const vector<Tree> &trees)
{
    if (t.x == myShack.x && t.y == myShack.y)
        return false;
    if (t.x == enemyShack.x && t.y == enemyShack.y)
        return false;

    if (isOnTree(t, trees))
        return false;

    return true;
}

bool plant(
    const Troll &t,
    const vector<Tree> &trees,
    const Shack &shack,
    vector<Action> &actions)
{
    string type;

    if (t.carryPlum)
        type = "PLUM";
    else if (t.carryLemon)
        type = "LEMON";
    else if (t.carryApple)
        type = "APPLE";
    else if (t.carryBanana)
        type = "BANANA";
    else
        return false;

    emitAction(actions, Action::plant(t.id, type));
    return true;
}

// Returns the nearest free cell at manhattan distance 1..3 from shack,
// scanning outward so we plant as close as possible.
Position findPlantSpot(const Shack &shack, const vector<Tree> &trees)
{
    for (int d = 1; d <= 3; d++)
    {
        for (int dx = -d; dx <= d; dx++)
        {
            int dyAbs = d - abs(dx);
            for (int s = -1; s <= 1; s += 2)
            {
                int cx = shack.x + dx;
                int cy = shack.y + s * dyAbs;
                bool hasTree = false;
                for (const auto &tr : trees)
                    if (tr.x == cx && tr.y == cy)
                    {
                        hasTree = true;
                        break;
                    }
                if (!hasTree)
                    return {cx, cy};
                if (dyAbs == 0)
                    break;
            }
        }
    }
    Position none;
    none.x = -1;
    none.y = -1;
    return none;
}

void plantAndChopTrees(
    const vector<Troll> &trolls,
    const vector<Troll> &enemyTrolls,
    const Troll &t,
    const Shack &myShack,
    const Shack &enemyShack,
    const vector<Tree> &trees,
    vector<Action> &actions)
{
    cerr << "plantAndChopTrees(): Troll " << t.id << "[" << t.task << "] carry=" << t.carried() << "/" << t.carryCapacity << " | wood=" << t.carryWood << endl;

    // On a tree → chop it
    for (const auto &tr : trees)
        if (tr.x == t.x && tr.y == t.y)
        {
            emitAction(actions, Action::chop(t.id));
            return;
        }

    // Carry wood → return to shack immediately
    if (t.carryWood > 0)
    {
        t.handleReturn(myShack, actions);
        return;
    }

    bool carryingFruit = t.carryPlum > 0 || t.carryLemon > 0 ||
                         t.carryApple > 0 || t.carryBanana > 0;

    // Carry nothing → pick a fruit from shack
    if (!carryingFruit)
    {
        if (!pickFruitFromShack(t, myShack, actions))
            chopEnemyTrees(trolls, t, myShack, enemyShack, trees, actions);
        return;
    }

    // Carry fruit → plant here if valid, otherwise move to nearest plant spot
    if (isPlantable(t, myShack, enemyShack, trees))
        plant(t, trees, myShack, actions);
    else
    {
        Position target = findPlantSpot(myShack, trees);
        if (target.x != -1)
            emitAction(actions, Action::move(t.id, target.x, target.y));
        else
            chopEnemyTrees(trolls, t, myShack, enemyShack, trees, actions);
    }
}

// =====================================================
// PLAY TROLLS
// =====================================================

void playTrolls(
    vector<Troll> &trolls,
    const vector<Troll> &enemyTrolls,
    vector<Tree> &trees,
    Shack &myShack,
    Shack &enemyShack,
    vector<Action> &actions)
{
    for (int i = 0; i < (int)trolls.size(); i++)
    {
        Troll &t = trolls[i];

        if (t.task == CHOPPERWARRIOR)
            chopEnemyTrees(trolls, t, myShack, enemyShack, trees, actions);
        else
            plantAndChopTrees(trolls, enemyTrolls, t, myShack, enemyShack, trees, actions);
    }
}

// =====================================================
// ENGINE
// =====================================================

// =====================================================
// MCTS
// =====================================================

constexpr int MAX_NODES = 100000;

struct Node
{
    State state;
    vector<Action> actions;
    vector<Node *> children;
    int remainingUnexpandedChildren = 0;
    int visits = 0;
    float score = 0.0f;
};

Node nodePool[MAX_NODES];
int nodeCount = 0;

Node *allocNode()
{
    Node *n = &nodePool[nodeCount++];

    n->actions.clear();
    n->children.clear();
    n->remainingUnexpandedChildren = 0;
    n->visits = 0;
    n->score = 0.0f;

    return n;
}

void resetNodePool() { nodeCount = 0; }

// Placeholder heuristic: resource differential. Replace with a domain-specific
// evaluation when ready (tree counts, troll positioning, carried fruit, etc.).
float heuristic(const State &s)
{
    float myRes = s.myShack.plum + s.myShack.lemon + s.myShack.apple +
                  s.myShack.banana + s.myShack.iron + s.myShack.wood;
    float enRes = s.enemyShack.plum + s.enemyShack.lemon + s.enemyShack.apple +
                  s.enemyShack.banana + s.enemyShack.iron + s.enemyShack.wood;
    return myRes - enRes;
}

constexpr float UCT_C = 1.41421356f;

int selectUnexpandedChild(Node *node)
{
    // Pick uniformly among the remaining NULL slots.
    int pick = rand() % node->remainingUnexpandedChildren;

    for (int i = 0; i < (int)node->children.size(); i++)
    {
        if (node->children[i] != nullptr)
            continue;
        if (pick == 0)
            return i;
        pick--;
    }

    return -1;
}

// Pick the next child index: a random unexpanded slot if the node isn't
// fully expanded yet, otherwise the UCT-best child.
int selectChild(Node *node)
{
    int bestIdx = 0;
    float bestUct = -1e30f;
    float logN = logf((float)node->visits);
    for (int i = 0; i < (int)node->children.size(); i++)
    {
        Node *c = node->children[i];

        float exploit = c->score / (float)c->visits;
        float explore = UCT_C * sqrtf(logN / (float)c->visits);
        float uct = exploit + explore;

        if (uct > bestUct)
        {
            bestUct = uct;
            bestIdx = i;
        }
    }
    return bestIdx;
}

// Create a child node by applying the action at index `idx`. Decrements
// the parent's unexpanded counter.
Node *expand(Node *node, int idx)
{
    Node *child = allocNode();

    child->state = node->state;
    child->state.applyActions({node->actions[idx]});

    node->children[idx] = child;
    node->remainingUnexpandedChildren--;

    return child;
}

void finalizeExpansionOnFirstVisit(Node *node)
{
    // Generate actions and fulfill children vector with NULLs to mark them as unexpanded.
    node->actions = node->state.generatePossibleActions(0);
    node->children.assign(node->actions.size(), nullptr);
    node->remainingUnexpandedChildren = (int)node->actions.size();
}

float mcts(Node *node)
{
    // Leaf (visited once, no actions yet) -> generate actions and
    // fill children with NULLs so we can track which slots remain.
    if (node->actions.empty())
        finalizeExpansionOnFirstVisit(node);

    int childId;
    Node *childNode;
    float childValue;
    if (node->remainingUnexpandedChildren > 0)
    {
        childId = selectUnexpandedChild(node);
        childNode = expand(node, childId);
        childValue = heuristic(childNode->state);
    }
    else
    {
        childId = selectChild(node);
        childNode = node->children[childId];
        childValue = mcts(childNode);
    }

    node->visits++;
    node->score += childValue;

    return childValue;
}

Action runMCTS(const State &rootState)
{
    resetNodePool();
    Node *root = allocNode();
    root->state = rootState;

    auto start = chrono::steady_clock::now();
    int iters = 0;
    while (true)
    {
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                           chrono::steady_clock::now() - start)
                           .count();
        if (elapsed >= 45)
            break;
        if (nodeCount >= MAX_NODES - 64)
            break;

        mcts(root);
        iters++;
    }
    cerr << "[MCTS] iters=" << iters << " nodes=" << nodeCount << endl;

    // Pick the most-visited root child (robust child).
    int bestIdx = -1;
    int bestVisits = -1;
    for (int i = 0; i < (int)root->children.size(); i++)
    {
        Node *c = root->children[i];
        if (c && c->visits > bestVisits)
        {
            bestVisits = c->visits;
            bestIdx = i;
        }
    }

    if (bestIdx < 0 || root->actions.empty())
        return Action::move(0, 0, 0);

    return root->actions[bestIdx];
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

    state.trees.resize(n);
    for (auto &t : state.trees)
        cin >> t.type >> t.x >> t.y >> t.size >> t.health >> t.fruits >> t.cooldown;
}

void parseTrolls(State &state)
{
    int n;
    cin >> n;

    state.trolls.clear();
    state.enemyTrolls.clear();

    for (int i = 0; i < n; i++)
    {
        Troll t;

        cin >> t.id >> t.player >> t.x >> t.y >> t.movementSpeed >> t.carryCapacity >> t.harvestPower >> t.chopPower >> t.carryPlum >> t.carryLemon >> t.carryApple >> t.carryBanana >> t.carryIron >> t.carryWood;

        if (t.player == 0)
        {
            state.trolls.push_back(t);

            if (state.myShack.x == -1)
            {
                state.myShack.x = t.x;
                state.myShack.y = t.y;
            }
        }
        else
        {
            state.enemyTrolls.push_back(t);
            if (state.enemyShack.x == -1)
            {
                state.enemyShack.x = t.x;
                state.enemyShack.y = t.y;
            }
        }
    }
}

// =====================================================
// ACTION OUTPUT
// =====================================================

vector<Action> transformMoveActions(const vector<Action> &actions, const vector<Troll> &trolls, int w, int h)
{
    vector<Action> filtered;
    for (const Action &action : actions)
    {
        if (action.type != Action::MOVE)
        {
            filtered.push_back(action);
            continue;
        }

        int trollId = action.id;
        int tx = action.x, ty = action.y;

        const Troll *mover = nullptr;
        for (const auto &t : trolls)
            if (t.id == trollId)
            {
                mover = &t;
                break;
            }

        if (!mover)
        {
            cerr << "Error: no troll found with id " << trollId << endl;
            filtered.push_back(action);
            continue;
        }

        Position step = pickMoveTarget(mover->x, mover->y, tx, ty,
                                       mover->movementSpeed,
                                       trolls, trollId, w, h);

        if (step.x == -1)
        {
            cerr << "[MOVE REDIRECT] Troll " << trollId << " cannot move toward (" << tx << "," << ty << ") because all nearby cells are occupied, redirecting to original target" << endl;
            filtered.push_back(action);
            continue;
        }

        cerr << "[MOVE REWRITE] Troll " << trollId
             << " step (" << step.x << "," << step.y
             << ") toward (" << tx << "," << ty << ")" << endl;
        filtered.push_back(Action::move(trollId, step.x, step.y));
    }
    return filtered;
}

void displayActions(const vector<Action> &actions)
{
    for (int i = 0; i < (int)actions.size(); i++)
    {
        cout << actions[i].toString();
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

    State prevState;
    int turn = 0;

    while (true)
    {
        turn++;

        prevState = state;

        parseResources(state);
        parseTrees(state);
        parseTrolls(state);

        assignTrollTasks(state.trolls);

        vector<Action> actions;

        if (state.trolls.size() < 2)
            generateBestTrollStats(state.trolls, state.myShack, actions);

        playTrolls(state.trolls, state.enemyTrolls, state.trees, state.myShack, state.enemyShack, actions);

        vector<Action> filtered = transformMoveActions(actions, state.trolls, w, h);
        displayActions(filtered);
    }
}
