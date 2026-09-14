#include "Game.h"
#pragma execution_character_set("utf-8")
#include "imgui/imgui.h"
#include "EnemyText.h"
#include <cmath>
#include <cstdio>

namespace
{
    constexpr int BreakBallSelection(int index) { return -index - 2; }
    constexpr int SelectedBreakBall(int selection) { return selection <= -2 ? -selection - 2 : -1; }

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

    int FindStatusEffectIndex(const nlohmann::json& enemy, StatusEffectType type)
    {
        return FindEffectIndex(enemy, "status_effects", type);
    }
}

// Stage Editor Player Radius の処理を実行する。
float GameDebugController::StageEditorPlayerRadius() const
{
    float radius = 2.4f;
    for (const auto& ball : m_DebugBallCatalog) radius = (std::max)(radius, ball.status.radius);
    for (const auto& ball : m_DebugSetup.deck) radius = (std::max)(radius, ball.status.radius);
    return radius;
}

// Test Stage Editor Layout の処理を実行する。
void GameDebugController::TestStageEditorLayout()
{
    const auto report = StageLayoutEditor::Inspect(m_StageEditor.draft, m_DebugEnemyCatalog, StageEditorPlayerRadius());
    if (!report["valid"].get<bool>()) { m_DebugMessage = "配置のエラーを解消してから試遊してください。"; return; }
    const auto stage = StageLayoutEditor::Decode(m_StageEditor.draft, m_DebugEnemyCatalog);
    m_DebugSetup.enemies.clear();
    for (const auto& e : stage.enemies) m_DebugSetup.enemies.push_back({e, e.enemyData.maxHp});
    m_DebugSetup.breakBallPositions = stage.breakBallPositions;
    m_DebugSetup.playerPosition = {0, TableConfig::FIELD_HEIGHT, 0};
    m_DebugRequest = 1;
}

// Stage Editorを描画する。
void GameDebugController::DrawStageEditor(Game&)
{
    using Json = nlohmann::json;
    auto& editor = m_StageEditor;
    if (editor.draft.is_null()) { ImGui::TextUnformatted("既存ステージを読み込めませんでした。"); return; }
    auto change = [&](Json next) { int selection = editor.selected; editor.Replace(next); editor.selected = selection; };
    ImGui::BeginChild("stage_properties", ImVec2(285, 0), true);
    if (ImGui::BeginCombo("読み込む", editor.draft["id"].get<std::string>().c_str()))
    {
        for (const auto& stage : m_DebugStages) if (ImGui::Selectable(stage.id.c_str()))
        {
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
        auto next = editor.draft; next["id"] = id; next["stage_type"] = "normal";
        next["enemies"] = Json::array(); next["break_balls"] = Json::array();
        editor.Replace(next); editor.proposal = nullptr;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.undo.empty()); if (ImGui::Button("元に戻す")) editor.Undo(false); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(editor.redo.empty()); if (ImGui::Button("やり直す")) editor.Undo(true); ImGui::EndDisabled();
    auto next = editor.draft;
    const auto id = next["id"].get<std::string>();
    if (!ImGui::IsAnyItemActive()) snprintf(editor.name, sizeof(editor.name), "%s", id.c_str());
    if (ImGui::InputText("ステージ名", editor.name, sizeof(editor.name))) { next["id"] = editor.name; change(next); }
    int type = next["stage_type"] == "normal" ? 0 : (next["stage_type"] == "boss" ? 2 : 1);
    const char* types[] = {"通常", "中ボス", "最終ボス"};
    if (ImGui::Combo("種別", &type, types, 3))
    {
        next["stage_type"] = type == 0 ? "normal" : type == 1 ? "midBoss" : "boss";
        if (type == 2 && (!next.contains("break_balls") || next["break_balls"].empty()))
            next["break_balls"] = {{{"x", -4.0f}, {"z", 10.0f}}, {{"x", 4.0f}, {"z", 10.0f}}};
        if (type != 2) next["break_balls"] = Json::array();
        editor.placingBreakBall = false;
        change(next);
    }
    int difficulty = next["difficulty"], par = next["par"];
    if (ImGui::SliderInt("難易度", &difficulty, 1, type == 0 ? 3 : 99)) { next["difficulty"] = difficulty; change(next); }
    if (ImGui::SliderInt("基準ショット数", &par, 1, 99)) { next["par"] = par; change(next); }
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
    }
    ImGui::BeginDisabled(type != 2 || editor.draft["break_balls"].size() >= 16);
    if (ImGui::Checkbox("ブレイクボールの追加モード", &editor.placingBreakBall) && editor.placingBreakBall)
        editor.placing = false;
    ImGui::EndDisabled();
    if (type != 2) editor.placingBreakBall = false;
    ImGui::Checkbox("2単位のグリッドに吸着", &editor.snap);
    ImGui::TextWrapped("青：敵球 / 黄：ブレイクボール / 白：自球開始位置。球をドラッグして移動できます。");
    ImGui::TextWrapped("追加モード中は、空いている場所をクリックして配置します。黄色の輪はAI案です。");
    ImGui::TextWrapped("既存ステージの読み込み・新規作成は、現在の下書きを置き換えます。必要な配置は先に保存してください。");
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("stage_placement", ImVec2(0, 0), false);

    const float width = (std::max)(200.0f, (std::min)(ImGui::GetContentRegionAvail().x - 8, 600.0f));
    const float scale = width / TableConfig::GetFieldWidth();
    const float height = TableConfig::GetFieldDepth() * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float hw = TableConfig::GetFieldWidth() * .5f, hd = TableConfig::GetFieldDepth() * .5f;
    auto screen = [&](float x, float z) { return ImVec2(origin.x + (x + hw) * scale, origin.y + (hd - z) * scale); };
    auto* draw = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("stage_canvas", ImVec2(width, height));
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
    for (int i = 0; i < static_cast<int>(editor.draft["enemies"].size()); ++i)
    {
        const auto& e = editor.draft["enemies"][i]; float x = e["x"], z = e["z"], r = radiusFor(e);
        auto position = screen(x, z);
        draw->AddCircleFilled(position, r * scale, editor.selected == i ? IM_COL32(240, 145, 65, 255) : IM_COL32(85, 170, 235, 255));
        char number[16]; snprintf(number, sizeof(number), "%d", i + 1);
        draw->AddText(ImVec2(position.x - 4, position.y - 7), IM_COL32(0, 0, 0, 255), number);
        if (std::hypot(mx - x, mz - z) < r + 1) enemyHit = i;
    }
    constexpr float breakBallRadius = 2.5f;
    int breakBallHit = -1;
    for (int i = 0; i < static_cast<int>(editor.draft["break_balls"].size()); ++i)
    {
        const auto& ball = editor.draft["break_balls"][i];
        const float x = ball["x"], z = ball["z"];
        const auto position = screen(x, z);
        const bool selected = editor.selected == BreakBallSelection(i);
        draw->AddCircleFilled(position, breakBallRadius * scale,
            selected ? IM_COL32(255, 145, 35, 255) : IM_COL32(255, 215, 35, 255));
        draw->AddCircle(position, breakBallRadius * scale,
            selected ? IM_COL32_WHITE : IM_COL32(110, 75, 0, 255), 24, selected ? 3.0f : 2.0f);
        const auto label = "B" + std::to_string(i + 1);
        draw->AddText(ImVec2(position.x - 7, position.y - 7), IM_COL32(20, 20, 10, 255), label.c_str());
        if (std::hypot(mx - x, mz - z) < breakBallRadius + 1) breakBallHit = i;
    }
    if (hovered && breakBallHit >= 0)
        ImGui::SetTooltip("ブレイクボール #%d（%.1f, %.1f）", breakBallHit + 1,
            editor.draft["break_balls"][breakBallHit]["x"].get<float>(),
            editor.draft["break_balls"][breakBallHit]["z"].get<float>());
    else if (hovered && enemyHit >= 0)
        ImGui::SetTooltip("#%d %s", enemyHit + 1, EnemyLabel(editor.draft["enemies"][enemyHit]["enemy_id"].get<std::string>()));
    if (!editor.proposal.is_null()) for (size_t i = 0; i < editor.proposal["enemies"].size(); ++i)
    {
        const auto& e = editor.proposal["enemies"][i]; const float x = e["x"], z = e["z"], r = radiusFor(e);
        const auto position = screen(x, z);
        draw->AddCircle(position, r * scale, IM_COL32(255, 225, 70, 255), 32, 2);
        const auto label = "AI" + std::to_string(i + 1);
        draw->AddText(ImVec2(position.x + r * scale, position.y), IM_COL32(255, 225, 70, 255), label.c_str());
        if (hovered && std::hypot(mx - x, mz - z) < r + 1) ImGui::SetTooltip("AI #%d %s", static_cast<int>(i + 1), EnemyLabel(e["enemy_id"].get<std::string>()));
    }
    if (!editor.proposal.is_null() && editor.proposal.contains("break_balls"))
        for (size_t i = 0; i < editor.proposal["break_balls"].size(); ++i)
        {
            const auto& ball = editor.proposal["break_balls"][i];
            const auto position = screen(ball["x"].get<float>(), ball["z"].get<float>());
            draw->AddCircle(position, breakBallRadius * scale, IM_COL32(255, 120, 230, 255), 24, 2.5f);
            const auto label = "AI B" + std::to_string(i + 1);
            draw->AddText(ImVec2(position.x + breakBallRadius * scale, position.y),
                IM_COL32(255, 120, 230, 255), label.c_str());
        }
    if (hovered && ImGui::IsMouseClicked(0))
    {
        editor.selected = breakBallHit >= 0 ? BreakBallSelection(breakBallHit) : enemyHit;
        if (breakBallHit >= 0 || enemyHit >= 0)
        {
            editor.dragging = true;
            editor.dragStart = editor.draft;
        }
        else if (editor.placingBreakBall && editor.draft["stage_type"] == "boss" &&
            editor.draft["break_balls"].size() < 16)
        {
            auto added = editor.draft;
            if (editor.snap) { mx = std::round(mx / 2) * 2; mz = std::round(mz / 2) * 2; }
            const float r = breakBallRadius + .01f;
            added["break_balls"].push_back({{"x", std::clamp(mx, -hw + r, hw - r)},
                {"z", std::clamp(mz, -hd + r, hd - r)}});
            editor.Replace(added);
            editor.selected = BreakBallSelection(static_cast<int>(added["break_balls"].size()) - 1);
        }
        else if (editor.placing && !m_DebugEnemyCatalog.empty() && editor.draft["enemies"].size() < 32)
        {
            auto added = editor.draft;
            if (editor.snap) { mx = std::round(mx / 2) * 2; mz = std::round(mz / 2) * 2; }
            added["enemies"].push_back({{"enemy_id", m_DebugEnemyCatalog[editor.palette].id}, {"x", mx}, {"z", mz}});
            editor.Replace(added); editor.selected = static_cast<int>(added["enemies"].size()) - 1;
        }
    }
    if (editor.dragging && editor.selected != -1)
    {
        const int breakIndex = SelectedBreakBall(editor.selected);
        auto& entry = breakIndex >= 0
            ? editor.draft["break_balls"][breakIndex]
            : editor.draft["enemies"][editor.selected];
        const float r = breakIndex >= 0 ? breakBallRadius + .01f : radiusFor(entry) + .01f;
        if (editor.snap) { mx = std::round(mx / 2) * 2; mz = std::round(mz / 2) * 2; }
        if (ImGui::IsMouseDown(0))
        {
            entry["x"] = std::clamp(mx, -hw + r, hw - r);
            entry["z"] = std::clamp(mz, -hd + r, hd - r);
        }
        else
        {
            editor.dragging = false; auto moved = editor.draft; editor.draft = editor.dragStart; change(moved);
        }
    }
    if (editor.selected >= static_cast<int>(editor.draft["enemies"].size())) editor.selected = -1;
    const int selectedBreakBall = SelectedBreakBall(editor.selected);
    if (selectedBreakBall >= static_cast<int>(editor.draft["break_balls"].size())) editor.selected = -1;
    if (editor.selected >= 0)
    {
        ImGui::Text("選択中 #%d : %s", editor.selected + 1, EnemyLabel(editor.draft["enemies"][editor.selected]["enemy_id"].get<std::string>()));
        ImGui::SeparatorText("初期バフ・デバフ");
        ImGui::TextWrapped("この敵だけが戦闘開始時から持つ効果です。効果量は攻撃力／防御力の増減値です。");
        for (const StatusEffectType effectType : AllStatusEffectTypes)
        {
            const auto& selectedEnemy = editor.draft["enemies"][editor.selected];
            const int effectIndex = FindStatusEffectIndex(selectedEnemy, effectType);
            bool enabled = effectIndex >= 0;
            ImGui::PushID(ToString(effectType));
            if (ImGui::Checkbox(StatusEffectLabel(effectType), &enabled))
            {
                auto edited = editor.draft;
                auto& enemy = edited["enemies"][editor.selected];
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
                    edited["enemies"][editor.selected]["status_effects"][effectIndex]["magnitude"] = magnitude;
                    change(edited);
                }
            }
            ImGui::PopID();
        }
        const auto& selectedEnemyForGimmick =
            editor.draft["enemies"][editor.selected];
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
                edited["enemies"][editor.selected]["nuisance_status_effects"] =
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
                edited["enemies"][editor.selected]["nuisance_status_effects"] =
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
        if (ImGui::Button("選択した敵を削除")) { auto edited = editor.draft; edited["enemies"].erase(editor.selected); editor.Replace(edited); }
        ImGui::SameLine();
        if (ImGui::Button("選択した敵を複製") && editor.selected >= 0 && editor.draft["enemies"].size() < 32)
        {
            auto edited = editor.draft; auto copy = edited["enemies"][editor.selected]; copy["x"] = copy["x"].get<float>() + 8; edited["enemies"].push_back(copy); editor.Replace(edited); editor.selected = static_cast<int>(edited["enemies"].size()) - 1;
        }
    }
    else if (SelectedBreakBall(editor.selected) >= 0)
    {
        const int index = SelectedBreakBall(editor.selected);
        const auto& ball = editor.draft["break_balls"][index];
        ImGui::Text("選択中 B%d : ブレイクボール (%.1f, %.1f)",
            index + 1, ball["x"].get<float>(), ball["z"].get<float>());
        if (ImGui::Button("選択したブレイクボールを削除"))
        {
            auto edited = editor.draft;
            edited["break_balls"].erase(index);
            editor.Replace(edited);
        }
        ImGui::SameLine();
        if (ImGui::Button("選択したブレイクボールを複製") &&
            editor.draft["break_balls"].size() < 16)
        {
            auto edited = editor.draft;
            auto copy = edited["break_balls"][index];
            copy["x"] = copy["x"].get<float>() + 8.0f;
            edited["break_balls"].push_back(copy);
            editor.Replace(edited);
            editor.selected = BreakBallSelection(static_cast<int>(edited["break_balls"].size()) - 1);
        }
    }
    auto report = StageLayoutEditor::Inspect(editor.draft, m_DebugEnemyCatalog, StageEditorPlayerRadius());
    for (const auto& error : report["errors"]) ImGui::TextColored(ImVec4(1, .5f, .3f, 1), "%s", error.get<std::string>().c_str());
    if (report["metrics"].contains("total_base_hp"))
    {
        const auto& metrics = report["metrics"];
        ImGui::Text("敵%d体 / ブレイク%d個 / 基礎HP合計%d / 貫通の並び%d組 / 近接%d組（目安）",
            static_cast<int>(editor.draft["enemies"].size()), metrics["break_ball_count"].get<int>(),
            metrics["total_base_hp"].get<int>(), metrics["pierce_aligned_pairs"].get<int>(),
            metrics["nearby_collision_pairs"].get<int>());
    }
    if (!editor.proposal.is_null())
    {
        ImGui::TextWrapped("AI案：%s / %s / 難易度%d / 基準%dショット / %d体", editor.proposal["id"].get<std::string>().c_str(),
            editor.proposal["stage_type"] == "boss" ? "最終ボス" : editor.proposal["stage_type"] == "normal" ? "通常" : "中ボス",
            editor.proposal["difficulty"].get<int>(), editor.proposal["par"].get<int>(), static_cast<int>(editor.proposal["enemies"].size()));
        ImGui::BeginDisabled(editor.proposalRevision != editor.revision || editor.dragging);
        if (ImGui::Button("AI案を採用")) { editor.Accept(); editor.message = "AI案を編集中の配置へ採用しました。保存は別操作です。"; }
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("AI案を破棄")) editor.proposal = nullptr;
        if (!editor.proposal.is_null() && editor.proposalRevision != editor.revision) ImGui::TextUnformatted("編集後の古いAI案です。最新の配置で再提案してください。");
    }
    ImGui::BeginDisabled(!report["valid"].get<bool>() || editor.dragging);
    if (ImGui::Button("この配置で試遊")) TestStageEditorLayout();
    ImGui::SameLine();
    bool existing = std::any_of(m_DebugStages.begin(), m_DebugStages.end(), [&](const auto& s) { return s.id == editor.draft["id"].get<std::string>(); });
    if (ImGui::Button(existing ? "同名ステージに上書き保存" : "新しいステージとして保存"))
    {
        try { editor.Save("assets/data/stage_01.json", m_DebugEnemyCatalog, StageEditorPlayerRadius()); m_DebugStages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json"); editor.message = "保存しました。次のステージ生成から使用されます。直前のファイルは .editor.bak に保存しました。"; }
        catch (const std::exception& e) { editor.message = e.what(); }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("保存対象は敵の種類・配置・初期バフ／デバフ、ブレイクボールの初期配置、ステージ情報です。デバッグ専用のHP・性能変更は保存されません。試遊には現在のデッキを使います。");
    if (!editor.message.empty()) ImGui::TextWrapped("%s", editor.message.c_str());
    ImGui::EndChild();
}
