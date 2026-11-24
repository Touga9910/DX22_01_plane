#pragma once
#pragma execution_character_set("utf-8")
#include <string>

// 敵定義IDをプレイヤー向けの日本語表示名へ変換する。未登録のIDはそのまま表示する。
inline const char* EnemyLabel(const std::string& id)
{
    if (id == "enemy_normal") return "通常球";
    if (id == "enemy_strong") return "強敵球";
    if (id == "enemy_tank") return "タンク球";
    if (id == "enemy_striker") return "ストライカー";
    if (id == "enemy_midboss_guard") return "中ボス：ガード球";
    if (id == "enemy_midboss_pocket") return "中ボス：ポケット球";
    if (id == "enemy_nuisance_spawner") return "お邪魔ボール生成球";
    if (id == "enemy_collision_shell") return "衝突装甲球";
    if (id == "enemy_swarm_amplifier") return "群体強化球";
    if (id == "enemy_impact_brittle") return "衝撃脆化球";
    if (id == "enemy_protection_resonator") return "防護共鳴球";
    if (id == "enemy_boss_core") return "最終ボス：装甲球";
    if (id == "enemy_boss_minion") return "取り巻き球";
    return id.c_str();
}
