#include "../RunMap.h"
#include <cassert>
#include <iostream>

int main()
{
    for (unsigned seed = 0; seed < 200; ++seed)
    {
        RunMap map; map.Generate(seed);
        RunMap same; same.Generate(seed);
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
    std::cout << "Run map: 200 seeds, all-node reachability, connected routes, recovery access, save round trips, legacy entries and corrupt paths passed.\n";
}
