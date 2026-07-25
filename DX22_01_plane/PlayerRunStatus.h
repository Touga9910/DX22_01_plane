#pragma once

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
