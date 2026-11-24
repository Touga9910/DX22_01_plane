#pragma once

// Validated battle progression requests used outside the Game implementation.
class GameBattleCommands final
{
public:
    static void CompleteCurrentStage();
    static void BeginAimDirection();
    static void BeginPowerSelection();
    static void CancelShot();
    static int ConsumeBankShotDamageMultiplier();
    static int ConsumePlayerEnemyRelicDamageBonus();

    /// <summary>
    /// 戦闘コマンドの静的インターフェースをインスタンス化できないようにする。
    /// </summary>
    GameBattleCommands() = delete;
};
