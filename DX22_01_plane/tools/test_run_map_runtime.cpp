#include "../RunMap.h"
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
    for (unsigned seed = 0; seed < 200; ++seed)
    {
        RunMap map;
        map.Generate(seed);
        const auto initial = map.Save();
        assert(initial.at("version") == 2);
        assert(RunMap::Restore(initial).Snapshot() == map.Snapshot());
        assert(map.Available() == std::vector<int>({0, 1, 2}));

        int finalApproaches = 0;
        for (const auto& node : map.Nodes())
        {
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
        for (int floor = 0; floor < 17; ++floor)
        {
            const auto choices = map.Available();
            assert(!choices.empty());
            assert(map.Choose(choices[(seed + floor) % choices.size()]));
            assert(RunMap::Restore(map.Save()).Save() == map.Save());
            assert(map.CompleteActive());
            assert(map.CompletedAreas() == (std::min)(15, floor + 1));
        }
        assert(map.Available().empty());
    }
    RunMap legacy; legacy.Generate(20260817, 6, 9);
    legacy.MigrateEntry(StageRouteType::Shop);
    assert(legacy.Active() == 0 && legacy.CompletedAreas() == 6);
    assert(RunMap::Restore(legacy.Save()).Snapshot() == legacy.Snapshot());
    legacy.CompleteActive(); assert(legacy.CompletedAreas() == 7);
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
    std::cout << "Run map: 200 seeds, both versions, all-node reachability, noncrossing routes, recovery access, save round trips, legacy entries and corrupt paths passed.\n";
}
