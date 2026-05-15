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

    int macroTurnCount = 0;

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
                    int d = bfs_dist_lookup[t.y][t.x][tr.y][tr.x];
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

        // MOVE_AND_CHOP: every reachable tree
        if (turn > 200 && t.chopPower > 0)
        {
            for (const auto &tr : trees)
            {
                int d = bfs_dist_lookup[t.y][t.x][tr.y][tr.x];
                if (d < 0)
                    continue;
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
                    if (dShack != 1)
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

    vector<ActionSet> generatePlayerActionSets(int player, const ActionSet &base = {}) const
    {
        const vector<Troll> &playerTrolls = (player == 0) ? trolls : enemyTrolls;
        const Shack &shack = (player == 0) ? myShack : enemyShack;

        // For each troll, either keep the unfinished macro inherited from `base`
        // or generate fresh candidates. Trolls busy on a macro are not branched on.
        vector<vector<Action>> perTroll;
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
                perTroll.push_back({*kept});
                continue;
            }

            vector<Action> trollActions = generateTrollMacroActions(t, shack, playerTrolls);
            if (trollActions.empty())
            {
                cerr << "No actions available for troll " << t.id << " at turn " << turn << endl;
                exit(0);
            }

            perTroll.push_back(move(trollActions));
        }

        // Cartesian product across trolls
        vector<vector<Action>> combos;
        combos.push_back({});
        for (const auto &trollActions : perTroll)
        {
            vector<vector<Action>> next;
            next.reserve(combos.size() * trollActions.size());
            for (const vector<Action> &existing : combos)
                for (const Action &a : trollActions)
                {
                    vector<Action> actions = existing;
                    actions.push_back(a);
                    next.push_back(move(actions));
                }
            combos = move(next);
        }

        // TODO: Remove impossible combinaisons of macro actions when t >= 2 ?
        // - CHOP the same tree twice
        // - PLANT on the same cell twice
        // - ?

        // If TRAIN is possible, append it to every combo (always emitted as PRIMITIVE)
        int n = (int)playerTrolls.size();
        bool canTrain = shack.plum >= n + 4 &&
                        shack.lemon >= n + 4 &&
                        shack.apple >= n + 4 &&
                        shack.iron >= n + 4;

        vector<ActionSet> result;
        result.reserve(combos.size());
        for (vector<Action> &actions : combos)
        {
            if (canTrain)
                actions.push_back(Action::train(player, 2, 2, 2, 2));

            ActionSet set;
            set.actions = move(actions);
            result.push_back(move(set));
        }

        return result;
    }

    // =====================================================
    // ACTION APPLICATION
    // =====================================================

    void applyMacroActions(ActionSet &set)
    {
        // int turnStart = turn;
        if (set.actions.size() == 0)
        {
            cerr << "applyMacroActions: Applying empty ActionSet !!!" << endl;
            exit(0);
        }

        // Primitives finish the turn they execute. Macros finish when their own
        // findNextPrimitiveAction sets macroTaskFinished.
        bool anyFinished = false;
        int safety = 0;
        while (!anyFinished)
        {
            if (++safety > 50)
            {
                cerr << "applyMacroActions stuck, turn=" << turn << " set=" << set.toString() << endl;
                exit(0);
            }

            vector<Action> primitiveActions;
            primitiveActions.reserve(set.actions.size());

            for (Action &a : set.actions)
            {
                if (a.macroTaskFinished)
                {
                    cerr << "Macro action " << a.toString() << " is already finished before using it !!! " << turn << endl;
                    exit(0);
                }

                primitiveActions.push_back(a.findNextPrimitiveAction(*this));
                if (a.macroTaskFinished)
                    anyFinished = true;
            }

            applyActions(primitiveActions);
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

    auto cellOccupied = [&](int cx, int cy) {
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
    macroTurnCount++;

    if (category == PRIMITIVE)
    {
        macroTaskFinished = true;
        return *this;
    }

    // Defensive cap: a macro that hasn't reached its target after MAX_MACRO_TURNS
    // is forced to finish. Prevents infinite loops if pathing gets permanently
    // blocked (e.g. mutually-blocking trolls beyond moveToward's heuristic).
    constexpr int MAX_MACRO_TURNS = 20;
    if (macroTurnCount >= MAX_MACRO_TURNS)
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

    // cerr << "Macro action " << toString() << " FINISHED in " << macroTurnCount << " turns" << endl;

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
    }

    return *this;
}

// =====================================================
// MCTS
// =====================================================

constexpr int MAX_NODES = 100000;

struct Node
{
    State state;
    // Unfinished macro actions inherited from parent's chosen ActionSet.
    // These are reused for busy trolls when generating this node's children.
    ActionSet base;
    vector<ActionSet> actionSets;
    vector<Node *> children;
    int remainingUnexpandedChildren = 0;
    int visits = 0;
    float score = 0.0f;
};

Node nodePool[MAX_NODES];
int nodeCount = 0;

Node *allocNode()
{
    if (nodeCount >= MAX_NODES - 1)
    {
        cerr << "Node pool exhausted!" << endl;
        exit(1);
    }

    Node *n = &nodePool[nodeCount++];

    n->base.actions.clear();
    n->actionSets.clear();
    n->children.clear();
    n->remainingUnexpandedChildren = 0;
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
    constexpr int CHOPPING_TURN = 100;
    constexpr int MAX_TURN = 300;

    // Compute game points
    float myRes = s.myShack.plum + s.myShack.lemon + s.myShack.apple +
                  s.myShack.banana + 4 * s.myShack.wood;
    float enRes = s.enemyShack.plum + s.enemyShack.lemon + s.enemyShack.apple +
                  s.enemyShack.banana + 4 * s.enemyShack.wood;

    // Set each troll as 50 game points (arbitrary, just to value having more trolls on the board)
    for (const auto &t : s.trolls)
        myRes += 50;
    for (const auto &t : s.enemyTrolls)
        enRes += 50;

    for (const auto &t : s.trolls)
    {
        int dist = bfs_dist_lookup[s.myShack.y][s.myShack.x][t.y][t.x];
        if (dist <= 0)
            dist = 20; // High value

        // Weight carried fruits depending on the distance with its shacks
        // 50% of shack value by default
        // And go to 99% when next to shack
        int ressourceValue = 0.5f * (t.carryPlum + t.carryLemon + t.carryApple + t.carryBanana + t.carryIron) + 2 * t.carryWood;
        myRes += ressourceValue + (ressourceValue / (1.1 * dist));
    }
    for (const auto &t : s.enemyTrolls)
    {
        int dist = bfs_dist_lookup[s.enemyShack.y][s.enemyShack.x][t.y][t.x];
        if (dist <= 0)
            dist = 20; // High value

        // Weight carried fruits depending on the distance with its shack
        // 50% of shack value by default
        // And go to 99% when next to shack
        int ressourceValue = 0.5f * (t.carryPlum + t.carryLemon + t.carryApple + t.carryBanana + t.carryIron) + 2 * t.carryWood;
        enRes += ressourceValue + (ressourceValue / (1.1 * dist));
    }

    // Score trees in [0,0.5] based on position relative to both shacks.
    // A tree closer to our shack is good for us (we can chop/harvest it faster), while a tree closer to enemy shack is bad for them (they can chop/harvest it faster).
    // We add the net value to myRes so the difference myRes-enRes reflects tree map control.
    for (const auto &tree : s.trees)
    {
        int dMy = bfs_dist_lookup[s.myShack.y][s.myShack.x][tree.y][tree.x];
        int dEn = bfs_dist_lookup[s.enemyShack.y][s.enemyShack.x][tree.y][tree.x];
        if (dMy <= 0 || dEn <= 0)
            continue;

        float treeScore = 2;
        if (s.isNearWater(tree.x, tree.y))
            treeScore *= 1.5;

        if (s.turn > CHOPPING_TURN)
            treeScore *= mapRange(s.turn, CHOPPING_TURN, MAX_TURN, 1.0f, 0);

        // Tree score only adds to myRes to encourage planting trees
        myRes += treeScore / dMy;
        enRes += treeScore / dEn;
    }

    return myRes - enRes;
}

constexpr float UCT_C = 1.5;
// constexpr float UCT_C = 1.41421356f;

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

    // Apply on a local copy so the parent's stored ActionSet is preserved while
    // applyMacroActions mutates per-action macroTaskFinished flags.
    ActionSet applied = node->actionSets[idx];

    // cerr << "[MCTS] Expanding child " << idx << " / " << node->actionSets.size() << " with ActionSet : " << applied.toString() << endl;

    child->state.applyMacroActions(applied);

    // Carry still-unfinished macros over to the child — those trolls remain busy.
    for (const Action &a : applied.actions)
        if (!a.macroTaskFinished)
            child->base.actions.push_back(a);

    node->children[idx] = child;
    node->remainingUnexpandedChildren--;

    return child;
}

void finalizeExpansionOnFirstVisit(Node *node)
{
    // Generate action sets and fulfill children vector with NULLs to mark them as unexpanded.
    node->actionSets = node->state.generatePlayerActionSets(0, node->base);
    node->children.assign(node->actionSets.size(), nullptr);
    node->remainingUnexpandedChildren = (int)node->actionSets.size();
}

float mcts(Node *node, int depth = 0)
{
    // cerr << "[MCTS] Visiting node at depth " << depth << " with " << node->visits << " visits and score " << node->score << endl;

    if (depth > 100)
    {
        cerr << "[MCTS] deep depth=" << depth << " nodes=" << nodeCount << endl;
        exit(0);
    }

    // Leaf (visited once, no actions yet) -> generate actions and
    // fill children with NULLs so we can track which slots remain.
    if (node->actionSets.empty())
        finalizeExpansionOnFirstVisit(node);

    int childId;
    Node *childNode;
    float childValue;
    if (node->remainingUnexpandedChildren > 0)
    {
        childId = selectUnexpandedChild(node);
        childNode = expand(node, childId);
        childValue = heuristic(childNode->state);

        childNode->visits++;
        childNode->score += childValue;
    }
    else
    {
        childId = selectChild(node);
        childNode = node->children[childId];
        childValue = mcts(childNode, depth + 1);
    }

    node->visits++;
    node->score += childValue;

    return childValue;
}

int getMostVisitedChild(Node *node, int depth = 0)
{
    int bestIdx = -1;
    int bestVisits = -1;
    for (int i = 0; i < (int)node->children.size(); i++)
    {
        Node *c = node->children[i];

        if (depth == 0)
        {
            vector<Action> &actions = node->actionSets[i].actions;

            cerr << "ActionSet " << i << " / " << node->children.size() << ": visits=" << (c->visits) << " quality=" << (c->score / c->visits) << " | Actions :";
            for (const auto &a : actions)
                cerr << " | " << a.toString();
            cerr << endl;
        }

        if (c->visits > bestVisits)
        {
            bestVisits = c->visits;
            bestIdx = i;
        }
    }
    return bestIdx;
}

void displayGoldPath(Node *node, int depth = 0)
{
    int bestIdx = getMostVisitedChild(node, 999);
    Node *childNode = node->children[bestIdx];
    cerr << "Gold path step " << depth << " | Actions: " << node->actionSets[bestIdx].toString() << " | Visits=" << childNode->visits << " score=" << childNode->score << endl;

    if (childNode->visits > 1)
        displayGoldPath(childNode, 999);
}

vector<Action> runMCTS(const State &rootState)
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
        if (elapsed >= 40)
            break;
        if (nodeCount >= MAX_NODES - 64)
            break;

        mcts(root);
        iters++;
    }

    cerr << "[MCTS] iters=" << iters << " nodes=" << nodeCount << endl;
    // displayGoldPath(root);

    int bestIdx = getMostVisitedChild(root);
    return root->actionSets[bestIdx].actions;
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
