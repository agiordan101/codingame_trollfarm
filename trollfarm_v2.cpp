#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>

/*
    Game plan :
        1. Generate a second troll first turn
        2. Choose one as the choppingWarrior and the other asthe  choppingScaler
        3. Behave as follows :
            Both :
                - If an enemy is chopping a tree and I can reach it before he finishes, do it (when at least one slot is empty)
            choppingWarrior :
                - Chop the closest tree to the enemy shack
                - Return to my shack when carry capacity is full
            choppingScaler :
                - Pick any fruit in my shack
                - If an enemy can steal the wood before I finish harvest it :
                - Go away from it
                - Else plant it and harvest it
                - Return to my shack immediately after chopping
                -

    Création de 3 trolls :
        1 -> Harvester : (1, 1, 1, 1)
        2 -> Harvester : (1, 3, 3, 0) or (1, 2, 2, 0) or (1, 1, 1, 0) Instant !
        3 -> Chopper :   (2, 3, 0, 3)

    Generate tasks based on ressources and needs for 3 trolls
*/
/*
    Notes :

    Chopper:
        - Rentrer après chaque tree
        - Prendre les arbres les plus proche du shack adverse, trier avec la distance à notre shak si égalité

    Plant:
        Si shack <= 3 ou (shack <= 6 && wetCell)
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
// GLOBAL HELPERS
// =====================================================

int manhattan(int x1, int y1, int x2, int y2)
{
    return abs(x1 - x2) + abs(y1 - y2);
}

void emitAction(vector<string> &actions, const string &action)
{
    cerr << "[ACTION] " << action << endl;
    actions.push_back(action);
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
// SHACK RESOURCES
// =====================================================

class ShackResources
{
public:
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

    void handleReturn(const Position &shack,
                      vector<string> &actions) const
    {
        if (manhattan(x, y, shack.x, shack.y) <= 1)
        {
            emitAction(actions, "DROP " + to_string(id));
        }
        else
        {
            emitAction(actions,
                       "MOVE " + to_string(id) + " " +
                           to_string(shack.x) + " " + to_string(shack.y));
        }
    }
};

// Returns the 1 or 2 cells adjacent to (sx,sy) that begin a path toward (tx,ty).
vector<Position> nextStepsToward(int sx, int sy, int tx, int ty)
{
    vector<Position> steps;
    if (tx != sx) { Position p; p.x = sx + (tx > sx ? 1 : -1); p.y = sy;                      steps.push_back(p); }
    if (ty != sy) { Position p; p.x = sx;                       p.y = sy + (ty > sy ? 1 : -1); steps.push_back(p); }
    return steps;
}

bool isWalkable(int x, int y, const vector<string> &grid)
{
    if (y < 0 || y >= (int)grid.size() || x < 0 || x >= (int)grid[y].size())
        return false;
    char c = grid[y][x];
    return c == '.' || c == '+' || c == '0' || c == '1';
}

bool isOccupied(const Position &p,
                const vector<Troll> &trolls,
                int selfId)
{
    for (const auto &t : trolls)
        if (t.id != selfId && t.x == p.x && t.y == p.y) return true;
    return false;
}

// Returns a redirect cell when any direct step is blocked or unwalkable.
// Tries free primary steps first, then the remaining adjacent cells as fallback.
// Returns {-1,-1} when no redirect is needed or all neighbours are unavailable.
Position findMoveRedirect(int sx, int sy, int tx, int ty,
                          const vector<Troll> &trolls,
                          int selfId,
                          const vector<string> &grid)
{
    auto primary = nextStepsToward(sx, sy, tx, ty);

    auto blocked = [&](const Position &p) {
        return !isWalkable(p.x, p.y, grid) || isOccupied(p, trolls, selfId);
    };

    bool anyBlocked = false;
    for (const auto &p : primary)
        if (blocked(p)) { anyBlocked = true; break; }

    if (!anyBlocked) { Position none; none.x = -1; none.y = -1; return none; }

    // Prefer a free primary step
    for (const auto &p : primary)
        if (!blocked(p)) return p;

    // All primary steps blocked – try remaining adjacent cells as fallback
    set<pair<int, int>> primarySet;
    for (const auto &p : primary) primarySet.insert({p.x, p.y});

    Position adj[4] = {{sx + 1, sy}, {sx - 1, sy}, {sx, sy + 1}, {sx, sy - 1}};
    for (const auto &p : adj)
    {
        if (primarySet.count({p.x, p.y})) continue;
        if (!blocked(p)) return p;
    }

    Position none; none.x = -1; none.y = -1; return none;
}

bool generateBestTrollStats(
    const vector<Troll> &existing,
    const ShackResources &res,
    vector<string> &actions)
{
    int n = (int)existing.size();

    int bestMs = 1;
    for (int ms = 3; ms >= 1; ms--)
    {
        int cost = n + ms * ms;
        if (res.plum >= cost)
        {
            bestMs = ms;
            break;
        }
    }

    int bestCc = 1;
    for (int cc = 3; cc >= 1; cc--)
    {
        int cost = n + cc * cc;
        if (res.lemon >= cost)
        {
            bestCc = cc;
            break;
        }
    }

    int bestHp = 1;
    for (int hp = 3; hp >= 1; hp--)
    {
        int cost = n + hp * hp;
        if (res.apple >= cost)
        {
            bestHp = hp;
            break;
        }
    }

    int bestCp = 0;
    for (int cp = 3; cp >= 1; cp--)
    {
        int cost = n + cp * cp;

        if (res.iron < cost)
            continue;

        bestCp = cp;
        break;
    }

    if (bestMs == 0 || bestCc == 0 || (bestHp == 0 && bestCp == 0))
        return false;

    string action =
        "TRAIN " +
        to_string(bestMs) + " " +
        to_string(bestCc) + " " +
        to_string(bestHp) + " " +
        to_string(bestCp);

    emitAction(actions, action);
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
// MAP HELPERS
// =====================================================

static vector<Position> ironMines;

const Tree *bestEnemyTree(
    const vector<Troll> &trolls,
    const Troll &t,
    Position enemyShack,
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
    const Position &myShack,
    const Position &enemyShack,
    const vector<Tree> &trees,
    vector<string> &actions)
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
        emitAction(actions, "CHOP " + to_string(t.id));
    }
    else
    {
        emitAction(actions,
                   "MOVE " + to_string(t.id) + " " +
                       to_string(tr->x) + " " + to_string(tr->y));
    }

    return true;
}

// =====================================================
// CHOPPING SCALING
// =====================================================

string pickableFruit(const ShackResources &res)
{
    if (res.banana > 0)
        return "BANANA";
    if (res.apple > 0)
        return "APPLE";
    if (res.plum > 0)
        return "PLUM";
    if (res.lemon > 0)
        return "LEMON";
    return "";
}

bool pickFruitFromShack(
    const Troll &t,
    const Position &myShack,
    const ShackResources &myResources,
    vector<string> &actions)
{
    string fruitType = pickableFruit(myResources);
    if (fruitType.empty())
        return false;

    if (manhattan(t.x, t.y, myShack.x, myShack.y) <= 1)
        emitAction(actions, "PICK " + to_string(t.id) + " " + fruitType);
    else
        emitAction(actions, "MOVE " + to_string(t.id) + " " +
                                to_string(myShack.x) + " " + to_string(myShack.y));
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
bool isPlantable(const Troll &t, const Position &myShack, const Position &enemyShack, const vector<Tree> &trees)
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
    const Position &shack,
    vector<string> &actions)
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

    emitAction(actions, "PLANT " + to_string(t.id) + " " + type);
    return true;
}

// Returns the nearest free cell at manhattan distance 1..3 from shack,
// scanning outward so we plant as close as possible.
Position findPlantSpot(const Position &shack, const vector<Tree> &trees)
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
    const Position &myShack,
    const Position &enemyShack,
    const vector<Tree> &trees,
    const ShackResources &myResources,
    vector<string> &actions)
{
    cerr << "plantAndChopTrees(): Troll " << t.id << "[" << t.task << "] carry=" << t.carried() << "/" << t.carryCapacity << " | wood=" << t.carryWood << endl;

    // On a tree → chop it
    for (const auto &tr : trees)
        if (tr.x == t.x && tr.y == t.y)
        {
            emitAction(actions, "CHOP " + to_string(t.id));
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
        if (!pickFruitFromShack(t, myShack, myResources, actions))
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
            emitAction(actions, "MOVE " + to_string(t.id) + " " +
                                    to_string(target.x) + " " + to_string(target.y));
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
    Position &myShack,
    Position &enemyShack,
    ShackResources &myResources,
    vector<string> &actions)
{
    for (int i = 0; i < (int)trolls.size(); i++)
    {
        Troll &t = trolls[i];

        if (t.task == CHOPPERWARRIOR)
            chopEnemyTrees(trolls, t, myShack, enemyShack, trees, actions);
        else
            plantAndChopTrees(trolls, enemyTrolls, t, myShack, enemyShack, trees, myResources, actions);
    }
}

// =====================================================
// PARSING
// =====================================================

void parseMap(int w, int h, vector<string> &grid)
{
    grid.resize(h);

    for (int y = 0; y < h; y++)
    {
        getline(cin, grid[y]);

        for (int x = 0; x < w; x++)
        {
            if (grid[y][x] == '+')
            {
                Position p;
                p.x = x;
                p.y = y;
                ironMines.push_back(p);
            }
        }
    }
}

void parseResources(ShackResources &me, ShackResources &enemy)
{
    cin >> me.plum >> me.lemon >> me.apple >> me.banana >> me.iron >> me.wood;
    cin >> enemy.plum >> enemy.lemon >> enemy.apple >> enemy.banana >> enemy.iron >> enemy.wood;
}

vector<Tree> parseTrees()
{
    int n;
    cin >> n;

    vector<Tree> trees(n);

    for (auto &t : trees)
        cin >> t.type >> t.x >> t.y >> t.size >> t.health >> t.fruits >> t.cooldown;

    return trees;
}

vector<Troll> parseTrolls(Position &myShack, Position &enemyShack, vector<Troll> &enemyTrolls)
{
    int n;
    cin >> n;

    vector<Troll> trolls;

    for (int i = 0; i < n; i++)
    {
        Troll t;

        cin >> t.id >> t.player >> t.x >> t.y >> t.movementSpeed >> t.carryCapacity >> t.harvestPower >> t.chopPower >> t.carryPlum >> t.carryLemon >> t.carryApple >> t.carryBanana >> t.carryIron >> t.carryWood;

        if (t.player == 0)
        {
            trolls.push_back(t);

            if (myShack.x == -1)
            {
                myShack.x = t.x;
                myShack.y = t.y;
            }
        }
        else
        {
            enemyTrolls.push_back(t);
            if (enemyShack.x == -1)
            {
                enemyShack.x = t.x;
                enemyShack.y = t.y;
            }
        }
    }

    return trolls;
}

// =====================================================
// MAIN
// =====================================================

int main()
{

    int w, h;
    cin >> w >> h;
    cin.ignore();

    vector<string> grid;

    parseMap(w, h, grid);

    Position myShack;
    Position enemyShack;

    myShack.x = -1;
    enemyShack.x = -1;

    int turn = 0;

    while (true)
    {
        turn++;

        ShackResources myResources, enemyResources;
        parseResources(myResources, enemyResources);

        vector<Tree> trees = parseTrees();
        vector<Troll> enemyTrolls;
        vector<Troll> trolls = parseTrolls(myShack, enemyShack, enemyTrolls);

        assignTrollTasks(trolls);

        vector<string> actions;

        if (trolls.size() < 2)
            generateBestTrollStats(trolls, myResources, actions);

        playTrolls(trolls, enemyTrolls, trees, myShack, enemyShack, myResources, actions);

        vector<string> filtered;
        for (const string &action : actions)
        {
            if (action.substr(0, 4) == "MOVE")
            {
                int trollId = stoi(action.substr(5));
                int tx, ty;
                sscanf(action.c_str(), "MOVE %*d %d %d", &tx, &ty);

                const Troll *mover = nullptr;
                for (const auto &t : trolls)
                    if (t.id == trollId) { mover = &t; break; }

                if (mover)
                {
                    Position redirect = findMoveRedirect(mover->x, mover->y, tx, ty, trolls, trollId, grid);
                    if (redirect.x != -1)
                    {
                        cerr << "[MOVE REDIRECT] Troll " << trollId << " redirected to " << redirect.x << " " << redirect.y << endl;
                        filtered.push_back("MOVE " + to_string(trollId) + " " + to_string(redirect.x) + " " + to_string(redirect.y));
                        continue;
                    }
                }
            }
            filtered.push_back(action);
        }

        for (int i = 0; i < (int)filtered.size(); i++)
        {
            cout << filtered[i];
            if (i + 1 < (int)filtered.size())
                cout << ";";
        }
        cout << endl;
    }
}
