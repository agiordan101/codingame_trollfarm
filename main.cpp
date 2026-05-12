#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <set>

/*
    Vide coding v3
    Train troll if enough turns left (Best config possible)
    Plant trees around shack
    MINE IRON SEULEMENT SI STOCK < 2 * TROLL COUNT
*/
/*
    Next steps :
        - Generate tasks based on ressources and needs

    Roles :
        Order de création de trolls :
            1 -> Harvester : (1, 1, 1, 1)
            2 -> Harvester : (1, 3, 3, 0) or (1, 2, 2, 0) or (1, 1, 1, 0) Instant !
            3 -> Chopper :   (2, 3, 0, 3)
            4 -> Chopper :   (2, 3, 0, 3)
            5 -> Harvester : (1, 3, 3, 0)
            6+ -> Alternate between Chopper and Harvester, starting with a third Chopper

    Créer les tasks en fonction des ressources manquante pour créer le prochain troll
    Assigner un Chopper au fer si on a besoin de fer.

    Assigner les trolls à l'iron selon leur stats chopPower

    Harvester:
        - Rentrer que si full ou qu'on vient de planter

    Chopper:
        - Rentrer après chaque tree
        - Prendre les arbres les plus proche du shack adverse, trier avec la distance à notre shak si égalité

    Plant:
        Si shack <= 3 ou (shack <= 6 && wetCell)

    Anytime :
        - Si un enemy chop un tree et que je peux l'atteindre avant qu'il ait fini -> Le faire (lorsqu'au moins un slot est vide)
*/

using namespace std;

const int MAX_TURNS = 300;

// =====================================================
// POSITION
// =====================================================

class Position {
public:
    int x = -1;
    int y = -1;
};

// =====================================================
// GLOBAL HELPERS
// =====================================================

int manhattan(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

void emitAction(vector<string>& actions, const string& action) {
    cerr << "[ACTION] " << action << endl;
    actions.push_back(action);
}

// =====================================================
// TREE
// =====================================================

class Tree {
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

class ShackResources {
public:
    int plum, lemon, apple, banana, iron, wood;
};

// =====================================================
// TROLL
// =====================================================

class Troll {
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

    int carried() const {
        return carryPlum + carryLemon + carryApple +
               carryBanana + carryIron + carryWood;
    }

    void handleReturn(const Position& shack,
                      vector<string>& actions) const
    {
        if (manhattan(x,y,shack.x,shack.y) == 1) {
            emitAction(actions, "DROP " + to_string(id));
        } else {
            emitAction(actions,
                "MOVE " + to_string(id) + " " +
                to_string(shack.x) + " " + to_string(shack.y)
            );
        }
    }
};

struct TrollStats {
    int movementSpeed;
    int carryCapacity;
    int harvestPower;
    int chopPower;
    bool valid;
};

TrollStats getBestTrollStats(
    const vector<Troll>& existing,
    const ShackResources& res)
{
    int n = (int)existing.size();

    int bestMs = 1;
    int bestCc = 1;
    int bestHp = 1;
    int bestCp = 0;

    // =========================
    // movementSpeed (max first)
    // =========================
    for (int ms = 5; ms >= 1; ms--) {
        int cost = n + ms * ms;
        if (res.plum >= cost) {
            bestMs = ms;
            break;
        }
    }

    // =========================
    // carryCapacity
    // =========================
    for (int cc = 5; cc >= 1; cc--) {
        int cost = n + cc * cc;
        if (res.lemon >= cost) {
            bestCc = cc;
            break;
        }
    }

    // =========================
    // harvestPower
    // =========================
    for (int hp = 5; hp >= 1; hp--) {
        int cost = n + hp * hp;
        if (res.apple >= cost) {
            bestHp = hp;
            break;
        }
    }

    // =========================
    // chopPower (iron-gated)
    // =========================
    for (int cp = 3; cp >= 0; cp--) {
        int cost = n + cp * cp;

        if (cp > 0 && res.iron < cost)
            continue;

        bestCp = cp;
        break;
    }

    TrollStats result;
    result.movementSpeed = bestMs;
    result.carryCapacity = bestCc;
    result.harvestPower  = bestHp;
    result.chopPower     = bestCp;
    result.valid = true;

    if (bestMs == 0 || bestCc == 0 || (bestHp == 0 && bestCp == 0))
    {
        result.valid = false;
    }

    return result;
}

// =====================================================
// IRON FINDER (UPDATED)
// =====================================================

Position closestIron(
    int x, int y,
    const vector<Position>& mines)
{
    int best = 1e9;
    Position res;

    for (const auto& m : mines) {
        int d = manhattan(x,y,m.x,m.y);
        if (d < best) {
            best = d;
            res = m;
        }
    }

    return res;
}

// =====================================================
// PARSING
// =====================================================

void parseMap(int w, int h,
              vector<string>& grid,
              vector<Position>& ironMines)
{
    grid.resize(h);

    for (int y = 0; y < h; y++) {
        getline(cin, grid[y]);

        for (int x = 0; x < w; x++) {
            if (grid[y][x] == '+') {
                Position p;
                p.x = x;
                p.y = y;
                ironMines.push_back(p);
            }
        }
    }
}

void parseResources(ShackResources& me, ShackResources& enemy) {
    cin >> me.plum >> me.lemon >> me.apple >> me.banana >> me.iron >> me.wood;
    cin >> enemy.plum >> enemy.lemon >> enemy.apple >> enemy.banana >> enemy.iron >> enemy.wood;
}

vector<Tree> parseTrees() {
    int n;
    cin >> n;

    vector<Tree> trees(n);

    for (auto& t : trees)
        cin >> t.type >> t.x >> t.y >> t.size >> t.health >> t.fruits >> t.cooldown;

    return trees;
}

vector<Troll> parseTrolls(Position& shack) {
    int n;
    cin >> n;

    vector<Troll> trolls;

    for (int i = 0; i < n; i++) {
        Troll t;

        cin >> t.id >> t.player >> t.x >> t.y
            >> t.movementSpeed >> t.carryCapacity
            >> t.harvestPower >> t.chopPower
            >> t.carryPlum >> t.carryLemon
            >> t.carryApple >> t.carryBanana
            >> t.carryIron >> t.carryWood;

        if (t.player == 0) {
            trolls.push_back(t);

            if (shack.x == -1) {
                shack.x = t.x;
                shack.y = t.y;
            }
        }
    }

    return trolls;
}

// =====================================================
// IRON LOGIC (UPDATED)
// =====================================================

bool handleIronMining(
    const Troll& t,
    const Position& shack,
    const vector<Position>& mines,
    vector<string>& actions)
{
    if (t.carryIron >= t.carryCapacity) {
        t.handleReturn(shack, actions);
        return true;
    }

    Position iron = closestIron(t.x, t.y, mines);

    if (iron.x == -1)
        return false;

    if (manhattan(t.x,t.y,iron.x,iron.y) == 1) {
        emitAction(actions, "MINE " + to_string(t.id));
    } else {
        emitAction(actions,
            "MOVE " + to_string(t.id) + " " +
            to_string(iron.x) + " " +
            to_string(iron.y)
        );
    }

    return true;
}

// =====================================================
// TREE LOGIC
// =====================================================

bool handleTrees(
    const Troll& t,
    const vector<Tree>& trees,
    vector<string>& actions)
{
    int best = -1;
    int bestDist = 1e9;

    for (int i = 0; i < trees.size(); i++) {
        if (trees[i].fruits == 0) continue;

        int d = manhattan(t.x,t.y,trees[i].x,trees[i].y);

        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }

    if (best == -1)
        return false;

    const Tree& tr = trees[best];

    if (t.x == tr.x && t.y == tr.y) {
        emitAction(actions, "HARVEST " + to_string(t.id));
    } else {
        emitAction(actions,
            "MOVE " + to_string(t.id) + " " +
            to_string(tr.x) + " " + to_string(tr.y)
        );
    }

    return true;
}

// =====================================================
// PLANT
// =====================================================

bool plant(
    Troll& t,
    vector<Tree>& trees,
    const Position& shack,
    vector<string>& actions)
{
    int d = manhattan(t.x,t.y,shack.x,shack.y);

    if (d == 0 || d > 3)
        return false;

    bool exists = false;
    for (auto& tr : trees)
        if (tr.x == t.x && tr.y == t.y)
            exists = true;

    if (exists)
        return false;

    string type;

    if (t.carryPlum) type = "PLUM";
    else if (t.carryLemon) type = "LEMON";
    else if (t.carryApple) type = "APPLE";
    else if (t.carryBanana) type = "BANANA";
    else
        return false;

    emitAction(actions,
        "PLANT " + to_string(t.id) + " " + type
    );

    return true;
}

// =====================================================
// PLAY TROLLS
// =====================================================

void playTrolls(
    vector<Troll>& trolls,
    vector<Tree>& trees,
    vector<Position>& mines,
    Position& shack,
    ShackResources& resources,
    vector<string>& actions)
{
    int bestMiner = -1;
    int bestPower = -1;

    for (int i = 0; i < trolls.size(); i++) {
        if (trolls[i].chopPower > bestPower) {
            bestPower = trolls[i].chopPower;
            bestMiner = i;
        }
    }

    bool allowMining = resources.iron < 2 * trolls.size();

    for (int i = 0; i < trolls.size(); i++) {

        Troll& t = trolls[i];

        if (plant(t,trees,shack,actions))
            continue;

        bool isMiner = (i == bestMiner);

        if (allowMining && isMiner) {
            if (handleIronMining(t, shack, mines, actions))
                continue;
        }

        if (t.carried() > 0) {
            t.handleReturn(shack, actions);
            continue;
        }

        if (handleTrees(t, trees, actions))
            continue;
    }
}

// =====================================================
// TRAIN
// =====================================================

void trainAction(
    const vector<Troll>& trolls,
    const ShackResources& me,
    int turn,
    vector<string>& actions)
{
    int n = trolls.size();

    TrollStats stats = getBestTrollStats(trolls, me);

    if (!stats.valid)
        return;

    int costPlum  = n + stats.movementSpeed * stats.movementSpeed;
    int costLemon = n + stats.carryCapacity * stats.carryCapacity;
    int costApple = n + stats.harvestPower * stats.harvestPower;
    int costIron  = n + stats.chopPower * stats.chopPower;

    int totalCost = costPlum + costLemon + costApple + costIron;

    bool canTrain =
        me.plum  >= costPlum &&
        me.lemon >= costLemon &&
        me.apple >= costApple &&
        me.iron  >= costIron;

    if (canTrain && totalCost * 6 < (MAX_TURNS - turn)) {

        string action =
            "TRAIN " +
            to_string(stats.movementSpeed) + " " +
            to_string(stats.carryCapacity) + " " +
            to_string(stats.harvestPower) + " " +
            to_string(stats.chopPower);

        emitAction(actions, action);
    }
}

// =====================================================
// MAIN
// =====================================================

int main() {

    int w,h;
    cin >> w >> h;
    cin.ignore();

    vector<string> grid;
    vector<Position> mines;

    parseMap(w,h,grid,mines);

    Position shack;
    shack.x = -1;

    int turn = 0;

    while (true) {

        turn++;

        ShackResources me, enemy;
        parseResources(me,enemy);

        vector<Tree> trees = parseTrees();
        vector<Troll> trolls = parseTrolls(shack);

        vector<string> actions;

        trainAction(trolls, me, turn, actions);

        playTrolls(trolls,trees,mines,shack,me,actions);

        for (int i = 0; i < actions.size(); i++) {
            cout << actions[i];
            if (i + 1 < actions.size()) cout << ";";
        }
        cout << endl;
    }
}
