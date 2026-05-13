#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>
#include <queue>
#include <cstdlib>

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
    

    Algorithm :
        - Faire un MCTS classique avec une heuristic.
        - Faire un macro-MCTS en ajoutant des macro actions. Soit un séquence défini de k actions qui termine sur un état à t+k tours
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
    enum Type { MOVE, HARVEST, PLANT, CHOP, PICK, DROP, TRAIN, MINE };
    Type type;
    int id = 0;
    int x = 0, y = 0;
    string resource;
    int moveSpeed = 0, carryCapacity = 0, harvestPower = 0, chopPower = 0;

    static Action move(int id, int x, int y)                            { return {MOVE, id, x, y}; }
    static Action harvest(int id)                                       { return {HARVEST, id}; }
    static Action plant(int id, const string &res)                      { return {PLANT, id, 0, 0, res}; }
    static Action chop(int id)                                          { return {CHOP, id}; }
    static Action pick(int id, const string &res)                       { return {PICK, id, 0, 0, res}; }
    static Action drop(int id)                                          { return {DROP, id}; }
    static Action train(int ms, int cc, int hp, int cp)                 { return {TRAIN, 0, 0, 0, "", ms, cc, hp, cp}; }
    static Action mine(int id)                                          { return {MINE, id}; }

    string toString() const
    {
        switch (type)
        {
        case MOVE:    return "MOVE " + to_string(id) + " " + to_string(x) + " " + to_string(y);
        case HARVEST: return "HARVEST " + to_string(id);
        case PLANT:   return "PLANT " + to_string(id) + " " + resource;
        case CHOP:    return "CHOP " + to_string(id);
        case PICK:    return "PICK " + to_string(id) + " " + resource;
        case DROP:    return "DROP " + to_string(id);
        case TRAIN:   return "TRAIN " + to_string(moveSpeed) + " " + to_string(carryCapacity) + " " + to_string(harvestPower) + " " + to_string(chopPower);
        case MINE:    return "MINE " + to_string(id);
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

    emitAction(actions, Action::train(bestMs, bestCc, bestHp, bestCp));
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

struct State
{
    char grid[MAX_MAP_HEIGHT][MAX_MAP_WIDTH];
    Shack myShack;
    Shack enemyShack;
    vector<Tree> trees;
    vector<Troll> trolls;
    vector<Troll> enemyTrolls;
};

// =====================================================
// MAP HELPERS
// =====================================================

static vector<Position> ironMines;

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
