#include "Game.h"
#pragma execution_character_set("utf-8")
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameStageOverrideState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "BossShotPlanner.h"
#include "GameDebugController.h"
#include "GameDebugHost.h"
#include "json/json.hpp"
#include "GameUi.h"
#include "GameMcpBridge.h"
#include "BalanceLogger.h"
#include "EnemyBall.h"
#include "PlayerBall.h"
#include "PlayerBallText.h"
#include "EnemyText.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

namespace
{
    const char* BallLabel(const std::string& id)
    {
        return PlayerBallText::GetName(id);
    }
    void EditStatus(BallStatus& s, bool includeDefense)
    {
        ImGui::SliderInt("攻撃", &s.attack, 0, 999, "%d", ImGuiSliderFlags_AlwaysClamp);
		if (includeDefense)
			ImGui::SliderInt("防御", &s.defense, 0, 999, "%d", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("質量", &s.mass, 0.1f, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("半径", &s.radius, 0.1f, 12, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("反発係数", &s.restitution, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("摩擦", &s.friction, 0.001f, 0.2f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("押し出し倍率", &s.knockbackTransfer, 0, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (s.abilities.pierce)
        {
            ImGui::SliderInt("貫通回数", &s.pierceMaxUses, 0, 16);
            ImGui::SliderFloat("貫通後の速度維持率", &s.pierceSpeedRetention, 0, 1);
        }
        if (s.abilities.anchor)
        {
            ImGui::SliderFloat("停止への減速倍率", &s.anchorBrakeMultiplier, 1, 5);
            ImGui::SliderFloat("停止速度の二乗", &s.anchorStopSpeedSquared, 0.03f, 1);
            ImGui::Checkbox("停止中の押し戻し無効", &s.anchorKnockbackImmune);
        }
    }
    const std::filesystem::path PresetPath = "saves/debug_battle_setup.json";
}

bool GameDebugHost::CanOpenEditor(const Game& game)
{
    return game.GetCurrentSceneType() == SceneType::Title;
}

DebugRunSnapshot GameDebugHost::CaptureRunSnapshot(const Game& game)
{
    DebugRunSnapshot snapshot;
    snapshot.progressionProfile = game.m_PersistenceRuntime->Profile();
    snapshot.autoPlayEnabled = game.m_BalanceRuntime->IsAutoPlayEnabled();
    snapshot.validationEnabled = game.m_BalanceRuntime->IsValidationEnabled();
    return snapshot;
}

void GameDebugHost::PrepareSession(
    Game& game,
    const DebugBattleSetup& setup)
{
    setup.ApplyProgressionTo(game.m_PersistenceRuntime->Profile());
    game.m_BalanceRuntime->SetAutoPlayEnabled(false);
    game.m_BalanceRuntime->SetValidationEnabled(false);
}

void GameDebugHost::ApplyRunSettings(
    Game& game,
    const DebugBattleSetup& setup,
    const std::vector<PlayerBallData>& ballCatalog)
{
    setup.ApplyProgressionTo(game.m_PersistenceRuntime->Profile());
    game.m_PersistenceRuntime->ActiveAscension() = setup.selectedAscension;
    std::vector<PlayerBallData> unlockedCatalog;
    for (const PlayerBallData& ball : ballCatalog)
    {
        if (game.m_PersistenceRuntime->Profile().IsBallUnlocked(ball.definitionId))
        {
            unlockedCatalog.push_back(ball);
        }
    }
    game.m_RunController->Deck().SetCatalog(unlockedCatalog);
    game.m_RunController->Deck().SetDefaultDeck(setup.deck);
    game.m_RunController->Deck().ResetToDefault();
    game.m_RunController->Status().maxHp = setup.EffectiveMaxHp();
    game.m_RunController->Status().currentHp = std::clamp(
        setup.hp,
        1,
        game.m_RunController->Status().maxHp);
    game.m_RunController->Status().money = setup.money;
    game.m_RunController->RestHealRatio() = (std::max)(
        0.05f,
        game.m_PersistenceRuntime->DefaultRestHealRatio() -
            ProgressionProfile::RestHealPenalty(
                game.m_PersistenceRuntime->ActiveAscension()));
    game.m_RunController->Relics() = setup.relics;
}

void GameDebugHost::StartBattle(
    Game& game,
    std::uint32_t seed,
    StageData stage)
{
    const StageType stageType = stage.stageType;
    game.m_BossShotPlanner->Reset();
    game.m_StageOverrideState->ClearQueued();
    game.StartNewRun("debug_sandbox", "manual_debug", "", "", seed);
    game.m_StageOverrideState->Queue(std::move(stage));
    game.StartNextBattle(stageType);
}

void GameDebugHost::EndSession(
    Game& game,
    const DebugRunSnapshot& snapshot)
{
    game.DeactivateRunSession();
	game.SetPaused(false);
    game.m_BalanceRuntime->SetAutoPlayEnabled(snapshot.autoPlayEnabled);
    game.m_BalanceRuntime->SetValidationEnabled(snapshot.validationEnabled);
    game.m_StageOverrideState->ClearQueued();
    game.m_BossShotPlanner->Reset();
    game.m_PersistenceRuntime->Profile() = snapshot.progressionProfile;
    game.m_PersistenceRuntime->ActiveAscension() = 0;
    game.LoadPlayerStatusFromJson();
}

void GameDebugHost::CaptureCurrentPlayerStatus(Game& game)
{
    game.CaptureCurrentPlayerStatus();
}

void GameDebugHost::SetPaused(Game& game, bool paused)
{
	game.SetPaused(paused);
}

void GameDebugHost::UpdateExternalBridge(Game& game)
{
    if (game.m_GameMcpBridge != nullptr)
    {
        game.m_GameMcpBridge->Update(game);
    }
}

float GameDebugHost::GetDefaultRestHealRatio(const Game& game)
{
    return game.m_PersistenceRuntime->DefaultRestHealRatio();
}

DebugAutomationSnapshot GameDebugHost::CaptureAutomation(const Game& game)
{
    DebugAutomationSnapshot snapshot;
    const GameBalanceAutomationSnapshot automation =
        game.m_BalanceRuntime->CaptureAutomationSnapshot();
    snapshot.enabled = automation.enabled;
    snapshot.stopAfterCurrentRunRequested =
        automation.stopAfterCurrentRunRequested;
    snapshot.stopAfterCurrentRunDefault =
        automation.stopAfterCurrentRunDefault;
    snapshot.runCount = automation.runCount;
    snapshot.maximumRuns = automation.maximumRuns;
    return snapshot;
}

void GameDebugHost::SetAutoPlayEnabled(Game& game, bool enabled)
{
    game.m_BalanceRuntime->SetAutoPlayEnabled(enabled);
}

void GameDebugHost::SetStopAfterCurrentRunRequested(
    Game& game,
    bool requested)
{
    game.m_BalanceRuntime->SetStopAfterCurrentRunRequested(requested);
}

void GameDebugHost::SaveSnapshot(Game& game)
{
    game.SaveDebugSnapshot();
}

// Debug Modeを開く。
void GameDebugController::Open(Game& game)
{
    if (!m_DebugMode && !GameDebugHost::CanOpenEditor(game)) return;
    if (m_DebugBallCatalog.empty())
    {
        m_DebugBallCatalog = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {}).ballDefinitions;
        m_DebugEnemyCatalog = StageDataLoader::LoadEnemyDefinitions("assets/data/enemy_data.json");
        m_DebugStages = StageDataLoader::LoadAll("assets/data/stage_01.json", "assets/data/enemy_data.json");
        m_DebugSetup.deck = PlayerBallDataLoader::LoadDeck("assets/data/player_deck.json", m_DebugBallCatalog);
        if (!m_DebugStages.empty())
            for (const auto& spawn : m_DebugStages.front().enemies)
                m_DebugSetup.enemies.push_back({spawn, spawn.enemyData.maxHp});
    }
    m_DebugEditorOpen = true;
    if (m_StageEditor.draft.is_null() && !m_DebugStages.empty())
    {
        try
        {
            m_StageEditor.Load("assets/data/stage_01.json", m_DebugStages.front());
            for (size_t i = 0; i < m_DebugEnemyCatalog.size(); ++i)
                if (m_DebugEnemyCatalog[i].id == "enemy_normal") m_StageEditor.palette = static_cast<int>(i);
        }
        catch (const std::exception& e) { m_DebugMessage = e.what(); }
    }
    Game::ResetFrameTiming();
}

// Debug Run Settingsを適用
void GameDebugController::ApplyRunSettings(Game& game)
{
    GameDebugHost::ApplyRunSettings(game, m_DebugSetup, m_DebugBallCatalog);
}

// Debug Battleを開始
bool GameDebugController::StartBattle(Game& game)
{
    m_DebugMessage = m_DebugSetup.Validate();
    if (!m_DebugMessage.empty()) return false;
    // 描画中にSceneを破棄しない。再戦でも前の球を破棄してから初期状態を作る。
    if (m_DebugMode) game.ChangeScene(SceneType::Title);
    m_DebugPreviousRun = GameDebugHost::CaptureRunSnapshot(game);
    GameDebugHost::PrepareSession(game, m_DebugSetup);
    m_DebugMode = true;
    m_DebugActiveSetup = m_DebugSetup;
    m_DebugEditorOpen = m_DebugBattleFinished = false;
    StageData stage;
    stage.id = "debug_battle";
    for (const auto& enemy : m_DebugSetup.enemies)
    {
        stage.enemies.push_back(enemy.spawn);
        // 編集した半径を見た目にも反映し、当たり判定だけが大きくならないようにする。
        const float radius = enemy.spawn.enemyData.status.radius;
        stage.enemies.back().enemyData.scale = {radius, radius, radius};
        if (enemy.spawn.enemyData.id == "enemy_boss_core") stage.stageType = StageType::Boss;
        else if (stage.stageType != StageType::Boss && enemy.spawn.enemyData.id.find("midboss") != std::string::npos)
            stage.stageType = StageType::MidBoss;
    }
    GameDebugHost::StartBattle(game, m_DebugSetup.seed, std::move(stage));
    for (auto* player : GameRuntime::World().GetComponents<PlayerBall>()) player->SetState(PlayerBall::State::Idle);
    BalanceLogger::GetInstance().RecordEvent("debug_battle_setup", m_DebugSetup.ToJson());
    m_DebugMessage = "設定した条件で戦闘を開始しました。";
    Game::ResetFrameTiming();
    return true;
}

// Debug Battle Playerを適用
void GameDebugController::ApplyBattlePlayer(PlayerBall* player)
{
    if (!m_DebugMode || !player) return;
    player->GetBall()->ResetAtPosition(m_DebugSetup.playerPosition);
    player->GetBall()->SetInitialPosition(m_DebugSetup.playerPosition);
}

// Debug Battle Enemyを適用
void GameDebugController::ApplyBattleEnemy(EnemyBall* enemy, std::size_t index)
{
    if (!m_DebugMode || !enemy || index >= m_DebugSetup.enemies.size()) return;
    const DebugBattleSetup::Enemy& configured = m_DebugSetup.enemies[index];
    const int configuredMaxHp = (std::max)(1, configured.spawn.enemyData.maxHp);
    const float hpRatio = std::clamp(
        static_cast<float>(configured.hp) / static_cast<float>(configuredMaxHp),
        0.0f,
        1.0f);
    const int scaledHp = std::clamp(
        static_cast<int>(std::lround(enemy->GetMaxHP() * hpRatio)),
        1,
        enemy->GetMaxHP());
    enemy->SetHP(scaledHp);
    enemy->SetDebugBossState(m_DebugSetup.armor, m_DebugSetup.breakShots);
}

// Debug Modeを終了
void GameDebugController::End(Game& game)
{
    if (!m_DebugMode) return;
    BalanceLogger::GetInstance().EndRun(
        "debug_closed",
        game.GetPlayerCurrentHp(),
        game.GetPlayerMaxHp(),
        0);
    m_DebugMode = m_DebugEditorOpen = m_DebugBattleFinished = false;
    if (m_DebugPreviousRun.has_value())
    {
        GameDebugHost::EndSession(game, *m_DebugPreviousRun);
        m_DebugPreviousRun.reset();
    }
}

// Debug Battleを終了
void GameDebugController::FinishBattle(Game& game, bool victory)
{
    if (m_DebugBattleFinished) return;
    GameDebugHost::CaptureCurrentPlayerStatus(game);
    const auto enemies = GameRuntime::World().GetComponents<EnemyBall>();
    const int defeated = static_cast<int>(std::count_if(enemies.begin(), enemies.end(), [](auto* e) { return e->IsDefeated(); }));
    auto& logger = BalanceLogger::GetInstance();
    const int currentHp = game.GetPlayerCurrentHp();
    const int maximumHp = game.GetPlayerMaxHp();
    logger.EndShot(currentHp, static_cast<int>(enemies.size()) - defeated, defeated);
    logger.EndStage(victory ? "clear" : "game_over", currentHp, maximumHp, defeated);
    logger.EndRun(victory ? "debug_clear" : "debug_game_over", currentHp, maximumHp, victory ? 1 : 0);
    m_DebugBattleFinished = m_DebugEditorOpen = true;
    GameDebugHost::SetPaused(game, false);
    m_DebugMessage = victory ? "戦闘クリア。条件を変えるか、同じ条件で再戦できます。" : "戦闘終了（HP 0）。条件を変えるか、同じ条件で再戦できます。";
    Game::ResetFrameTiming();
}

// Debug Modeを更新
bool GameDebugController::Update(Game& game)
{
    const int request = std::exchange(m_DebugRequest, 0);
    if (request == 1) { StartBattle(game); Game::ResetFrameTiming(); return true; }
    if (request == 2)
    {
        m_DebugEditorOpen = false;
        if (m_DebugMode) game.ChangeScene(SceneType::Title);
        Game::ResetFrameTiming(); return true;
    }
    if (m_DebugEditorOpen)
    {
        // 編集中も状態を公開するが、外部からの戦闘操作はBridge側で拒否
        GameDebugHost::UpdateExternalBridge(game);
        Game::ResetFrameTiming(); return true;
    }
    return false;
}

// Debug Presetを保存
void GameDebugController::SavePreset()
{
    m_DebugMessage = m_DebugSetup.Validate();
    if (!m_DebugMessage.empty()) return;
    try
    {
        std::filesystem::create_directories(PresetPath.parent_path());
        const auto temporary = PresetPath.string() + ".tmp";
        { std::ofstream file(temporary); file << m_DebugSetup.ToJson().dump(2); file.close(); if (!file) throw std::runtime_error("書き込みに失敗しました。"); }
        if (!MoveFileExA(temporary.c_str(), PresetPath.string().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("保存ファイルの更新に失敗しました。");
        m_DebugMessage = "デバッグ条件を保存しました（通常セーブとは別ファイル）。";
    }
    catch (const std::exception& e) { m_DebugMessage = std::string("保存できませんでした：") + e.what(); }
}

// Debug Presetを読み込む。
bool GameDebugController::LoadPreset()
{
    try
    {
        if (std::filesystem::file_size(PresetPath) > 1024 * 1024) throw std::runtime_error("ファイルが大きすぎます。");
        std::ifstream file(PresetPath); nlohmann::json value; file >> value;
        auto next = DebugBattleSetup::FromJson(value, m_DebugBallCatalog, m_DebugEnemyCatalog);
        m_DebugSetup = std::move(next); // 検証に失敗したときは編集中の条件を維持
        m_DebugMessage = "デバッグ条件を読み込みました。「この条件で戦闘開始」で適用します。";
        return true;
    }
    catch (const std::exception& e) { m_DebugMessage = std::string("読み込めませんでした：") + e.what(); }
    return false;
}

// Debug Modeを描画
void GameDebugController::Draw(Game& game)
{
    static bool stageTabActive = true;
    if (m_DebugMode && !m_DebugEditorOpen)
    {
        GameUi::PrepareWindow("debug_mode_controls", ImVec2(740, 20), ImVec2(450, 130));
        ImGui::Begin("デバッグ戦闘", nullptr, ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("通常セーブ・通常ランの集計から独立しています。");
        if (ImGui::Button("条件を編集（一時停止）")) Open(game);
        ImGui::SameLine();
        if (ImGui::Button("同じ条件で再戦")) { m_DebugSetup = m_DebugActiveSetup; m_DebugRequest = 1; }
        if (ImGui::Button("デバッグを終了")) m_DebugRequest = 2;
        ImGui::End();
    }
    if (!m_DebugEditorOpen) return;
    GameUi::PrepareWindow("debug_mode_editor", ImVec2(50, 20), ImVec2(1120, 660));
    ImGui::Begin("デバッグモード：戦闘条件", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("デッキや配置を設定して1戦を試せます。編集内容は次の戦闘開始時に反映します。");
    if (ImGui::Button(stageTabActive ? "編集中のステージで試遊" : "この条件で戦闘開始", ImVec2(205, 36)))
    {
        if (stageTabActive) TestStageEditorLayout(); else m_DebugRequest = 1;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_DebugMode || m_DebugBattleFinished);
    if (ImGui::Button("編集中の条件を適用せず再開", ImVec2(260, 36))) { m_DebugEditorOpen = false; GameDebugHost::SetPaused(game, false); Game::ResetFrameTiming(); }
    ImGui::EndDisabled(); ImGui::SameLine();
    if (ImGui::Button("タイトルへ戻る", ImVec2(180, 36))) m_DebugRequest = 2;
    ImGui::BeginDisabled(stageTabActive);
    if (ImGui::Button("デバッグ条件を保存")) SavePreset(); ImGui::SameLine();
    if (ImGui::Button("デバッグ条件を読み込む")) LoadPreset();
    ImGui::EndDisabled();
    if (!m_DebugMessage.empty()) ImGui::TextWrapped("%s", m_DebugMessage.c_str());
    const auto error = m_DebugSetup.Validate();
    if (!error.empty()) ImGui::TextColored(ImVec4(1, .45f, .3f, 1), "%s", error.c_str());
    if (ImGui::BeginTabBar("debug_setup_tabs"))
    {
        stageTabActive = false;
        if (ImGui::BeginTabItem("ステージエディター"))
        {
            stageTabActive = true;
            DrawStageEditor(game);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("デッキ"))
        {
            ImGui::Text("%d / 64個（登録%d種類・1個だけでも実験できます）",
                static_cast<int>(m_DebugSetup.deck.size()),
                static_cast<int>(m_DebugBallCatalog.size()));
            struct BallCategoryTab
            {
                const char* label;
                BallCategory category;
            };
            const BallCategoryTab categoryTabs[] = {
                { "標準", BallCategory::Standard },
                { "重量", BallCategory::Heavy },
                { "貫通", BallCategory::Pierce },
                { "反射", BallCategory::Bounce },
                { "アンカー", BallCategory::Anchor },
            };
            if (ImGui::BeginTabBar("debug_ball_category_tabs"))
            {
                for (const BallCategoryTab& tab : categoryTabs)
                {
                    if (!ImGui::BeginTabItem(tab.label)) continue;
                    bool firstButton = true;
                    for (const auto& definition : m_DebugBallCatalog)
                    {
                        if (definition.category != tab.category) continue;
                        if (!firstButton) ImGui::SameLine();
                        firstButton = false;
                        ImGui::PushID(definition.definitionId.c_str());
                        ImGui::BeginDisabled(m_DebugSetup.deck.size() >= 64);
                        if (ImGui::Button(BallLabel(definition.definitionId)))
                            m_DebugSetup.deck.push_back(definition);
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::TextDisabled("上の球名で追加。各行を開いて強化・性能を編集できます。");
            for (size_t i = 0; i < m_DebugSetup.deck.size();)
            {
                auto& ball = m_DebugSetup.deck[i]; ImGui::PushID(static_cast<int>(i));
                bool remove = ImGui::SmallButton("削除"); ImGui::SameLine();
				const bool open = ImGui::TreeNode("ball", "#%d %s +%d  攻撃%d", static_cast<int>(i + 1), BallLabel(ball.definitionId), ball.upgradeLevel, ball.status.attack);
                if (open)
                {
                    int level = ball.upgradeLevel;
                    if (ImGui::SliderInt("強化段階（既定性能に戻す）", &level, 0, 2))
                    {
                        const auto found = std::find_if(m_DebugBallCatalog.begin(), m_DebugBallCatalog.end(), [&](const auto& b) { return b.definitionId == ball.definitionId; });
                        if (found != m_DebugBallCatalog.end()) { ball = *found; ball.upgradeLevel = level; if (level > 0) ball.status = ball.upgradeTable[level - 1]; }
                    }
					if (ImGui::TreeNode("性能を個別に変更")) { EditStatus(ball.status, false); ImGui::TreePop(); }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove) m_DebugSetup.deck.erase(m_DebugSetup.deck.begin() + i); else ++i;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("HP・所持金・レリック"))
        {
            ImGui::InputInt("最大HP", &m_DebugSetup.maxHp);
            m_DebugSetup.maxHp = std::clamp(m_DebugSetup.maxHp, 1, 9999);
            m_DebugSetup.hp = (std::min)(m_DebugSetup.hp, m_DebugSetup.maxHp);
            ImGui::InputInt("開始HP", &m_DebugSetup.hp);
            m_DebugSetup.hp = std::clamp(m_DebugSetup.hp, 1, m_DebugSetup.maxHp);
            ImGui::InputInt("所持金", &m_DebugSetup.money, 10, 100);
            m_DebugSetup.money = std::clamp(m_DebugSetup.money, 0, 999999);
            const std::uint32_t seedStep = 1;
            ImGui::InputScalar("乱数シード", ImGuiDataType_U32, &m_DebugSetup.seed, &seedStep);
            ImGui::TextDisabled("同じ条件・シードでデッキの抽選を再現します。基準難易度とDDAは無効、選択したアセンションだけを適用します。");
            for (int i = 0; i < game.GetRelicCount(); ++i)
            {
                const auto* relic = game.GetRelic(i);
                ImGui::Checkbox(relic->name, &m_DebugSetup.relics[static_cast<size_t>(i)]);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", relic->description);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("進行・アセンション"))
        {
            ImGui::TextWrapped(
                "ここでの解除状態はデバッグ戦とMCP表示だけに適用され、通常プロフィールへは保存されません。");
            if (ImGui::Button("現在の通常プロフィールをコピー"))
            {
                const ProgressionProfile& profile = m_DebugPreviousRun.has_value()
                    ? m_DebugPreviousRun->progressionProfile
                    : game.GetProgressionProfile();
                m_DebugSetup.highestUnlockedAscension =
                    profile.highestUnlockedAscension;
                m_DebugSetup.selectedAscension = profile.selectedAscension;
                m_DebugSetup.achievements = profile.achievements;
            }
            ImGui::SliderInt(
                "解放済み最高アセンション",
                &m_DebugSetup.highestUnlockedAscension,
                0,
                ProgressionProfile::MaximumAscension,
                "A%d",
                ImGuiSliderFlags_AlwaysClamp);
            m_DebugSetup.selectedAscension = std::clamp(
                m_DebugSetup.selectedAscension,
                0,
                m_DebugSetup.highestUnlockedAscension);
            ImGui::SliderInt(
                "今回適用するアセンション",
                &m_DebugSetup.selectedAscension,
                0,
                m_DebugSetup.highestUnlockedAscension,
                "A%d",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::Text(
                "実効最大HP %d / 敵HP x%.2f / 敵攻撃 +%d / 休憩回復 %.0f%%",
                m_DebugSetup.EffectiveMaxHp(),
                ProgressionProfile::EnemyHpMultiplier(m_DebugSetup.selectedAscension),
                ProgressionProfile::EnemyAttackBonus(m_DebugSetup.selectedAscension),
                (std::max)(0.05f, GameDebugHost::GetDefaultRestHealRatio(game) -
                    ProgressionProfile::RestHealPenalty(m_DebugSetup.selectedAscension)) * 100.0f);
            if (m_DebugSetup.selectedAscension == 0)
                ImGui::BulletText("A0  %s", ProgressionProfile::AscensionRule(0));
            else
                for (int level = 1; level <= m_DebugSetup.selectedAscension; ++level)
                    ImGui::BulletText(
                        "A%d  %s",
                        level,
                        ProgressionProfile::AscensionRule(level));

            ImGui::SeparatorText("実績の解除状態");
            if (ImGui::Button("すべて解除"))
                m_DebugSetup.achievements.fill(true);
            ImGui::SameLine();
            if (ImGui::Button("すべて未解除"))
                m_DebugSetup.achievements.fill(false);
            ImGui::BeginChild(
                "debug_achievement_list",
                ImVec2(0.0f, 250.0f),
                ImGuiChildFlags_Borders);
            for (const AchievementDefinition& achievement : AchievementCatalog)
            {
                const size_t index = static_cast<size_t>(achievement.id);
                ImGui::PushID(static_cast<int>(index));
                ImGui::Checkbox(
                    achievement.name,
                    &m_DebugSetup.achievements[index]);
                ImGui::TextDisabled("解放：%s", achievement.reward);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("条件：%s", achievement.condition);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("敵編成・配置"))
        {
            if (ImGui::BeginCombo("既存ステージから編成を作る", "ステージを選択"))
            {
                for (const auto& stage : m_DebugStages)
                    if (ImGui::Selectable(stage.id.c_str()))
                    {
                        m_DebugSetup.enemies.clear();
                        for (const auto& spawn : stage.enemies) m_DebugSetup.enemies.push_back({spawn, spawn.enemyData.maxHp});
                    }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo("敵を追加", "種類を選択"))
            {
                for (const auto& definition : m_DebugEnemyCatalog)
                    if (ImGui::Selectable(EnemyLabel(definition.id)) && m_DebugSetup.enemies.size() < 32)
                    {
                        EnemySpawnData spawn; spawn.enemyId = definition.id; spawn.enemyData = definition;
                        spawn.position = {0, TableConfig::FIELD_HEIGHT, 20};
                        m_DebugSetup.enemies.push_back({spawn, definition.maxHp});
                    }
                ImGui::EndCombo();
            }
            ImGui::TextDisabled("Xが左右、Zが前後です。球同士の重なりも、不具合の再現用に指定できます。");
            ImGui::SliderFloat("自球 X", &m_DebugSetup.playerPosition.x, -65, 65);
            ImGui::SliderFloat("自球 Z", &m_DebugSetup.playerPosition.z, -33, 33);
            ImGui::SliderInt("ボス Break残りショット", &m_DebugSetup.breakShots, 0, 2);
            if (m_DebugSetup.breakShots > 0) m_DebugSetup.armor = 0;
            else { m_DebugSetup.armor = (std::max)(1, m_DebugSetup.armor); ImGui::SliderInt("ボス Armor", &m_DebugSetup.armor, 1, 2); }
            ImGui::SeparatorText("ブレイクボール");
            ImGui::TextDisabled("ボス戦の開始時と、使用後の再配置で優先する位置です。");
            ImGui::BeginDisabled(m_DebugSetup.breakBallPositions.size() >= 16);
            if (ImGui::Button("ブレイクボールを追加"))
            {
                const float x = m_DebugSetup.breakBallPositions.size() % 2 == 0 ? -4.0f : 4.0f;
                m_DebugSetup.breakBallPositions.push_back(
                    {x, TableConfig::FIELD_HEIGHT, 10.0f});
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("%d / 16個", static_cast<int>(m_DebugSetup.breakBallPositions.size()));
            for (size_t i = 0; i < m_DebugSetup.breakBallPositions.size();)
            {
                auto& position = m_DebugSetup.breakBallPositions[i];
                ImGui::PushID(static_cast<int>(i));
                const bool remove = ImGui::SmallButton("削除");
                ImGui::SameLine();
                ImGui::Text("ブレイクボール #%d", static_cast<int>(i + 1));
                ImGui::SliderFloat("X##break_ball", &position.x, -65, 65);
                ImGui::SliderFloat("Z##break_ball", &position.z, -33, 33);
                position.y = TableConfig::FIELD_HEIGHT;
                ImGui::PopID();
                if (remove) m_DebugSetup.breakBallPositions.erase(
                    m_DebugSetup.breakBallPositions.begin() + i);
                else ++i;
            }
            for (size_t i = 0; i < m_DebugSetup.enemies.size();)
            {
                auto& enemy = m_DebugSetup.enemies[i]; ImGui::PushID(static_cast<int>(i));
                bool remove = ImGui::SmallButton("削除"); ImGui::SameLine();
                if (ImGui::TreeNode("enemy", "#%d %s HP%d/%d (%.1f, %.1f)", static_cast<int>(i + 1), EnemyLabel(enemy.spawn.enemyData.id), enemy.hp, enemy.spawn.enemyData.maxHp, enemy.spawn.position.x, enemy.spawn.position.z))
                {
                    ImGui::SliderFloat("X", &enemy.spawn.position.x, -65, 65);
                    ImGui::SliderFloat("Z", &enemy.spawn.position.z, -33, 33);
                    ImGui::SliderInt("最大HP", &enemy.spawn.enemyData.maxHp, 1, 9999, "%d", ImGuiSliderFlags_AlwaysClamp);
                    enemy.hp = (std::min)(enemy.hp, enemy.spawn.enemyData.maxHp);
                    ImGui::SliderInt("現在HP", &enemy.hp, 1, enemy.spawn.enemyData.maxHp, "%d", ImGuiSliderFlags_AlwaysClamp);
					if (ImGui::TreeNode("性能")) { EditStatus(enemy.spawn.enemyData.status, true); ImGui::TreePop(); }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove) m_DebugSetup.enemies.erase(m_DebugSetup.enemies.begin() + i); else ++i;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Gameは呼び出し窓口だけを保ち、デバッグの手順はControllerへ委譲
void Game::OpenDebugMode() { m_DebugController->Open(*this); }
bool Game::IsDebugMode() const { return m_DebugController->IsActive(); }
const std::vector<DirectX::SimpleMath::Vector3>& Game::GetDebugBreakBallPositions() const
{
    return m_DebugController->GetActiveBreakBallPositions();
}
void Game::ApplyDebugBattlePlayer(PlayerBall* player) { m_DebugController->ApplyBattlePlayer(player); }
void Game::ApplyDebugBattleEnemy(EnemyBall* enemy, std::size_t index) { m_DebugController->ApplyBattleEnemy(enemy, index); }
void Game::ApplyDebugRunSettings() { m_DebugController->ApplyRunSettings(*this); }
void Game::EndDebugMode() { m_DebugController->End(*this); }
void Game::FinishDebugBattle(bool victory) { m_DebugController->FinishBattle(*this, victory); }
bool Game::UpdateDebugMode() { return m_DebugController->Update(*this); }
void Game::DrawDebugMode() { m_DebugController->Draw(*this); }
bool Game::LoadDebugPreset() { return m_DebugController->LoadPreset(); }
