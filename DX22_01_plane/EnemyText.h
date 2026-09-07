#pragma once
#pragma execution_character_set("utf-8")
#include <string>

inline const char* EnemyLabel(const std::string& id)
{
    if (id == "enemy_normal") return "通常球";
    if (id == "enemy_strong") return "強敵球";
    if (id == "enemy_tank") return "タンク球";
    if (id == "enemy_striker") return "ストライカー";
    if (id == "enemy_midboss_guard") return "中ボス：ガード球";
    if (id == "enemy_midboss_pocket") return "中ボス：ポケット球";
    if (id == "enemy_boss_core") return "最終ボス：Armor球";
    return id.c_str();
}
