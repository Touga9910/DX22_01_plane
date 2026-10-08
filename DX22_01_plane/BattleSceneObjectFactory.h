#pragma once

#include <vector>

class GameObject;
class PlayerBall;
class BattleSceneServices;
class TableFrame;
struct StageData;

struct BattleArenaObjects
{
    PlayerBall* player = nullptr;
    TableFrame* tableFrame = nullptr;
};

class BattleSceneObjectFactory
{
public:
    explicit BattleSceneObjectFactory(BattleSceneServices& services)
        : m_Services(services)
    {
    }

    BattleArenaObjects CreateArena(
        std::vector<GameObject*>& sceneObjects) const;
    void CreateStageObjects(
        const StageData& stage,
        std::vector<GameObject*>& sceneObjects) const;
    void BeginSimulation(PlayerBall& player) const;

private:
    BattleSceneServices& m_Services;
};
