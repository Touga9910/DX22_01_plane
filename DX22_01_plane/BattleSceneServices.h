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
    /// <summary>
    /// 基底クラスのポインターを通じて戦闘シーンのサービスを破棄できるようにする。
    /// </summary>
    virtual ~BattleSceneServices() = default;

    /// <summary>
    /// 戦闘シーンとサービスが共有するゲームコンテキストを返す。
    /// </summary>
    virtual Game& GameContext() const = 0;
    /// <summary>
    /// 現在の戦闘にステージ差し替えが設定されていれば、その定義を返す。
    /// </summary>
    virtual const StageData* CurrentStageOverride() const = 0;
    /// <summary>
    /// 現在選択されている戦闘ステージのIDを返す。
    /// </summary>
    virtual const std::string& SelectedStageId() const = 0;
    /// <summary>
    /// 現在の難度に応じて敵の能力値を補正する。
    /// </summary>
    virtual void ApplyEnemyDifficultyScaling(EnemyData& enemyData) const = 0;
    /// <summary>
    /// デバッグ設定で指定されたプレイヤー球の状態を反映する。
    /// </summary>
    virtual void ApplyDebugPlayer(PlayerBall* player) const = 0;
    /// <summary>
    /// 敵の生成順を使い、対応するデバッグ設定をその敵球に反映する。
    /// </summary>
    virtual void ApplyDebugEnemy(
        EnemyBall* enemy,
        std::size_t index) const = 0;
    /// <summary>
    /// 開始した戦闘ステージの定義をゲーム側へ通知する。
    /// </summary>
    virtual void NotifyStageStarted(const StageData& stage) const = 0;
    /// <summary>
    /// ボス戦の開始時に配置するミニオンを生成して返す。
    /// </summary>
    virtual std::vector<EnemyBall*> SpawnInitialBossMinions() const = 0;
    /// <summary>
    /// 戦闘がデバッグモードで進行しているかを返す。
    /// </summary>
    virtual bool IsDebugMode() const = 0;
    /// <summary>
    /// デバッグ戦闘で使用するブレイク球の配置座標を返す。
    /// </summary>
    virtual const std::vector<DirectX::SimpleMath::Vector3>&
        DebugBreakBallPositions() const = 0;
    /// <summary>
    /// 戦闘中の敵球を取得する。
    /// </summary>
    virtual std::vector<EnemyBall*> Enemies() const = 0;
    /// <summary>
    /// 理由を記録し、戦闘予測のキャッシュを無効にする。
    /// </summary>
    virtual void InvalidateCombatForecast(const char* reason) const = 0;
};
