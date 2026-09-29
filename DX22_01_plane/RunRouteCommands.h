#pragma once

#include <string>
#include <vector>

// Performs route validation, logging and transition as one operation so human,
// autoplay and external callers cannot accidentally apply only part of it.
class RunRouteCommands final
{
public:
    static bool Choose(
        int nodeId,
        int selectedIndex,
        const std::vector<int>& offeredNodeIds,
        const std::string& controllerType);

    RunRouteCommands() = delete;
};
