#pragma once
#include "json/json.hpp"
#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

enum class StageRouteType { NormalBattle, MidBoss, Shop, RestSite, FinalBoss, BossPreparation };

// ルート種別をセーブデータやMCP出力で使用する文字列IDへ変換
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
    int floor = 0;
    int lane = 0;
    StageRouteType type = StageRouteType::NormalBattle;
    std::vector<int> next;
};

// バージョン1のマップ生成規則は固定されている。
// 乱数エンジンの出力に剰余演算を行うことで、STLの分布実装に依存せず、
// 同じSeedから同じマップを再生成できる。
// セーブデータにはSeedと実際に通過した経路を保存
class RunMap
{
    std::uint32_t m_Seed = 0;
    int m_StartArea = 0;
    int m_AreaCount = 15;
    int m_EntryOverride = -1;    // 旧形式セーブデータから移行した開始地点の種類（通常時は-1）
    std::vector<RunMapNode> m_Nodes;
    std::vector<int> m_Path;
    int m_Active = -1;

    // 指定した2ノード間を接続重複を除外し、接続先IDを昇順に保つ。
    void Connect(int from, int to)
    {
        auto& next = m_Nodes.at(from).next;
        if (std::find(next.begin(), next.end(), to) == next.end()) next.push_back(to);
        std::sort(next.begin(), next.end());
    }

public:
    // 指定Seedから3レーン構成のランマップを生成し、進行状態を初期化
    // 通常エリアの後ろには、ボス準備ノードと最終ボスノードが必ず追加される。
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
            
            // 4エリアごとに中央レーンを休憩所にする。
            if (area % 4 == 0) m_Nodes[row * 3 + 1].type = StageRouteType::RestSite;
            
            // 特定エリアでは左右どちらかのレーンをショップとして確保
            if (area == 3 || area == 7 || area == 11 || area == 14)
            {
                protectedLane = static_cast<int>((rng() % 2) * 2);
                m_Nodes[row * 3 + protectedLane].type = StageRouteType::Shop;
            }
            
            // 5エリアごとに左右どちらかのレーンを中ボスとして確保
            if (area % 5 == 0)
            {
                protectedLane = static_cast<int>((rng() % 2) * 2);
                m_Nodes[row * 3 + protectedLane].type = StageRouteType::MidBoss;
            }
            
            // 各行に最低1つは通常戦闘を用意
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

    // 生成済みの全ノードを読み取り専用で返す。
    const std::vector<RunMapNode>& Nodes() const { return m_Nodes; }

    // 完了済みノードを、通過した順番で返す。
    const std::vector<int>& Path() const { return m_Path; }

    // 現在進行中のノードIDを返す。未選択の場合は-1を返す。
    int Active() const { return m_Active; }

    // このマップ生成時に指定された開始エリア番号を返す。
    int StartArea() const { return m_StartArea; }

    // このマップに含まれる通常エリアの行数を返す。
    int AreaCount() const { return m_AreaCount; }

    // IDに対応するノードを返す。範囲外のIDの場合はnullptrを返す。
    const RunMapNode* Node(int id) const
    {
        return id >= 0 && id < static_cast<int>(m_Nodes.size()) ? &m_Nodes[id] : nullptr;
    }
    // 指定ノードが完了済みの経路に含まれているかを判定
    bool Visited(int id) const { return std::find(m_Path.begin(), m_Path.end(), id) != m_Path.end(); }

    // 現在選択できる次のノードID一覧を返す。
    // ノード進行中は空、開始時は先頭3レーン、以降は直前ノードの接続先となる。
    std::vector<int> Available() const
    {
        if (m_Nodes.empty() || m_Active >= 0) return {};
        if (!m_Path.empty()) return m_Nodes[m_Path.back()].next;
        return m_AreaCount > 0 ? std::vector<int>{0, 1, 2} : std::vector<int>{ 0 };
    }
    
    // 選択可能なノードを進行中として設定選択できない場合はfalseを返す。
    bool Choose(int id)
    {
        const auto options = Available();
        if (std::find(options.begin(), options.end(), id) == options.end()) return false;
        m_Active = id;
        return true;
    }
    
    // 現在進行中のノード選択を取り消し、再選択できる状態に戻す。
    void CancelActive() { m_Active = -1; }

    // 進行中ノードを完了済み経路へ追加進行中でない場合はfalseを返す。
    bool CompleteActive()
    {
        if (m_Active < 0) return false;
        m_Path.push_back(m_Active); m_Active = -1;
        return true;
    }
    
    // 完了した通常エリア数に開始エリア番号を加え、ラン全体での到達数を返す。
    // ボス準備ノードと最終ボスノードは通常エリア数に含めない。
    int CompletedAreas() const
    {
        return m_StartArea + static_cast<int>(std::count_if(m_Path.begin(), m_Path.end(),
            [this](int id) { return id < m_AreaCount * 3; }));
    }
    
    // 現在位置または次の選択候補から、指定ノードへ将来到達できるかを探索
    // 存在しないノードと既に完了したノードは到達不可として扱う。
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
    
    // 旧形式セーブの開始地点を先頭ノードへ移し、施設・戦闘内の状態を再現
    // 新規マップの開始前にのみ使用でき、通常戦闘から休憩所までを受け付ける。
    void MigrateEntry(StageRouteType type)
    {
        if (m_AreaCount == 0 || !m_Path.empty() || m_Active != -1 ||
            type > StageRouteType::RestSite) throw std::runtime_error("Invalid legacy map entry");
        m_EntryOverride = static_cast<int>(type);
        m_Nodes[0].type = type;
        Choose(0);
    }
    
    // マップを再生成・復元するための最小情報をJSONへ保存
    // ノード本体は固定生成規則に従ってSeedから再生成するため保存しない。
    nlohmann::json Save() const
    {
        return { {"version", 1}, {"seed", m_Seed}, {"start_area", m_StartArea},
            {"area_count", m_AreaCount}, {"path", m_Path}, {"active_node_id", m_Active},
            {"legacy_entry_type", m_EntryOverride} };
    }
    
    // JSONのSeedからマップを再生成し、完了経路と進行中ノードを検証しながら復元
    // バージョン不一致や接続しない経路など、不正なデータの場合は例外を送出
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
    
    // セーブ情報に全ノードの表示・進行状態を加えた参照用JSONを生成
    // 
    // 
    // 
    // 
    // 
    // 
    // 
    // UI、デバッグ表示、MCPからのゲーム状態取得で扱いやすい形式を返す。
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