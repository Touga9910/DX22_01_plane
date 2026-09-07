#include "Game.h"
#pragma execution_character_set("utf-8")
#include "imgui/imgui.h"
#include "EnemyText.h"
#include <cmath>
#include <cstdio>

float Game::StageEditorPlayerRadius() const
{
    float radius = 2.4f;
    for (const auto& ball : m_DebugBallCatalog) radius = (std::max)(radius, ball.status.radius);
    for (const auto& ball : m_DebugSetup.deck) radius = (std::max)(radius, ball.status.radius);
    return radius;
}

void Game::TestStageEditorLayout()
{
    const auto report = StageLayoutEditor::Inspect(m_StageEditor.draft, m_DebugEnemyCatalog, StageEditorPlayerRadius());
    if (!report["valid"].get<bool>()) { m_DebugMessage = "配置のエラーを解消してから試遊してください。"; return; }
    const auto stage = StageLayoutEditor::Decode(m_StageEditor.draft, m_DebugEnemyCatalog);
    m_DebugSetup.enemies.clear();
    for (const auto& e : stage.enemies) m_DebugSetup.enemies.push_back({e, e.enemyData.maxHp});
    m_DebugSetup.playerPosition = {0, TableConfig::FIELD_HEIGHT, 0};
    m_DebugRequest = 1;
}

void Game::DrawStageEditor()
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
        auto next = editor.draft; next["id"] = id; next["stage_type"] = "normal"; next["enemies"] = Json::array(); editor.Replace(next); editor.proposal = nullptr;
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
    if (ImGui::Combo("種別", &type, types, 3)) { next["stage_type"] = type == 0 ? "normal" : type == 1 ? "midBoss" : "boss"; change(next); }
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
        ImGui::Checkbox("追加モード（空いている場所をクリック）", &editor.placing);
    }
    ImGui::Checkbox("2単位のグリッドに吸着", &editor.snap);
    ImGui::TextWrapped("敵をドラッグして移動。白球は通常の開始位置。黄色の輪はAI案です。");
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
    int hit = -1;
    for (int i = 0; i < static_cast<int>(editor.draft["enemies"].size()); ++i)
    {
        const auto& e = editor.draft["enemies"][i]; float x = e["x"], z = e["z"], r = radiusFor(e);
        auto position = screen(x, z);
        draw->AddCircleFilled(position, r * scale, editor.selected == i ? IM_COL32(240, 145, 65, 255) : IM_COL32(85, 170, 235, 255));
        char number[16]; snprintf(number, sizeof(number), "%d", i + 1);
        draw->AddText(ImVec2(position.x - 4, position.y - 7), IM_COL32(0, 0, 0, 255), number);
        if (std::hypot(mx - x, mz - z) < r + 1) hit = i;
    }
    if (hovered && hit >= 0) ImGui::SetTooltip("#%d %s", hit + 1, EnemyLabel(editor.draft["enemies"][hit]["enemy_id"].get<std::string>()));
    if (!editor.proposal.is_null()) for (size_t i = 0; i < editor.proposal["enemies"].size(); ++i)
    {
        const auto& e = editor.proposal["enemies"][i]; const float x = e["x"], z = e["z"], r = radiusFor(e);
        const auto position = screen(x, z);
        draw->AddCircle(position, r * scale, IM_COL32(255, 225, 70, 255), 32, 2);
        const auto label = "AI" + std::to_string(i + 1);
        draw->AddText(ImVec2(position.x + r * scale, position.y), IM_COL32(255, 225, 70, 255), label.c_str());
        if (hovered && std::hypot(mx - x, mz - z) < r + 1) ImGui::SetTooltip("AI #%d %s", static_cast<int>(i + 1), EnemyLabel(e["enemy_id"].get<std::string>()));
    }
    if (hovered && ImGui::IsMouseClicked(0))
    {
        editor.selected = hit;
        if (hit >= 0) { editor.dragging = true; editor.dragStart = editor.draft; }
        else if (editor.placing && !m_DebugEnemyCatalog.empty() && editor.draft["enemies"].size() < 32)
        {
            auto added = editor.draft;
            if (editor.snap) { mx = std::round(mx / 2) * 2; mz = std::round(mz / 2) * 2; }
            added["enemies"].push_back({{"enemy_id", m_DebugEnemyCatalog[editor.palette].id}, {"x", mx}, {"z", mz}});
            editor.Replace(added); editor.selected = static_cast<int>(added["enemies"].size()) - 1;
        }
    }
    if (editor.dragging && editor.selected >= 0)
    {
        auto& e = editor.draft["enemies"][editor.selected]; float r = radiusFor(e) + .01f;
        if (editor.snap) { mx = std::round(mx / 2) * 2; mz = std::round(mz / 2) * 2; }
        if (ImGui::IsMouseDown(0)) { e["x"] = std::clamp(mx, -hw + r, hw - r); e["z"] = std::clamp(mz, -hd + r, hd - r); }
        else
        {
            editor.dragging = false; auto moved = editor.draft; editor.draft = editor.dragStart; change(moved);
        }
    }
    if (editor.selected >= static_cast<int>(editor.draft["enemies"].size())) editor.selected = -1;
    if (editor.selected >= 0)
    {
        ImGui::Text("選択中 #%d : %s", editor.selected + 1, EnemyLabel(editor.draft["enemies"][editor.selected]["enemy_id"].get<std::string>()));
        if (ImGui::Button("選択した敵を削除")) { auto edited = editor.draft; edited["enemies"].erase(editor.selected); editor.Replace(edited); }
        ImGui::SameLine();
        if (ImGui::Button("選択した敵を複製") && editor.selected >= 0 && editor.draft["enemies"].size() < 32)
        {
            auto edited = editor.draft; auto copy = edited["enemies"][editor.selected]; copy["x"] = copy["x"].get<float>() + 8; edited["enemies"].push_back(copy); editor.Replace(edited); editor.selected = static_cast<int>(edited["enemies"].size()) - 1;
        }
    }
    auto report = StageLayoutEditor::Inspect(editor.draft, m_DebugEnemyCatalog, StageEditorPlayerRadius());
    for (const auto& error : report["errors"]) ImGui::TextColored(ImVec4(1, .5f, .3f, 1), "%s", error.get<std::string>().c_str());
    if (report["metrics"].contains("total_base_hp"))
    {
        const auto& metrics = report["metrics"];
        ImGui::Text("敵%d体 / 基礎HP合計%d / 貫通の並び%d組 / 衝突しやすい近接%d組（目安）", static_cast<int>(editor.draft["enemies"].size()), metrics["total_base_hp"].get<int>(), metrics["pierce_aligned_pairs"].get<int>(), metrics["nearby_collision_pairs"].get<int>());
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
    ImGui::TextWrapped("保存対象は敵の種類・配置とステージ情報です。デバッグ専用のHP・性能変更は保存されません。試遊には現在のデッキを使います。");
    if (!editor.message.empty()) ImGui::TextWrapped("%s", editor.message.c_str());
    ImGui::EndChild();
}
