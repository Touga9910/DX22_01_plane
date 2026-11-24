#pragma execution_character_set("utf-8")
#include "GameDebugController.h"
#include "json/json.hpp"
#include "imgui/imgui.h"
#include "EnemyText.h"
#include "TableGeometry.h"
#include <cmath>
#include <cstdio>

namespace
{

    /// <summary>
    /// 状態効果の種類に対応するステージエディターの表示名を返す。
    /// </summary>
    const char* StatusEffectLabel(StatusEffectType type)
    {
        switch (type)
        {
        case StatusEffectType::AttackUp: return "攻撃上昇（赤↑）";
        case StatusEffectType::AttackDown: return "攻撃低下（赤↓）";
        case StatusEffectType::DefenseUp: return "防御上昇（青↑）";
        case StatusEffectType::DefenseDown: return "防御低下（青↓）";
        default: return "不明な状態効果";
        }
    }

    /// <summary>
    /// 敵データの指定した配列から状態効果を探し、一致する要素の位置を返す。
    /// </summary>
    int FindEffectIndex(
        const nlohmann::json& enemy,
        const char* field,
        StatusEffectType type)
    {
        if (!enemy.contains(field) || !enemy[field].is_array()) return -1;
        const auto& effects = enemy[field];
        for (int index = 0; index < static_cast<int>(effects.size()); ++index)
        {
            if (effects[index].value("type", std::string()) == ToString(type)) return index;
        }
        return -1;
    }

    /// <summary>
    /// 敵の状態効果一覧から指定された種類の位置を返す。
    /// </summary>
    int FindStatusEffectIndex(const nlohmann::json& enemy, StatusEffectType type)
    {
        return FindEffectIndex(enemy, "status_effects", type);
    }
}

/// <summary>
/// 既定の半径とデバッグ用デッキの球の半径を比較し、配置検証に使う最大値を返す。
/// </summary>
float GameDebugController::StageEditorPlayerRadius() const
{
    float radius = 2.4f;
    for (const auto& ball : m_DebugSetup.deck) radius = (std::max)(radius, ball.status.radius);
    return radius;
}

/// <summary>
/// 編集中の配置を検証し、問題がなければその配置のデバッグ試遊を要求する。
/// </summary>
void GameDebugController::TestStageEditorLayout()
{
    if (m_StageEditor.dragging) { m_DebugMessage = "ドラッグを終了してから適用してください。"; return; }
    const auto report = StageLayoutEditor::Inspect(m_StageEditor.draft, m_DebugEnemyCatalog, StageEditorPlayerRadius());
    if (!report["valid"].get<bool>()) { m_DebugMessage = "配置のエラーを解消してから試遊してください。"; return; }
    const StageData stage = m_StageEditor.BuildPlaytestStage(m_DebugEnemyCatalog);
    m_DebugStageOverride = stage;
    m_DebugUseStageLayout = true;
    m_DebugSetup.enemyValidation.enabled = false;
    m_DebugSetup.enemies.clear();
    for (const auto& e : stage.enemies) m_DebugSetup.enemies.push_back({e, e.enemyData.maxHp});
    m_DebugSetup.breakBallPositions.clear();
    m_DebugSetup.playerPosition = {0, TableConfig::FIELD_HEIGHT, 0};
    m_DebugRequest = m_DebugRunActive ? 5 : 1;
}

/// <summary>
/// ステージの読み込み、配置編集、検証、試遊に使うデバッグ画面を描画する。
/// </summary>
void GameDebugController::DrawStageEditor(Game&)
{
    using Json = nlohmann::json;
    auto& editor = m_StageEditor;
    if (editor.draft.is_null()) { ImGui::TextUnformatted("既存ステージを読み込めませんでした。"); return; }
    auto change = [&](Json next) { int selection = editor.selected; editor.Replace(next); editor.selected = selection; };
    ImGui::BeginChild("stage_properties", ImVec2(285, 0), true);
    const auto preview = StageLayoutEditor::DisplayName(editor.draft);
    if (ImGui::BeginCombo("読み込む", preview.c_str()))
    {
        for (const auto& stage : m_DebugStages)
        {
            const auto label = stage.DisplayName() + "###" + stage.id;
            if (!ImGui::Selectable(label.c_str())) continue;
            try
            {
                // Refresh both the selected stage and conflict baseline from disk together.
                auto fresh = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
                auto found = std::find_if(fresh.begin(), fresh.end(), [&](const auto& s) { return s.id == stage.id; });
                if (found == fresh.end()) throw std::runtime_error("ステージが見つかりません。");
                editor.Load("assets/data/stage_01.json", *found); editor.message = "ステージを読み込みました。";
            }
            catch (const std::exception& e) { editor.message = e.what(); }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("新規ステージ"))
    {
        int suffix = 1; std::string id;
        do { id = "custom_" + std::to_string(suffix++); }
        while (std::any_of(m_DebugStages.begin(), m_DebugStages.end(), [&](const auto& s) { return s.id == id; }));
        Json next = {{"id", id}, {"stage_type", "normal"}, {"difficulty", 1}, {"par", 4},
            {"enemies", Json::array()}, {"break_balls", Json::array()},
            {"turns_per_phase", PhaseBattleData{}.turnsPerPhase},
            {"additional_phases", Json::array({Json::array()})}};
        editor.Replace(next);
        editor.activePhase = 0;
        editor.proposal = nullptr;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.undo.empty()); if (ImGui::Button("元に戻す")) editor.Undo(false); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(editor.redo.empty()); if (ImGui::Button("やり直す")) editor.Undo(true); ImGui::EndDisabled();
    auto next = editor.draft;
    if (!ImGui::IsAnyItemActive())
    {
        snprintf(editor.name, sizeof(editor.name), "%s", next.value("name", std::string{}).c_str());
        snprintf(editor.stageId, sizeof(editor.stageId), "%s", next.at("id").get<std::string>().c_str());
    }
    if (ImGui::InputTextWithHint("ステージ名", "敵数は自動で付加", editor.name, sizeof(editor.name)))
    {
        next["name"] = editor.name;
        change(next);
    }
    if (ImGui::InputText("ステージID", editor.stageId, sizeof(editor.stageId)))
    {
        next["id"] = editor.stageId;
        change(next);
    }
    int type = next["stage_type"] == "normal" ? 0 : (next["stage_type"] == "boss" ? 2 : 1);
    const char* types[] = {"通常", "中ボス", "最終ボス"};
    if (ImGui::Combo("種別", &type, types, 3))
    {
        next["stage_type"] = type == 0 ? "normal" : type == 1 ? "midBoss" : "boss";
        if (type != 2) next["break_balls"] = Json::array();
        editor.placingBreakBall = false;
        change(next);
    }
    int difficulty = next["difficulty"], par = next["par"];
    if (ImGui::SliderInt("難易度", &difficulty, 1, type == 0 ? 3 : 99)) { next["difficulty"] = difficulty; change(next); }
    if (ImGui::SliderInt("基準ショット数", &par, 1, 99)) { next["par"] = par; change(next); }
    if (!next.contains("additional_phases") && ImGui::Button("フェーズ戦闘にする"))
    {
        next["turns_per_phase"] = PhaseBattleData{}.turnsPerPhase;
        next["additional_phases"] = Json::array({Json::array()});
        editor.Replace(next);
    }
    if (next.contains("additional_phases"))
    {
        ImGui::SeparatorText("フェーズ戦闘");
        int turns = next["turns_per_phase"];
        if (ImGui::SliderInt("進行間隔（ターン）", &turns, 1, 99, "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            next["turns_per_phase"] = turns;
            change(next);
        }
        ImGui::TextWrapped("設定ターン数の経過、または盤上の敵の全滅で次のフェーズに進みます。");
        auto& phases = next["additional_phases"];
        editor.activePhase = std::clamp(editor.activePhase, 0, static_cast<int>(phases.size()));
        const std::string current = "フェーズ " + std::to_string(editor.activePhase + 1);
        if (ImGui::BeginCombo("編集する配置", current.c_str()))
        {
            for (int i = 0; i <= static_cast<int>(phases.size()); ++i)
            {
                const std::string label = "フェーズ " + std::to_string(i + 1);
                if (ImGui::Selectable(label.c_str(), editor.activePhase == i))
                {
                    editor.activePhase = i;
                    editor.selected = -1;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("フェーズを追加"))
        {
            phases.push_back(Json::array());
            editor.Replace(next);
            editor.activePhase = static_cast<int>(phases.size());
        }
        if (editor.activePhase > 0 && phases.size() > 1)
        {
            ImGui::SameLine();
            if (ImGui::Button("フェーズを削除"))
            {
                phases.erase(editor.activePhase - 1);
                editor.Replace(next);
                --editor.activePhase;
            }
        }
    }
    else
    {
        editor.activePhase = 0;
    }
    if (!m_DebugEnemyCatalog.empty())
    {
        editor.palette = std::clamp(editor.palette, 0, static_cast<int>(m_DebugEnemyCatalog.size()) - 1);
        if (ImGui::BeginCombo("配置する敵", EnemyLabel(m_DebugEnemyCatalog[editor.palette].id)))
        {
            for (int i = 0; i < static_cast<int>(m_DebugEnemyCatalog.size()); ++i)
                if (ImGui::Selectable(EnemyLabel(m_DebugEnemyCatalog[i].id), editor.palette == i)) editor.palette = i;
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("敵の追加モード", &editor.placing) && editor.placing)
            editor.placingBreakBall = false;
        ImGui::Button("敵をドラッグして追加");
        if (ImGui::IsItemActivated()) m_DebugEnemyDragCatalog = editor.palette;
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("DEBUG_ENEMY_CATALOG", &editor.palette, sizeof(editor.palette));
            ImGui::TextUnformatted(EnemyLabel(m_DebugEnemyCatalog[editor.palette].id));
            ImGui::EndDragDropSource();
        }
    }
    if (type == 2)
        ImGui::TextWrapped("最終ボスの取り巻きは本編と同じルールで自動生成されます。旧ブレイクボール座標は使用しません。");
    ImGui::Checkbox("2単位のグリッドに吸着", &editor.snap);
    ImGui::TextWrapped("青：敵球 / 白：自球開始位置。球をドラッグして移動できます。");
    ImGui::TextWrapped("追加モード中は、空いている場所をクリックして配置します。黄色の輪はAI案です。");
    ImGui::TextWrapped("既存ステージの読み込み・新規作成は、現在の下書きを置き換えます。必要な配置は先に保存してください。");
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("stage_placement", ImVec2(0, 0), false);

    auto phaseEnemies = [&](Json& layout) -> Json&
    {
        return editor.activePhase == 0
            ? layout.at("enemies")
            : layout.at("additional_phases").at(editor.activePhase - 1);
    };
    const float width = (std::max)(200.0f, (std::min)(ImGui::GetContentRegionAvail().x - 8, 600.0f));
    const float scale = width / TableConfig::GetFieldWidth();
    const float height = TableConfig::GetFieldDepth() * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float hw = TableConfig::GetFieldWidth() * .5f, hd = TableConfig::GetFieldDepth() * .5f;
    auto screen = [&](float x, float z) { return ImVec2(origin.x + (x + hw) * scale, origin.y + (hd - z) * scale); };
    auto* draw = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("stage_canvas", ImVec2(width, height));
    int droppedEnemy = -1;
    if (ImGui::IsMouseReleased(0) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        droppedEnemy = m_DebugEnemyDragCatalog;
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload("DEBUG_ENEMY_CATALOG"))
            droppedEnemy = *static_cast<const int*>(payload->Data);
        ImGui::EndDragDropTarget();
    }
    if (droppedEnemy >= 0 && droppedEnemy < static_cast<int>(m_DebugEnemyCatalog.size()) && phaseEnemies(editor.draft).size() < 32)
    {
        const auto& mouse = ImGui::GetIO().MousePos;
        float x = (mouse.x - origin.x) / scale - hw, z = hd - (mouse.y - origin.y) / scale;
        if (editor.snap) { x = std::round(x / 2) * 2; z = std::round(z / 2) * 2; }
        const float radius = m_DebugEnemyCatalog[droppedEnemy].status.radius + .01f;
        auto added = editor.draft;
        phaseEnemies(added).push_back({{"enemy_id", m_DebugEnemyCatalog[droppedEnemy].id},
            {"x", std::clamp(x, -hw + radius, hw - radius)},
            {"z", std::clamp(z, -hd + radius, hd - radius)}});
        editor.Replace(added);
        editor.selected = static_cast<int>(phaseEnemies(added).size()) - 1;
    }
    if (ImGui::IsMouseReleased(0)) m_DebugEnemyDragCatalog = -1;
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(20, 74, 64, 255));
    for (float x = -60; x <= 60; x += 10) draw->AddLine(screen(x, -hd), screen(x, hd), IM_COL32(80, 120, 110, 100));
    for (float z = -30; z <= 30; z += 10) draw->AddLine(screen(-hw, z), screen(hw, z), IM_COL32(80, 120, 110, 100));
    for (const auto& pocket : TableConfig::GetPocketCenters()) draw->AddCircleFilled(screen(pocket.x, pocket.z), TableConfig::POCKET_RADIUS * scale, IM_COL32(10, 15, 15, 255));
    draw->AddCircleFilled(screen(0, 0), StageEditorPlayerRadius() * scale, IM_COL32(230, 240, 255, 255));
    draw->AddText(screen(0, -4), IM_COL32_WHITE, "START");
    auto radiusFor = [&](const Json& enemy) {
        auto found = std::find_if(m_DebugEnemyCatalog.begin(), m_DebugEnemyCatalog.end(), [&](const auto& e) { return e.id == enemy["enemy_id"].get<std::string>(); });
        return found == m_DebugEnemyCatalog.end() ? 2.4f : found->status.radius;
    };
    const auto& mouse = ImGui::GetIO().MousePos;
    float mx = (mouse.x - origin.x) / scale - hw, mz = hd - (mouse.y - origin.y) / scale;
    const bool hovered = ImGui::IsItemHovered();
    int enemyHit = -1;
    for (int i = 0; i < static_cast<int>(phaseEnemies(editor.draft).size()); ++i)
    {
        const auto& e = phaseEnemies(editor.draft)[i]; float x = e["x"], z = e["z"], r = radiusFor(e);
        auto position = screen(x, z);
        draw->AddCircleFilled(position, r * scale, editor.selected == i ? IM_COL32(240, 145, 65, 255) : IM_COL32(85, 170, 235, 255));
        char number[16]; snprintf(number, sizeof(number), "%d", i + 1);
        draw->AddText(ImVec2(position.x - 4, position.y - 7), IM_COL32(0, 0, 0, 255), number);
        if (std::hypot(mx - x, mz - z) < r + 1) enemyHit = i;
    }
    if (hovered && enemyHit >= 0)
        ImGui::SetTooltip("#%d %s", enemyHit + 1, EnemyLabel(phaseEnemies(editor.draft)[enemyHit]["enemy_id"].get<std::string>()));
    const Json* proposedEnemies = nullptr;
    if (!editor.proposal.is_null() &&
        (editor.activePhase == 0 || (editor.proposal.contains("additional_phases") &&
            editor.proposal.at("additional_phases").size() >= static_cast<std::size_t>(editor.activePhase))))
        proposedEnemies = &phaseEnemies(editor.proposal);
    for (std::size_t i = 0; proposedEnemies && i < proposedEnemies->size(); ++i)
    {
        const auto& e = (*proposedEnemies)[i]; const float x = e["x"], z = e["z"], r = radiusFor(e);
        const auto position = screen(x, z);
        draw->AddCircle(position, r * scale, IM_COL32(255, 225, 70, 255), 32, 2);
        const auto label = "AI" + std::to_string(i + 1);
        draw->AddText(ImVec2(position.x + r * scale, position.y), IM_COL32(255, 225, 70, 255), label.c_str());
        if (hovered && std::hypot(mx - x, mz - z) < r + 1) ImGui::SetTooltip("AI #%d %s", static_cast<int>(i + 1), EnemyLabel(e["enemy_id"].get<std::string>()));
    }
    if (hovered && ImGui::IsMouseClicked(0))
    {
        editor.selected = enemyHit;
        if (enemyHit >= 0)
        {
            editor.dragging = true;
            editor.dragStart = editor.draft;
        }
        else if (editor.placing && !m_DebugEnemyCatalog.empty() && phaseEnemies(editor.draft).size() < 32)
        {
            auto added = editor.draft;
            if (editor.snap) { mx = std::round(mx / 2) * 2; mz = std::round(mz / 2) * 2; }
            phaseEnemies(added).push_back({{"enemy_id", m_DebugEnemyCatalog[editor.palette].id}, {"x", mx}, {"z", mz}});
            editor.Replace(added); editor.selected = static_cast<int>(phaseEnemies(added).size()) - 1;
        }
    }
    if (editor.dragging && editor.selected != -1)
    {
        auto& entry = phaseEnemies(editor.draft)[editor.selected];
        const float r = radiusFor(entry) + .01f;
        if (editor.snap) { mx = std::round(mx / 2) * 2; mz = std::round(mz / 2) * 2; }
        // Include the release position even if the final move/release share a frame.
        entry["x"] = std::clamp(mx, -hw + r, hw - r);
        entry["z"] = std::clamp(mz, -hd + r, hd - r);
        if (!ImGui::IsMouseDown(0))
        {
            editor.dragging = false; auto moved = editor.draft; editor.draft = editor.dragStart; change(moved);
        }
    }
    if (editor.selected >= static_cast<int>(phaseEnemies(editor.draft).size())) editor.selected = -1;
    if (editor.selected >= 0)
    {
        ImGui::Text("選択中 #%d : %s", editor.selected + 1, EnemyLabel(phaseEnemies(editor.draft)[editor.selected]["enemy_id"].get<std::string>()));
        ImGui::SeparatorText("初期バフ・デバフ");
        ImGui::TextWrapped("この敵が出現時から持つ効果です。効果量は攻撃力／防御力の増減値です。");
        for (const StatusEffectType effectType : AllStatusEffectTypes)
        {
            const auto& selectedEnemy = phaseEnemies(editor.draft)[editor.selected];
            const int effectIndex = FindStatusEffectIndex(selectedEnemy, effectType);
            bool enabled = effectIndex >= 0;
            ImGui::PushID(ToString(effectType));
            if (ImGui::Checkbox(StatusEffectLabel(effectType), &enabled))
            {
                auto edited = editor.draft;
                auto& enemy = phaseEnemies(edited)[editor.selected];
                if (!enemy.contains("status_effects") || !enemy["status_effects"].is_array())
                    enemy["status_effects"] = Json::array();
                const int existingIndex = FindStatusEffectIndex(enemy, effectType);
                if (enabled && existingIndex < 0)
                    enemy["status_effects"].push_back({{"type", ToString(effectType)}, {"magnitude", 1}});
                else if (!enabled && existingIndex >= 0)
                    enemy["status_effects"].erase(existingIndex);
                if (enemy["status_effects"].empty()) enemy.erase("status_effects");
                change(edited);
                ImGui::PopID();
                continue;
            }
            if (enabled)
            {
                int magnitude = selectedEnemy["status_effects"][effectIndex].value("magnitude", 1);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::DragInt("効果量", &magnitude, 0.1f, 1, StatusEffectCollection::MaxMagnitude, "%d", ImGuiSliderFlags_AlwaysClamp))
                {
                    auto edited = editor.draft;
                    phaseEnemies(edited)[editor.selected]["status_effects"][effectIndex]["magnitude"] = magnitude;
                    change(edited);
                }
            }
            ImGui::PopID();
        }
        const auto& selectedEnemyForGimmick =
            phaseEnemies(editor.draft)[editor.selected];
        const auto selectedDefinition = std::find_if(
            m_DebugEnemyCatalog.begin(),
            m_DebugEnemyCatalog.end(),
            [&](const EnemyData& data)
            {
                return data.id == selectedEnemyForGimmick["enemy_id"].get<std::string>();
            });
        if (selectedDefinition != m_DebugEnemyCatalog.end() &&
            selectedDefinition->nuisanceBall.enabled)
        {
            ImGui::SeparatorText("お邪魔ボール");
            ImGui::TextWrapped("場にある間、選択したデバフを自球に与えます。");
            StatusEffectType selectedType = StatusEffectType::AttackDown;
            int magnitude = 1;
            if (selectedEnemyForGimmick.contains("nuisance_status_effects") &&
                selectedEnemyForGimmick["nuisance_status_effects"].is_array() &&
                !selectedEnemyForGimmick["nuisance_status_effects"].empty())
            {
                const auto& effect = selectedEnemyForGimmick["nuisance_status_effects"].front();
                TryParseStatusEffectType(effect.value("type", std::string()), selectedType);
                magnitude = effect.value("magnitude", magnitude);
            }
            else
            {
                if (selectedDefinition->nuisanceBall.debuffs.Has(StatusEffectType::DefenseDown))
                    selectedType = StatusEffectType::DefenseDown;
                magnitude = selectedDefinition->nuisanceBall.debuffs.GetMagnitude(selectedType);
                magnitude = (std::max)(1, magnitude);
            }

            int debuffIndex = selectedType == StatusEffectType::DefenseDown ? 1 : 0;
            const char* debuffs[] = {"攻撃低下", "防御低下"};
            if (ImGui::Combo("付与デバフ", &debuffIndex, debuffs, 2))
            {
                auto edited = editor.draft;
                phaseEnemies(edited)[editor.selected]["nuisance_status_effects"] =
                    Json::array({{
                        {"type", ToString(debuffIndex == 0
                            ? StatusEffectType::AttackDown
                            : StatusEffectType::DefenseDown)},
                        {"magnitude", magnitude}
                    }});
                change(edited);
            }
            if (ImGui::DragInt("デバフ効果量", &magnitude, 0.1f, 1,
                StatusEffectCollection::MaxMagnitude, "%d", ImGuiSliderFlags_AlwaysClamp))
            {
                auto edited = editor.draft;
                phaseEnemies(edited)[editor.selected]["nuisance_status_effects"] =
                    Json::array({{
                        {"type", ToString(debuffIndex == 0
                            ? StatusEffectType::AttackDown
                            : StatusEffectType::DefenseDown)},
                        {"magnitude", magnitude}
                    }});
                change(edited);
            }
            ImGui::TextDisabled("初回%dターン / 再生成%dターン / 出現の1ターン前に予告",
                selectedDefinition->nuisanceBall.initialDelayTurns,
                selectedDefinition->nuisanceBall.respawnDelayTurns);
        }
        ImGui::Separator();
        if (ImGui::Button("選択した敵を削除")) { auto edited = editor.draft; phaseEnemies(edited).erase(editor.selected); editor.Replace(edited); }
        ImGui::SameLine();
        if (ImGui::Button("選択した敵を複製") && editor.selected >= 0 && phaseEnemies(editor.draft).size() < 32)
        {
            auto edited = editor.draft; auto copy = phaseEnemies(edited)[editor.selected]; copy["x"] = copy["x"].get<float>() + 8; phaseEnemies(edited).push_back(copy); editor.Replace(edited); editor.selected = static_cast<int>(phaseEnemies(edited).size()) - 1;
        }
    }
    auto report = StageLayoutEditor::Inspect(editor.draft, m_DebugEnemyCatalog, StageEditorPlayerRadius());
    for (const auto& error : report["errors"]) ImGui::TextColored(ImVec4(1, .5f, .3f, 1), "%s", error.get<std::string>().c_str());
    const auto& metrics = report.contains("phase_metrics")
        ? report.at("phase_metrics").at(editor.activePhase) : report.at("metrics");
    if (metrics.contains("total_base_hp"))
    {
        ImGui::Text("配置敵%d体 / 旧ブレイク%d個（不使用） / 基礎HP合計%d / 貫通の並び%d組 / 近接%d組（目安）",
            static_cast<int>(phaseEnemies(editor.draft).size()), metrics["legacy_break_ball_count"].get<int>(),
            metrics["total_base_hp"].get<int>(), metrics["pierce_aligned_pairs"].get<int>(),
            metrics["nearby_collision_pairs"].get<int>());
    }
    if (!editor.proposal.is_null())
    {
        ImGui::TextWrapped("AI案：%s / %s / 難易度%d / 基準%dショット / %d体", StageLayoutEditor::DisplayName(editor.proposal).c_str(),
            editor.proposal["stage_type"] == "boss" ? "最終ボス" : editor.proposal["stage_type"] == "normal" ? "通常" : "中ボス",
            editor.proposal["difficulty"].get<int>(), editor.proposal["par"].get<int>(), static_cast<int>(editor.proposal["enemies"].size()));
        ImGui::BeginDisabled(editor.proposalRevision != editor.revision || editor.dragging);
        if (ImGui::Button("AI案を採用")) { editor.Accept(); editor.message = "AI案を編集中の配置へ採用しました。保存は別操作です。"; }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("AI案を破棄")) editor.proposal = nullptr;
        if (!editor.proposal.is_null() && editor.proposalRevision != editor.revision) ImGui::TextUnformatted("編集後の古いAI案です。最新の配置で再提案してください。");
    }
    ImGui::BeginDisabled(!report["valid"].get<bool>() || editor.dragging);
    if (ImGui::Button("配置とデッキを適用して最初から開始")) TestStageEditorLayout();
    ImGui::SameLine();
    bool existing = std::any_of(m_DebugStages.begin(), m_DebugStages.end(), [&](const auto& s) { return s.id == editor.draft["id"].get<std::string>(); });
    if (ImGui::Button(existing ? "既存ステージに上書き保存" : "新しいステージとして保存"))
    {
        try { editor.Save("assets/data/stage_01.json", m_DebugEnemyCatalog, StageEditorPlayerRadius()); m_DebugStages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json"); editor.message = "保存しました。次のステージ生成から使用されます。直前のファイルは .editor.bak に保存しました。"; }
        catch (const std::exception& e) { editor.message = e.what(); }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("敵の配置と初期効果、フェーズの進行間隔を保存します。デバッグ専用のHP・性能変更は保存されません。試遊には現在のデッキを使います。");
    if (!editor.message.empty()) ImGui::TextWrapped("%s", editor.message.c_str());
    ImGui::EndChild();
}
