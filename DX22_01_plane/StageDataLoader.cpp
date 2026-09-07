#include "StageDataLoader.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include "TableConfig.h"
#include "json/json.hpp"

using json = nlohmann::json;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr const char* kDefaultEnemyId = "enemy_normal";

    Vector3 LoadVector3(const json& value, const Vector3& defaultValue)
    {
        if (value.is_array() && value.size() == 3)
        {
            return Vector3(
                value[0].get<float>(),
                value[1].get<float>(),
                value[2].get<float>());
        }

        if (value.is_object())
        {
            return Vector3(
                value.value("x", defaultValue.x),
                value.value("y", defaultValue.y),
                value.value("z", defaultValue.z));
        }

        return defaultValue;
    }

    BallStatus LoadBallStatus(const json& value)
    {
        BallStatus status;
        if (!value.is_object())
        {
            return status;
        }

        status.attack = value.value("attack", status.attack);
        status.defense = value.value("defense", status.defense);
        status.mass = value.value("mass", status.mass);
        status.radius = value.value("radius", status.radius);
        status.restitution = value.value("restitution", status.restitution);
        status.friction = value.value("friction", status.friction);

        if (value.contains("abilities") && value["abilities"].is_object())
        {
            const json& abilities = value["abilities"];
            status.abilities.split =
                abilities.value("split", status.abilities.split);
            status.abilities.pierce =
                abilities.value("pierce", status.abilities.pierce);
            status.abilities.anchor =
                abilities.value("anchor", status.abilities.anchor);
        }

        return status;
    }

    EnemyData LoadEnemyMasterData(const json& value)
    {
        EnemyData data;
        if (!value.is_object())
        {
            return data;
        }

        data.id = value.value("id", data.id);
        data.modelFilePath =
            value.value("modelFilePath", data.modelFilePath);
        data.textureDirectory =
            value.value("textureDirectory", data.textureDirectory);
        data.rewardMoney = value.value("rewardMoney", data.rewardMoney);
        data.rewardExp = value.value("rewardExp", data.rewardExp);

        if (value.contains("status"))
        {
            data.status = NormalizeBallStatus(LoadBallStatus(value["status"]));
            data.maxHp = (std::max)(1, value["status"].value("maxHp", data.maxHp));
        }
        if (value.contains("scale"))
        {
            data.scale = LoadVector3(value["scale"], data.scale);
        }

        if (value.contains("gimmick") && value["gimmick"].is_object())
        {
            const auto& gimmick = value["gimmick"];
            data.frontalDamageMultiplier = std::clamp(gimmick.value("frontalDamageMultiplier", 1.0f), 0.0f, 1.0f);
            data.pocketDamageRatio = std::clamp(gimmick.value("pocketDamageRatio", 0.0f), 0.0f, 1.0f);
        }
        return data;
    }

    std::unordered_map<std::string, EnemyData> LoadEnemyMasterMap(
        const std::string& filePath)
    {
        std::unordered_map<std::string, EnemyData> enemyMap;
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            std::cerr << "[StageDataLoader] Enemy master JSONを開けません: "
                << filePath << std::endl;
            return enemyMap;
        }

        try
        {
            json root;
            file >> root;
            if (!root.contains("enemies") || !root["enemies"].is_array())
            {
                std::cerr
                    << "[StageDataLoader] enemy masterにenemies配列がありません: "
                    << filePath << std::endl;
                return enemyMap;
            }

            for (const json& enemyJson : root["enemies"])
            {
                EnemyData data = LoadEnemyMasterData(enemyJson);
                if (data.id.empty())
                {
                    std::cerr
                        << "[StageDataLoader] enemy masterに空のIDがあります"
                        << std::endl;
                    continue;
                }
                enemyMap[data.id] = data;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "[StageDataLoader] Enemy master JSONの読み込みに失敗: "
                << filePath << " / " << e.what() << std::endl;
        }

        return enemyMap;
    }

    StageType ParseStageType(const std::string& value)
    {
        if (value == "normal")
        {
            return StageType::Normal;
        }
        if (value == "midBoss")
        {
            return StageType::MidBoss;
        }
        if (value == "boss")
        {
            return StageType::Boss;
        }

        std::cerr << "[StageDataLoader] 不明なstageType「" << value
            << "」をnormalとして扱います" << std::endl;
        return StageType::Normal;
    }

    bool HasRequiredStageFields(const json& stageJson, size_t index)
    {
        const char* requiredFields[] =
        {
            "id",
            "stageType",
            "difficulty",
            "enemies"
        };

        for (const char* field : requiredFields)
        {
            if (!stageJson.contains(field))
            {
                std::cerr << "[StageDataLoader] stages[" << index
                    << "] に必須項目「" << field << "」がありません"
                    << std::endl;
                return false;
            }
        }

        if (!stageJson["id"].is_string() ||
            !stageJson["stageType"].is_string() ||
            !stageJson["difficulty"].is_number_integer() ||
            !stageJson["enemies"].is_array())
        {
            std::cerr << "[StageDataLoader] stages[" << index
                << "] の必須項目の型が不正です" << std::endl;
            return false;
        }

        return true;
    }
}

std::vector<EnemyData> StageDataLoader::LoadEnemyDefinitions(
    const std::string& enemyMasterFilePath)
{
    const std::unordered_map<std::string, EnemyData> enemyMasterMap =
        LoadEnemyMasterMap(enemyMasterFilePath);
    std::vector<EnemyData> definitions;
    definitions.reserve(enemyMasterMap.size());
    for (const auto& [id, data] : enemyMasterMap)
    {
        definitions.push_back(data);
    }
    std::sort(
        definitions.begin(),
        definitions.end(),
        [](const EnemyData& left, const EnemyData& right)
        {
            return left.id < right.id;
        });
    return definitions;
}

std::vector<StageData> StageDataLoader::LoadAll(
    const std::string& stageFilePath,
    const std::string& enemyMasterFilePath)
{
    std::vector<StageData> stages;
    const std::unordered_map<std::string, EnemyData> enemyMasterMap =
        LoadEnemyMasterMap(enemyMasterFilePath);
    if (enemyMasterMap.empty())
    {
        return stages;
    }

    std::ifstream file(stageFilePath);
    if (!file.is_open())
    {
        std::cerr << "[StageDataLoader] Stage JSONを開けません: "
            << stageFilePath << std::endl;
        return stages;
    }

    try
    {
        json root;
        file >> root;
        if (!root.contains("stages") || !root["stages"].is_array())
        {
            std::cerr << "[StageDataLoader] stages配列がありません: "
                << stageFilePath << std::endl;
            return stages;
        }

        std::unordered_set<std::string> loadedIds;
        const json& stageArray = root["stages"];
        for (size_t stageIndex = 0;
            stageIndex < stageArray.size();
            ++stageIndex)
        {
            const json& stageJson = stageArray[stageIndex];
            if (!stageJson.is_object() ||
                !HasRequiredStageFields(stageJson, stageIndex))
            {
                continue;
            }

            StageData stage;
            stage.id = stageJson["id"].get<std::string>();
            if (stage.id.empty())
            {
                std::cerr << "[StageDataLoader] stages[" << stageIndex
                    << "] のidが空です" << std::endl;
                continue;
            }
            if (!loadedIds.insert(stage.id).second)
            {
                std::cerr << "[StageDataLoader] stage IDが重複しています: "
                    << stage.id << std::endl;
                continue;
            }

            stage.stageType =
                ParseStageType(stageJson["stageType"].get<std::string>());
            stage.difficulty = stageJson["difficulty"].get<int>();
            stage.par = stageJson.value("par", stage.par);
            stage.preserveLayout = stageJson.value("preserveLayout", false);

            const json& enemyArray = stageJson["enemies"];
            for (size_t enemyIndex = 0;
                enemyIndex < enemyArray.size();
                ++enemyIndex)
            {
                const json& spawnJson = enemyArray[enemyIndex];
                if (!spawnJson.is_object() ||
                    !spawnJson.contains("position"))
                {
                    std::cerr << "[StageDataLoader] stage「" << stage.id
                        << "」の敵[" << enemyIndex
                        << "] にpositionがありません" << std::endl;
                    continue;
                }

                EnemySpawnData spawn;
                spawn.enemyId =
                    spawnJson.value("enemyId", std::string(kDefaultEnemyId));
                const auto enemyIt = enemyMasterMap.find(spawn.enemyId);
                if (enemyIt == enemyMasterMap.end())
                {
                    std::cerr << "[StageDataLoader] stage「" << stage.id
                        << "」の敵[" << enemyIndex << "] が参照するenemy ID「"
                        << spawn.enemyId << "」は存在しません" << std::endl;
                    continue;
                }

                try
                {
                    spawn.position = LoadVector3(
                        spawnJson["position"],
                        Vector3(0.0f, TableConfig::FIELD_HEIGHT, 0.0f));
                }
                catch (const std::exception& e)
                {
                    std::cerr << "[StageDataLoader] stage「" << stage.id
                        << "」の敵[" << enemyIndex
                        << "] のpositionが不正です: " << e.what()
                        << std::endl;
                    continue;
                }

                spawn.enemyData = enemyIt->second;
                spawn.enemyData.initPosition = spawn.position;
                stage.enemies.push_back(spawn);
            }

            stages.push_back(std::move(stage));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[StageDataLoader] Stage JSONの読み込みに失敗: "
            << stageFilePath << " / " << e.what() << std::endl;
        stages.clear();
    }

    std::cout << "[StageDataLoader] " << stages.size()
        << "件のステージを読み込みました" << std::endl;
    return stages;
}

const StageData* StageDataLoader::FindById(
    const std::vector<StageData>& stages,
    const std::string& stageId)
{
    for (const StageData& stage : stages)
    {
        if (stage.id == stageId)
        {
            return &stage;
        }
    }

    return nullptr;
}
