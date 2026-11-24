#include "../RunMap.h"
#include "../RunProgressController.h"
#include <cassert>
#include <iostream>

/// <summary>
/// 複数のシードとマップ版で、経路の再現性や到達可能性、保存と破損データの扱いを検証する。
/// </summary>
int main()
{
    for (unsigned seed = 0; seed < 200; ++seed)
    {
        RunMap map; map.Generate(seed, 0, 15, 1);
        RunMap same; same.Generate(seed, 0, 15, 1);
        assert(map.Snapshot() == same.Snapshot());
        assert(map.Nodes().size() == 47);
        assert(!map.Choose(-1) && !map.Choose(3) && !map.Choose(46));
        for (const auto& n : map.Nodes())
        {
            assert(map.Reachable(n.id));
            if (n.id != 46) assert(!n.next.empty());
            for (int next : n.next) assert(map.Node(next)->floor == n.floor + 1);
            if (n.floor == 2 || n.floor == 6 || n.floor == 10)
            {
                // All three incoming lanes can elect the guaranteed recovery node.
                const int rest = (n.floor + 1) * 3 + 1;
                assert(std::find(n.next.begin(), n.next.end(), rest) != n.next.end());
                assert(map.Node(rest)->type == StageRouteType::RestSite);
            }
        }
        for (int area = 1; area <= 15; ++area)
        {
            auto has = [&](StageRouteType type) {
                for (int lane = 0; lane < 3; ++lane)
                    if (map.Node((area - 1) * 3 + lane)->type == type) return true;
                return false;
            };
            assert(has(StageRouteType::NormalBattle));
            if (area == 3 || area == 7 || area == 11 || area == 14) assert(has(StageRouteType::Shop));
            if (area % 5 == 0) assert(has(StageRouteType::MidBoss));
        }
        for (int area = 0; area < 15; ++area)
        {
            auto choices = map.Available();
            assert(!choices.empty());
            const int id = choices[(seed + area) % choices.size()];
            assert(map.Choose(id));
            assert(map.Available().empty() && !map.Choose(id));
            assert(RunMap::Restore(map.Save()).Snapshot() == map.Snapshot());
            assert(map.CompletedAreas() == area);
            assert(map.CompleteActive() && !map.CompleteActive());
            assert(map.CompletedAreas() == area + 1);
            assert(RunMap::Restore(map.Save()).Snapshot() == map.Snapshot());
            assert(!map.Choose(id));
        }
        assert(map.Available() == std::vector<int>{45});
        assert(!map.Choose(46));
        assert(map.Choose(45) && map.CompleteActive());
        assert(map.CompletedAreas() == 15);
        assert(map.Choose(46) && map.CompleteActive());
        assert(map.Available().empty());
        assert(RunMap::Restore(map.Save()).Snapshot() == map.Snapshot());
    }
    bool sawEarlyEvent = false;
    bool sawLateEvent = false;
    bool selectedEvent = false;
    for (unsigned seed = 0; seed < 200; ++seed)
    {
        RunMap map;
        map.Generate(seed, 0, 15, 3);
        const auto initial = map.Save();
        assert(initial.at("version") == 3);
        assert(RunMap::Restore(initial).Snapshot() == map.Snapshot());
        assert(map.Available() == std::vector<int>({0, 1, 2}));

        RunMap previousVersion;
        previousVersion.Generate(seed, 0, 15, 2);
        assert(previousVersion.Nodes().size() == map.Nodes().size());
        assert(RunMap::Restore(previousVersion.Save()).Snapshot() == previousVersion.Snapshot());

        int earlyEvents = 0;
        int lateEvents = 0;
        int finalApproaches = 0;
        for (const auto& node : map.Nodes())
        {
            const auto& previousNode = *previousVersion.Node(node.id);
            assert(node.floor == previousNode.floor && node.lane == previousNode.lane);
            assert(node.next == previousNode.next);
            if (node.type == StageRouteType::Event)
            {
                assert(previousNode.type == StageRouteType::NormalBattle);
                assert(node.floor >= 2 && node.floor <= 12);
                if (node.floor <= 6) ++earlyEvents;
                else ++lateEvents;
                for (const auto& incoming : map.Nodes())
                    assert(incoming.next.size() != 1 || incoming.next.front() != node.id);
            }
            else
            {
                assert(node.type == previousNode.type);
            }
            assert(map.Reachable(node.id));
            if (node.type != StageRouteType::FinalBoss) assert(!node.next.empty());
            for (int id : node.next)
            {
                const auto& next = *map.Node(id);
                assert(next.floor == node.floor + 1);
                for (const auto& other : map.Nodes())
                    if (other.floor == node.floor && other.lane > node.lane)
                        for (int end : other.next)
                            assert(map.Node(end)->lane >= next.lane);
            }
            if (node.floor == 14)
            {
                ++finalApproaches;
                assert(node.next.size() == 1);
                assert(map.Node(node.next.front())->type == StageRouteType::BossPreparation);
            }
        }
        assert(finalApproaches == 5);
        assert(earlyEvents <= 1 && lateEvents <= 1);
        sawEarlyEvent |= earlyEvents == 1;
        sawLateEvent |= lateEvents == 1;
        for (int floor = 0; floor < 15; ++floor)
        {
            assert(std::any_of(map.Nodes().begin(), map.Nodes().end(), [floor](const RunMapNode& node)
            {
                return node.floor == floor && node.type == StageRouteType::NormalBattle;
            }));
        }
        for (int floor = 0; floor < 17; ++floor)
        {
            const auto choices = map.Available();
            assert(!choices.empty());
            const int choice = choices[(seed + floor) % choices.size()];
            assert(map.Choose(choice));
            if (map.Node(choice)->type == StageRouteType::Event)
            {
                selectedEvent = true;
                const RunMap restored = RunMap::Restore(map.Save());
                assert(restored.Active() == choice && restored.Node(choice)->type == StageRouteType::Event);
            }
            else
            {
                assert(RunMap::Restore(map.Save()).Save() == map.Save());
            }
            assert(map.CompleteActive());
            assert(map.CompletedAreas() == (std::min)(15, floor + 1));
        }
        assert(map.Available().empty());
    }
    assert(sawEarlyEvent && sawLateEvent && selectedEvent);
    assert(std::string(RunMapRouteId(StageRouteType::Event)) == "event");

    int normalAndEventCount = 0;
    int eventCount = 0;
    for (unsigned seed = 0; seed < 200; ++seed)
    {
        RunMap map;
        map.Generate(seed);
        assert(map.Version() == 4);
        assert(RunMap::Restore(map.Save()).Snapshot() == map.Snapshot());

        RunMap original;
        original.Generate(seed, 0, 15, 2);
        assert(original.Nodes().size() == map.Nodes().size());
        int mapPoolSize = 0;
        int mapEvents = 0;
        for (const auto& node : map.Nodes())
        {
            const auto& originalNode = *original.Node(node.id);
            assert(node.floor == originalNode.floor && node.lane == originalNode.lane);
            assert(node.next == originalNode.next);
            if (node.type == StageRouteType::Event)
            {
                assert(node.floor >= 2 && node.floor < 15);
                assert(originalNode.type == StageRouteType::NormalBattle);
                ++mapEvents;
            }
            else
            {
                assert(node.type == originalNode.type);
            }
            if (node.type == StageRouteType::NormalBattle || node.type == StageRouteType::Event)
                ++mapPoolSize;
            if (!node.next.empty())
            {
                assert(std::any_of(node.next.begin(), node.next.end(), [&map](int next)
                {
                    return map.Node(next)->type != StageRouteType::Event;
                }));
            }
        }
        for (int floor = 0; floor < 15; ++floor)
        {
            assert(std::any_of(map.Nodes().begin(), map.Nodes().end(), [floor](const RunMapNode& node)
            {
                return node.floor == floor && node.type == StageRouteType::NormalBattle;
            }));
        }
        assert(mapEvents <= (mapPoolSize + 2) / 4);
        normalAndEventCount += mapPoolSize;
        eventCount += mapEvents;
    }
    assert(eventCount * 100 >= normalAndEventCount * 24);
    assert(eventCount * 100 <= normalAndEventCount * 26);

    for (int version : {1, 2, 3, 4})
    {
        RunProgressController progress;
        RunProgressState state;
        state.map.Generate(20261124, 0, 15, version);
        progress.Restore(std::move(state));
        for (int area = 1; area <= 15; ++area)
        {
            const auto available = progress.GetMap().Available();
            assert(!available.empty() && progress.ChooseNode(available.front()));
            assert(progress.CompleteNormalArea(false, 15, 0) == (area < 15
                ? AreaCompletionResult::Advanced : AreaCompletionResult::BossPreparationEntered));
        }
        const RunMap& map = progress.GetMap();
        assert(progress.IsBossPreparation());
        const RunMapNode* active = map.Node(map.Active());
        assert(active != nullptr && active->type == StageRouteType::BossPreparation);
        assert(progress.CompleteBossPreparation());
        assert(progress.IsFinalBossReady());
        const auto available = map.Available();
        assert(available.size() == 1 && map.Node(available.front())->type == StageRouteType::FinalBoss);
    }

    RunMap legacy; legacy.Generate(20260817, 6, 9);
    legacy.MigrateEntry(StageRouteType::Shop);
    assert(legacy.Active() == 0 && legacy.CompletedAreas() == 6);
    assert(RunMap::Restore(legacy.Save()).Snapshot() == legacy.Snapshot());
    legacy.CompleteActive(); assert(legacy.CompletedAreas() == 7);

    RunMap debugEvent;
    assert(!debugEvent.SetDebugEventEntry());
    debugEvent.Generate(20261124, 6, 9);
    assert(debugEvent.SetDebugEventEntry());
    assert(!debugEvent.SetDebugEventEntry() && debugEvent.Active() == -1);
    assert(debugEvent.Available().front() == 0 && debugEvent.Node(0)->type == StageRouteType::Event);
    assert(RunMap::Restore(debugEvent.Save()).Snapshot() == debugEvent.Snapshot());
    assert(debugEvent.Choose(0));
    assert(RunMap::Restore(debugEvent.Save()).Snapshot() == debugEvent.Snapshot());
    RunMap old;
    old.Generate(20261124, 6, 9, 3);
    assert(!old.SetDebugEventEntry());

    RunMap prep; prep.Generate(1, 15, 0);
    assert(prep.Available() == std::vector<int>{0});
    assert(prep.Choose(0) && prep.CompleteActive() && prep.Choose(1));
    assert(RunMap::Restore(prep.Save()).Snapshot() == prep.Snapshot());
    RunMap base; base.Generate(1);
    for (const auto& mutation : {"jump", "repeat", "active", "version", "size", "entry"})
    {
        auto saved = base.Save();
        const std::string name(mutation);
        if (name == "jump") saved["path"] = {0, 9};
        if (name == "repeat") saved["path"] = {0, 0};
        if (name == "active") saved["active_node_id"] = 46;
        if (name == "version") saved["version"] = 999;
        if (name == "size") saved["area_count"] = 1000000;
        if (name == "entry") saved["legacy_entry_type"] = 5;
        bool rejected = false;
        try { RunMap::Restore(saved); } catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }
    std::cout << "Run map: 200 seeds, all versions, reachability, event ratio ("
        << eventCount << "/" << normalAndEventCount
        << "), boss preparation, save round trips, entry overrides and corrupt paths passed.\n";
}
