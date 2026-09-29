#pragma once

#include <vector>

class RunMap;
enum class StageRouteType;

struct RunRouteStatusSnapshot
{
    int areaProgress = 0;
    int normalRouteAreaGoal = 0;
    int playerCurrentHp = 0;
    int playerMaxHp = 0;
    int playerMoney = 0;
    int deckBallCount = 0;
};

class RunRouteView final
{
public:
    static const RunMap& Map();
    static std::vector<int> AvailableNodeIds();
    static RunRouteStatusSnapshot CaptureStatus();
    static StageRouteType NodeType(int nodeId);
    static const char* LogName(StageRouteType routeType);

    RunRouteView() = delete;
};
