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

        - Tester un HMCTS-OP avec le graph MAXQ suivant :

    Engine :
        C'est pas grave que l'engine ne simule pas exactement le comportement du jeu.
        Certains comportements de l'adversaire sont trop aléatoires pour que simuler le jeu précisément par rapport à ses actions n'apporte pas grand chose,
            mis à part de la complexité et du temps perdu.
        Exemple : Pas besoin d'ancitiper que planter 2 arbre différents en même temps sur la même case ne résulte en rien.
        Par contre : Simuler que 2 trolls coupent le même arbre en même temps et la répartition du bois est important

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

class Action
{
public:
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
    int trollid = 0;
    int playerid = 0;
    int x = 0, y = 0;
    string resource;
    int moveSpeed = 0, carryCapacity = 0, harvestPower = 0, chopPower = 0;

    static Action move(int trollid, int playerid, int x, int y) { return Action(MOVE, trollid, playerid, x, y); }
    static Action harvest(int trollid, int playerid) { return Action(HARVEST, trollid, playerid); }
    static Action plant(int trollid, int playerid, const string &res) { return Action(PLANT, trollid, playerid, 0, 0, res); }
    static Action chop(int trollid, int playerid) { return Action(CHOP, trollid, playerid); }
    static Action pick(int trollid, int playerid, const string &res) { return Action(PICK, trollid, playerid, 0, 0, res); }
    static Action drop(int trollid, int playerid) { return Action(DROP, trollid, playerid); }
    static Action train(int playerid, int ms, int cc, int hp, int cp) { return Action(TRAIN, 0, playerid, 0, 0, "", ms, cc, hp, cp); }
    static Action mine(int trollid, int playerid) { return Action(MINE, trollid, playerid); }

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
        }
        return "";
    }

private:
    Action(Type t, int trollid = 0, int playerid = 0, int x = 0, int y = 0, string res = "", int ms = 0, int cc = 0, int hp = 0, int cp = 0)
        : type(t), trollid(trollid), playerid(playerid), x(x), y(y), resource(res), moveSpeed(ms), carryCapacity(cc), harvestPower(hp), chopPower(cp) {}
};

// =====================================================
// ACTION SET
// =====================================================

class ActionSet
{
public:
    int playerid = 0;
    vector<Action> actions;
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
            emitAction(actions, Action::drop(id, player));
        else
            emitAction(actions, Action::move(id, player, shack.x, shack.y));
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
    // actions[i] corresponds to playerTrolls[i] (TRAIN not included here)
    bool isValidActionSet(const ActionSet &as, const vector<Troll> &playerTrolls) const
    {
        int n = (int)as.actions.size();

        // Collect MOVE destinations; reject duplicate destinations (rule 1)
        set<pair<int, int>> moveDests;
        for (int i = 0; i < n; i++)
        {
            const Action &a = as.actions[i];
            if (a.type != Action::MOVE)
                continue;
            if (!moveDests.insert({a.x, a.y}).second)
                return false;
        }

        // Reject MOVE into a cell occupied by a non-moving ally (rule 2)
        for (int i = 0; i < n; i++)
        {
            const Action &a = as.actions[i];
            if (a.type != Action::MOVE)
                continue;
            for (int j = 0; j < n; j++)
            {
                if (j == i || as.actions[j].type == Action::MOVE)
                    continue;
                if (playerTrolls[j].x == a.x && playerTrolls[j].y == a.y)
                    return false;
            }
        }

        return true;
    }

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
                    actions.push_back(Action::move(t.id, t.player, x, y));
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
                    actions.push_back(Action::mine(t.id, t.player));
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
                    actions.push_back(Action::chop(t.id, t.player));

                break;
            }
        }

        if (onTree)
        {
            // HARVEST: on tree with fruits and carry capacity available
            if (onTree->fruits > 0 && t.harvestPower > 0 && t.canCarry())
                actions.push_back(Action::harvest(t.id, t.player));
        }
        else
        {
            // PLANT: on grass with no tree under, for any fruit type carried
            if (t.carryPlum > 0)
                actions.push_back(Action::plant(t.id, t.player, "PLUM"));
            if (t.carryLemon > 0)
                actions.push_back(Action::plant(t.id, t.player, "LEMON"));
            if (t.carryApple > 0)
                actions.push_back(Action::plant(t.id, t.player, "APPLE"));
            if (t.carryBanana > 0)
                actions.push_back(Action::plant(t.id, t.player, "BANANA"));
        }

        if (manhattan(t.x, t.y, shack.x, shack.y) <= 1)
        {
            if (t.canCarry())
            {
                // PICK: when adjacent to own shack, carry capacity available and fruit is available in shack
                if (shack.plum > 0)
                    actions.push_back(Action::pick(t.id, t.player, "PLUM"));
                if (shack.lemon > 0)
                    actions.push_back(Action::pick(t.id, t.player, "LEMON"));
                if (shack.apple > 0)
                    actions.push_back(Action::pick(t.id, t.player, "APPLE"));
                if (shack.banana > 0)
                    actions.push_back(Action::pick(t.id, t.player, "BANANA"));
            }

            // DROP: adjacent to own shack, with something to drop
            if (t.isCarrying())
                actions.push_back(Action::drop(t.id, t.player));
        }
    }

public:
    vector<ActionSet> generatePlayerActionSets(int player) const
    {
        const vector<Troll> &playerTrolls = (player == 0) ? trolls : enemyTrolls;
        const Shack &shack = (player == 0) ? myShack : enemyShack;

        // Collect per-troll action lists
        vector<vector<Action>> perTroll;
        for (const Troll &t : playerTrolls)
        {
            vector<Action> trollActions;
            generateTrollActions(t, shack, playerTrolls, trollActions);
            perTroll.push_back(move(trollActions));
        }

        // Cartesian product across trolls
        vector<ActionSet> result;
        result.push_back(ActionSet{player, {}});
        for (const auto &trollActions : perTroll)
        {
            vector<ActionSet> next;
            next.reserve(result.size() * trollActions.size());
            for (const ActionSet &existing : result)
                for (const Action &a : trollActions)
                {
                    ActionSet ns = existing;
                    ns.actions.push_back(a);
                    next.push_back(move(ns));
                }
            result = move(next);
        }

        // Filter impossible combinations
        result.erase(
            remove_if(result.begin(), result.end(),
                      [&](const ActionSet &as)
                      { return !isValidActionSet(as, playerTrolls); }),
            result.end());

        // If TRAIN is possible, append it to every ActionSet
        int n = (int)playerTrolls.size();
        if (shack.plum >= n + 4 &&
            shack.lemon >= n + 4 &&
            shack.apple >= n + 4 &&
            shack.iron >= n + 4)
        {
            Action trainAction = Action::train(player, 2, 2, 2, 2);
            for (ActionSet &as : result)
                as.actions.push_back(trainAction);
        }

        return result;
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

        trees.push_back(tree);
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
// MAP HELPERS
// =====================================================

// =====================================================
// MCTS
// =====================================================

constexpr int MAX_NODES = 100000;

struct Node
{
    State state;
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
    Node *n = &nodePool[nodeCount++];

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
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
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
    child->state.applyActions(node->actionSets[idx].actions);

    node->children[idx] = child;
    node->remainingUnexpandedChildren--;

    return child;
}

void finalizeExpansionOnFirstVisit(Node *node)
{
    // Generate action sets and fulfill children vector with NULLs to mark them as unexpanded.
    node->actionSets = node->state.generatePlayerActionSets(0);
    node->children.assign(node->actionSets.size(), nullptr);
    node->remainingUnexpandedChildren = (int)node->actionSets.size();
}

float mcts(Node *node)
{
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
        childValue = mcts(childNode);

        node->visits++;
        node->score += childValue;
    }

    return childValue;
}

ActionSet runMCTS(const State &rootState)
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
        ActionSet &as = root->actionSets[i];

        cerr << "ActionSet " << i << ": visits=" << (c->visits) << " quality=" << (c->score / c->visits) << " | Actions :";
        for (const auto &a : as.actions)
            cerr << " | " << a.toString();
        cerr << endl;

        if (c->visits > bestVisits)
        {
            bestVisits = c->visits;
            bestIdx = i;
        }
    }

    if (bestIdx < 0 || root->actionSets.empty())
        return ActionSet{};

    return root->actionSets[bestIdx];
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

        ActionSet actionSet = runMCTS(state);

        displayActions(actionSet.actions);
    }
}
