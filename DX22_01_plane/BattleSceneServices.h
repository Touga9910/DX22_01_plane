#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace DirectX::SimpleMath
{
    struct Vector3;
}

class EnemyBall;
class Game;
class PlayerBall;
struct EnemyData;
struct StageData;

// BattleScene and its helpers use this narrow port instead of locating Game
// through the process-wide singleton.
class BattleSceneServices
{
public:
    virtual ~BattleSceneServices() = default;

    virtual Game& GameContext() const = 0;
    virtual const StageData* CurrentStageOverride() const = 0;
    virtual const std::string& SelectedStageId() const = 0;
    virtual void ApplyEnemyDifficultyScaling(EnemyData& enemyData) const = 0;
    virtual void ApplyDebugPlayer(PlayerBall* player) const = 0;
    virtual void ApplyDebugEnemy(
        EnemyBall* enemy,
        std::size_t index) const = 0;
    virtual void NotifyStageStarted(const StageData& stage) const = 0;
    virtual bool IsDebugMode() const = 0;
    virtual const std::vector<DirectX::SimpleMath::Vector3>&
        DebugBreakBallPositions() const = 0;
    virtual std::vector<EnemyBall*> Enemies() const = 0;
    virtual void InvalidateCombatForecast(const char* reason) const = 0;
};
