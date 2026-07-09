#include "StageDataLoader.h"

#include <fstream>
#include <iostream>
#include <unordered_map>
#include "json/json.hpp"

using json = nlohmann::json;
using namespace DirectX::SimpleMath;

namespace
{
    Vector3 LoadVector3(const json& j, const Vector3& defaultValue)
    {
        if (!j.is_object())
        {
            return defaultValue;
        }

        Vector3 v = defaultValue;

        v.x = j.value("x", defaultValue.x);
        v.y = j.value("y", defaultValue.y);
        v.z = j.value("z", defaultValue.z);

        return v;
    }

    // 
    BallStatus LoadBallStatus(const json& j)
    {
        BallStatus status;

        if (!j.is_object())
        {
            return status;
        }

        status.maxHp = j.value("maxHp", status.maxHp);
        status.attack = j.value("attack", status.attack);
        status.defense = j.value("defense", status.defense);

        return status;
    }

	// 敵のマスターデータをJSONから読み込む関数（1体分のデータ）
    EnemyData LoadEnemyMasterData(const json& j)
    {
        EnemyData data;

        if (!j.is_object())
        {
            return data;
        }

        data.id = j.value("id", data.id);
        data.modelFilePath = j.value("modelFilePath", data.modelFilePath);
        data.textureDirectory = j.value("textureDirectory", data.textureDirectory);

        if (j.contains("status"))
        {
            data.status = LoadBallStatus(j["status"]);
        }

        if (j.contains("scale"))
        {
            data.scale = LoadVector3(j["scale"], data.scale);
        }

        data.rewardMoney = j.value("rewardMoney", data.rewardMoney);
        data.rewardExp = j.value("rewardExp", data.rewardExp);

        return data;
    }

	// 敵のマスターデータをJSONファイルから読み込み、IDをキーとしたunordered_mapに格納する関数
    std::unordered_map<std::string, EnemyData> LoadEnemyMasterMap(
        const std::string& filePath
    )
    {
        std::unordered_map<std::string, EnemyData> enemyMap;

        std::ifstream file(filePath);

        if (!file.is_open())
        {
            std::cout << "Enemy Master JSONを開けませんでした: "
                << filePath << std::endl;
            return enemyMap;
        }

        try
        {
            json root;
            file >> root;

            if (root.contains("enemies") && root["enemies"].is_array())
            {
                for (const auto& enemyJson : root["enemies"])
                {
                    EnemyData data = LoadEnemyMasterData(enemyJson);

                    if (!data.id.empty())
                    {
                        enemyMap[data.id] = data;
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "Enemy Master JSONの読み込みに失敗しました: "
                << filePath << std::endl;
            std::cout << e.what() << std::endl;
        }

        return enemyMap;
    }
}

StageData StageDataLoader::Load(
    const std::string& stageFilePath,
    const std::string& enemyMasterFilePath
)
{
    StageData stageData;

    std::unordered_map<std::string, EnemyData> enemyMasterMap =
        LoadEnemyMasterMap(enemyMasterFilePath);

    std::ifstream file(stageFilePath);

    if (!file.is_open())
    {
        std::cout << "Stage JSONを開けませんでした: "
            << stageFilePath << std::endl;
        return stageData;
    }

    try
    {
        json root;
        file >> root;

        stageData.stageId = root.value("stageId", stageData.stageId);
        stageData.par = root.value("par", stageData.par);

        if (root.contains("enemySpawns") && root["enemySpawns"].is_array())
        {
            for (const auto& spawnJson : root["enemySpawns"])
            {
                std::string enemyId = spawnJson.value("enemyId", "");

                auto it = enemyMasterMap.find(enemyId);

                if (it == enemyMasterMap.end())
                {
                    std::cout << "敵IDがenemy_data.jsonに存在しません: "
                        << enemyId << std::endl;
                    continue;
                }

                EnemyData enemyData = it->second;

                if (spawnJson.contains("initPosition"))
                {
                    enemyData.initPosition = LoadVector3(
                        spawnJson["initPosition"],
                        enemyData.initPosition
                    );
                }

                stageData.enemies.push_back(enemyData);
            }
        }

        std::cout << "Loaded Stage: " << stageData.stageId << std::endl;
        std::cout << "Loaded Enemy Count: "
            << stageData.enemies.size() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "Stage JSONの読み込みに失敗しました: "
            << stageFilePath << std::endl;
        std::cout << e.what() << std::endl;
    }

    for (const EnemyData& enemy : stageData.enemies)
    {
        std::cout << "Enemy: "
            << enemy.id
            << " HP: " << enemy.status.maxHp
            << " Attack: " << enemy.status.attack
            << " Pos("
            << enemy.initPosition.x << ", "
            << enemy.initPosition.y << ", "
            << enemy.initPosition.z << ")"
            << std::endl;
    }

    return stageData;
}