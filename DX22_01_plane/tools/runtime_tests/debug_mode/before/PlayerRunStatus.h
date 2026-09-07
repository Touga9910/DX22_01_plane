#pragma once

#include <algorithm>
#include <string>

struct PlayerRunStatus
{
    int maxHp = 10;
    int currentHp = 10;

    int money = 0;    // ラン中に所持しているMoney
    int progress = 1;

    void SetSelectedStageId(const std::string& id)
    {
        selectedStageId = id;
    }

    const std::string& GetSelectedStageId() const
    {
        return selectedStageId;
    }

    void SetLastStageId(const std::string& id)
    {
        lastStageId = id;
    }

    const std::string& GetLastStageId() const
    {
        return lastStageId;
    }

private:
    std::string selectedStageId;
    std::string lastStageId;
};

inline PlayerRunStatus NormalizePlayerRunStatus(PlayerRunStatus status)
{
    status.maxHp = (std::max)(1, status.maxHp);
    status.currentHp = std::clamp(status.currentHp, 0, status.maxHp);
    status.progress = (std::max)(1, status.progress);
    return status;
}
