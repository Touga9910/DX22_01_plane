#include "StageLayoutEditor.h"
#pragma execution_character_set("utf-8")
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
    std::string ReadSource(const std::filesystem::path& path)
    {
        if (std::filesystem::file_size(path) > 4 * 1024 * 1024) throw std::runtime_error("ステージファイルが大きすぎます。");
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("ステージファイルを開けません。");
        std::ostringstream bytes; bytes << file.rdbuf(); return bytes.str();
    }
    int Integer(const StageLayoutEditor::Json& j, const char* key)
    {
        const auto& v = j.at(key);
        if (!v.is_number_integer() || v.get<double>() < 1 || v.get<double>() > 99)
            throw std::runtime_error(std::string(key) + " は1～99の整数です。");
        return v.get<int>();
    }
}

StageLayoutEditor::Json StageLayoutEditor::Encode(const StageData& stage)
{
    Json result = {{"id", stage.id}, {"stage_type", ToString(stage.stageType)}, {"difficulty", stage.difficulty}, {"par", stage.par}, {"enemies", Json::array()}};
    for (const auto& e : stage.enemies) result["enemies"].push_back({{"enemy_id", e.enemyId}, {"x", e.position.x}, {"z", e.position.z}});
    return result;
}

StageData StageLayoutEditor::Decode(const Json& value, const std::vector<EnemyData>& catalog)
{
    StageData stage;
    stage.id = value.at("id").get<std::string>();
    if (stage.id.empty() || stage.id.size() > 64 || !std::all_of(stage.id.begin(), stage.id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    })) throw std::runtime_error("ステージ名は英数字・_・-・.で1～64文字です。");
    auto type = value.at("stage_type").get<std::string>();
    if (type == "normal") stage.stageType = StageType::Normal;
    else if (type == "midBoss" || type == "midboss") stage.stageType = StageType::MidBoss;
    else if (type == "boss") stage.stageType = StageType::Boss;
    else throw std::runtime_error("ステージ種別が不正です。");
    stage.difficulty = Integer(value, "difficulty"); stage.par = Integer(value, "par");
    if (stage.stageType == StageType::Normal && stage.difficulty > 3)
        throw std::runtime_error("現在の通常ステージ抽選に対応する難易度は1～3です。");
    stage.preserveLayout = true;
    const auto& enemies = value.at("enemies");
    if (!enemies.is_array() || enemies.empty() || enemies.size() > 32) throw std::runtime_error("敵は1～32体にしてください。");
    int cores = 0;
    for (const auto& e : enemies)
    {
        EnemySpawnData spawn;
        spawn.enemyId = e.at("enemy_id").get<std::string>();
        const auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto& def) { return def.id == spawn.enemyId; });
        if (found == catalog.end()) throw std::runtime_error("未登録の敵種類です：" + spawn.enemyId);
        if (!e.at("x").is_number() || !e.at("z").is_number()) throw std::runtime_error("座標は数値で指定してください。");
        float x = e.at("x").get<float>(), z = e.at("z").get<float>();
        if (!std::isfinite(x) || !std::isfinite(z)) throw std::runtime_error("座標が有限値ではありません。");
        spawn.enemyData = *found;
        spawn.position = {x, TableConfig::FIELD_HEIGHT, z};
        spawn.enemyData.initPosition = spawn.position;
        stage.enemies.push_back(spawn);
        cores += spawn.enemyId == "enemy_boss_core";
    }
    if (cores > 1 || (cores == 1 && stage.stageType != StageType::Boss) || (cores == 0 && stage.stageType == StageType::Boss))
        throw std::runtime_error("最終ボスステージにはArmor球を1体配置してください（他の種別には配置できません）。");
    return stage;
}

StageLayoutEditor::Json StageLayoutEditor::Inspect(const Json& value, const std::vector<EnemyData>& catalog, float playerRadius)
{
    Json report = {{"valid", false}, {"errors", Json::array()}, {"metrics", Json::object()}};
    try
    {
        const auto stage = Decode(value, catalog);
        double closestGap = 10000, closestPlayer = 10000; int hp = 0, chains = 0, lines = 0;
        const float hw = TableConfig::GetFieldWidth() * .5f, hd = TableConfig::GetFieldDepth() * .5f;
        for (size_t i = 0; i < stage.enemies.size(); ++i)
        {
            const auto& a = stage.enemies[i]; const float r = a.enemyData.status.radius;
            const std::string prefix = "#" + std::to_string(i + 1) + " ";
            if (!(r > 0) || !std::isfinite(r) || std::abs(a.position.x) + r >= hw || std::abs(a.position.z) + r >= hd)
                report["errors"].push_back(prefix + "球が壁に接触・はみ出しています。");
            float playerDistance = std::hypot(a.position.x, a.position.z);
            double playerGap = playerDistance - r - playerRadius;
            closestPlayer = (std::min)(closestPlayer, playerGap);
            if (playerGap <= 0) report["errors"].push_back(prefix + "通常の自球開始位置と重なっています。");
            for (const auto& pocket : TableConfig::GetPocketCenters())
                if (std::hypot(a.position.x - pocket.x, a.position.z - pocket.z) <= r + TableConfig::POCKET_RADIUS)
                    report["errors"].push_back(prefix + "ポケットに近すぎます。");
            hp += a.enemyData.maxHp;
            for (size_t k = 0; k < i; ++k)
            {
                const auto& b = stage.enemies[k];
                double gap = std::hypot(a.position.x - b.position.x, a.position.z - b.position.z) - r - b.enemyData.status.radius;
                closestGap = (std::min)(closestGap, gap);
                if (gap <= 0) report["errors"].push_back(prefix + "他の敵球と重なっています。");
                if (gap > 0 && gap <= 8) ++chains;
                // Geometric proxy only: same forward ray from the starting position.
                float bDistance = std::hypot(b.position.x, b.position.z);
                if (playerDistance > 0 && bDistance > 0 && a.position.x * b.position.x + a.position.z * b.position.z > 0 &&
                    std::abs(a.position.x * b.position.z - a.position.z * b.position.x) / (std::max)(playerDistance, bDistance) < (std::min)(r, b.enemyData.status.radius)) ++lines;
            }
        }
        report["valid"] = report["errors"].empty();
        report["metrics"] = {{"enemy_count", stage.enemies.size()}, {"total_base_hp", hp}, {"minimum_player_gap", closestPlayer},
            {"minimum_enemy_gap", stage.enemies.size() > 1 ? Json(closestGap) : Json(nullptr)}, {"nearby_collision_pairs", chains}, {"pierce_aligned_pairs", lines},
            {"note", "幾何的な目安。貫通は開始位置からの同方向の並び、重量は表面間8以内の組数。勝率・実際の衝突やアンカーの有効性は試遊で確認してください。"}};
    }
    catch (const std::exception& e) { report["errors"].push_back(e.what()); }
    return report;
}

void StageLayoutEditor::Replace(const Json& value)
{
    if (draft == value) return;
    if (!draft.is_null()) { undo.push_back(draft); if (undo.size() > 64) undo.erase(undo.begin()); }
    redo.clear(); draft = value; ++revision; selected = -1;
}
void StageLayoutEditor::Undo(bool forward)
{
    auto& from = forward ? redo : undo; auto& to = forward ? undo : redo;
    if (from.empty()) return;
    to.push_back(draft); draft = from.back(); from.pop_back(); ++revision; selected = -1;
}
void StageLayoutEditor::Load(const std::filesystem::path& path, const StageData& stage)
{
    auto bytes = ReadSource(path); Replace(Encode(stage)); sourceBytes = std::move(bytes);
    proposal = nullptr; undo.clear(); redo.clear(); ++revision;
}
void StageLayoutEditor::Save(const std::filesystem::path& path, const std::vector<EnemyData>& catalog, float playerRadius)
{
    auto report = Inspect(draft, catalog, playerRadius);
    if (!report["valid"].get<bool>()) throw std::runtime_error("配置のエラーを解消してから保存してください。");
    const auto current = ReadSource(path);
    if (current != sourceBytes) throw std::runtime_error("元ファイルが外部で変更されました。既存ステージを読み直してください。");
    auto root = Json::parse(current); auto& stages = root.at("stages");
    if (!stages.is_array()) throw std::runtime_error("stages配列がありません。");
    auto stage = Decode(draft, catalog);
    Json* target = nullptr;
    for (auto& entry : stages) if (entry.at("id") == stage.id)
    {
        if (target) throw std::runtime_error("同名ステージが重複しています。");
        target = &entry;
    }
    if (!target) { stages.push_back(Json::object()); target = &stages.back(); }
    (*target)["id"] = stage.id; (*target)["stageType"] = ToString(stage.stageType);
    (*target)["difficulty"] = stage.difficulty; (*target)["par"] = stage.par; (*target)["preserveLayout"] = true;
    (*target)["enemies"] = Json::array();
    for (const auto& e : stage.enemies) (*target)["enemies"].push_back({{"enemyId", e.enemyId}, {"position", {e.position.x, e.position.y, e.position.z}}});
    const auto output = root.dump(2) + "\n";
    auto temporary = path; temporary += L".editor.tmp";
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << output; file.close(); if (!file) throw std::runtime_error("書き込みに失敗しました。"); }
    auto backup = path; backup += L".editor.bak";
    // ReplaceFile keeps the prior file as a backup and never exposes partial JSON.
    if (!ReplaceFileW(path.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr))
        throw std::runtime_error("ステージファイルを更新できませんでした。");
    sourceBytes = output;
}
void StageLayoutEditor::Propose(const Json& value, std::uint64_t expected, const std::vector<EnemyData>& catalog, float playerRadius)
{
    if (dragging || expected != revision) throw std::runtime_error("配置が変更されています。最新のrevisionを取得してください。");
    if (!Inspect(value, catalog, playerRadius)["valid"].get<bool>()) throw std::runtime_error("配置案にエラーがあります。検証結果を確認してください。");
    proposal = Encode(Decode(value, catalog)); proposalRevision = revision;
}
bool StageLayoutEditor::Accept()
{
    if (proposal.is_null() || proposalRevision != revision || dragging) return false;
    Replace(proposal); proposal = nullptr; return true;
}
StageLayoutEditor::Json StageLayoutEditor::Snapshot(const std::vector<EnemyData>& catalog, float playerRadius) const
{
    Json definitions = Json::array();
    Json pockets = Json::array();
    for (const auto& p : TableConfig::GetPocketCenters()) pockets.push_back({{"x", p.x}, {"z", p.z}});
    for (const auto& e : catalog) definitions.push_back({{"enemy_id", e.id}, {"radius", e.status.radius}, {"hp", e.maxHp}, {"mass", e.status.mass}});
    return {{"revision", revision}, {"draft", draft}, {"report", Inspect(draft, catalog, playerRadius)}, {"proposal", proposal},
        {"proposal_is_current", !proposal.is_null() && proposalRevision == revision}, {"catalog", definitions},
        {"bounds", {{"half_width", TableConfig::GetFieldWidth() * .5f}, {"half_depth", TableConfig::GetFieldDepth() * .5f}, {"player_x", 0}, {"player_z", 0}, {"player_radius", playerRadius}, {"pocket_radius", TableConfig::POCKET_RADIUS}, {"pockets", pockets}}},
        {"workflow", "validate_stage_layout → propose_stage_layout(expected_revision) → 画面でAI案を採用 → 試遊／保存。AIはファイルを直接変更しません。"}};
}
