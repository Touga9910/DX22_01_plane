#include "Game.h"
#include "GameDebugController.h"
#include "GameDebugHost.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameUi.h"
#include "EnemyBall.h"
#include "EnemyText.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>

namespace
{
	const char* GetBattleStateDebugName(BattleState state)
	{
		switch (state)
		{
		case BattleState::Inactive:        return "Inactive";
		case BattleState::AimingDirection: return "AimingDirection";
		case BattleState::AimingPower:     return "AimingPower";
		case BattleState::BallsMoving:     return "BallsMoving";
		case BattleState::EnemyAttack:     return "EnemyAttack";
		case BattleState::TurnEnd:         return "TurnEnd";
		case BattleState::Finished:        return "Finished";
		default:                            return "Unknown";
		}
	}

	const char* GetIncomingDamagePhaseLabel(
		BattleState state,
		bool clearReward)
	{
		if (clearReward)
		{
			return "敵全滅（被ダメージなし）";
		}

		switch (state)
		{
		case BattleState::AimingDirection:
		case BattleState::AimingPower:
		case BattleState::BallsMoving:
			return "このターンの敵攻撃前";

		case BattleState::EnemyAttack:
			return "敵攻撃処理中";

		case BattleState::TurnEnd:
			return "このターンの敵攻撃は処理済み（次ターン参考）";

		case BattleState::Finished:
			return "戦闘終了";

		default:
			return "戦闘外（参考値）";
		}
	}

	const char* GetIncomingDamageValueLabel(BattleState state)
	{
		switch (state)
		{
		case BattleState::AimingDirection:
		case BattleState::AimingPower:
		case BattleState::BallsMoving:
		case BattleState::EnemyAttack:
			return "このターンの予測被ダメージ";

		case BattleState::TurnEnd:
			return "次ターンの参考被ダメージ";

		default:
			return "参考被ダメージ";
		}
	}
}

void GameDebugController::DrawDiagnostics(Game& game)
{
	if (GameUi::showDebugger)
	{
	GameUi::PrepareWindow("debugger", ImVec2(20, 70), ImVec2(570, 620));
	ImGui::Begin("Ball Debugger", &GameUi::showDebugger);
	const std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	ImGui::Text("Heavy Stacks: %d", game.GetHeavyCollisionCount());
	ImGui::Text("Pierce Stacks: %d", game.GetPierceTraceState().stackCount);
	ImGui::Text("Pierce Trace: %d / %d",
		static_cast<int>(game.GetPierceTraceState().traces.size()),
		static_cast<int>(PierceTraceRules::MaxTraceCount));
	for (const auto& trace : game.GetPierceTraceState().traces)
	{
		ImGui::BulletText(
			"Trace #%llu  (%.1f, %.1f)->(%.1f, %.1f) dir=(%.2f, %.2f)",
			static_cast<unsigned long long>(trace.id),
			trace.start.x, trace.start.z, trace.end.x, trace.end.z,
			trace.direction.x, trace.direction.z);
	}
	ImGui::Text("Player Anchor Stack: %d", game.GetPlayerAnchorStacks());
	for (std::size_t index = 0; index < enemies.size(); ++index)
		if (enemies[index] != nullptr)
			ImGui::BulletText("Enemy[%d] Anchor Stack: %d",
				static_cast<int>(index), enemies[index]->GetAnchorStacks());
	ImGui::Text("Reflection Stacks: %d",
		CushionChargeRules::TotalStacks(game.GetCushionCharges()));
	ImGui::Separator();
	if (this->m_DebugCombatForecastDirty)
	{
		RefreshCombatForecast(game);
	}
	const GameDebugController::CombatForecastSnapshot& forecast =
		this->m_DebugCombatForecast;
	auto drawHpBar = [](const char* id, int currentHp, int maxHp)
	{
		const float hpRatio = maxHp <= 0
			? 0.0f
			: std::clamp(
				static_cast<float>(currentHp) / static_cast<float>(maxHp),
				0.0f,
				1.0f);
		const ImVec4 hpColor = hpRatio < 0.25f
			? ImVec4(0.90f, 0.18f, 0.18f, 1.0f)
			: hpRatio < 0.50f
				? ImVec4(0.95f, 0.70f, 0.12f, 1.0f)
				: ImVec4(0.20f, 0.75f, 0.30f, 1.0f);
		const std::string overlay =
			std::to_string(currentHp) + " / " + std::to_string(maxHp);
		ImGui::PushID(id);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, hpColor);
		ImGui::ProgressBar(hpRatio, ImVec2(-1.0f, 0.0f), overlay.c_str());
		ImGui::PopStyleColor();
		ImGui::PopID();
	};

	ImGui::TextUnformatted("戦闘状況（状態変化時に更新）");
	ImGui::Separator();
	ImGui::TextDisabled(
		"フェーズ: %s",
		GetIncomingDamagePhaseLabel(
			game.GetBattleState(),
			game.IsClearRewardActive()));
	if (forecast.hasPlayer)
	{
		ImGui::Text(
			"プレイヤーHP: %d / %d  シールド: %d",
			forecast.playerCurrentHp,
			forecast.playerMaxHp,
			forecast.playerShield);
		drawHpBar(
			"player_hp",
			forecast.playerCurrentHp,
			forecast.playerMaxHp);

		const ImVec4 damageColor = forecast.lethal
			? ImVec4(1.0f, 0.25f, 0.25f, 1.0f)
			: forecast.expectedDamage > 0
				? ImVec4(1.0f, 0.75f, 0.20f, 1.0f)
				: ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
		ImGui::TextColored(
			damageColor,
			"%s: %d  攻撃後HP: %d%s",
			GetIncomingDamageValueLabel(game.GetBattleState()),
			forecast.expectedDamage,
			forecast.hpAfterAttack,
			forecast.lethal ? "  [致死]" : "");
		ImGui::Text(
			"理論: %d  実HP減少: %d  超過: %d",
			forecast.theoreticalDamage,
			forecast.expectedDamage,
			forecast.overkillDamage);
		ImGui::TextDisabled(
			"攻撃予定: %d体（撃破・ポケット中の敵は除外）",
			forecast.attackerCount);
	}
	else
	{
		ImGui::TextDisabled("プレイヤーHP: 対象なし");
		ImGui::TextDisabled("このターンの予測被ダメージ: 算出不可");
	}
	ImGui::TextDisabled(
		"更新 #%llu: %s",
		static_cast<unsigned long long>(forecast.updateRevision),
		forecast.updateReason.c_str());
	if (this->m_DebugLastEnemyAttackComparisonValid)
	{
		const int difference =
			this->m_DebugLastEnemyAttackActualDamage -
			this->m_DebugLastEnemyAttackPredictedDamage;
		const ImVec4 comparisonColor = difference == 0
			? ImVec4(0.45f, 1.0f, 0.45f, 1.0f)
			: ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
		ImGui::TextColored(
			comparisonColor,
			"直近敵攻撃  予測: %d  実測: %d  差分: %+d",
			this->m_DebugLastEnemyAttackPredictedDamage,
			this->m_DebugLastEnemyAttackActualDamage,
			difference);
	}

	ImGui::TextUnformatted("エネミーHP:");
	ImGui::Checkbox(
		"攻撃予定のみ",
		&this->m_DebugOnlyAttackers);
	ImGui::SameLine();
	ImGui::Checkbox(
		"撃破済み",
		&this->m_DebugShowDefeatedEnemies);
	ImGui::SameLine();
	ImGui::Checkbox(
		"ポケット中",
		&this->m_DebugShowPocketedEnemies);
	const char* sortLabels[] = { "生成順", "HP昇順", "危険度順" };
	if (ImGui::BeginCombo(
		"敵の並び順",
		sortLabels[this->m_DebugEnemySortMode]))
	{
		for (int mode = 0; mode < 3; ++mode)
		{
			const bool selected = mode == this->m_DebugEnemySortMode;
			if (ImGui::Selectable(sortLabels[mode], selected))
			{
				this->m_DebugEnemySortMode = mode;
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	std::vector<std::size_t> visibleEnemyIndices;
	visibleEnemyIndices.reserve(forecast.enemies.size());
	for (std::size_t i = 0; i < forecast.enemies.size(); ++i)
	{
		const GameDebugController::EnemyCombatSnapshot& enemy = forecast.enemies[i];
		if ((!this->m_DebugShowDefeatedEnemies && enemy.defeated) ||
			(!this->m_DebugShowPocketedEnemies && enemy.pocketed) ||
			(this->m_DebugOnlyAttackers && !enemy.canAttack))
		{
			continue;
		}
		visibleEnemyIndices.push_back(i);
	}
	if (this->m_DebugEnemySortMode == 1)
	{
		std::stable_sort(
			visibleEnemyIndices.begin(),
			visibleEnemyIndices.end(),
			[&forecast](std::size_t left, std::size_t right)
			{
				const GameDebugController::EnemyCombatSnapshot& a = forecast.enemies[left];
				const GameDebugController::EnemyCombatSnapshot& b = forecast.enemies[right];
				return static_cast<long long>(a.currentHp) * b.maxHp <
					static_cast<long long>(b.currentHp) * a.maxHp;
			});
	}
	else if (this->m_DebugEnemySortMode == 2)
	{
		std::stable_sort(
			visibleEnemyIndices.begin(),
			visibleEnemyIndices.end(),
			[&forecast](std::size_t left, std::size_t right)
			{
				return forecast.enemies[left].expectedDamage >
					forecast.enemies[right].expectedDamage;
			});
	}

	if (visibleEnemyIndices.empty())
	{
		ImGui::TextDisabled("  対象なし");
	}
	for (std::size_t index : visibleEnemyIndices)
	{
		const GameDebugController::EnemyCombatSnapshot& enemy = forecast.enemies[index];
		ImGui::Text(
			"  #%zu %s: %d / %d  攻撃: %d  予測: %d  [%s]",
			index + 1,
			EnemyLabel(enemy.id),
			enemy.currentHp,
			enemy.maxHp,
			enemy.attack,
			enemy.expectedDamage,
			enemy.state.c_str());
		drawHpBar(
			("enemy_hp_" + std::to_string(index)).c_str(),
			enemy.currentHp,
			enemy.maxHp);
		if (enemy.canAttack)
		{
			ImGui::TextDisabled(
				"    攻撃%d = %d%s",
				enemy.attack,
				enemy.expectedDamage,
				enemy.minimumDamageApplied ? "（最低保証）" : "");
		}
	}

	if (ImGui::CollapsingHeader("直近の被ダメージ履歴"))
	{
		if (this->m_DebugPlayerDamageHistory.empty())
		{
			ImGui::TextDisabled("記録なし");
		}
		for (const GameDebugController::PlayerDamageRecord& record :
			this->m_DebugPlayerDamageHistory)
		{
			if (record.hpBefore >= 0 && record.hpAfter >= 0)
			{
				ImGui::Text(
					"#%llu %s/%s: %d -> %d  (-%d)",
					static_cast<unsigned long long>(record.sequence),
					record.source.c_str(),
					record.sourceId.empty() ? "-" : EnemyLabel(record.sourceId),
					record.hpBefore,
					record.hpAfter,
					record.damage);
			}
			else
			{
				ImGui::Text(
					"#%llu %s/%s: -%d",
					static_cast<unsigned long long>(record.sequence),
					record.source.c_str(),
					record.sourceId.empty() ? "-" : EnemyLabel(record.sourceId),
					record.damage);
			}
		}
	}
	ImGui::Separator();

	ImGui::Text(
		"BattleState = %s",
		GetBattleStateDebugName(
			game.GetBattleState()));

	ImGui::Text(
		"ClearReward = %s",
		game.IsClearRewardActive()
		? "true"
		: "false");

	ImGui::Text("AreAllBallsStopped = %s",
		game.AreAllBallsStopped() ? "true" : "false");

	ImGui::Text("AreAllEnemiesDefeated = %s",
		game.AreAllEnemiesDefeated() ? "true" : "false");

	ImGui::Text("Player Run HP = %d / %d",
		game.GetPlayerCurrentHp(),
		game.GetPlayerMaxHp());
	ImGui::Text("Draw Pile Count = %d", game.GetPlayerDeckCount());
	ImGui::Text("Discard Pile Count = %d", game.GetPlayerDiscardCount());
	ImGui::Text("Offer Count = %d", game.GetBallOfferCount());
	ImGui::Text("Total Deck Count = %d", game.GetDeckBallCount());
	ImGui::Text("Current Ball Used = %s",
		game.IsCurrentPlayerBallUsed() ? "true" : "false");

	const PlayerBallData* currentDebugBall =
		game.GetCurrentPlayerBallData();
	if (currentDebugBall != nullptr)
	{
		ImGui::Text(
			"Current Ball ID = %s",
			currentDebugBall->definitionId.c_str()
		);

		ImGui::Text(
			"Current Ball Attack = %d",
			currentDebugBall->status.attack
		);
	}
	else
	{
		ImGui::Text("Current Ball ID = none");
	}

	DebugAutomationSnapshot automation =
		GameDebugHost::CaptureAutomation(game);
	ImGui::BeginDisabled(game.IsDebugMode());
	if (ImGui::Button(
		automation.enabled
			? "Stop Balance Auto Play"
			: "Start Balance Auto Play"))
	{
		const bool startAutoPlay = !automation.enabled;
		GameDebugHost::SetAutoPlayEnabled(game, startAutoPlay);
		GameDebugHost::SetStopAfterCurrentRunRequested(
			game,
			startAutoPlay &&
			automation.stopAfterCurrentRunDefault);
		automation = GameDebugHost::CaptureAutomation(game);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text(
		"F8 / Runs: %d%s",
		automation.runCount,
		automation.maximumRuns > 0
			? " (limited)"
			: " (unlimited)");
	ImGui::BeginDisabled(!automation.enabled);
	if (ImGui::Button(
		automation.stopAfterCurrentRunRequested
			? "Continue After This Run (F9)"
			: "Stop At This Run End (F9)"))
	{
		GameDebugHost::SetStopAfterCurrentRunRequested(
			game,
			!automation.stopAfterCurrentRunRequested);
		automation = GameDebugHost::CaptureAutomation(game);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted(
		automation.stopAfterCurrentRunRequested
			? "Stops on game over / game clear"
			: "Continuous runs");

	if (ImGui::Button("Save Debug Snapshot"))
	{
		GameDebugHost::SaveSnapshot(game);
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("debug_state_snapshot.txt");

	for (int i = 0; i < players.size(); i++)
	{
		players[i]->DrawImGui();
	}

	for (int i = 0; i < enemies.size(); i++)
	{
		std::string label = "EnemyBall " + std::to_string(i);
		enemies[i]->DrawImGui(label);
	}

	ImGui::Text(
		"Player Money = %d",
		game.GetPlayerMoney()
	);

	ImGui::End();

	}

}
