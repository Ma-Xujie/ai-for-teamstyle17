#include "teamstyle17.h"
#include <iostream>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <math.h>

using namespace std;

#define NORM(a) sqrt(a.x * a.x + a.y * a.y + a.z * a.z)
#define DOT_PRODUCT(a, b) (a.x * b.x + a.y * b.y + a.z * b.z)
#define DISTANCE(a, b) sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z))
#define CAN_REACH(p, r) (p.x + r < kMapSize && p.x - r > 0 && p.y + r < kMapSize && p.y - r > 0 && p.z + r < kMapSize && p.z - r > 0)

const double PI = 3.141592653589793;
double ValueDecreaseRate[500];
double RadiusBonus[200];
int Power2[10];
int MyTeam;
bool EnemyHasLongAttack = false;
bool EnemyHasShield = false;
const Object *nearest_enemy = NULL;
double nearest_enemy_dis;
const Object *boss = NULL;
double nearest_boss_dis;
double advanced_energy_value = 2000.;

void Init() {
    RadiusBonus[0] = 0;
    for (int i = 0; i != 500; ++i) {  // 参数是随便写的
        if (i == 0 || i == 1) {
            ValueDecreaseRate[i] = 1;
        } else if (i == 2) {
            ValueDecreaseRate[i] = 0.9;
        } else {
            ValueDecreaseRate[i] = ValueDecreaseRate[i - 1] * 0.85;
        }
    }
    Power2[0] = 1;
    for (int i = 1; i != 10; ++i) {
        Power2[i] = Power2[i - 1] << 1;
    }
    MyTeam = GetStatus()->team_id;
}


inline double PLDistance(const Position &point, const Position &line_point_1, const Position &line_point_2) {
    double a, b, c, p, S;  // 三边长,半周长, 面积
    a = DISTANCE(point, line_point_1);
    b = DISTANCE(point, line_point_2);
    c = DISTANCE(line_point_1, line_point_2);
    if (c == 0) return double(0);
    p = (a + b + c) / 2;
    S = sqrt(p * (p - a) * (p - b) * (p - c));
    return 2 * S / c;
}

enum ActionType {
    MOVE,
    USE_SKILL,
    UPGRADE_SKILL
};

struct Action {
    Action(double val, ActionType act, int user_id) : value(val), action(act), user(user_id) {
    }
    double value;
    ActionType action;
    int user;
    int target;
    SkillType skill;
    Position des;
    const char *msg = "";

    void Do() {
        if (action == MOVE) {
            Move(user, des);
        } else if (action == USE_SKILL) {
            switch (skill) {
            case LONG_ATTACK:
                LongAttack(user, target);
                break;
            case SHORT_ATTACK:
                ShortAttack(user);
                break;
            case SHIELD:
                Shield(user);
                break;
            case DASH:
                Dash(user);
                break;
            case VISION_UP:
                break;
            case HEALTH_UP:
                HealthUp(user);
                break;
            case kSkillTypes:
                break;
            }
        } else if (action == UPGRADE_SKILL) {
            UpgradeSkill(user, skill);
        }
    }

    void Print(const Map *map, const PlayerObject *player) {
        printf("%4d HP %d/%d ", map->time, player->health, player->max_health);
        switch (action) {
        case MOVE:
            printf("Move(%f), pos=(%f,%f,%f), speed=(%f,%f,%f)", value, player->pos.x, player->pos.y, player->pos.z, des.x, des.y, des.z);
            break;
        case USE_SKILL:
            printf("Use Skill %d, level=%d", skill, player->skill_level[skill]);
            break;
        case UPGRADE_SKILL:
            printf("Upgrade Skill %d, abi=%d, cur_level=%d", skill, player->ability, player->skill_level[skill]);
            break;
        }
        printf("  %s\n", msg);
    }
};

vector<Action> actions;

bool CompareAction(const Action &a1, const Action &a2) {
    return a1.value > a2.value;
}

inline double ValueFunc(const Object *obj, const PlayerObject *player, int dist) {  // 参数是随便写的
    double decrease = ValueDecreaseRate[dist];
    if (obj->type == ENERGY) {
        return double(kFoodHealth) * decrease;
    } else if (obj->type == ADVANCED_ENERGY) {
        return advanced_energy_value * decrease;
    } else if (obj->type == DEVOUR) {
        return -4000.;
    } else if (obj->type == PLAYER) {
        if (obj->radius / player->radius < kEatableRatio) {
            return 8000. * player->radius / obj->radius * decrease;
        } else if (player->radius / obj->radius < kEatableRatio) {
            return -5000. * obj->radius / player->radius * decrease;
        } else {
            return 0;
        }
    } else if (obj->type == BOSS) {
        if (obj->radius / player->radius < kEatableRatio) {
            return 20000. * player->radius / obj->radius * decrease;
        } else if (player->radius / obj->radius < kEatableRatio && dist < obj->radius / 100 + 5) {
            return -5000. * obj->radius / player->radius * decrease;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
}

Position future_des[200];
Position tmp_obj;
double MoveValue(const Map *map, const PlayerObject *player, const Speed &speed) {
    int norm_speed = Norm(speed);
    double value = 0;
    int max_search_dist = player->vision / norm_speed;

    future_des[0] = player->pos;
    for (int i = 1; i <= max_search_dist; ++i) {
        future_des[i].x = future_des[i - 1].x + speed.x;
        future_des[i].y = future_des[i - 1].y + speed.y;
        future_des[i].z = future_des[i - 1].z + speed.z;
        if (!CAN_REACH(future_des[i], player->radius)) {
            max_search_dist = i - 1;
            break;
        }
    }

    // printf("max_search_dist = %d\n", max_search_dist);

    for (int i = 0; i != map->objects_number; ++i) {
        auto obj = map->objects + i;
        if (obj->type == ADVANCED_ENERGY) {
            tmp_obj.x = obj->pos.x - player->pos.x;
            tmp_obj.y = obj->pos.y - player->pos.y;
            tmp_obj.z = obj->pos.z - player->pos.z;
            if (DOT_PRODUCT(tmp_obj, speed) > 0 && PLDistance(obj->pos, player->pos, future_des[0]) < player->radius) {
                for (int j = 1; j <= max_search_dist; ++j) {
                    if (DISTANCE(obj->pos, future_des[j]) < player->radius) {
                        max_search_dist = j;
                        break;
                    }
                    if (j < 3 && boss && player->radius / boss->radius < kEatableRatio && DISTANCE(future_des[j], boss->pos) < boss->radius + 300) {
                        value -= 1000 * ValueDecreaseRate[j];
                    }
                }
            }
        }
    }

    for (int i = 0; i != map->objects_number; ++i) {
        auto obj = map->objects + i;
        tmp_obj.x = obj->pos.x - player->pos.x;
        tmp_obj.y = obj->pos.y - player->pos.y;
        tmp_obj.z = obj->pos.z - player->pos.z;
        if (DOT_PRODUCT(tmp_obj, speed) > 0 && PLDistance(obj->pos, player->pos, future_des[0]) < player->radius) {
            int j = (DISTANCE(player->pos, obj->pos) - player->radius) / norm_speed;
            if (j < 1) {
                j = 1;
            }
            while (j <= max_search_dist) {
                if (DISTANCE(obj->pos, future_des[j]) < player->radius + 4 * j) {
                    value += ValueFunc(obj, player, j);
                    break;
                }
                ++j;
            }
        }
    }
    return value;
}

int time_biases[10];
double time_bias;
int move_lasting_times[10];
int move_lasting_time;
int last_move_time;

int try_times = 1600;

void MonteCarloMove(const Map *map, const PlayerObject *player_now) {
    time_bias = time_biases[0] + time_biases[1] + time_biases[2];
    time_bias /= 3;
    // printf("time_bias = %f\n", time_bias);

    PlayerObject player_future = *player_now;
    PlayerObject *player = &player_future;

    player_future.pos.x += player_future.speed.x * time_bias;
    player_future.pos.y += player_future.speed.y * time_bias;
    player_future.pos.z += player_future.speed.z * time_bias;

    int cnt = 0;

    while (!CAN_REACH(player_future.pos, player_future.radius) && ++cnt < 5) {
        player_future.pos.x -= player_future.speed.x;
        player_future.pos.y -= player_future.speed.y;
        player_future.pos.z -= player_future.speed.z;
    }

    if (cnt == 5) {
        player->pos = player_now->pos;
    }

    double speed_val = 100.;
    if (player->dash_time > 0) {
        speed_val += kDashSpeed[player->skill_level[DASH]];
    }
    double max_value = 0;
    Speed best_speed = { double(rand() % 1000 - 500), double(rand() % 1000 - 500), double(rand() % 1000 - 500) };
    // printf("time = %d, obj = %d\n", map->time, map->objects_number);

    double theta;
    double phi;
    Position speed;
    Position des;
    double val = 0;
    for (int i = 0; i != try_times; ++i) {
        theta = double(rand() % 1000) / 500. * PI;  // 球面均匀分布
        phi = acos(double(rand() % 1000) / 500. - 1.);
        speed.x = speed_val * sin(theta) * cos(phi);
        speed.y = speed_val * sin(theta) * sin(phi);
        speed.z = speed_val * cos(theta);
        des.x = player->pos.x + speed.x;
        des.y = player->pos.y + speed.y;
        des.z = player->pos.z + speed.z;
        if (!CAN_REACH(des, player->radius)) {
            continue;
        }
        val = MoveValue(map, player, speed);
        if (val > max_value) {
            best_speed = speed;
            max_value = val;
            // printf("val=%f at %5d\n", max_value, i);
        }
    }
    actions.emplace_back(max_value, MOVE, player->id);
    actions.back().des = best_speed;

    auto t = GetTime();
    time_biases[2] = time_biases[1];
    time_biases[1] = time_biases[0];
    time_biases[0] = t - map->time;
    if (time_biases[0] == 0 && try_times < 2700) {
        try_times *= 1.1;
    } else if (time_biases[0] >= 2 && try_times > 1200) {
        try_times /= 1.1;
    }
    // printf("try_times = %d\n", try_times);
}

int SkillCost(const PlayerObject *player, SkillType skill) {
    int level = player->skill_level[skill];
    int ret = kBasicSkillPrice[skill];
    if (level == 5) return INF_;
    else if (false) {// skill == HEALTH_UP){
        // return ret * Power2[level];
    } else {
        if (level == 0) {
            int cnt = 0;
            for (int i = 0; i != kSkillTypes; ++i) {
                if (player->skill_level[i] > 0) {
                    ++cnt;
                }
            }
            return ret * Power2[cnt];
        } else {
            return ret * Power2[level];
        }
    }
}

bool CanUpgrade(const PlayerObject *player, SkillType skill) {
    return player->ability >= SkillCost(player, skill);
}

bool ShieldCheck(const PlayerObject *player) {
    return !EnemyHasLongAttack || player->skill_level[SHIELD] > 0;
}

bool HasAttackSkillCheck(const PlayerObject *player) {
    return (player->skill_level[LONG_ATTACK] > 0 || EnemyHasShield) && player->skill_level[SHORT_ATTACK] > 0;
}

bool UpgradeLongAttackCheck(const PlayerObject *player) {
    return !EnemyHasShield && player->skill_level[SHORT_ATTACK] >= 3 && player->skill_level[LONG_ATTACK] != 5;
}

void TryUpgradeSkill(const PlayerObject *player) {
    Action upgrade(0, UPGRADE_SKILL, player->id);
    if (CanUpgrade(player, HEALTH_UP)) {
        upgrade.value = 10000;
        upgrade.skill = HEALTH_UP;
        actions.push_back(upgrade);
    } else if (CanUpgrade(player, SHIELD)) {
        upgrade.value = 10000;
        upgrade.skill = SHIELD;
        actions.push_back(upgrade);
    } else if (player->skill_level[HEALTH_UP] == 5 && player->skill_level[SHIELD] == 5) {
        if (CanUpgrade(player, SHORT_ATTACK)
                   && (player->skill_level[SHORT_ATTACK] == 0 || HasAttackSkillCheck(player))
                   && !UpgradeLongAttackCheck(player)) {
            upgrade.value = 10000;
            upgrade.skill = SHORT_ATTACK;
            actions.push_back(upgrade);
        } else if (CanUpgrade(player, LONG_ATTACK) && !EnemyHasShield) {
            upgrade.value = 10000;
            upgrade.skill = LONG_ATTACK;
            actions.push_back(upgrade);
        } else if (player->skill_level[SHORT_ATTACK] == 5 && (player->skill_level[LONG_ATTACK] == 5 || EnemyHasShield) && CanUpgrade(player, DASH)) { // 现在不点 dash 了
            upgrade.value = 10000;
            upgrade.skill = DASH;
            actions.push_back(upgrade);
        }
    }
}

int last_use[kSkillTypes];

bool CanUse(const Map *map, const PlayerObject *player, SkillType skill) {
    return player->skill_cd[skill] == 0 && map->time - last_use[skill] > 10;
}

bool NoSpikeInRange(const Map *map, const Position &p1, const Position &p2, double radius) {
    Position d = Displacement(p1, p2);
    for (int i = 0; i != map->objects_number; ++i) {
        auto obj = &map->objects[i];
        if (obj->type == DEVOUR && PLDistance(obj->pos, p1, p2) < radius) {
            if (DotProduct(d, Displacement(p1, obj->pos)) > 0 && DotProduct(d, Displacement(p2, obj->pos)) < 0) {
                return false;
            }
        }
    }
    return true;
}

void FindNearestEnemy(const Map *map, const PlayerObject *player) {
    Action move_to_boss(0, MOVE, player->id);
    nearest_enemy = NULL;
    nearest_enemy_dis = 100000000;
    boss = NULL;
    for (int i = 0; i != map->objects_number; ++i) {
        auto obj = map->objects + i;
        if (obj->type == BOSS) {
            boss = obj;
            if (boss->radius / player->radius < kEatableRatio && !CanUse(map, player, DASH)
                && NoSpikeInRange(map, player->pos, boss->pos, player->radius)) {  // For FUN..
                move_to_boss.value = 200000;
                move_to_boss.msg = "Find BOSS!!!";
                move_to_boss.des = Displacement(player->pos, boss->pos);
                actions.push_back(move_to_boss);
                return;
            }
        }else if (obj->type == PLAYER && obj->team_id != MyTeam) {
            if (obj->long_attack_casting != -1) {
                EnemyHasLongAttack = true;
            }
            if (obj->shield_time > 0) {
                EnemyHasShield = true;
            }
            auto dis = DISTANCE(player->pos, obj->pos) - player->radius - obj->radius;
            if (dis < nearest_enemy_dis) {
                nearest_enemy = obj;
                nearest_enemy_dis = dis;
            }
        }
    }
}

void TryUseSkill(const Map *map, const PlayerObject *player) {
    Action use(0, USE_SKILL, player->id);
    if (CanUse(map, player, SHORT_ATTACK)
        && nearest_enemy_dis < kShortAttackRange[player->skill_level[SHORT_ATTACK]]
        && nearest_enemy->shield_time <= 0) {
        use.value = 100000;
        use.skill = SHORT_ATTACK;
        use.msg = "Attack ENEMY";
        last_use[SHORT_ATTACK] = map->time;
        actions.push_back(use);
    } else if (CanUse(map, player, LONG_ATTACK)
               && nearest_enemy_dis < kLongAttackRange[player->skill_level[LONG_ATTACK]]) {
        use.value = 100000;
        use.skill = LONG_ATTACK;
        use.target = nearest_enemy->id;
        use.msg = "Attack ENEMY";
        last_use[LONG_ATTACK] = map->time;
        actions.push_back(use);
    } else if (boss && player->radius > boss->radius
               && (nearest_enemy == NULL || DISTANCE(player->pos, boss->pos) < DISTANCE(boss->pos, nearest_enemy->pos))) {
        double boss_dis = DISTANCE(player->pos, boss->pos);
        if (CanUse(map, player, SHORT_ATTACK)
            && boss_dis - boss->radius - player->radius < kShortAttackRange[player->skill_level[SHORT_ATTACK]]) {
            use.value = 100000;
            use.skill = SHORT_ATTACK;
            use.msg = "Attack BOSS";
            last_use[SHORT_ATTACK] = map->time;
            actions.push_back(use);
        } else if (CanUse(map, player, LONG_ATTACK) &&
                   boss_dis < kLongAttackRange[player->skill_level[LONG_ATTACK]] - 500) {
            use.value = 100000;
            use.skill = LONG_ATTACK;
            use.target = boss->id;
            use.msg = "Attack BOSS";
            last_use[LONG_ATTACK] = map->time;
            actions.push_back(use);
        }
    } else if (CanUse(map, player, SHIELD) && ((nearest_enemy != NULL
               && (nearest_enemy->long_attack_casting != -1 || nearest_enemy_dis < kShortAttackRange[5]))
               || player->skill_level[SHIELD] == 5)) {
        use.value = 100000;
        use.skill = SHIELD;
        last_use[SHIELD] = map->time;
        actions.push_back(use);
    } else if (CanUse(map, player, DASH)) {
        use.value = 10000;
        use.skill = DASH;
        last_use[DASH] = map->time;
        actions.push_back(use);
    }
}

void CalcAdvancedEnergyValue(const PlayerObject *player){
    if (player->skill_level[SHORT_ATTACK] == 5){
        advanced_energy_value = 100.;
    }
}

void AIMain() {
    // Write Your AI Codes Here :-)
    Init();
    while (true) {
        actions.clear();
        const Map *map = GetMap();
        const PlayerObject *player = GetStatus()->objects;
        CalcAdvancedEnergyValue(player);
        FindNearestEnemy(map, player);
        TryUpgradeSkill(player);
        TryUseSkill(map, player);
        if (!actions.empty()) {
            sort(actions.begin(), actions.end(), CompareAction);
            actions.front().Do();
            actions.front().Print(map, player);
            while (map->time == GetTime());
        } else {
            actions.clear();
            MonteCarloMove(map, player);
            actions.front().Do();
            // actions.front().Print(map, player);
        }
    }
}
