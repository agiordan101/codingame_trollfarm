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

/*
    Create chop until the end macro move
    Facteur d 'agressivité en fonction de la distance avec le shack adverse

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

            MacroAction :
                - Une macro action est une séquence d'actions primitives implicite.
                - Exemple : Troll X MOVE à un tree, pour ensuite HARVEST jusqu'a ce qu'il soit plein ou que l'arbre n'ait plus de fruits
                - C'est un nombre de tours défini

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



        - Tester un HMCTS-OP avec le graph MAXQ suivant :

    Engine :
        C'est pas grave que l'engine ne simule pas exactement le comportement du jeu.
        Certains comportements de l'adversaire sont trop aléatoires pour que simuler le jeu précisément par rapport à ses actions n'apporte pas grand chose,
            mis à part de la complexité et du temps perdu.
        Exemple : Pas besoin d'ancitiper que planter 2 arbre différents en même temps sur la même case ne résulte en rien.
        Par contre : Simuler que 2 trolls coupent le même arbre en même temps et la répartition du bois est important

        L'ordre du gold path est trop souvent les actions :
            Gold path step 1 | Actions: MOVE_AND_PLANT 0 4 3 LEMON;  | Visits=111 score=-4696.24
            Gold path step 2 | Actions: MOVE_AND_HARVEST 0 5 1;  | Visits=5 score=-206.503
            Gold path step 3 | Actions: MOVE_AND_CHOP 0 14 8;  | Visits=1 score=-42.793

        Est ce que forcer des primitive action en step 1 est vraiment utile ?
        Forcer des primitive action en step 1 empêche de réutiliser l'arbre entre les turns. Parce que la node 1 change tous le temps même si l'action 2 est toujours la meme macro action.


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

class State
{
public:
    int turn = 1;
    int w = 0, h = 0;
    char grid[MAX_MAP_HEIGHT][MAX_MAP_WIDTH];
    Shack myShack;
    Shack enemyShack;
    vector<Tree> trees;
    vector<Troll> trolls;
    vector<Troll> enemyTrolls;

private:
    vector<Action> generateTrollMacroActions(const Troll &t, const Shack &shack, const vector<Troll> &allies) const
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
            int N = (int)allies.size() * 2;
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

        // MOVE_AND_PLANT: walkable, tree-free cells at d<=2 from ally shack, for each fruit carried
        if (t.carryPlum > 0 || t.carryLemon > 0 || t.carryApple > 0 || t.carryBanana > 0)
        {
            // Can be precomputed
            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    if (!isCellWalkable(grid[y][x]))
                        continue;
                    int dShack = bfs_dist_lookup[shack.y][shack.x][y][x];
                    if (dShack > 2 || dShack < 0)
                        continue;

                    if (findTreeIndex(x, y) >= 0)
                        continue;

                    if (t.carryPlum > 0)
                        actions.push_back(Action::moveAndPlant(t.id, t.player, x, y, "PLUM"));
                    if (t.carryLemon > 0)
                        actions.push_back(Action::moveAndPlant(t.id, t.player, x, y, "LEMON"));
                    if (t.carryApple > 0)
                        actions.push_back(Action::moveAndPlant(t.id, t.player, x, y, "APPLE"));
                    if (t.carryBanana > 0)
                        actions.push_back(Action::moveAndPlant(t.id, t.player, x, y, "BANANA"));
                }
            }
        }

        // MOVE_AND_PICK / MOVE_AND_DROP: target is the closest walkable cell adjacent
        // to the shack (shack cell itself is not walkable).
        bool canPick = !t.isCarrying() && (shack.plum > 0 || shack.lemon > 0 || shack.apple > 0 || shack.banana > 0);
        bool canDrop = t.isCarrying();
        if (canPick || canDrop)
        {
            // Can be precomputed
            // Find the closest walkable cell adjacent to the shack, to use as MOVE destination for both PICK and DROP macro actions
            constexpr int dxs[4] = {1, -1, 0, 0};
            constexpr int dys[4] = {0, 0, 1, -1};
            int shackAdjX = -1, shackAdjY = -1, shackAdjDist = -1;
            for (int k = 0; k < 4; k++)
            {
                int nx = shack.x + dxs[k], ny = shack.y + dys[k];
                if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                    continue;
                if (!isCellWalkable(grid[ny][nx]))
                    continue;
                int d = bfs_dist_lookup[t.y][t.x][ny][nx];
                if (d >= 0 && (shackAdjDist < 0 || d < shackAdjDist))
                {
                    shackAdjDist = d;
                    shackAdjX = nx;
                    shackAdjY = ny;
                }
            }

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

        // MOVE_AND_MINE_ONCE: closest reachable walkable cell adjacent to closest mine
        if (turn < 200 && t.chopPower > 0 && t.canCarry())
        {
            // Can be precomputed
            constexpr int dxs[4] = {1, -1, 0, 0};
            constexpr int dys[4] = {0, 0, 1, -1};
            int bestDist = -1;
            int bestX = -1, bestY = -1;
            for (const auto &mine : ironMines)
            {
                for (int k = 0; k < 4; k++)
                {
                    int nx = mine.x + dxs[k];
                    int ny = mine.y + dys[k];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
                        continue;
                    if (!isCellWalkable(grid[ny][nx]))
                        continue;
                    int d = bfs_dist_lookup[t.y][t.x][ny][nx];
                    if (d < 0)
                        continue;
                    if (bestDist == -1 || d < bestDist)
                    {
                        bestDist = d;
                        bestX = nx;
                        bestY = ny;
                    }
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
        const vector<Troll> &playerTrolls = (player == 0) ? trolls : enemyTrolls;
        const Shack &shack = (player == 0) ? myShack : enemyShack;

        outPerTroll.clear();
        outPerTroll.reserve(playerTrolls.size());

        for (const Troll &t : playerTrolls)
        {
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

        // Per-action turn count: macros = 1 + ceil(bfs/ms), primitives = 1.
        // T = number of game turns to advance before at least one action finishes.
        vector<int> turns;
        turns.reserve(set.actions.size());
        for (Action &a : set.actions)
        {
            if (a.macroTaskFinished)
            {
                cerr << "Macro action " << a.toString() << " is already finished before using it !!! " << turn << endl;
                exit(0);
            }
            turns.push_back(a.macroTurnCount(*this));
        }
        int T = *min_element(turns.begin(), turns.end());

        // Turn 1: real moves via findNextPrimitiveAction (BFS-based moveToward).
        vector<Action> primitiveActions;
        primitiveActions.reserve(set.actions.size());
        for (Action &a : set.actions)
            primitiveActions.push_back(a.findNextPrimitiveAction(*this));
        applyActions(primitiveActions);

        // If any action finished on turn 1, we're done.
        for (const Action &a : set.actions)
            if (a.macroTaskFinished)
                return;

        // Turn 2..T-1: just grow trees and advance turn.
        for (int i = 0; i < T - 2; i++)
        {
            updateTrees();
            turn++;
        }

        // Turn T: teleport every macro troll to its target; finishing macros execute
        // their terminal primitive. Non-finishing macros stay in `set` and will
        // finish in 1 turn next time applyMacroActions is called (troll already at target).
        vector<Action> finalPrimitives;
        for (int i = 0; i < (int)set.actions.size(); i++)
        {
            Action &a = set.actions[i];

            // Teleport trolls
            Troll *t = findTrollById(a.trollid);
            if (t)
            {
                t->x = a.x;
                t->y = a.y;
            }

            // Execute terminal primitive for finished macro actions
            if (turns[i] == T)
            {
                a.macroTaskFinished = true;
                finalPrimitives.push_back(a.terminalPrimitive());
            }
        }
        applyActions(finalPrimitives);
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
        Troll *t = findTrollById(a.trollid);
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
        Troll *t = findTrollById(a.trollid);
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

        trees.push_back(move(tree));
    }

    void applyPick(const Action &a)
    {
        Troll *t = findTrollById(a.trollid);
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
        Troll *t = findTrollById(a.trollid);
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
        Troll *t = findTrollById(a.trollid);
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
        teamTrolls.push_back(move(nt));
    }

    void applyHarvest(const vector<Action> &actions)
    {
        // Bucket harvest actions by the tree they target (same cell as the troll)
        vector<vector<int>> byTree(trees.size());
        for (const Action &a : actions)
        {
            if (a.type != Action::HARVEST)
                continue;
            Troll *t = findTrollById(a.trollid);
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
            Troll *t = findTrollById(a.trollid);
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
    for (const Troll &t : s.trolls)
        if (t.id == trollid)
            return &t;
    return nullptr;
}

// Returns Action::move toward (targetX, targetY) picking the reachable cell
// within t.movementSpeed that minimises remaining BFS distance to the target.
// Skips ally-occupied cells. If no improving free cell exists, the troll stays
// put — picking a worsening cell would cause A→E→A oscillation because the
// BFS would just pull the troll back next turn. Repeated stalls are bounded by
// MAX_MACRO_TURNS in findNextPrimitiveAction, so MCTS sees the bad outcome.
Action Action::moveToward(const State &s, const Troll &t, int targetX, int targetY) const
{
    const vector<Troll> &allies = (t.player == 0) ? s.trolls : s.enemyTrolls;

    auto cellOccupied = [&](int cx, int cy)
    {
        for (const Troll &ally : allies)
            if (ally.id != t.id && ally.x == cx && ally.y == cy)
                return true;
        return false;
    };

    int bestX = t.x, bestY = t.y;
    int bestDist = bfs_dist_lookup[t.y][t.x][targetY][targetX];

    for (int cy = 0; cy < s.h; cy++)
        for (int cx = 0; cx < s.w; cx++)
        {
            int stepDist = bfs_dist_lookup[t.y][t.x][cy][cx];
            if (stepDist < 1 || stepDist > t.movementSpeed)
                continue;
            int remDist = bfs_dist_lookup[cy][cx][targetY][targetX];
            if (remDist < 0)
                continue;
            if (cellOccupied(cx, cy))
                continue;
            if (bestDist < 0 || remDist < bestDist)
            {
                bestDist = remDist;
                bestX = cx;
                bestY = cy;
            }
        }

    return Action::move(trollid, playerid, bestX, bestY);
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
    // is forced to finish. Prevents infinite loops if pathing gets permanently
    // blocked (e.g. mutually-blocking trolls beyond moveToward's heuristic).
    constexpr int MAX_MACRO_TURNS = 20;
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

    if (t->x == x && t->y == y)
        return 1;

    int d = bfs_dist_lookup[t->y][t->x][y][x];
    if (d < 0)
        return 1;

    int moveTurns = (d + t->movementSpeed - 1) / t->movementSpeed;
    return moveTurns + 1;
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
    constexpr int TRAIN_PHASE_START_DECAY = 150;
    constexpr int TRAIN_PHASE_END = 200;
    constexpr int CHOPPING_PHASE_START = 200;
    constexpr int TREE_PHASE_START_DECAY = 200;
    constexpr int MAX_TURN = 300;

    float myGamePts = s.myShack.plum + s.myShack.lemon + s.myShack.apple +
                      s.myShack.banana + 4 * s.myShack.wood;
    float enGamePts = s.enemyShack.plum + s.enemyShack.lemon + s.enemyShack.apple +
                      s.enemyShack.banana + 4 * s.enemyShack.wood;

    // Compute game points
    float myRes = myGamePts;
    float enRes = enGamePts;

    float chopPhase = 0;
    if (s.turn >= CHOPPING_PHASE_START)
        chopPhase = mapRange(s.turn, CHOPPING_PHASE_START, MAX_TURN, 0.0f, 1.0f);

    for (const auto &t : s.trolls)
    {
        // Having a troll is always good
        myRes += 50;

        float ressourceValue = 0.25f * (t.carryPlum + t.carryLemon + t.carryApple + t.carryBanana + t.carryIron) +
                               (1.0f + 2.0f * chopPhase) * t.carryWood;
        myRes += ressourceValue;
    }
    for (const auto &t : s.enemyTrolls)
    {
        // Having a troll is always good
        enRes += 50;

        float ressourceValue = 0.25f * (t.carryPlum + t.carryLemon + t.carryApple + t.carryBanana + t.carryIron) +
                               (1.0f + 2.0f * chopPhase) * t.carryWood;
        enRes += ressourceValue;
    }

    // Encourage balanced gathering of plum/lemon/apple/iron toward next troll training.
    // Cost = (trollCount + 4) of each. Bonus scales linearly up to 25 when fully ready.
    if (s.turn < TRAIN_PHASE_END)
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

        float trainPhaseWeight = 1;
        if (TRAIN_PHASE_START_DECAY < s.turn && s.turn < TRAIN_PHASE_END)
            trainPhaseWeight = mapRange(s.turn, TRAIN_PHASE_START_DECAY, TRAIN_PHASE_END, 1.0f, 0.0f);

        myRes += 40.0f * myTrainReady * trainPhaseWeight;
        enRes += 40.0f * enTrainReady * trainPhaseWeight;
    }

    // Each type of fruit produce a bonus if at least one tree is 3-cells near the shack
    // Banana is ignored because it's not used for training. It should be chopped for wood
    constexpr int NUM_FRUITS = 3;
    const string fruitTypes[NUM_FRUITS] = {"PLUM", "LEMON", "APPLE"};
    float bestMy[NUM_FRUITS] = {0.0f, 0.0f, 0.0f};
    float bestEn[NUM_FRUITS] = {0.0f, 0.0f, 0.0f};

    for (const auto &tree : s.trees)
    {
        int idx = -1;
        for (int i = 0; i < NUM_FRUITS; i++)
            if (tree.type == fruitTypes[i])
            {
                idx = i;
                break;
            }
        if (idx < 0)
            continue;

        int dMy = bfs_dist_lookup[s.myShack.y][s.myShack.x][tree.y][tree.x];
        int dEn = bfs_dist_lookup[s.enemyShack.y][s.enemyShack.x][tree.y][tree.x];
        if (dMy <= 0 || dEn <= 0)
            continue;

        float treeScore = 40;
        if (s.isNearWater(tree.x, tree.y))
            treeScore *= 1.5f;

        float treePhaseWeight = 1;
        if (TRAIN_PHASE_START_DECAY < s.turn && s.turn < TRAIN_PHASE_END)
            treePhaseWeight = mapRange(s.turn, TRAIN_PHASE_START_DECAY, TRAIN_PHASE_END, 1.0f, 0.0f);

        if (treeScore / (float)dMy > bestMy[idx])
            bestMy[idx] = treePhaseWeight * treeScore / (float)dMy;
        if (treeScore / (float)dEn > bestEn[idx])
            bestEn[idx] = treePhaseWeight * treeScore / (float)dEn;
    }

    for (int i = 0; i < NUM_FRUITS; i++)
    {
        myRes += bestMy[i];
        enRes += bestEn[i];
    }

    // Enemy score reliability decays with simulation depth: we don't simulate
    // opponent moves, so the further ahead we look, the less accurate enRes is.
    // int turnsElapsed = max(0, s.turn - g_rootTurn);
    // enRes += turnsElapsed * 0.1;

    constexpr float SCALE = 500.0f;
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
            cerr << "Troll " << t << " bandit:";
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

    State prevState;

    int turn = 0;
    while (true)
    {
        prevState = state;

        parseResources(state);
        parseTrees(state);
        parseTrolls(state);
        state.turn = turn;

        vector<Action> actions = runMCTS(state);

        displayActions(actions, state);
        turn++;
    }
}
