#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>

/*
    Vide coding v5

    Game plan :
        1. Generate a second troll as soon as possible (first turn generally)
        2. Havest as needed to create a third troll, with attributes specifically to chop trees like a boss
        3. Then focus on chopping enemy trees, while keeping 1/2 trolls harvesting fruits

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

    Anytime :
        - Si un enemy chop un tree et que je peux l'atteindre avant qu'il ait fini -> Le faire (lorsqu'au moins un slot est vide)
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

    void handleReturn(const Position &shack,
                      vector<string> &actions) const
    {
        if (manhattan(x, y, shack.x, shack.y) == 1)
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

bool canTrainTroll(
    const vector<Troll> &existing,
    const ShackResources &res,
    const int movementSpeed,
    const int carryCapacity,
    const int harvestPower,
    const int chopPower)
{
    int n = (int)existing.size();

    if (res.plum >= n + movementSpeed * movementSpeed &&
        res.lemon >= n + carryCapacity * carryCapacity &&
        res.apple >= n + harvestPower * harvestPower &&
        res.iron >= n + chopPower * chopPower)
        return true;
    return false;
}

// =====================================================
// MAP HELPERS
// =====================================================

static vector<Position> ironMines;

Position closestIron(int x, int y)
{
    int best = 1e9;
    Position res;

    for (const auto &m : ironMines)
    {
        int d = manhattan(x, y, m.x, m.y);
        if (d < best)
        {
            best = d;
            res = m;
        }
    }

    return res;
}

const Tree *closestTreeByType(
    int x, int y,
    const vector<Tree> &trees,
    const string &type)
{
    const Tree *best = nullptr;
    int bestDist = 1e9;

    for (const auto &tr : trees)
    {
        // cerr << "Checking tree at " << tr.x << " " << tr.y << " of type " << tr.type << " with " << tr.fruits << " fruits" << endl;
        if (tr.type != type || tr.fruits == 0)
            continue;

        int d = manhattan(x, y, tr.x, tr.y);
        if (d < bestDist)
        {
            bestDist = d;
            best = &tr;
        }
    }

    // cerr << "Closest tree of type " << type << " is at " << (best ? to_string(best->x) + " " + to_string(best->y) : "none") << endl;
    return best;
}

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

vector<Troll> parseTrolls(Position &myShack, Position &enemyShack)
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
// IRON LOGIC (UPDATED)
// =====================================================

bool handleIronMining(
    const Troll &t,
    const Position &shack,
    vector<string> &actions)
{
    if (!t.canCarry())
    {
        t.handleReturn(shack, actions);
        return true;
    }

    Position iron = closestIron(t.x, t.y);

    if (iron.x == -1)
        return false;

    if (manhattan(t.x, t.y, iron.x, iron.y) == 1)
    {
        emitAction(actions, "MINE " + to_string(t.id));
    }
    else
    {
        cerr << "Troll " << t.id << " is moving to iron mine at " << iron.x << " " << iron.y << endl;
        emitAction(actions,
                   "MOVE " + to_string(t.id) + " " +
                       to_string(iron.x) + " " +
                       to_string(iron.y));
    }

    return true;
}

bool harvestFruit(
    const Troll &t,
    const Position &shack,
    const vector<Tree> &trees,
    const string fruitType,
    vector<string> &actions)
{
    cerr << "Troll " << t.id << " should harvest " << fruitType << " with capacity of " << t.carried() << "/" << t.carryCapacity << endl;

    if (!t.canCarry())
    {
        t.handleReturn(shack, actions);
        return true;
    }

    const Tree *tr = closestTreeByType(t.x, t.y, trees, fruitType);
    if (!tr)
        return false;

    if (t.x == tr->x && t.y == tr->y)
    {
        emitAction(actions, "HARVEST " + to_string(t.id));
    }
    else
    {
        cerr << "Troll " << t.id << " is moving to " << fruitType << " tree at " << tr->x << " " << tr->y << endl;
        emitAction(actions,
                   "MOVE " + to_string(t.id) + " " +
                       to_string(tr->x) + " " + to_string(tr->y));
    }

    return true;
}

bool chopEnemyTree(
    const vector<Troll> &trolls,
    const Troll &t,
    const Position &myShack,
    const Position &enemyShack,
    const vector<Tree> &trees,
    vector<string> &actions)
{
    cerr << "Troll " << t.id << " should chop enemy tree with capacity of " << t.carried() << "/" << t.carryCapacity << endl;

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
// TREE LOGIC
// =====================================================

bool handleTrees(
    const Troll &t,
    const vector<Tree> &trees,
    vector<string> &actions)
{
    int best = -1;
    int bestDist = 1e9;

    for (int i = 0; i < trees.size(); i++)
    {
        if (trees[i].fruits == 0)
            continue;

        int d = manhattan(t.x, t.y, trees[i].x, trees[i].y);

        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }

    if (best == -1)
        return false;

    const Tree &tr = trees[best];

    if (t.x == tr.x && t.y == tr.y)
    {
        emitAction(actions, "HARVEST " + to_string(t.id));
    }
    else
    {
        cerr << "Troll " << t.id << " is moving to tree at " << tr.x << " " << tr.y << endl;
        emitAction(actions,
                   "MOVE " + to_string(t.id) + " " +
                       to_string(tr.x) + " " + to_string(tr.y));
    }

    return true;
}

// =====================================================
// PLANT
// =====================================================

bool plant(
    Troll &t,
    vector<Tree> &trees,
    const Position &shack,
    vector<string> &actions)
{
    int d = manhattan(t.x, t.y, shack.x, shack.y);

    if (d == 0 || d > 3)
        return false;

    bool exists = false;
    for (auto &tr : trees)
        if (tr.x == t.x && tr.y == t.y)
            exists = true;

    if (exists)
        return false;

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

    emitAction(actions,
               "PLANT " + to_string(t.id) + " " + type);

    return true;
}

// =====================================================
// PLAY TROLLS
// =====================================================

void playTrolls(
    vector<Troll> &trolls,
    vector<Tree> &trees,
    Position &myShack,
    Position &enemyShack,
    ShackResources &myResources,
    vector<string> &actions)
{
    // Mine for a third troll
    bool shouldHarvestPlums = trolls.size() < 3 && myResources.plum < trolls.size() + 2 * 2;
    bool shouldHarvestLemon = trolls.size() < 3 && myResources.lemon < trolls.size() + 3 * 3;
    bool shouldHarvestApples = trolls.size() < 3 && myResources.apple < trolls.size() + 0 * 0;
    bool shouldChopIron = trolls.size() < 3 && myResources.iron < trolls.size() + 3 * 3;

    int bestCarryCapacity = 0;
    int bestHarvestPower = 0;

    int bestTrollForLemon = -1;
    if (shouldHarvestLemon)
    {
        for (int i = 0; i < trolls.size(); i++)
        {
            if (trolls[i].carryCapacity >= bestCarryCapacity && trolls[i].harvestPower > bestHarvestPower)
            {
                bestHarvestPower = trolls[i].harvestPower;
                bestTrollForLemon = trolls[i].id;
            }
        }

        cerr << "Best troll for lemon is " << bestTrollForLemon << " with harvest power " << bestHarvestPower << endl;
    }
    int bestTrollForIron = -1;
    if (shouldChopIron)
    {
        bestCarryCapacity = 0;
        bestHarvestPower = 0;
        for (int i = 0; i < trolls.size(); i++)
        {
            // cerr << "Troll " << trolls[i].id << " has chop power " << trolls[i].chopPower << " and carry capacity " << trolls[i].carryCapacity << endl;

            if (trolls[i].carryCapacity >= bestCarryCapacity && trolls[i].chopPower > bestHarvestPower)
            {
                bestHarvestPower = trolls[i].chopPower;
                bestTrollForIron = trolls[i].id;
            }
        }

        cerr << "Best troll for iron is " << bestTrollForIron << " with chop power " << bestHarvestPower << endl;
    }

    int bestTrollForPlums = -1;
    if (shouldHarvestPlums)
    {
        bestCarryCapacity = 0;
        bestHarvestPower = 0;
        for (int i = 0; i < trolls.size(); i++)
        {
            if (trolls[i].carryCapacity >= bestCarryCapacity && trolls[i].harvestPower > bestHarvestPower)
            {
                bestHarvestPower = trolls[i].harvestPower;
                bestTrollForPlums = trolls[i].id;
            }
        }

        cerr << "Best troll for plums is " << bestTrollForPlums << " with harvest power " << bestHarvestPower << endl;
    }

    int bestTrollForApples = -1;
    if (shouldHarvestApples)
    {
        bestCarryCapacity = 0;
        bestHarvestPower = 0;
        for (int i = 0; i < trolls.size(); i++)
        {
            if (trolls[i].carryCapacity >= bestCarryCapacity && trolls[i].harvestPower > bestHarvestPower)
            {
                bestHarvestPower = trolls[i].harvestPower;
                bestTrollForApples = trolls[i].id;
            }
        }

        cerr << "Best troll for apples is " << bestTrollForApples << " with harvest power " << bestHarvestPower << endl;
    }

    for (int i = 0; i < trolls.size(); i++)
    {
        Troll &t = trolls[i];

        if (t.id == bestTrollForLemon)
        {
            if (harvestFruit(t, myShack, trees, "LEMON", actions))
                continue;
        }
        else if (t.id == bestTrollForIron)
        {
            if (handleIronMining(t, myShack, actions))
                continue;
        }
        else if (t.id == bestTrollForPlums)
        {
            if (harvestFruit(t, myShack, trees, "PLUM", actions))
                continue;
        }
        else if (t.id == bestTrollForApples)
        {
            if (harvestFruit(t, myShack, trees, "APPLE", actions))
                continue;
        }

        if (plant(t, trees, myShack, actions))
            continue;

        if (t.isCarrying())
        {
            t.handleReturn(myShack, actions);
            continue;
        }

        if (trolls.size() >= 3 && t.chopPower >= t.harvestPower)
        {
            chopEnemyTree(trolls, t, myShack, enemyShack, trees, actions);
            continue;
        }

        if (handleTrees(t, trees, actions))
            continue;
    }
}

// =====================================================
// TRAIN
// =====================================================

void trainFirstTroll(
    const vector<Troll> &trolls,
    const ShackResources &me,
    vector<string> &actions)
{
    // Big Chopper
    if (canTrainTroll(trolls, me, 2, 3, 0, 3))
    {
        string action =
            "TRAIN " +
            to_string(2) + " " +
            to_string(3) + " " +
            to_string(0) + " " +
            to_string(3);

        emitAction(actions, action);
    }
    // Medium Chopper
    else if (canTrainTroll(trolls, me, 2, 2, 0, 2))
    {
        string action =
            "TRAIN " +
            to_string(2) + " " +
            to_string(2) + " " +
            to_string(0) + " " +
            to_string(2);

        emitAction(actions, action);
    }
    // Big Harvester
    else if (canTrainTroll(trolls, me, 1, 3, 3, 0))
    {
        string action =
            "TRAIN " +
            to_string(1) + " " +
            to_string(3) + " " +
            to_string(3) + " " +
            to_string(0);

        emitAction(actions, action);
    }
    // Medium Harvester
    else if (canTrainTroll(trolls, me, 1, 2, 2, 0))
    {
        string action =
            "TRAIN " +
            to_string(1) + " " +
            to_string(2) + " " +
            to_string(2) + " " +
            to_string(0);

        emitAction(actions, action);
    }
    // Small Harvester
    else if (canTrainTroll(trolls, me, 1, 1, 1, 0))
    {
        string action =
            "TRAIN " +
            to_string(1) + " " +
            to_string(1) + " " +
            to_string(1) + " " +
            to_string(0);

        emitAction(actions, action);
    }
}

void trainAction(
    const vector<Troll> &trolls,
    const ShackResources &me,
    int turn,
    vector<string> &actions)
{
    int n = trolls.size();
    if (n >= 3)
        return;

    // Big Chopper
    if (canTrainTroll(trolls, me, 2, 3, 0, 3))
    {
        string action =
            "TRAIN " +
            to_string(2) + " " +
            to_string(3) + " " +
            to_string(0) + " " +
            to_string(3);

        emitAction(actions, action);
    }
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
        vector<Troll> trolls = parseTrolls(myShack, enemyShack);

        vector<string> actions;

        if (turn == 1)
            trainFirstTroll(trolls, myResources, actions);
        else
            trainAction(trolls, myResources, turn, actions);

        playTrolls(trolls, trees, myShack, enemyShack, myResources, actions);

        for (int i = 0; i < actions.size(); i++)
        {
            cout << actions[i];
            if (i + 1 < actions.size())
                cout << ";";
        }
        cout << endl;
    }
}
