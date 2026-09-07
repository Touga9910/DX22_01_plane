#pragma once
#include "json/json.hpp"
#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

enum class StageRouteType { NormalBattle, MidBoss, Shop, RestSite, FinalBoss, BossPreparation };

inline const char* RunMapRouteId(StageRouteType type)
{
    switch (type)
    {
    case StageRouteType::NormalBattle: return "battle";
    case StageRouteType::MidBoss: return "midboss";
    case StageRouteType::Shop: return "shop";
    case StageRouteType::RestSite: return "rest";
    case StageRouteType::FinalBoss: return "final_boss";
    case StageRouteType::BossPreparation: return "boss_preparation";
    default: return "unknown";
    }
}

struct RunMapNode
{
    int id = -1;
    int floor = 0; // Absolute area number, zero based. Final two rows are preparation/boss.
    int lane = 0;
    StageRouteType type = StageRouteType::NormalBattle;
    std::vector<int> next;
};

// Version 1 generation is frozen: engine output modulo constants is portable and
// independent of STL distribution implementations. Saves retain seed + exact path.
class RunMap
{
    std::uint32_t m_Seed = 0;
    int m_StartArea = 0;
    int m_AreaCount = 15;
    int m_EntryOverride = -1; // Only for a pre-map save already inside a facility/battle.
    std::vector<RunMapNode> m_Nodes;
    std::vector<int> m_Path;
    int m_Active = -1;

    void Connect(int from, int to)
    {
        auto& next = m_Nodes.at(from).next;
        if (std::find(next.begin(), next.end(), to) == next.end()) next.push_back(to);
        std::sort(next.begin(), next.end());
    }

public:
    void Generate(std::uint32_t seed, int startArea = 0, int areaCount = 15)
    {
        if (startArea < 0 || startArea > 1000000 || areaCount < 0 || areaCount > 15)
            throw std::runtime_error("Invalid run map extent");
        m_Seed = seed; m_StartArea = startArea; m_AreaCount = areaCount;
        m_EntryOverride = -1; m_Nodes.clear(); m_Path.clear(); m_Active = -1;
        std::mt19937 rng(seed);
        for (int row = 0; row < areaCount; ++row)
        {
            const int area = startArea + row + 1;
            for (int lane = 0; lane < 3; ++lane)
            {
                const unsigned roll = rng() % 150;
                auto type = roll < 95 ? StageRouteType::NormalBattle :
                    roll < 114 ? StageRouteType::MidBoss :
                    roll < 132 ? StageRouteType::Shop : StageRouteType::RestSite;
                if (area <= 2 || (area < 4 && type == StageRouteType::MidBoss))
                    type = StageRouteType::NormalBattle;
                m_Nodes.push_back({ row * 3 + lane, startArea + row, lane, type, {} });
            }
            int protectedLane = -1;
            // Optional recovery every four areas. The other lanes remain choices.
            if (area % 4 == 0) m_Nodes[row * 3 + 1].type = StageRouteType::RestSite;
            if (area == 3 || area == 7 || area == 11 || area == 14)
            {
                protectedLane = static_cast<int>((rng() % 2) * 2);
                m_Nodes[row * 3 + protectedLane].type = StageRouteType::Shop;
            }
            if (area % 5 == 0)
            {
                protectedLane = static_cast<int>((rng() % 2) * 2);
                m_Nodes[row * 3 + protectedLane].type = StageRouteType::MidBoss;
            }
            // Every row has a battle option; facilities cannot occupy an entire row.
            if (std::none_of(m_Nodes.end() - 3, m_Nodes.end(), [](const RunMapNode& n)
                { return n.type == StageRouteType::NormalBattle; }))
            {
                for (int lane = 0; lane < 3; ++lane)
                {
                    if (lane == protectedLane || (area % 4 == 0 && lane == 1)) continue;
                    m_Nodes[row * 3 + lane].type = StageRouteType::NormalBattle;
                    break;
                }
            }
        }
        const int prep = areaCount * 3;
        m_Nodes.push_back({ prep, startArea + areaCount, 1, StageRouteType::BossPreparation, { prep + 1 } });
        m_Nodes.push_back({ prep + 1, startArea + areaCount + 1, 1, StageRouteType::FinalBoss, {} });
        for (int row = 0; row + 1 < areaCount; ++row)
        {
            for (int lane = 0; lane < 3; ++lane) Connect(row * 3 + lane, (row + 1) * 3 + lane);
            for (int pair = 0; pair < 2; ++pair)
            {
                const bool right = rng() % 2 != 0;
                const bool recoveryRow = (startArea + row + 2) % 4 == 0;
                const int from = recoveryRow ? pair * 2 : pair + (right ? 0 : 1);
                const int to = recoveryRow ? 1 : pair + (right ? 1 : 0);
                Connect(row * 3 + from, (row + 1) * 3 + to);
            }
        }
        if (areaCount > 0)
            for (int lane = 0; lane < 3; ++lane) Connect((areaCount - 1) * 3 + lane, prep);
    }

    const std::vector<RunMapNode>& Nodes() const { return m_Nodes; }
    const std::vector<int>& Path() const { return m_Path; }
    int Active() const { return m_Active; }
    int StartArea() const { return m_StartArea; }
    int AreaCount() const { return m_AreaCount; }
    const RunMapNode* Node(int id) const
    {
        return id >= 0 && id < static_cast<int>(m_Nodes.size()) ? &m_Nodes[id] : nullptr;
    }
    bool Visited(int id) const { return std::find(m_Path.begin(), m_Path.end(), id) != m_Path.end(); }
    std::vector<int> Available() const
    {
        if (m_Nodes.empty() || m_Active >= 0) return {};
        if (!m_Path.empty()) return m_Nodes[m_Path.back()].next;
        return m_AreaCount > 0 ? std::vector<int>{0, 1, 2} : std::vector<int>{0};
    }
    bool Choose(int id)
    {
        const auto options = Available();
        if (std::find(options.begin(), options.end(), id) == options.end()) return false;
        m_Active = id;
        return true;
    }
    void CancelActive() { m_Active = -1; }
    bool CompleteActive()
    {
        if (m_Active < 0) return false;
        m_Path.push_back(m_Active); m_Active = -1;
        return true;
    }
    int CompletedAreas() const
    {
        return m_StartArea + static_cast<int>(std::count_if(m_Path.begin(), m_Path.end(),
            [this](int id) { return id < m_AreaCount * 3; }));
    }
    bool Reachable(int id) const
    {
        if (!Node(id) || Visited(id)) return false;
        std::vector<int> pending = m_Active >= 0 ? std::vector<int>{m_Active} : Available();
        std::vector<bool> seen(m_Nodes.size(), false);
        for (std::size_t i = 0; i < pending.size(); ++i)
        {
            const int current = pending[i];
            if (current == id) return true;
            if (seen[current]) continue;
            seen[current] = true;
            pending.insert(pending.end(), m_Nodes[current].next.begin(), m_Nodes[current].next.end());
        }
        return false;
    }
    void MigrateEntry(StageRouteType type)
    {
        if (m_AreaCount == 0 || !m_Path.empty() || m_Active != -1 ||
            type > StageRouteType::RestSite) throw std::runtime_error("Invalid legacy map entry");
        m_EntryOverride = static_cast<int>(type);
        m_Nodes[0].type = type;
        Choose(0);
    }
    nlohmann::json Save() const
    {
        return { {"version", 1}, {"seed", m_Seed}, {"start_area", m_StartArea},
            {"area_count", m_AreaCount}, {"path", m_Path}, {"active_node_id", m_Active},
            {"legacy_entry_type", m_EntryOverride} };
    }
    static RunMap Restore(const nlohmann::json& data)
    {
        if (data.at("version").get<int>() != 1) throw std::runtime_error("Unsupported run map version");
        RunMap result;
        result.Generate(data.at("seed").get<std::uint32_t>(), data.at("start_area").get<int>(), data.at("area_count").get<int>());
        const int entry = data.value("legacy_entry_type", -1);
        if (entry < -1 || entry > static_cast<int>(StageRouteType::RestSite))
            throw std::runtime_error("Invalid legacy map entry type");
        if (entry >= 0) { result.MigrateEntry(static_cast<StageRouteType>(entry)); result.m_Active = -1; }
        const auto& path = data.at("path");
        if (!path.is_array() || path.size() > 17) throw std::runtime_error("Invalid run map path");
        for (const auto& value : path)
        {
            if (!value.is_number_integer() || !result.Choose(value.get<int>())) throw std::runtime_error("Disconnected run map path");
            result.CompleteActive();
        }
        const int active = data.at("active_node_id").get<int>();
        if (active < -1 || (active >= 0 && !result.Choose(active))) throw std::runtime_error("Invalid active map node");
        return result;
    }
    nlohmann::json Snapshot() const
    {
        auto result = Save();
        result["nodes"] = nlohmann::json::array();
        const auto options = Available();
        for (const auto& n : m_Nodes)
            result["nodes"].push_back({ {"node_id", n.id}, {"area", n.floor + 1}, {"lane", n.lane},
                {"destination", RunMapRouteId(n.type)}, {"next_node_ids", n.next}, {"visited", Visited(n.id)},
                {"active", n.id == m_Active}, {"reachable", Reachable(n.id)},
                {"selectable", std::find(options.begin(), options.end(), n.id) != options.end()} });
        return result;
    }
};
