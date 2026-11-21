#pragma execution_character_set("utf-8")
#include "DebugRunPreset.h"
#include "PlayerBallSaveData.h"
#include "PlayerBallText.h"
#include <Windows.h>
#include <fstream>
#include <set>
#include <stdexcept>

namespace
{
    using Json = nlohmann::json;
    constexpr std::uintmax_t MaximumBytes = 4 * 1024 * 1024;
    constexpr size_t MaximumPresets = 64;
    const char* Destinations[] = {"route", "battle", "midboss", "shop", "rest", "boss_preparation", "final_boss"};

    void ValidateName(const std::string& name)
    {
        if (name.empty() || name.size() > 128 ||
            std::all_of(name.begin(), name.end(), [](unsigned char c) { return c == ' '; }) ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("プリセット名は空白だけを除き、128バイト以内で指定してください。");
    }

    Json ReadStore(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path))
            return {{"format", "dx22_debug_run_presets"}, {"version", 1}, {"presets", Json::object()}};
        if (std::filesystem::file_size(path) > MaximumBytes)
            throw std::runtime_error("プリセットファイルが大きすぎます（最大4MB）。");
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("プリセットファイルを開けません。");
        Json value; file >> value;
        if (value.at("format") != "dx22_debug_run_presets" ||
            !value.at("version").is_number_integer() || value.at("version") != 1 ||
            !value.at("presets").is_object() || value.at("presets").size() > MaximumPresets)
            throw std::runtime_error("未対応または不正なランプリセット一覧です。");
        for (const auto& entry : value.at("presets").items()) ValidateName(entry.key());
        return value;
    }

    void WriteStore(const std::filesystem::path& path, const Json& value)
    {
        const auto bytes = value.dump(2) + "\n";
        if (bytes.size() > MaximumBytes) throw std::runtime_error("保存後のファイルが4MBを超えます。");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        auto temporary = path; temporary += L".tmp";
        { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << bytes;
          file.close(); if (!file) throw std::runtime_error("プリセットを書き込めませんでした。"); }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("プリセットファイルを更新できませんでした。");
    }
}

std::vector<DebugRunPresetComparisonRow> CompareDebugRunPresets(
    const DebugRunPreset& before, const DebugRunPreset& after)
{
    std::vector<DebugRunPresetComparisonRow> rows;
    auto row = [&](const std::string& key, const std::string& label,
        const std::string& a, const std::string& b)
    {
        rows.push_back({key, label, a, b, a != b});
    };
    const char* destinations[] = {"ルート選択", "通常戦", "中ボス戦", "ショップ", "休憩所", "最終ボス前の休憩", "最終ボス戦"};
    auto progress = [](const DebugRunPreset& p) {
        return std::to_string(p.run.destination >= DebugRunDestination::Preparation ? kNormalRouteAreaGoal : p.run.completedAreas) + " / 15";
    };
    row("progress", "進行度", progress(before), progress(after));
    row("destination", "移動先", destinations[static_cast<int>(before.run.destination)], destinations[static_cast<int>(after.run.destination)]);
    row("stage", "ステージ", before.run.stageId.empty() ? "進行度から自動選択" : before.run.stageId,
        after.run.stageId.empty() ? "進行度から自動選択" : after.run.stageId);
    row("combat_mode", "抽選ルール", before.run.combatMode == RunCombatMode::PhaseTest ? "フェーズ制試遊" : "本編",
        after.run.combatMode == RunCombatMode::PhaseTest ? "フェーズ制試遊" : "本編");
    row("infinite_money", "ゴールド無限", before.run.infiniteMoney ? "ON" : "OFF", after.run.infiniteMoney ? "ON" : "OFF");
    row("money", "保存所持金", std::to_string(before.battle.money), std::to_string(after.battle.money));
    row("seed", "乱数シード", std::to_string(before.battle.seed), std::to_string(after.battle.seed));
    row("hp", "HP / 基礎最大HP", std::to_string(before.battle.hp) + " / " + std::to_string(before.battle.maxHp),
        std::to_string(after.battle.hp) + " / " + std::to_string(after.battle.maxHp));
    row("ascension", "アセンション", std::to_string(before.battle.selectedAscension), std::to_string(after.battle.selectedAscension));
    row("deck_count", "デッキ個数", std::to_string(before.battle.deck.size()), std::to_string(after.battle.deck.size()));
    auto ballJson = [](const PlayerBallData& ball) {
        auto value = PlayerBallSaveData::BallToJson(ball);
        value.erase("instance_id");
        return value;
    };
    auto ballName = [](const PlayerBallData& ball) {
        return std::string(PlayerBallText::GetName(ball.definitionId)) + " +" + std::to_string(ball.upgradeLevel) + " [" + ball.definitionId + "]";
    };
    for (size_t i = 0; i < (std::max)(before.battle.deck.size(), after.battle.deck.size()); ++i)
    {
        const auto* a = i < before.battle.deck.size() ? &before.battle.deck[i] : nullptr;
        const auto* b = i < after.battle.deck.size() ? &after.battle.deck[i] : nullptr;
        const auto key = "deck/" + std::to_string(i);
        const auto label = "デッキ #" + std::to_string(i + 1);
        row(key, label, a ? ballName(*a) : "なし（追加）", b ? ballName(*b) : "なし（削除）");
        // Same ball type: expose every changed saved performance/upgrade parameter.
        if (a && b && a->definitionId == b->definitionId)
        {
            const auto left = ballJson(*a).flatten(), right = ballJson(*b).flatten();
            rows.back().changed = left != right;
            std::set<std::string> paths;
            for (const auto& entry : left.items()) paths.insert(entry.key());
            for (const auto& entry : right.items()) paths.insert(entry.key());
            for (const auto& path : paths)
            {
                const auto av = left.contains(path) ? left.at(path).dump() : "なし";
                const auto bv = right.contains(path) ? right.at(path).dump() : "なし";
                if (av != bv) row(key + path, label + " " + path, av, bv);
            }
        }
    }
    for (const auto& relic : RelicCatalog)
    {
        const auto index = static_cast<size_t>(relic.type);
        row("relic/" + std::to_string(index), std::string("レリック：") + relic.name,
            before.battle.relics[index] ? "所持" : "なし", after.battle.relics[index] ? "所持" : "なし");
    }
    return rows;
}

nlohmann::json DebugRunPreset::ToJson() const
{
    const int destination = static_cast<int>(run.destination);
    if (destination < 0 || destination >= static_cast<int>(std::size(Destinations)))
        throw std::runtime_error("移動先が不正です。");
    return {{"format", "dx22_debug_run_preset"}, {"version", 1},
        {"run", {{"completed_areas", run.completedAreas}, {"destination", Destinations[destination]},
            {"combat_mode", ToString(run.combatMode)}, {"stage_id", run.stageId},
            {"infinite_money", run.infiniteMoney}}},
        {"battle_setup", battle.ToJson()}};
}

DebugRunPreset DebugRunPreset::FromJson(const nlohmann::json& value,
    const std::vector<PlayerBallData>& balls, const std::vector<EnemyData>& enemies,
    const std::vector<StageData>& stages)
{
    if (value.at("format") != "dx22_debug_run_preset" ||
        !value.at("version").is_number_integer() || value.at("version") != 1)
        throw std::runtime_error("未対応のランプリセット形式です。");
    DebugRunPreset next;
    const auto& run = value.at("run");
    const auto& area = run.at("completed_areas");
    if (!area.is_number_integer() || area.get<double>() < 0 || area.get<double>() > kNormalRouteAreaGoal)
        throw std::runtime_error("進行度は0～15の整数で指定してください。");
    next.run.completedAreas = area.get<int>();
    const auto destination = run.at("destination").get<std::string>();
    const auto found = std::find(std::begin(Destinations), std::end(Destinations), destination);
    if (found == std::end(Destinations)) throw std::runtime_error("移動先が不正です。");
    next.run.destination = static_cast<DebugRunDestination>(found - std::begin(Destinations));
    if (next.run.completedAreas == kNormalRouteAreaGoal &&
        next.run.destination != DebugRunDestination::Preparation && next.run.destination != DebugRunDestination::FinalBoss)
        throw std::runtime_error("進行度15では最終準備または最終ボスを指定してください。");
    const auto mode = run.at("combat_mode").get<std::string>();
    if (mode != "legacy" && mode != "phase_test") throw std::runtime_error("ステージ抽選ルールが不正です。");
    next.run.combatMode = RunCombatModeFromString(mode);
    next.run.infiniteMoney = run.at("infinite_money").get<bool>();
    next.run.stageId = run.at("stage_id").get<std::string>();
    if (!next.run.stageId.empty())
    {
        const bool battle = next.run.destination == DebugRunDestination::Battle ||
            next.run.destination == DebugRunDestination::MidBoss || next.run.destination == DebugRunDestination::FinalBoss;
        const StageType type = next.run.destination == DebugRunDestination::FinalBoss ? StageType::Boss :
            next.run.destination == DebugRunDestination::MidBoss ? StageType::MidBoss : StageType::Normal;
        if (!battle || std::none_of(stages.begin(), stages.end(), [&](const StageData& stage) {
            return stage.id == next.run.stageId && stage.stageType == type;
        })) throw std::runtime_error("指定ステージが存在しないか、移動先の戦闘種別と一致しません。");
    }
    next.battle = DebugBattleSetup::FromJson(value.at("battle_setup"), balls, enemies);
    return next;
}

std::vector<std::string> DebugRunPresetStore::List(const std::filesystem::path& path)
{
    const auto value = ReadStore(path);
    std::vector<std::string> names;
    for (const auto& entry : value.at("presets").items()) names.push_back(entry.key());
    return names;
}

nlohmann::json DebugRunPresetStore::Load(const std::filesystem::path& path, const std::string& name)
{
    ValidateName(name);
    const auto value = ReadStore(path);
    const auto found = value.at("presets").find(name);
    if (found == value.at("presets").end()) throw std::runtime_error("その名前のプリセットは保存されていません。");
    return *found;
}

void DebugRunPresetStore::Save(const std::filesystem::path& path, const std::string& name,
    const nlohmann::json& preset)
{
    ValidateName(name);
    auto value = ReadStore(path);
    auto& presets = value.at("presets");
    if (!presets.contains(name) && presets.size() >= MaximumPresets)
        throw std::runtime_error("プリセットは64件まで保存できます。");
    presets[name] = preset;
    WriteStore(path, value);
}

void DebugRunPresetStore::Rename(const std::filesystem::path& path, const std::string& oldName,
    const std::string& newName)
{
    ValidateName(oldName);
    ValidateName(newName);
    auto value = ReadStore(path);
    auto& presets = value.at("presets");
    if (!presets.contains(oldName)) throw std::runtime_error("変更元のプリセットが見つかりません。");
    if (oldName == newName) return;
    if (presets.contains(newName)) throw std::runtime_error("その名前は既に使われています。別の名前を指定してください。");
    const auto content = presets.at(oldName);
    presets.erase(oldName);
    presets[newName] = content;
    WriteStore(path, value);
}

void DebugRunPresetStore::Duplicate(const std::filesystem::path& path, const std::string& sourceName,
    const std::string& newName)
{
    ValidateName(sourceName);
    ValidateName(newName);
    auto value = ReadStore(path);
    auto& presets = value.at("presets");
    if (!presets.contains(sourceName)) throw std::runtime_error("複製元のプリセットが見つかりません。");
    if (presets.contains(newName)) throw std::runtime_error("その名前は既に使われています。別の複製名を指定してください。");
    if (presets.size() >= MaximumPresets) throw std::runtime_error("プリセットは64件まで保存できます。");
    const auto copy = presets.at(sourceName);
    presets[newName] = copy;
    WriteStore(path, value);
}

void DebugRunPresetStore::Delete(const std::filesystem::path& path, const std::string& name)
{
    ValidateName(name);
    auto value = ReadStore(path);
    if (value.at("presets").erase(name) == 0)
        throw std::runtime_error("削除するプリセットが見つかりません。");
    WriteStore(path, value);
}
