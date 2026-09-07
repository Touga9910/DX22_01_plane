#pragma once
#pragma execution_character_set("utf-8")

#include "PlayerBallSaveData.h"
#include "StageDataLoader.h"
#include "GameTypes.h"
#include "TableConfig.h"
#include <cmath>
#include <stdexcept>

// 通常のセーブとは独立した、戦闘開始条件だけのプリセット。
struct DebugBattleSetup
{
    struct Enemy
    {
        EnemySpawnData spawn;
        int hp = 1;
    };
    int maxHp = 50;
    int hp = 50;
    int money = 0;
    std::uint32_t seed = 20260904;
    DirectX::SimpleMath::Vector3 playerPosition{0, TableConfig::FIELD_HEIGHT, 0};
    std::vector<PlayerBallData> deck;
    std::vector<Enemy> enemies;
    std::array<bool, static_cast<size_t>(RelicType::Count)> relics{};
    int armor = 2;
    int breakShots = 0;

    std::string Validate() const
    {
        if (maxHp < 1 || maxHp > 9999 || hp < 1 || hp > maxHp || money < 0 || money > 999999)
            return "HP・最大HP・所持金が範囲外です。";
        if (deck.empty() || deck.size() > 64) return "デッキは1～64個にしてください。";
        if (enemies.empty() || enemies.size() > 32) return "敵は1～32体にしてください。";
        if (armor < 0 || armor > 2 || breakShots < 0 || breakShots > 2 || (armor == 0) != (breakShots > 0))
            return "Armorは1～2、またはArmor 0とBreak残り1～2を指定してください。";
        auto inside = [](const auto& pos, float radius) {
            return std::isfinite(pos.x) && std::isfinite(pos.z) &&
                std::abs(pos.x) + radius < TableConfig::GetFieldWidth() * 0.5f &&
                std::abs(pos.z) + radius < TableConfig::GetFieldDepth() * 0.5f;
        };
        for (const auto& ball : deck)
        {
            if (!IsValidBallStatus(ball.status) || ball.status.radius < 0.1f || ball.status.radius > 12 ||
                ball.upgradeLevel < 0 || ball.upgradeLevel > 2 || ball.definitionId.empty())
                return "ボールの性能が範囲外です（半径0.1～12）。";
            if (!inside(playerPosition, ball.status.radius)) return "自球の開始位置を台の内側へ移してください。";
        }
        int bosses = 0;
        for (const auto& enemy : enemies)
        {
            const auto& data = enemy.spawn.enemyData;
            if (data.maxHp < 1 || data.maxHp > 9999 || enemy.hp < 1 || enemy.hp > data.maxHp ||
                !IsValidBallStatus(data.status) || data.status.radius < 0.1f || data.status.radius > 12)
                return "敵のHP・性能が範囲外です。";
            if (!inside(enemy.spawn.position, data.status.radius)) return "敵の位置を台の内側へ移してください。";
            if (data.id == "enemy_boss_core") ++bosses;
        }
        if (bosses > 1) return "Armorボスは1体までにしてください。";
        return {};
    }

    nlohmann::json ToJson() const
    {
        using nlohmann::json;
        json balls = json::array(), foes = json::array();
        for (size_t i = 0; i < deck.size(); ++i)
        {
            auto ball = deck[i];
            ball.instanceId = i + 1;
            balls.push_back(PlayerBallSaveData::BallToJson(ball));
        }
        for (const auto& enemy : enemies)
            foes.push_back({{"id", enemy.spawn.enemyData.id}, {"hp", enemy.hp},
                {"max_hp", enemy.spawn.enemyData.maxHp}, {"status", WriteBallStatus(enemy.spawn.enemyData.status)},
                {"x", enemy.spawn.position.x}, {"z", enemy.spawn.position.z}});
        return {{"version", 1}, {"hp", hp}, {"max_hp", maxHp}, {"money", money}, {"seed", seed},
            {"player_x", playerPosition.x}, {"player_z", playerPosition.z}, {"deck", balls},
            {"enemies", foes}, {"relics", relics}, {"armor", armor}, {"break_shots", breakShots}};
    }

    static DebugBattleSetup FromJson(const nlohmann::json& j,
        const std::vector<PlayerBallData>& catalog, const std::vector<EnemyData>& enemyCatalog)
    {
        if (j.at("version").get<int>() != 1) throw std::runtime_error("未対応のプリセット形式です。");
        DebugBattleSetup next;
        next.hp = j.at("hp").get<int>(); next.maxHp = j.at("max_hp").get<int>();
        next.money = j.at("money").get<int>();
        const auto seedValue = j.at("seed").get<std::int64_t>();
        if (seedValue < 0 || seedValue > UINT32_MAX) throw std::runtime_error("シードが範囲外です。");
        next.seed = static_cast<std::uint32_t>(seedValue);
        next.playerPosition.x = j.at("player_x").get<float>(); next.playerPosition.z = j.at("player_z").get<float>();
        next.armor = j.at("armor").get<int>(); next.breakShots = j.at("break_shots").get<int>();
        next.relics = j.at("relics").get<decltype(next.relics)>();
        if (!j.at("deck").is_array() || j.at("deck").size() > 64 ||
            !j.at("enemies").is_array() || j.at("enemies").size() > 32 ||
            j.at("relics").size() != next.relics.size()) throw std::runtime_error("プリセットの個数が不正です。");
        for (const auto& value : j.at("deck"))
        {
            auto ball = PlayerBallSaveData::BallFromJson(value);
            if (std::none_of(catalog.begin(), catalog.end(), [&](const auto& d) { return d.definitionId == ball.definitionId; }))
                throw std::runtime_error("定義のないボールが含まれています。");
            next.deck.push_back(std::move(ball));
        }
        for (const auto& value : j.at("enemies"))
        {
            const auto id = value.at("id").get<std::string>();
            auto found = std::find_if(enemyCatalog.begin(), enemyCatalog.end(), [&](const auto& d) { return d.id == id; });
            if (found == enemyCatalog.end()) throw std::runtime_error("定義のない敵が含まれています。");
            Enemy enemy;
            enemy.spawn.enemyId = id; enemy.spawn.enemyData = *found;
            enemy.spawn.enemyData.maxHp = value.at("max_hp").get<int>();
            enemy.spawn.enemyData.status = ReadBallStatus(value.at("status"));
            enemy.hp = value.at("hp").get<int>();
            enemy.spawn.position = {value.at("x").get<float>(), TableConfig::FIELD_HEIGHT, value.at("z").get<float>()};
            next.enemies.push_back(std::move(enemy));
        }
        auto error = next.Validate();
        if (!error.empty()) throw std::runtime_error(error);
        return next;
    }
};
