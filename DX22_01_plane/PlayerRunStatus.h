#pragma once

#include <algorithm>
#include <string>

// 1回のランを通して引き継ぐプレイヤー状態を保持
// HP、所持金、進行度、選択中・直前ステージのIDを管理
struct PlayerRunStatus
{
    int maxHp = 10;       // ラン中の最大HP
    int currentHp = 10;   // 現在HP

    int money = 0;        // ラン中に所持しているMoney
    int progress = 1;     // ランの現在進行値

    // 次に使用するステージIDを設定
    void SetSelectedStageId(const std::string& id)
    {
        selectedStageId = id;
    }

    // 現在選択されているステージIDを返す
    const std::string& GetSelectedStageId() const
    {
        return selectedStageId;
    }

    // 直前に使用したステージIDを設定
    void SetLastStageId(const std::string& id)
    {
        lastStageId = id;
    }

    // 直前に使用したステージIDを返す
    const std::string& GetLastStageId() const
    {
        return lastStageId;
    }

private:
    std::string selectedStageId;   // 次に使用するステージのID
    std::string lastStageId;       // 直前に使用したステージのID
};

// ラン状態の値を有効範囲へ補正して返す
// 最大HPと進行度は最低1、現在HPは0～最大HPへ制限
inline PlayerRunStatus NormalizePlayerRunStatus(PlayerRunStatus status)
{
    status.maxHp = (std::max)(1, status.maxHp);
    status.currentHp = std::clamp(status.currentHp, 0, status.maxHp);
    status.progress = (std::max)(1, status.progress);
    return status;
}
