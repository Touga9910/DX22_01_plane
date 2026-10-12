#include "Game.h"
#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "GameShotPrediction.h"
#include "GameObject.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameSettingsRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameRunDefaults.h"
#include "GameRunSession.h"
#include "GameRewardState.h"
#include "GameEncounterBalance.h"
#include "GameBallSelectionState.h"
#include "GamePauseState.h"
#include "GameStageOverrideState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunProgressConstants.h"
#include "RunProgressController.h"
#include "StageSelector.h"
#include "Camera.h"
#include "GameUi.h"
#include "BallMechanics.h"

#pragma execution_character_set("utf-8")
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "GamePresentation.h"
#include "GameSaveManager.h"
#include "BossShotPlanner.h"
#include "GameDebugController.h"
#include "GameDebugHost.h"
#include "RunProgressController.h"
#include "json/json.hpp"
#include "BattleScene.h"
#include "ResultScene.h"
#include "RestSiteScene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"
#include "BallPhysicsComponent.h"
#include "BallPhysicsWorld.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui呼び出しに必要
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
#include "EnemyBall.h"
#include "BreakBall.h"   // DrawImGui呼び出しに必要
#include "EnemyAttackComponent.h"
#include "BallComponent.h"
#include "PlayerBallDataLoader.h"
#include "StageDataLoader.h"
#include "EnemyData.h"
#include "TableConfig.h"
#include "PocketReturnRules.h"
#include "UiText.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using DirectX::SimpleMath::Vector3;

Game* Game::m_Instance;//ゲームインスタンス

void GameRuntime::Initialize()
{
	Game::Init();
}

void GameRuntime::Update(double elapsedSeconds)
{
	Game::Update(elapsedSeconds);
}

void GameRuntime::ResetFrameTiming()
{
	Game::ResetFrameTiming();
}

void GameRuntime::Draw()
{
	Game::Draw();
}

void GameRuntime::Shutdown()
{
	Game::Uninit();
}

Camera& GameRuntime::MainCamera()
{
	return Camera::GetInstance();
}

GameWorld& GameRuntime::World()
{
	return Game::GetInstance()->m_RuntimeState->World();
}

Game& GameRuntime::CurrentGame()
{
	return *Game::GetInstance();
}

GameObject* GameRuntime::CreateObject(
	Game& game,
	const std::string& name)
{
	return game.CreateGameObject(name);
}

GameObject* GameRuntime::CreateObject(const std::string& name)
{
	return CurrentGame().CreateGameObject(name);
}

void GameRuntime::DestroyObject(GameObject* object)
{
	Game::GetInstance()->DeleteGameObject(object);
}

namespace
{
	const char* const kClearRewardNames[] =
	{
		UiText::RewardNewBall,
		UiText::RewardUpgrade,
		UiText::RewardExtraMoney,
	};
	constexpr int kClearRewardCount =
		static_cast<int>(sizeof(kClearRewardNames) / sizeof(kClearRewardNames[0]));
	constexpr int kAutoShopRemoveCost = 15;
	constexpr int kBankShotDamageMultiplier = 2;
	constexpr int kEmergencyRepairContactThreshold = 3;
	constexpr int kEmergencyRepairHealAmount = 1;

	const char* GetPlayerBallDisplayName(const std::string& definitionId)
	{
		return PlayerBallText::GetName(definitionId);
	}

	const char* GetPlayerBallAbilityName(const PlayerBallData& ball)
	{
		return PlayerBallText::GetTrait(ball);
	}

	const char* GetPlayerBallAimHint(const std::string& definitionId)
	{
		if (definitionId == "player_heavy")
			return PlayerBallText::Utf8(u8"狙い：敵同士を押し込んで連鎖させる");
		if (definitionId == "player_pierce")
			return PlayerBallText::Utf8(u8"狙い：複数の敵を一直線に通す");
		if (definitionId == "player_bounce")
			return PlayerBallText::Utf8(u8"狙い：壁反射で敵の裏側へ回り込む");
		if (definitionId == "player_anchor")
			return PlayerBallText::Utf8(u8"狙い：接触位置で止め、次の配置を作る");
		if (definitionId == "player_cushion_charge")
			return PlayerBallText::Utf8(u8"狙い：クッションを経由して次の球へつなぐ");
		if (definitionId == "player_chain_impact")
			return PlayerBallText::Utf8(u8"狙い：敵の密集地点に当てて周囲を巻き込む");
		if (definitionId == "player_refractive_pierce")
			return PlayerBallText::Utf8(u8"狙い：衝突後の屈折先まで読む");
		if (definitionId == "player_stop_shield")
			return PlayerBallText::Utf8(u8"狙い：早めに止めて次の被弾を防ぐ");
		if (definitionId == "player_trace_driver")
			return PlayerBallText::Utf8(u8"狙い：既存の貫通痕をなぞり、その先で壁反射する");
		if (definitionId == "player_pierce_finisher")
			return PlayerBallText::Utf8(u8"狙い：貫通痕を再利用し、異なる敵を連続で貫く");
		if (definitionId == "player_ricochet_finisher")
			return PlayerBallText::Utf8(u8"狙い：育てた壁を複数経由してから敵へ当てる");
		if (definitionId == "player_anchor_finisher")
			return PlayerBallText::Utf8(u8"狙い：印を持つ敵へ一撃を集中して回収する");
		return PlayerBallText::Utf8(u8"狙い：配置に合わせて確実に連鎖を始める");
	}

	struct BallCardInteraction
	{
		bool select = false;
		bool toggleHold = false;
		bool openDetails = false;
	};

	BallCardInteraction DrawBallSelectionCard(
		const PlayerBallData& ball,
		int index,
		int attack,
		const ImVec2& position,
		const ImVec2& size,
		bool selected,
		bool held,
		float expansion)
	{
		BallCardInteraction interaction;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const auto ballColor = PlayerBallText::GetColor(ball);
		const ImU32 accent = ImGui::ColorConvertFloat4ToU32(
			ImVec4(ballColor[0], ballColor[1], ballColor[2], 1.0f));
		const ImU32 cardBackground = selected
			? IM_COL32(27, 34, 46, 248)
			: (held ? IM_COL32(20, 36, 43, 244) : IM_COL32(15, 21, 30, 236));
		const ImU32 borderColor = selected
			? IM_COL32(255, 221, 118, 255)
			: (held ? IM_COL32(91, 222, 205, 255) : IM_COL32(92, 105, 126, 210));
		const ImVec2 cardMax(position.x + size.x, position.y + size.y);
		constexpr float cornerRadius = 12.0f;
		constexpr float footerHeight = 38.0f;
		// 展開の終盤までは球の識別情報だけを見せ、
		// 十分な高さができてから詳細とホールド操作を表示
		const bool detailsVisible = expansion >= 0.82f;

		drawList->AddRectFilled(
			ImVec2(position.x + 5.0f, position.y + 7.0f),
			ImVec2(cardMax.x + 5.0f, cardMax.y + 7.0f),
			IM_COL32(0, 0, 0, 105),
			cornerRadius);
		drawList->AddRectFilled(position, cardMax, cardBackground, cornerRadius);
		drawList->AddRectFilled(
			position,
			ImVec2(cardMax.x, position.y + 6.0f),
			accent,
			cornerRadius,
			ImDrawFlags_RoundCornersTop);
		drawList->AddRect(
			position,
			cardMax,
			borderColor,
			cornerRadius,
			0,
			selected ? 3.0f : 1.5f);

		ImGui::PushID(index);
		ImGui::SetCursorScreenPos(position);
		ImGui::InvisibleButton(
			"ball_card_select",
			ImVec2(size.x, detailsVisible ? size.y - footerHeight : size.y));
		const bool cardHovered = ImGui::IsItemHovered();
		interaction.select = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		interaction.openDetails = interaction.select && selected;
		interaction.toggleHold =
			!selected && cardHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

		const ImVec2 footerPosition(position.x, cardMax.y - footerHeight);
		bool footerHovered = false;
		if (detailsVisible)
		{
			ImGui::SetCursorScreenPos(footerPosition);
			ImGui::InvisibleButton(
				"ball_card_hold",
				ImVec2(size.x, footerHeight));
			footerHovered = ImGui::IsItemHovered();
			if (!selected && ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				interaction.toggleHold = true;
			}
			if (!selected && footerHovered &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				interaction.toggleHold = true;
			}
		}

		if (cardHovered || footerHovered)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}

		const float padding = 13.0f;
		const float contentWidth = size.x - padding * 2.0f;
		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize();
		char text[160]{};

		drawList->PushClipRect(position, cardMax, true);
		if (!detailsVisible)
		{
			sprintf_s(text, "[%s] %s",
				GetPlayerBallAbilityName(ball),
				PlayerBallText::GetRoles(ball.definitionId));
			drawList->AddText(
				font,
				fontSize * 0.76f,
				ImVec2(position.x + padding, position.y + 12.0f),
				IM_COL32(224, 231, 242, 255),
				text);
			const ImVec2 ballCenter(position.x + 30.0f, position.y + 54.0f);
			drawList->AddCircleFilled(ballCenter, 18.0f, IM_COL32(3, 7, 12, 210), 28);
			drawList->AddCircleFilled(ballCenter, 14.0f, accent, 28);
			drawList->AddCircle(ballCenter, 18.0f, accent, 28, 2.0f);
			drawList->AddText(
				font,
				fontSize * 0.95f,
				ImVec2(position.x + 56.0f, position.y + 42.0f),
				IM_COL32(255, 255, 255, 255),
				PlayerBallText::GetName(ball.definitionId));
			sprintf_s(text, PlayerBallText::Utf8(u8"攻撃力：%d"), attack);
			drawList->AddText(
				font,
				fontSize * 0.78f,
				ImVec2(position.x + 56.0f, position.y + 65.0f),
				IM_COL32(241, 224, 158, 255),
				text);
			drawList->AddText(
				font,
				fontSize * 0.71f,
				ImVec2(position.x + padding, position.y + 91.0f),
				IM_COL32(184, 195, 211, 255),
				PlayerBallText::GetShortDescription(ball.definitionId),
				nullptr,
				contentWidth);
			const char* stateText = selected
				? PlayerBallText::Utf8(u8"選択中")
				: (held ? "HOLD" : PlayerBallText::Utf8(u8"手札"));
			const ImVec2 stateSize = ImGui::CalcTextSize(stateText);
			drawList->AddText(
				ImVec2(cardMax.x - stateSize.x - 10.0f, cardMax.y - 20.0f),
				selected
					? IM_COL32(255, 221, 118, 255)
					: (held ? IM_COL32(91, 222, 205, 255) : IM_COL32(145, 156, 174, 230)),
				stateText);
			drawList->PopClipRect();
			ImGui::PopID();
			return interaction;
		}

		sprintf_s(text, "%d  [%s] %s", index + 1,
			GetPlayerBallAbilityName(ball),
			PlayerBallText::GetRoles(ball.definitionId));
		drawList->AddText(
			font,
			fontSize * 0.83f,
			ImVec2(position.x + padding, position.y + 13.0f),
			IM_COL32(220, 230, 243, 255),
			text);

		const ImVec2 ballCenter(position.x + 30.0f, position.y + 61.0f);
		drawList->AddCircleFilled(ballCenter, 18.0f, IM_COL32(3, 7, 12, 210), 28);
		drawList->AddCircleFilled(ballCenter, 14.0f, accent, 28);
		drawList->AddCircle(ballCenter, 18.0f, accent, 28, 2.0f);

		drawList->AddText(
			font,
			fontSize * 1.05f,
			ImVec2(position.x + 55.0f, position.y + 48.0f),
			IM_COL32(255, 255, 255, 255),
			PlayerBallText::GetName(ball.definitionId));
		sprintf_s(text, "Lv.%d  /  No.%llu", ball.upgradeLevel,
			static_cast<unsigned long long>(ball.instanceId));
		drawList->AddText(
			font,
			fontSize * 0.72f,
			ImVec2(position.x + 55.0f, position.y + 70.0f),
			IM_COL32(158, 171, 191, 255),
			text);

		// 数値を埋め込む文もUTF-8の書式文字列を使い、ImGuiへ渡す。
		sprintf_s(
			text,
			PlayerBallText::Utf8(u8"攻撃力：%d"),
			attack);
		drawList->AddText(
			font,
			fontSize * 0.83f,
			ImVec2(position.x + padding, position.y + 94.0f),
			IM_COL32(238, 242, 248, 255),
			text);
		sprintf_s(
			text,
			PlayerBallText::Utf8(u8"重さ %.1f   大きさ %.1f"),
			ball.status.mass,
			ball.status.radius);
		drawList->AddText(

			font,
			fontSize * 0.75f,
			ImVec2(position.x + padding, position.y + 115.0f),
			IM_COL32(176, 188, 205, 255),
			text);

		drawList->AddLine(
			ImVec2(position.x + padding, position.y + 139.0f),
			ImVec2(cardMax.x - padding, position.y + 139.0f),
			IM_COL32(83, 94, 113, 180));
		drawList->AddText(
			font,
			fontSize * 0.78f,
			ImVec2(position.x + padding, position.y + 149.0f),
			IM_COL32(249, 224, 139, 255),
			GetPlayerBallAimHint(ball.definitionId),
			nullptr,
			contentWidth);

		if (size.y >= 252.0f && size.x >= 190.0f)
		{
			drawList->AddText(
				font,
				fontSize * 0.68f,
				ImVec2(position.x + padding, position.y + 190.0f),
				IM_COL32(171, 181, 197, 255),
				PlayerBallText::GetDescription(ball.definitionId),
				nullptr,
				contentWidth);
		}

		const ImU32 footerBackground = selected
			? IM_COL32(184, 137, 38, 235)
			: (held ? IM_COL32(23, 122, 116, 245)
				: (footerHovered ? IM_COL32(58, 74, 96, 245) : IM_COL32(35, 45, 60, 240)));
		drawList->AddRectFilled(
			footerPosition,
			cardMax,
			footerBackground,
			cornerRadius,
			ImDrawFlags_RoundCornersBottom);
		const char* footerText = selected
			? PlayerBallText::Utf8(u8"今回使用")
			: (held
				? PlayerBallText::Utf8(u8"ホールド解除")
				: PlayerBallText::Utf8(u8"ホールド"));
		const ImVec2 footerTextSize = ImGui::CalcTextSize(footerText);
		drawList->AddText(
			ImVec2(
				position.x + (size.x - footerTextSize.x) * 0.5f,
				footerPosition.y + (footerHeight - footerTextSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 255),
			footerText);

		if (selected || held)
		{
			const char* badgeText = selected ? "SELECT" : "HOLD";
			const ImVec2 badgeSize = ImGui::CalcTextSize(badgeText);
			const ImVec2 badgeMin(cardMax.x - badgeSize.x - 20.0f, position.y + 11.0f);
			const ImVec2 badgeMax(cardMax.x - 8.0f, position.y + 11.0f + badgeSize.y + 8.0f);
			drawList->AddRectFilled(
				badgeMin,
				badgeMax,
				selected ? IM_COL32(220, 164, 51, 255) : IM_COL32(31, 151, 142, 255),
				6.0f);
			drawList->AddText(
				ImVec2(badgeMin.x + 6.0f, badgeMin.y + 4.0f),
				IM_COL32(255, 255, 255, 255),
				badgeText);
		}

		drawList->PopClipRect();
		ImGui::PopID();
		return interaction;
	}

	void DrawTermTooltip(const char* term, const char* explanation)
	{
		ImGui::TextColored(ImVec4(0.72f, 0.84f, 1.0f, 1.0f), "%s (?)", term);
		if (!ImGui::IsItemHovered()) return;
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
		ImGui::TextWrapped("%s", explanation);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	void DrawBallDetailPopup(const PlayerBallData& ball, int effectiveAttack)
	{
		const BallStatus& status = ball.status;
		ImGui::Text("%s  Lv.%d", PlayerBallText::GetName(ball.definitionId), ball.upgradeLevel);
		ImGui::Text("[%s]  %s", PlayerBallText::GetTrait(ball), PlayerBallText::GetRoles(ball.definitionId));
		ImGui::Text("攻撃力：%d", effectiveAttack);
		ImGui::Separator();
		ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball.definitionId));

		ImGui::Spacing();
		ImGui::TextUnformatted("主要能力値");
		ImGui::BulletText("質量 %.1f / 反発 %.0f%% / 摩擦 %.3f",
			status.mass, status.restitution * 100.0f, status.friction);
		if (ball.category == BallCategory::Heavy)
		{
			ImGui::BulletText("押し出し倍率 %.2f", status.knockbackTransfer);
			if (status.heavyFinisherDamagePerCollision > 0.0f)
				ImGui::BulletText("重量スタック1個ごとの追加攻撃 %.1f", status.heavyFinisherDamagePerCollision);
		}
		else if (ball.category == BallCategory::Pierce)
		{
			ImGui::BulletText("貫通回数 %d / 貫通時速度維持率 %.0f%%",
				status.pierceMaxUses, status.pierceSpeedRetention * 100.0f);
			if (status.pierceFinisherBaseBonus > 0)
				ImGui::BulletText("決着基本 +%d / 追加対象ごと +%d",
					status.pierceFinisherBaseBonus, status.pierceFinisherMultiTargetBonus);
		}
		else if (ball.category == BallCategory::Bounce)
		{
			if (status.cushionStackGenerateAmount > 0)
			{
				ImGui::BulletText("有効反射1回のスタック生成 %d / 最大回数 %d",
					status.cushionStackGenerateAmount, status.cushionReflectionLimit);
			}
			if (status.cushionConsumptionLimit > 0)
				ImGui::BulletText("1ショットの最大消費 %d / 反射速度 %.0f%% / 攻撃 +%d",
					status.cushionConsumptionLimit,
					status.cushionReflectionSpeedMultiplier * 100.0f,
					status.cushionBounceAttackBonus);
			if (status.ricochetStackConsume > 0)
				ImGui::BulletText("反射スタック消費 %d / %d衝突ごとに追加ダメージ +1",
					status.ricochetStackConsume,
					status.pinballCollisionsPerStage);
		}
		else if (ball.category == BallCategory::Anchor)
		{
			ImGui::BulletText("対象へのEnemy Anchor付与 %d",
				status.anchorEnemyStackGenerate);
			if (status.anchorPlayerStacksPerBonus > 0)
				ImGui::BulletText("Player Anchor %d個ごとに付与 +1",
					status.anchorPlayerStacksPerBonus);
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("次回強化");
		ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(ball).c_str());

		ImGui::Separator();
		ImGui::TextUnformatted("用語（マウスオーバーで説明）");
		DrawTermTooltip("重量スタック",
			"重量球で動かした敵の距離から、敵ごとに生成される資源。");
		DrawTermTooltip("貫通痕",
			"敵を貫通した始点と方向を保存する発射軌道。最大3本保持する。");
		DrawTermTooltip("クッションスタック",
			"クッション蓄積球の有効反射でプレイヤー側に生成される共有資源。");
		DrawTermTooltip("錨スタック",
			"敵ごとに保持するEnemy Anchorと、5消費ごとに増えるPlayer Anchorの2種類。");
	}

	int CountDefeatedEnemies(const std::vector<EnemyBall*>& enemies)
	{
		return static_cast<int>(std::count_if(
			enemies.begin(),
			enemies.end(),
			[](const EnemyBall* enemy)
			{
				return enemy != nullptr && enemy->IsDefeated();
			}));
	}

	int CountAliveEnemies(const std::vector<EnemyBall*>& enemies)
	{
		return static_cast<int>(std::count_if(
			enemies.begin(),
			enemies.end(),
			[](const EnemyBall* enemy)
			{
				return enemy != nullptr && !enemy->IsDefeated();
			}));
	}

	bool CanEnemyAttackThisTurn(const EnemyBall* enemy)
	{
		if (enemy == nullptr ||
			enemy->IsDefeated() ||
			enemy->IsPocketed() ||
			enemy->IsStunned())
		{
			return false;
		}

		GameObject* owner = enemy->GetGameObject();
		return owner != nullptr &&
			owner->GetComponent<EnemyAttackComponent>() != nullptr;
	}

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

	const char* GetSceneDebugName(Scene* scene)
	{
		if (dynamic_cast<TitleScene*>(scene)) return "TITLE";
		if (dynamic_cast<StageSelectScene*>(scene)) return "SELECT";
		if (dynamic_cast<BattleScene*>(scene)) return "BATTLE";
		if (dynamic_cast<RestSiteScene*>(scene)) return "REST_SITE";
		if (dynamic_cast<ShopScene*>(scene)) return "SHOP";
		if (dynamic_cast<ResultScene*>(scene)) return "RESULT";

		return "Unknown";
	}

	void WriteVector3(std::ofstream& file, const char* label, const DirectX::SimpleMath::Vector3& value)
	{
		file << label << " = ("
			<< value.x << ", "
			<< value.y << ", "
			<< value.z << ")\n";
	}

	void WriteBallDebugStatus(std::ofstream& file, const char* typeName, int index, BallComponent* ball)
	{
		if (ball == nullptr)
		{
			return;
		}

		file << "[" << typeName << " " << index << "]\n";
		WriteVector3(file, "Position", ball->GetPosition());
		WriteVector3(file, "Velocity", ball->GetVelocity());
		file << "HP = " << ball->GetHP() << " / " << ball->GetMaxHP() << "\n";
		file << "Radius = " << ball->GetRadius() << "\n";
		file << "IsStopped = " << (ball->IsStopped() ? "true" : "false") << "\n";
		file << "IsDefeated = " << (ball->IsDefeated() ? "true" : "false") << "\n";
		file << "\n";
	}
}

// コンストラクタ
Game::Game()
	: m_RuntimeState(std::make_unique<GameRuntimeState>())
	, m_Camera(Camera::GetInstance())
	, m_BattleState(std::make_unique<GameBattleState>())
	, m_RewardState(std::make_unique<GameRewardState>())
	, m_RunDefaults(std::make_unique<GameRunDefaults>())
	, m_RunSession(std::make_unique<GameRunSession>())
	, m_RunController(std::make_unique<RunController>())
	, m_StageOverrideState(std::make_unique<GameStageOverrideState>())
	, m_BossShotPlanner(std::make_unique<BossShotPlanner>())
	, m_BallSelectionState(std::make_unique<GameBallSelectionState>())
	, m_BalanceRuntime(std::make_unique<GameBalanceRuntime>())
	, m_SettingsRuntime(std::make_unique<GameSettingsRuntime>())
	, m_PersistenceRuntime(std::make_unique<GamePersistenceRuntime>())
	, m_PendingShotTelemetry(std::make_unique<nlohmann::json>(
		nlohmann::json::object()))
	, m_EncounterBalance(std::make_unique<GameEncounterBalance>())
	, m_DebugController(std::make_unique<GameDebugController>())
	, m_PauseState(std::make_unique<GamePauseState>())
{
	InitializeBattleSceneFactory();
}

// デストラクタ
Game::~Game()
{
	m_RuntimeState->ResetScene();
	DeleteAllGameObjects();
}

// Debug Combat Forecastを無効化
void GameDebugController::InvalidateCombatForecast(const char* reason)
{
	m_DebugCombatForecastDirty = true;
	if (reason != nullptr && reason[0] != '\0')
	{
		m_DebugCombatForecastPendingReason = reason;
	}
}

// Debug Combat Forecastを更新
void GameDebugController::RefreshCombatForecast(Game& game)
{
	if (!m_DebugCombatForecastDirty)
	{
		return;
	}

	CombatForecastSnapshot next{};
	next.updateRevision = m_DebugCombatForecast.updateRevision + 1;
	next.updateReason = m_DebugCombatForecastPendingReason;

	const std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	const PlayerBall* player = players.empty() ? nullptr : players.front();
	if (player != nullptr)
	{
		next.hasPlayer = true;
		next.playerCurrentHp = player->GetHP();
		next.playerMaxHp = player->GetMaxHP();
		next.playerShield = game.GetPlayerShield();
		next.hpAfterAttack = (std::max)(0, player->GetHP());
	}
	int remainingShield = next.playerShield;

	next.enemies.reserve(enemies.size());
	for (const EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		EnemyCombatSnapshot enemySnapshot{};
		enemySnapshot.id = enemy->GetEnemyId();
		enemySnapshot.currentHp = enemy->GetHP();
		enemySnapshot.maxHp = enemy->GetMaxHP();
		enemySnapshot.attack = enemy->GetAttack();
		const bool canAttack = CanEnemyAttackThisTurn(enemy);
		enemySnapshot.defeated = enemy->IsDefeated();
		enemySnapshot.pocketed = enemy->IsPocketed();
		enemySnapshot.canAttack = canAttack;
		enemySnapshot.state = enemy->IsDefeated()
			? "撃破"
			: enemy->IsPocketed()
				? "ポケット中"
				: canAttack
					? "攻撃予定"
					: "攻撃不可";

		if (canAttack && player != nullptr && !player->IsDefeated())
		{
			next.attackerCount++;
			enemySnapshot.damageBeforeMinimum = enemy->GetAttack();
			enemySnapshot.expectedDamage =
				PlayerBall::CalculateIncomingDamage(enemy->GetAttack());
			enemySnapshot.minimumDamageApplied =
				enemySnapshot.damageBeforeMinimum < 1;
			next.theoreticalDamage += enemySnapshot.expectedDamage;
			const int absorbed = (std::min)(
				remainingShield,
				enemySnapshot.expectedDamage);
			remainingShield -= absorbed;
			const int appliedDamage = (std::min)(
				next.hpAfterAttack,
				enemySnapshot.expectedDamage - absorbed);
			next.expectedDamage += appliedDamage;
			next.hpAfterAttack -= appliedDamage;
		}

		next.enemies.push_back(std::move(enemySnapshot));
	}

	next.overkillDamage = (std::max)(
		0,
		next.theoreticalDamage - next.playerCurrentHp);
	next.lethal = next.hasPlayer &&
		next.playerCurrentHp > 0 &&
		next.hpAfterAttack <= 0;
	m_DebugCombatForecast = std::move(next);
	m_DebugCombatForecastDirty = false;
}

// Debug Player Damageを記録
void GameDebugController::RecordPlayerDamage(
	const std::string& source,
	const std::string& sourceId,
	int damage,
	int hpBefore,
	int hpAfter)
{
	if (damage <= 0)
	{
		return;
	}

	PlayerDamageRecord record{};
	record.sequence = ++m_DebugDamageSequence;
	record.source = source;
	record.sourceId = sourceId;
	record.damage = damage;
	record.hpBefore = hpBefore;
	record.hpAfter = hpAfter;
	m_DebugPlayerDamageHistory.push_front(std::move(record));
	constexpr std::size_t kMaximumHistory = 10;
	while (m_DebugPlayerDamageHistory.size() > kMaximumHistory)
	{
		m_DebugPlayerDamageHistory.pop_back();
	}
}

bool Game::IsRunSessionActive() const
{
	return m_RunSession->IsActive();
}

void Game::BeginRunSession()
{
	m_RunSession->Begin();
}

void Game::ActivateRunSession()
{
	m_RunSession->Activate();
}

void Game::DeactivateRunSession()
{
	m_RunSession->Deactivate();
}

void Game::CompleteRunSession(RunResultSnapshot result)
{
	m_RunSession->Complete(std::move(result));
}

const RunResultSnapshot& Game::GetLastRunResult() const
{
	return m_RunSession->LastResult();
}

bool Game::IsClearRewardActive() const
{
	return m_RewardState->IsActive();
}

int Game::GetClearRewardBallOfferCount() const
{
	return m_RewardState->BallOfferCount();
}

int Game::GetClearRewardBallOfferCatalogIndex(int offerIndex) const
{
	return m_RewardState->BallOfferCatalogIndex(offerIndex);
}

const PlayerBallData* Game::GetClearRewardBallOffer(int offerIndex) const
{
	return m_RunController->Deck().GetCatalogBall(
		GetClearRewardBallOfferCatalogIndex(offerIndex));
}

bool Game::AddClearRewardBallOffer(int offerIndex)
{
	return m_RunController->Deck().AddCatalogBall(
		GetClearRewardBallOfferCatalogIndex(offerIndex));
}

int Game::GetSelectedBallOfferIndex() const
{
	return m_BallSelectionState->SelectedOfferIndex();
}

int Game::GetHeldBallOfferIndex() const
{
	return m_BallSelectionState->HeldOfferIndex();
}

bool Game::IsMidBossRelicSelectionActive() const
{
	return m_RewardState->IsMidBossRelicSelectionActive();
}

bool Game::IsClearRewardChosen() const
{
	return m_RewardState->IsChosen();
}

const StageData* Game::GetCurrentStageOverride() const
{
	return m_StageOverrideState->Current();
}

int GameDebugController::BeginEnemyAttackForecast(Game& game)
{
	InvalidateCombatForecast("敵攻撃開始");
	RefreshCombatForecast(game);
	return m_DebugCombatForecast.expectedDamage;
}

void GameDebugController::EndEnemyAttackForecast(
	int predictedDamage,
	int actualDamage)
{
	m_DebugLastEnemyAttackComparisonValid = true;
	m_DebugLastEnemyAttackPredictedDamage = predictedDamage;
	m_DebugLastEnemyAttackActualDamage = actualDamage;
}

void GameDebugController::ResetDiagnostics(const char* reason)
{
	m_DebugPlayerDamageHistory.clear();
	m_DebugDamageSequence = 0;
	m_DebugLastEnemyAttackComparisonValid = false;
	InvalidateCombatForecast(reason);
}

void Game::InvalidateDebugCombatForecast(const char* reason)
{
	m_DebugController->InvalidateCombatForecast(reason);
}

void Game::RecordDebugPlayerDamage(
	const std::string& source,
	const std::string& sourceId,
	int damage,
	int hpBefore,
	int hpAfter)
{
	m_DebugController->RecordPlayerDamage(
		source,
		sourceId,
		damage,
		hpBefore,
		hpAfter);
}

void Game::RequestPauseToggle()
{
	m_PauseState->RequestPauseToggle();
}

void Game::RequestManualSave()
{
	m_PauseState->RequestManualSave();
}

void Game::RequestFullscreenToggle()
{
	m_PauseState->RequestFullscreenToggle();
}

bool Game::IsPaused() const
{
	return m_PauseState->IsPaused();
}

void Game::SetPaused(bool paused)
{
	m_PauseState->SetPaused(paused);
}

void Game::ResumeAndDismissPause()
{
	m_PauseState->ResumeAndDismissConfirmation();
}

// Pauseが可能か判定
bool Game::CanPause() const
{
	return IsRunSessionActive() &&
		(dynamic_cast<BattleScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<StageSelectScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<RestSiteScene*>(GetCurrentScene()) != nullptr ||
		 dynamic_cast<ShopScene*>(GetCurrentScene()) != nullptr);
}

// And Return To Titleを保存
bool Game::SaveAndReturnToTitle()
{
	if (IsDebugMode()) { m_DebugController->RequestExit(); return true; }
	if (IsClearRewardActive() && !IsClearRewardChosen())
	{
		SetSaveLoadMessage(
			"報酬を選択して次のルートへ進んでから保存してください。");
		return false;
	}

	CaptureCurrentPlayerStatus();
	SceneType resumeScene = SceneType::Max;
	bool sceneAlreadyActive = true;
	if (dynamic_cast<BattleScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = IsClearRewardActive()
			? SceneType::Select
			: SceneType::Battle;
		sceneAlreadyActive = false;
	}
	else if (dynamic_cast<StageSelectScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = SceneType::Select;
	}
	else if (dynamic_cast<RestSiteScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = SceneType::RestSite;
	}
	else if (dynamic_cast<ShopScene*>(GetCurrentScene()) != nullptr)
	{
		resumeScene = SceneType::Shop;
	}

	if (resumeScene == SceneType::Max ||
		!SaveRunCheckpoint(resumeScene, sceneAlreadyActive, true))
	{
		return false;
	}

	ResumeAndDismissPause();
	DeactivateRunSession();
	ChangeScene(SceneType::Title);
	return true;
}

// 初期化
void Game::Init()
{
	// 静的インスタンスをここで1つだけ生成
	if (m_Instance == nullptr) {
		m_Instance = new Game();
	}
	// 描画処理を初期化
	Renderer::Init();
	m_Instance->m_SettingsRuntime->Initialize();

	// 入力処理を初期化
	Input::Create();

	// カメラを初期化
	m_Instance->m_Camera.Init();

	m_Instance->m_PersistenceRuntime->Profile().Load();
	m_Instance->LoadPlayerStatusFromJson();
	m_Instance->m_BalanceRuntime->autoPlayer.LoadConfig();
	m_Instance->LoadDifficultyProfileConfig();
	m_Instance->m_BalanceRuntime->scaling.LoadConfig();
	m_Instance->m_BalanceRuntime->validation.LoadConfig(
		"assets/data/balance_validation.json",
		m_Instance->m_BalanceRuntime->baselineDifficultyProfile);
	m_Instance->LoadEncounterBalanceConfig();
	m_Instance->LoadPocketRulesConfig();
	m_Instance->InitializeBattleController();

	// 最初のシーンを読み込む
	m_Instance->m_RuntimeState->InitializeScene(SceneType::Title);
	m_Instance->m_GameMcpBridge =
		std::make_unique<GameMcpBridge>();
	m_Instance->m_GameMcpBridge->Initialize(*m_Instance);
	m_Instance->m_GamePresentation =
		std::make_unique<GamePresentation>();
	m_Instance->m_GamePresentation->Initialize(*m_Instance);
	// 保存済みの実験条件を直接開くための任意起動引数。
	if (std::wstring(GetCommandLineW()).find(L"--debug-battle") != std::wstring::npos)
	{
		m_Instance->OpenDebugMode();
		if (m_Instance->LoadDebugPreset()) m_Instance->m_DebugController->RequestStart();
	}
}

// 更新
void Game::Update(double elapsedSeconds)
{
	m_Instance->RemoveDestroyedGameObjects();

	// 入力処理を更新
	Input::Update();
	if (m_Instance->UpdateDebugMode()) return;
	if (m_Instance->m_PauseState->ConsumeDisplayApplyRequest())
	{
		m_Instance->m_SettingsRuntime->ApplyDisplaySettings();
	}
	if (m_Instance->m_PauseState->ConsumeFullscreenToggleRequest() || Input::GetKeyTrigger(VK_F11))
	{
		m_Instance->m_SettingsRuntime->ToggleFullscreen();
	}
	if ((m_Instance->m_PauseState->ConsumePauseToggleRequest() || Input::GetKeyTrigger(VK_ESCAPE)) &&
		m_Instance->CanPause() &&
		!m_Instance->m_BalanceRuntime->autoPlayer.IsEnabled())
	{
		const bool wasPaused = m_Instance->m_PauseState->TogglePaused();
		if (wasPaused)
		{
			m_Instance->m_SettingsRuntime->SaveIfDirty();
		}
	}
	if ((m_Instance->m_PauseState->ConsumeManualSaveRequest() || Input::GetKeyTrigger(VK_F6)) &&
		!m_Instance->m_BalanceRuntime->autoPlayer.IsEnabled())
	{
		m_Instance->SaveCurrentRun();
	}
	if (m_Instance->m_PauseState->IsPaused())
	{
		ResetFrameTiming();
		return;
	}
	m_Instance->m_PersistenceRuntime->AdvanceFrame(
		m_Instance->IsRunSessionActive());
	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Update(*m_Instance);
	}

	if (m_Instance->m_GameMcpBridge != nullptr)
	{
		m_Instance->m_GameMcpBridge->Update(*m_Instance);
	}

	if (m_Instance->m_BalanceRuntime->autoPlayer.Update(*m_Instance))
	{
		ResetFrameTiming();
		return;
	}

	// ==========================
	// ClearReward中はゲーム本編を更新しない
	// ==========================
	if (m_Instance->IsClearRewardActive())
	{
		ResetFrameTiming();
		m_Instance->UpdateClearReward();
		return;
	}

	if (!m_Instance->m_BalanceRuntime->autoPlayer.IsEnabled() &&
		dynamic_cast<BattleScene*>(m_Instance->GetCurrentScene()) != nullptr &&
		m_Instance->m_RunController->Deck().GetOfferCount() > 0)
	{
		m_Instance->UpdateBallSelection();
	}

	// シーンを更新
	if (Scene* scene = m_Instance->GetCurrentScene())
	{
		scene->Update();
	}

	// カメラを更新
	m_Instance->m_Camera.Update();

	// オブジェクトを更新
	m_Instance->m_RuntimeState->World().Update();

	// Input/AI/MCP have run once. Only physical motion may catch up here.
	m_Instance->UpdateFixedPhysics(elapsedSeconds);

	m_Instance->m_RuntimeState->World().LateUpdate();
	m_Instance->RemoveDestroyedGameObjects();
	m_Instance->m_BattleState->controller.Update();

	const BattleResult battleResult =
		m_Instance->m_BattleState->controller.ConsumeResult();

	if (battleResult != BattleResult::None)
	{
		m_Instance->m_LastBattleResult = battleResult;
	}

	switch (battleResult)
	{
	case BattleResult::Victory:
		m_Instance->StartClearReward();
		return;

	case BattleResult::Defeat:
		m_Instance->ProcessGameOver();
		return;

	default:
		break;
	}

}

// Frame Timingを初期状態へ戻す。
void Game::ResetFrameTiming()
{
	if (m_Instance != nullptr)
	{
		m_Instance->m_RuntimeState->ResetPhysicsTiming();
	}
}

// Fixed Physicsを更新
void Game::UpdateFixedPhysics(double elapsedSeconds)
{
	const int steps =
		m_RuntimeState->BeginPhysicsFrame(elapsedSeconds);

	for (int step = 0; step < steps; ++step)
	{
		const BattleState previousState =
			m_BattleState->controller.GetState();

		m_RuntimeState->World().FixedUpdate();

		const BattleState physicsState = GetBattleState();
		const BallPhysicsWorldContext physicsContext{
			m_RuntimeState->World(),
			[this, physicsState]()
			{
				return GetBattleState() == physicsState;
			},
			[this](
				BallComponent& ball,
				const Vector3& from,
				const Vector3& to)
			{
				NotifyTraceMovement(ball, from, to);
			}
		};
		const auto physicsResult =
			BallPhysicsWorld::Step(physicsContext);

		m_RuntimeState->RecordPhysicsStep(
			physicsResult.substeps,
			physicsResult.limitReached);

		const bool stopCatchUp =
			m_BattleState->controller.OnFixedStepCompleted();

		const BattleState currentState =
			m_BattleState->controller.GetState();

		if (currentState != previousState ||
			stopCatchUp ||
			m_BattleState->controller.IsFinished())
		{
			if (previousState == BattleState::BallsMoving)
			{
				GameShotPrediction::WriteVerificationActual();
			}

			m_RuntimeState->StopPhysicsCatchUp();
			break;
		}
	}
}

// 描画
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
			enemy.id.c_str(),
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
					record.sourceId.empty() ? "-" : record.sourceId.c_str(),
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
					record.sourceId.empty() ? "-" : record.sourceId.c_str(),
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

void Game::Draw()
{
	Renderer::DrawStart();

	m_Instance->m_RuntimeState->World().Draw();

	ImGui::BeginDisabled(m_Instance->IsPaused() || m_Instance->m_DebugController->IsEditorOpen());
	if (m_Instance->GetCurrentScene() != nullptr)
	{
		m_Instance->GetCurrentScene()->DrawUI();
	}

	m_Instance->m_DebugController->DrawDiagnostics(*m_Instance);
	if (dynamic_cast<BattleScene*>(m_Instance->GetCurrentScene()) != nullptr &&
		m_Instance->m_RunController->Deck().GetOfferCount() > 0)
	{
		m_Instance->DrawBallSelectionUI();
	}

	// ==========================
	// 報酬UIを最後に重ねる
	// ==========================
	if (m_Instance->IsClearRewardActive())
	{
		m_Instance->DrawClearRewardUI();
	}

	ImGui::EndDisabled();

	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Draw(*m_Instance);
	}

	m_Instance->DrawDebugMode();
	if (m_Instance->IsPaused() && !m_Instance->m_DebugController->IsEditorOpen())
	{
		m_Instance->DrawPauseUI();
	}

	if (!m_Instance->GetSaveLoadMessage().empty())
	{
		ImGui::SetNextWindowPos(GameUi::MainPosition(20.0f, ImGui::GetMainViewport()->WorkSize.y - 40.0f), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
		ImGui::SetNextWindowBgAlpha(0.88f);
		const ImGuiWindowFlags saveNoticeFlags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoNav;
		ImGui::Begin("##SaveLoadNotice", nullptr, saveNoticeFlags);
		ImGui::TextUnformatted(m_Instance->GetSaveLoadMessage().c_str());
		ImGui::End();
	}

	// Application owns ImGui rendering and presentation, including detached windows.
}

// Pause UIを描画




PausePresentationIntent GamePresentation::DrawPause(
	const PausePresentationModel& model)
{
	PausePresentationIntent intent{};
	intent.settings = model.settings;
	GameSettings& settings = intent.settings;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.61f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags dimmerFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus;
	ImGui::Begin("##PauseDimmer", nullptr, dimmerFlags);
	ImGui::End();
	ImGui::PopStyleVar(2);

	GameUi::PrepareWindow("pause", ImVec2(335, 35), ImVec2(610, 650));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("ポーズ", nullptr, flags);

	if (ImGui::Button("ポーズ解除", ImVec2(-1.0f, 42.0f)))
	{
		intent.resume = true;
	}
	ImGui::TextDisabled("Escでもゲームへ戻れます");
	ImGui::SeparatorText("音量");
	bool settingsChanged = false;
	int bgmPercent = static_cast<int>(std::lround(settings.bgmVolume * 100.0f));
	int sePercent = static_cast<int>(std::lround(settings.seVolume * 100.0f));
	if (ImGui::SliderInt("BGM音量", &bgmPercent, 0, 100, "%d%%"))
	{
		settings.bgmVolume = static_cast<float>(bgmPercent) / 100.0f;
		settingsChanged = true;
	}
	if (ImGui::SliderInt("SE音量", &sePercent, 0, 100, "%d%%"))
	{
		settings.seVolume = static_cast<float>(sePercent) / 100.0f;
		settingsChanged = true;
	}
	ImGui::TextDisabled("音声実装後に利用する設定値として保存されます");

	ImGui::SeparatorText("演出");
	const bool vibrationWasEnabled = settings.vibrationEnabled;
	settingsChanged |= ImGui::Checkbox(
		"コントローラー振動",
		&settings.vibrationEnabled);
	settingsChanged |= ImGui::Checkbox(
		"画面フラッシュ",
		&settings.screenFlashEnabled);
	settingsChanged |= ImGui::Checkbox(
		"カメラ揺れ",
		&settings.cameraShakeEnabled);
	if (vibrationWasEnabled && !settings.vibrationEnabled)
	{
		Input::SetVibration(0, 0.0f);
	}

	ImGui::SeparatorText("画面");
	settingsChanged |= ImGui::Checkbox(
		"フルスクリーン",
		&settings.fullscreen);
	if (ImGui::BeginCombo(
		"解像度",
		SettingsManager::GetResolutionLabel(settings.resolutionIndex)))
	{
		for (int index = 0;
			index < SettingsManager::GetResolutionCount();
			++index)
		{
			const bool selected = index == settings.resolutionIndex;
			if (ImGui::Selectable(
				SettingsManager::GetResolutionLabel(index),
				selected))
			{
				settings.resolutionIndex = index;
				settingsChanged = true;
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("画面設定を適用", ImVec2(-1.0f, 34.0f)))
	{
		intent.applyDisplay = true;
	}

	ImGui::SeparatorText("ランを中断");
	if (model.debugMode)
	{
		if (ImGui::Button("デバッグを終了してタイトルへ", ImVec2(-1, 40)))
			intent.requestDebugExit = true;
	}
	else if (!model.confirmReturnToTitle)
	{
		if (ImGui::Button("セーブしてタイトルへ戻る", ImVec2(-1.0f, 40.0f)))
		{
			intent.requestReturnConfirmation = true;
		}
	}
	else
	{
		ImGui::TextWrapped(
			"ランのセーブデータは1個だけです。現在のセーブを上書きしてタイトルへ戻ります。");
		if (model.battleScene && !model.clearRewardActive)
		{
			ImGui::TextDisabled("戦闘中のランは、この戦闘の最初から再開します。");
		}
		if (ImGui::Button("上書きして戻る", ImVec2(260.0f, 38.0f)))
		{
			intent.saveAndReturnToTitle = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("キャンセル", ImVec2(260.0f, 38.0f)))
		{
			intent.cancelReturnToTitle = true;
		}
	}

	if (settingsChanged)
	{
		intent.settingsChanged = true;
	}
	ImGui::End();
	return intent;
}

// 終了処理
void Game::Uninit()
{
	if (m_Instance != nullptr)
	{
		m_Instance->m_SettingsRuntime->SaveIfDirty();
		m_Instance->m_GamePresentation.reset();
		if (m_Instance->m_GameMcpBridge != nullptr)
		{
			m_Instance->m_GameMcpBridge->Shutdown(*m_Instance);
		}

		const std::vector<PlayerBall*> players =
			GameRuntime::World().GetComponents<PlayerBall>();
		const std::vector<EnemyBall*> enemies =
			GameRuntime::World().GetComponents<EnemyBall>();
		const int playerHp =
			players.empty() || players[0] == nullptr
			? m_Instance->m_RunController->Status().currentHp
			: players[0]->GetHP();

		BalanceLogger& logger =
			BalanceLogger::GetInstance();
		logger.EndShot(
			playerHp,
			CountAliveEnemies(enemies),
			CountDefeatedEnemies(enemies));
		logger.EndStage(
			"application_exit",
			playerHp,
			m_Instance->m_RunController->Status().maxHp,
			CountDefeatedEnemies(enemies));
		logger.EndRun(
			"application_exit",
			playerHp,
			m_Instance->m_RunController->Status().maxHp,
			m_Instance->m_RunController->Progress().GetClearedBattleCount());
	}

	// カメラの終了処理
	m_Instance->m_Camera.Uninit();

	// オブジェクトの終了処理は所有者であるGameWorldへ委譲
	m_Instance->m_RuntimeState->World().Clear();


	// 入力処理を終了
	Input::Release();

	// 描画処理を終了
	Renderer::Uninit();

	// インスタンスを削除
	delete m_Instance;
}

// インスタンスを取得
Game* Game::GetInstance()
{
	return m_Instance;
}

Scene* Game::GetCurrentScene() const
{
	return m_RuntimeState->CurrentScene();
}

SceneType Game::GetCurrentSceneType() const
{
	return m_RuntimeState->CurrentSceneType();
}

bool Game::IsVibrationEnabled() const
{
	return m_SettingsRuntime->IsVibrationEnabled();
}

bool Game::IsScreenFlashEnabled() const
{
	return m_SettingsRuntime->IsScreenFlashEnabled();
}

bool Game::IsCameraShakeEnabled() const
{
	return m_SettingsRuntime->IsCameraShakeEnabled();
}

const std::string& Game::GetSaveLoadMessage() const
{
	return m_PersistenceRuntime->SaveLoadMessage();
}

const ProgressionProfile& Game::GetProgressionProfile() const
{
	return m_PersistenceRuntime->Profile();
}

const std::vector<std::string>& Game::GetLastProgressionUnlocks() const
{
	return m_PersistenceRuntime->LastProgressionUnlocks();
}

int Game::GetActiveAscension() const
{
	return m_PersistenceRuntime->ActiveAscension();
}

// GameObject生成要求を専用のGameWorldへ委譲
GameObject* Game::CreateGameObject(const std::string& name)
{
	return m_RuntimeState->World().Create(name);
}

// シーン遷移前後のゲーム固有処理を調停し、シーン寿命管理をSceneManagerへ委譲
void Game::ChangeScene(SceneType sceneType)
{
	if (sceneType < SceneType::Title || sceneType >= SceneType::Max)
	{
		std::cerr << "[Game] 無効なシーンへの遷移要求を拒否しました" << std::endl;
		return;
	}

	ResetFrameTiming();
	int score = 0;
	Scene* currentScene = m_Instance->GetCurrentScene();

	if (currentScene != nullptr)
	{
		m_Instance->CaptureCurrentPlayerStatus();
		if (!m_Instance->m_PersistenceRuntime->IsRestoringRunSave() &&
			(sceneType == SceneType::Select ||
			 sceneType == SceneType::Battle ||
			 sceneType == SceneType::RestSite ||
			 sceneType == SceneType::Shop))
		{
			m_Instance->SaveRunCheckpoint(sceneType, false, false);
		}

		if (BattleScene* battleScene =
			dynamic_cast<BattleScene*>(currentScene))
		{
			score = battleScene->GetScore();
		}
	}

	// =====================================
	// 新しいステージへ入るときに報酬取得状態をリセット
	// =====================================
	if (sceneType == SceneType::Battle)
	{
		m_RewardState->Deactivate();
		m_LastBattleResult = BattleResult::None;

		m_BattleState->controller.StartBattle();

		m_RunController->Deck().Reset();

		if (!BeginBallSelection())
		{
			m_BattleState->controller.NotifyPlayerDefeated();
		}

		m_RunController->BeginStageReward();
		m_RewardState->ClearMessage();
	}
	else
	{
		m_RewardState->Deactivate();
		m_BattleState->controller.Reset();
	}

	if (sceneType == SceneType::Title && IsDebugMode())
	{
		EndDebugMode();
	}

	if (!m_Instance->m_RuntimeState->ChangeScene(sceneType))
	{
		if (sceneType == SceneType::Battle)
		{
			m_BattleState->controller.Reset();
		}
		std::cerr << "[Game] シーンの生成に失敗しました" << std::endl;
		return;
	}

	if (sceneType == SceneType::Result)
	{
		if (ResultScene* resultScene = dynamic_cast<ResultScene*>(
			m_Instance->GetCurrentScene()))
		{
			resultScene->SetScore(score);
		}
	}

	m_Instance->InvalidateDebugCombatForecast("シーン変更");
}

// GameObject削除要求を専用のGameWorldへ委譲
void Game::DeleteGameObject(GameObject* gameObject)
{
	m_RuntimeState->World().RequestDestroy(gameObject);
}

// 削除要求済みGameObjectの破棄を専用のGameWorldへ委譲
void Game::RemoveDestroyedGameObjects()
{
	m_RuntimeState->World().RemoveDestroyed();
}

// ゲーム空間に存在するすべてのGameObjectを終了して破棄
void Game::DeleteAllGameObjects()
{
	m_RuntimeState->World().Clear();
}

// All Enemies Defeatedかどうかを判定
bool Game::AreAllEnemiesDefeated() const
{
	return m_BattleState->controller.AreAllEnemiesDefeated();
}

// Current Pocket Finisher Ratioを取得
float Game::GetCurrentPocketFinisherRatio() const
{
	switch (m_BattleState->controller.GetStageType())
	{
	case StageType::MidBoss:
		return m_BattleState->controller.PocketRules().midBossFinisherRatio;
	case StageType::Boss:
		return m_BattleState->controller.PocketRules().bossFinisherRatio;
	case StageType::Normal:
	default:
		return m_BattleState->controller.PocketRules().normalFinisherRatio;
	}
}

// Enemy Pocket Finisher Eligibleかどうかを判定
bool Game::IsEnemyPocketFinisherEligible(const EnemyBall* enemy) const
{
    if (enemy && enemy->IsArmorBoss()) return false;
	if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed() ||
		enemy->GetMaxHP() <= 0)
	{
		return false;
	}
	const int pocketDamage = BallMechanics::PocketDamage(enemy->GetMaxHP(), enemy->GetPocketDamageRatio());
	const float hpRatio = static_cast<float>((std::max)(0, enemy->GetHP() - pocketDamage)) /
		static_cast<float>(enemy->GetMaxHP());
	return hpRatio <= GetCurrentPocketFinisherRatio() + 0.0001f;
}

// Player Pocket Damage Amountを取得
int Game::GetPlayerPocketDamageAmount() const
{
	return (std::max)(1, static_cast<int>(std::ceil(
		static_cast<float>(m_RunController->Status().maxHp) *
		m_BattleState->controller.PocketRules().playerDamageRatio)));
}

// Enemy Pocketを処理
void Game::HandleEnemyPocket(EnemyBall* enemy)
{
    if (enemy && enemy->IsArmorBoss()) return;
	if (enemy == nullptr || enemy->IsPocketed())
	{
		return;
	}
	const bool wasAlreadyDefeated = enemy->IsDefeated();
	const int hpBefore = enemy->GetHP();
	const int maxHp = (std::max)(1, enemy->GetMaxHP());
	const int pocketDamage = enemy->ApplyPocketDamage();
	const float hpRatio = static_cast<float>(enemy->GetHP()) /
		static_cast<float>(maxHp);
	if (pocketDamage > 0)
	{
		RecordBalanceEvent("enemy_pocket_damage", {
			{ "enemy_id", enemy->GetEnemyId() }, { "damage", pocketDamage },
			{ "hp_before", hpBefore }, { "hp_after", enemy->GetHP() },
		});
	}
	const float finisherRatio = GetCurrentPocketFinisherRatio();
	const Vector3 pocketPosition = enemy->GetPosition();
	if (wasAlreadyDefeated || enemy->IsDefeated() || hpRatio <= finisherRatio + 0.0001f)
	{
		NotifyPocketFeedback(
			pocketPosition,
			false,
			true,
			hpBefore);
		enemy->Defeat();
		if (!wasAlreadyDefeated && pocketDamage < hpBefore)
		{
			NotifyEnemyDefeated(enemy);
		}
		enemy->RemoveFromFieldAfterPocket();
		RecordBalanceEvent(
			"enemy_pocket_finisher",
			{
				{ "enemy_id", enemy->GetEnemyId() },
				{ "stage_type", ToString(m_BattleState->controller.GetStageType()) },
				{ "hp_before", hpBefore },
				{ "max_hp", maxHp },
				{ "hp_ratio", hpRatio },
				{ "finisher_ratio", finisherRatio },
				{ "already_defeated", wasAlreadyDefeated },
			});
		return;
	}

	enemy->EnterPocketQueue(pocketPosition);
	NotifyPocketFeedback(
		pocketPosition,
		false,
		false,
		pocketDamage);
	m_BattleState->controller.QueuePocketedEnemy(enemy);
	RecordBalanceEvent(
		"enemy_pocket_controlled",
		{
			{ "enemy_id", enemy->GetEnemyId() },
			{ "stage_type", ToString(m_BattleState->controller.GetStageType()) },
			{ "hp", enemy->GetHP() },
			{ "max_hp", maxHp },
			{ "hp_ratio", hpRatio },
			{ "finisher_ratio", finisherRatio },
			{ "queue_size", m_BattleState->controller.GetPocketQueueSize() },
		});
}

// Pocket Queue Indexを取得
int Game::GetPocketQueueIndex(const EnemyBall* enemy) const
{
	return m_BattleState->controller.GetPocketQueueIndex(enemy);
}

// Player Pocket Return Positionを検索
Vector3 Game::FindPlayerPocketReturnPosition(const PlayerBall* player)
{
	std::uniform_real_distribution<float> xDistribution(
		-m_BattleState->controller.PocketRules().playerReturnHalfWidth,
		m_BattleState->controller.PocketRules().playerReturnHalfWidth);
	std::uniform_real_distribution<float> zDistribution(
		-m_BattleState->controller.PocketRules().playerReturnHalfDepth,
		m_BattleState->controller.PocketRules().playerReturnHalfDepth);
	const float playerRadius = player != nullptr && player->GetBall() != nullptr
		? player->GetBall()->GetRadius()
		: 2.4f;

	for (int attempt = 0; attempt < 24; attempt++)
	{
		const Vector3 candidate(
			xDistribution(m_BattleState->controller.PocketRandomEngine()),
			TableConfig::FIELD_HEIGHT,
			zDistribution(m_BattleState->controller.PocketRandomEngine()));
		bool blocked = false;
		for (BallComponent* ball :
			GameRuntime::World().GetComponents<BallComponent>())
		{
			if (ball == nullptr || ball->GetGameObject() == nullptr ||
				!ball->GetGameObject()->IsActive() ||
				(player != nullptr && ball == player->GetBall()))
			{
				continue;
			}
			Vector3 difference = candidate - ball->GetPosition();
			difference.y = 0.0f;
			const float clearance = playerRadius + ball->GetRadius() + 0.5f;
			if (difference.LengthSquared() < clearance * clearance)
			{
				blocked = true;
				break;
			}
		}
		if (!blocked)
		{
			return candidate;
		}
	}
	return Vector3(0.0f, TableConfig::FIELD_HEIGHT, 0.0f);
}

// Pocketed Playerを復元
void Game::RestorePocketedPlayer()
{
	for (PlayerBall* player :
		GameRuntime::World().GetComponents<PlayerBall>())
	{
		if (player == nullptr || player->IsDefeated() ||
			!player->IsPocketed())
		{
			continue;
		}

		const Vector3 returnPosition =
			FindPlayerPocketReturnPosition(player);
		player->ReturnFromPocket(returnPosition);
		RecordBalanceEvent(
			"player_pocket_returned",
			{
				{ "position_x", returnPosition.x },
				{ "position_z", returnPosition.z },
			});
	}
}

// Enemy Pocket Return Positionを検索
Vector3 Game::FindEnemyPocketReturnPosition(
	const EnemyBall* returningEnemy) const
{
	const float radius = returningEnemy != nullptr
		? returningEnemy->GetRadius()
		: 2.4f;
	const Vector3 pocketEntryPosition = returningEnemy != nullptr
		? returningEnemy->GetPocketEntryPosition()
		: Vector3(0.0f, TableConfig::FIELD_HEIGHT,
			TableConfig::GetFieldDepth() * 0.5f);
	const float inwardDistance = (std::max)(
		m_BattleState->controller.PocketRules().enemyReturnTopEdgeOffset,
		TableConfig::POCKET_RADIUS + radius + 0.5f);
	const Vector3 basePosition = PocketReturnRules::ReturnAnchor(
		pocketEntryPosition,
		inwardDistance);
	const Vector3 inward = PocketReturnRules::InwardDirection(
		pocketEntryPosition);
	const Vector3 tangent(-inward.z, 0.0f, inward.x);
	const float spacing = radius * 2.0f + 1.0f;
	const std::array<int, 9> offsets{ 0, -1, 1, -2, 2, -3, 3, -4, 4 };
	const float horizontalLimit =
		TableConfig::GetFieldWidth() * 0.5f - radius - 0.5f;
	const float verticalLimit =
		TableConfig::GetFieldDepth() * 0.5f - radius - 0.5f;
	for (int depthStep = 0; depthStep < 5; ++depthStep)
	{
		for (int offset : offsets)
		{
			const float depthOffset = static_cast<float>(depthStep) * spacing;
			const float lateralOffset = static_cast<float>(offset) * spacing;
			const Vector3 candidate(
				basePosition.x + inward.x * depthOffset + tangent.x * lateralOffset,
				TableConfig::FIELD_HEIGHT,
				basePosition.z + inward.z * depthOffset + tangent.z * lateralOffset);
			if (std::abs(candidate.x) > horizontalLimit ||
				std::abs(candidate.z) > verticalLimit)
			{
				continue;
			}
			bool blocked = false;
			for (BallComponent* ball :
				GameRuntime::World().GetComponents<BallComponent>())
			{
				if (ball == nullptr || ball->GetGameObject() == nullptr ||
					!ball->GetGameObject()->IsActive() ||
					(returningEnemy != nullptr &&
						ball == returningEnemy->GetBall()))
				{
					continue;
				}
				Vector3 difference = candidate - ball->GetPosition();
				difference.y = 0.0f;
				const float clearance = radius + ball->GetRadius() + 0.5f;
				if (difference.LengthSquared() < clearance * clearance)
				{
					blocked = true;
					break;
				}
			}
			if (!blocked)
			{
				return candidate;
			}
		}
	}
	return Vector3(
		std::clamp(basePosition.x, -horizontalLimit, horizontalLimit),
		TableConfig::FIELD_HEIGHT,
		std::clamp(basePosition.z, -verticalLimit, verticalLimit));
}

// Next Pocketed Enemyを復元
void Game::RestoreNextPocketedEnemy()
{
	while (m_BattleState->controller.GetPocketQueueSize() > 0)
	{
		EnemyBall* enemy = m_BattleState->controller.PopPocketedEnemy();
		if (enemy == nullptr || !ContainsComponent(enemy) ||
			enemy->IsDefeated() || !enemy->IsPocketed())
		{
			continue;
		}
		const Vector3 returnPosition =
			FindEnemyPocketReturnPosition(enemy);
		enemy->ReturnFromPocket(returnPosition);
		InvalidateDebugCombatForecast("エネミー復帰");
		RecordBalanceEvent(
			"enemy_pocket_returned",
			{
				{ "enemy_id", enemy->GetEnemyId() },
				{ "pocket_entry_x", enemy->GetPocketEntryPosition().x },
				{ "pocket_entry_z", enemy->GetPocketEntryPosition().z },
				{ "position_x", returnPosition.x },
				{ "position_z", returnPosition.z },
				{ "remaining_queue_size", m_BattleState->controller.GetPocketQueueSize() },
			});
		break;
	}
}

// Finalize Run Result の処理を実行
void Game::FinalizeRunResult(bool completed)
{
	std::vector<RelicType> acquiredRelics;
	for (const RelicDefinition& relic : RelicCatalog)
	{
		if (HasRelic(relic.type))
		{
			acquiredRelics.push_back(relic.type);
		}
	}
	RunResultSnapshot result = m_PersistenceRuntime->Statistics().CreateSnapshot(
		completed,
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		acquiredRelics);
	result.clearedBattles =
		m_RunController->Progress().GetClearedBattleCount();
	CompleteRunSession(std::move(result));
	RecordPersistentProgress();
}

// Persistent Progressを記録
void Game::RecordPersistentProgress()
{
	auto& unlocks = m_PersistenceRuntime->LastProgressionUnlocks();
	unlocks.clear();
	if (!m_PersistenceRuntime->PersistentProgressEligible()) return;
	try
	{
		unlocks = m_PersistenceRuntime->Profile().RecordRun(
			GetLastRunResult(),
			m_PersistenceRuntime->ActiveAscension());
		m_PersistenceRuntime->Profile().Save();
	}
	catch (const std::exception& e)
	{
		unlocks.push_back(
			std::string("恒久進行を保存できませんでした：") + e.what());
	}
}

// Selected Ascensionを設定
void Game::SetSelectedAscension(int level)
{
	if (HasValidRunSave()) return;
	auto& profile = m_PersistenceRuntime->Profile();
	profile.selectedAscension = std::clamp(
		level,
		0,
		profile.highestUnlockedAscension);
	try { profile.Save(); } catch (...) {}
}

// Normal Route Areaを完了
void Game::CompleteNormalRouteArea(const char* areaType)
{
	// 現在の通常エリアを完了し、ラン進行と関連ログを同期
	const bool longValidationRun =
		m_BalanceRuntime->validation.UsesExtendedRoute(
			kNormalRouteAreaGoal);
	const AreaCompletionResult completion =
		m_RunController->Progress().CompleteNormalArea(
			longValidationRun,
			m_BalanceRuntime->validation.GetMaximumClearedStages(),
			m_BalanceRuntime->routeSelectionSeed + static_cast<std::uint32_t>(
				m_RunController->Progress().GetAreaProgress() + 1));
	if (completion == AreaCompletionResult::Rejected)
	{
		return;
	}

	const int areaProgress = m_RunController->Progress().GetAreaProgress();
	m_RunController->Status().progress = (std::min)(
		kNormalRouteAreaGoal,
		areaProgress + 1);
	m_PersistenceRuntime->Statistics().CompleteArea(areaProgress);
	RecordBalanceEvent(
		"route_area_completed",
		{
			{ "map_path", m_RunController->Progress().GetMap().Path() },
			{ "area_type", areaType != nullptr ? areaType : "unknown" },
			{ "area_progress", areaProgress },
			{ "area_goal", kNormalRouteAreaGoal },
		});

	if (completion == AreaCompletionResult::MapExtended)
	{
		RecordBalanceEvent(
			"run_map_extended",
			m_RunController->Progress().GetMap().Snapshot());
	}
	if (completion == AreaCompletionResult::BossPreparationEntered)
	{
		m_RunController->Status().progress = kNormalRouteAreaGoal;
		RecordBalanceEvent(
			"boss_preparation_entered",
			{
				{ "area_progress", areaProgress },
			});
	}
}

// Next Route After Areaへ進める。
void Game::EnterNextRouteAfterArea()
{
	// 現在のランフェーズに応じて次のルート選択先へ遷移
	if (m_RunController->Progress().IsBossPreparation())
	{
		ChangeScene(SceneType::RestSite);
	}
	else
	{
		ChangeScene(SceneType::Select);
	}
}

// Shopから退出
void Game::LeaveShop()
{
	CompleteNormalRouteArea("shop");
	EnterNextRouteAfterArea();
}

// Rest Siteから退出
void Game::LeaveRestSite()
{
	// 休憩所の利用完了を記録して次のルートへ進める。
	if (m_RunController->Progress().IsBossPreparation())
	{
		if (!m_RunController->Progress().CompleteBossPreparation())
		{
			return;
		}
		RecordBalanceEvent(
			"boss_preparation_completed",
			{
				{ "area_progress", m_RunController->Progress().GetAreaProgress() },
				{ "next_phase", ToString(m_RunController->Progress().GetPhase()) },
			});
		ChangeScene(SceneType::Select);
		return;
	}

	CompleteNormalRouteArea("rest_site");
	EnterNextRouteAfterArea();
}

// Continue After Clear Reward の処理を実行
void Game::ContinueAfterClearReward()
{
	m_RewardState->Exit();
	EnterNextRouteAfterArea();
}

// Final Boss Runを完了
void Game::CompleteFinalBossRun()
{
	// 最終ボス撃破時のラン完了処理と永続結果を確定
	m_RunController->Progress().RecordBattleCleared();
	m_PersistenceRuntime->Statistics().DefeatFinalBoss();
	m_RunController->Progress().CompleteFinalBoss();
	RecordBalanceEvent(
		"final_boss_defeated",
		{
			{ "stage_id", m_RunController->Status().GetSelectedStageId() },
			{ "area_progress", m_RunController->Progress().GetAreaProgress() },
			{ "cleared_battle_count", m_RunController->Progress().GetClearedBattleCount() },
		});
	BalanceLogger::GetInstance().EndRun(
		"completed",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		m_RunController->Progress().GetClearedBattleCount());
	FinalizeRunResult(true);
	GameSaveManager::Remove();
	ChangeScene(SceneType::Result);
}

// Game Overを処理
void Game::ProcessGameOver()
{
	m_RewardState->Exit();
	if (IsDebugMode()) { FinishDebugBattle(false); return; }
	DiscardCurrentPlayerBall();

	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_RunController->Status().currentHp,
		CountAliveEnemies(enemies),
		CountDefeatedEnemies(enemies));
	logger.EndStage(
		"game_over",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		CountDefeatedEnemies(enemies));
	logger.EndRun(
		"game_over",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		m_RunController->Progress().GetClearedBattleCount());
	FinalizeRunResult(false);
	GameSaveManager::Remove();

	ChangeScene(SceneType::Result);
}

// Clear Rewardの新規ボール候補を、重複を許して3枠抽選
void Game::RollClearRewardBallOffers()
{
	m_RewardState->SetBallOffers(
		m_RunController->Deck().RollCatalogOfferIndices(
			kClearRewardBallOfferSize));
}

// Clear Rewardを開始
void Game::StartClearReward()
{
	if (IsDebugMode()) { FinishDebugBattle(true); return; }
	DiscardCurrentPlayerBall();

	const std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_RunController->Status().currentHp,
		CountAliveEnemies(enemies),
		CountDefeatedEnemies(enemies));
	logger.EndStage(
		"clear",
		m_RunController->Status().currentHp,
		m_RunController->Status().maxHp,
		CountDefeatedEnemies(enemies));
	if (m_RunController->Progress().GetPhase() == RunPhase::FinalBoss &&
		m_BattleState->controller.GetStageType() == StageType::Boss)
	{
		// 最終ボス後は、以後使えないMoneyや取得物を選ばせない。
		CompleteFinalBossRun();
		return;
	}

	// 敵全滅報酬を所持Moneyへ加算する
	CollectStageRewardMoney();

	// 全滅時は攻撃フェーズへ進まないため、報酬計算後に撃破済みの敵を取り除く。
	for (EnemyBall* enemy : enemies)
	{
		if (enemy != nullptr && enemy->IsDefeated())
		{
			DeleteGameObject(enemy->GetGameObject());
		}
	}

	m_RewardState->Prepare(UiText::ChooseClearReward);
	m_RunController->MidBossRelicOffers().clear();
	if (m_BattleState->controller.GetStageType() == StageType::MidBoss)
	{
		RollMidBossRelicOffers();
	}
	m_RunController->Progress().RecordBattleCleared();
	if (m_BattleState->controller.GetStageType() == StageType::MidBoss)
	{
		m_PersistenceRuntime->Statistics().DefeatMidBoss();
	}
	CompleteNormalRouteArea(
		m_BattleState->controller.GetStageType() == StageType::MidBoss
		? "midboss_battle"
		: "normal_battle");
	if (m_BalanceRuntime->validation.HasReachedMaximumClearedStages(
		m_RunController->Progress().GetClearedBattleCount(),
		kNormalRouteAreaGoal))
	{
		RecordBalanceEvent(
			"balance_validation_run_completed",
			{
				{ "reason", "maximum_cleared_stages_reached" },
				{ "cleared_stage_count", m_RunController->Progress().GetClearedBattleCount() },
				{ "maximum_cleared_stages",
					m_BalanceRuntime->validation.GetMaximumClearedStages() },
			});
		logger.EndRun(
			"validation_complete",
			m_RunController->Status().currentHp,
			m_RunController->Status().maxHp,
			m_RunController->Progress().GetClearedBattleCount());
		FinalizeRunResult(true);
		GameSaveManager::Remove();
		ChangeScene(SceneType::Result);
		return;
	}
	RollClearRewardBallOffers();
	nlohmann::json newBallCandidates = nlohmann::json::array();
	for (int offerIndex = 0;
		offerIndex < GetClearRewardBallOfferCount();
		offerIndex++)
	{
		const int catalogIndex =
			GetClearRewardBallOfferCatalogIndex(offerIndex);
		const PlayerBallData* ball =
			GetClearRewardBallOffer(offerIndex);
		if (ball != nullptr)
		{
			newBallCandidates.push_back(
				{
					{ "offer_index", offerIndex },
					{ "catalog_index", catalogIndex },
					{ "ball_id", ball->definitionId },
				});
		}
	}
	nlohmann::json upgradeCandidates = nlohmann::json::array();
	for (int index = 0;
		index < m_RunController->Deck().GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball = m_RunController->Deck().GetRewardTarget(index);
		if (ball != nullptr && ball->CanUpgrade())
		{
			const int upgradeCost =
				GetClearRewardUpgradeCost(index);
			upgradeCandidates.push_back(
				{
					{ "deck_index", index },
					{ "instance_id", ball->instanceId },
					{ "ball_id", ball->definitionId },
					{ "upgrade_level", ball->upgradeLevel },
					{ "upgrade_cost", upgradeCost },
					{ "affordable",
						upgradeCost >= 0 &&
						m_RunController->Status().money >= upgradeCost },
				});
		}
	}
	RecordBalanceEvent(
		"clear_reward_offered",
		{
			{
				"reward_types",
				{ "new_ball", "upgrade_ball", "extra_money" }
			},
			{ "new_ball_candidates", std::move(newBallCandidates) },
			{ "upgrade_candidates", std::move(upgradeCandidates) },
			{
				"upgrade_cost_by_current_level",
				{
					{ "0", 15 },
					{ "1", 30 },
				}
			},
			{ "extra_money_amount", kClearRewardExtraMoney },
		});
	m_RewardState->Activate();
}

// Current Stageを完了
void Game::CompleteCurrentStage()
{
	if (!IsClearRewardActive())
	{
		StartClearReward();
	}
}

// Scheduled Stage Typeを取得
StageType Game::GetScheduledStageType() const
{
	// 現在のランフェーズから次に開始すべき戦闘種別を返す。
	if (m_RunController->Progress().GetPhase() == RunPhase::FinalBossReady ||
		m_RunController->Progress().GetPhase() == RunPhase::FinalBoss)
	{
		return StageType::Boss;
	}
	return StageType::Normal;
}

// Next Battleを開始
void Game::StartNextBattle()
{
	StartNextBattle(GetScheduledStageType());
}

// Next Battleを開始
void Game::StartNextBattle(StageType stageType)
{
	// 選択された戦闘種別に対応するステージを確定して戦闘へ遷移
	const bool enteringFinalBoss = m_RunController->Progress().BeginFinalBoss();
	if (enteringFinalBoss)
	{
		stageType = StageType::Boss;
	}
	const std::string previousStageId =
		m_RunController->Status().GetSelectedStageId().empty()
		? m_RunController->Status().GetLastStageId()
		: m_RunController->Status().GetSelectedStageId();

	if (StageData* overrideStage =
		m_StageOverrideState->PromoteQueuedToCurrent())
	{
		m_RunController->Status().SetLastStageId(previousStageId);
		m_RunController->Status().SetSelectedStageId(
			overrideStage->id);
		if (enteringFinalBoss)
		{
			overrideStage->stageType = StageType::Boss;
		}
		ChangeScene(SceneType::Battle);
		return;
	}

	m_StageOverrideState->ClearCurrent();
	const std::vector<StageData> stages = StageDataLoader::LoadAll(
		"assets/data/stage_01.json",
		"assets/data/enemy_data.json");

	const StageData* selectedStage = m_RunController->StageSelection().SelectStage(
		stages,
		stageType,
		m_RunController->Status().progress,
		previousStageId);
	if (selectedStage == nullptr)
	{
		m_RunController->Progress().CancelActiveNode();
		if (enteringFinalBoss)
		{
			m_RunController->Progress().CancelFinalBossStart();
		}
		std::cerr << "[Game] 戦闘ステージを選択できなかったため、"
			"シーン遷移を中止します" << std::endl;
		return;
	}

	m_RunController->Status().SetLastStageId(previousStageId);
	m_RunController->Status().SetSelectedStageId(selectedStage->id);
	ChangeScene(SceneType::Battle);
}

// Clear Rewardを更新
void Game::UpdateClearReward()
{
	const bool confirmed = m_RewardState->ConsumeConfirmationRequest() ||
		Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE);
	if (m_RewardState->IsMidBossRelicSelectionActive())
	{
		const int offerCount = GetMidBossRelicOfferCount();
		if (offerCount <= 0)
		{
			m_RewardState->FinishMidBossRelicSelection();
			return;
		}
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A) ||
			Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
		{
			m_RewardState->SelectPreviousRelic(offerCount);
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D) ||
			Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
		{
			m_RewardState->SelectNextRelic(offerCount);
		}
		if (confirmed)
		{
			AcquireMidBossRelicOffer(m_RewardState->SelectedRelicOfferIndex());
		}
		return;
	}

	if (m_RewardState->IsChosen())
	{
		if (confirmed)
		{
			ContinueAfterClearReward();
		}
		return;
	}

	if (Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
	{
		m_RewardState->SelectPreviousReward(kClearRewardCount);
	}
	if (Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
	{
		m_RewardState->SelectNextReward(kClearRewardCount);
	}

	int targetCount = 0;
	if (m_RewardState->SelectedRewardIndex() == 0)
	{
		targetCount = GetClearRewardBallOfferCount();
	}
	else if (m_RewardState->SelectedRewardIndex() == 1)
	{
		targetCount = m_RunController->Deck().GetRewardTargetCount();
	}

	if (targetCount > 0)
	{
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A))
		{
			m_RewardState->SelectPreviousBall(targetCount);
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D))
		{
			m_RewardState->SelectNextBall(targetCount);
		}
	}

	if (!confirmed)
	{
		return;
	}

	bool rewardApplied = false;
	nlohmann::json rewardDetails =
	{
		{ "money_before", m_RunController->Status().money },
	};
	switch (m_RewardState->SelectedRewardIndex())
	{
	case 0:
	{
		std::string acquiredBallId;
		const int catalogIndex =
			GetClearRewardBallOfferCatalogIndex(
				m_RewardState->SelectedRewardBallIndex());
		if (const PlayerBallData* selected =
			GetClearRewardBallOffer(
				m_RewardState->SelectedRewardBallIndex()))
		{
			acquiredBallId = selected->definitionId;
			rewardDetails["reward"] = "new_ball";
			rewardDetails["ball_id"] = selected->definitionId;
			rewardDetails["offer_index"] =
				m_RewardState->SelectedRewardBallIndex();
			rewardDetails["catalog_index"] = catalogIndex;
		}
		rewardApplied = AddClearRewardBallOffer(
			m_RewardState->SelectedRewardBallIndex());
		if (rewardApplied)
		{
			PublishGameEvent(BallAcquiredEvent{ acquiredBallId });
		}
		m_RewardState->SetMessage(rewardApplied
			? UiText::NewBallAdded
			: UiText::NoBallAvailable);
		break;
	}
	case 1:
	{
		const PlayerBallData* selected =
			m_RunController->Deck().GetRewardTarget(
				m_RewardState->SelectedRewardBallIndex());
		const int upgradeCost =
			GetClearRewardUpgradeCost(
				m_RewardState->SelectedRewardBallIndex());
		if (selected != nullptr)
		{
			rewardDetails["reward"] = "upgrade_ball";
			rewardDetails["ball_id"] = selected->definitionId;
			rewardDetails["instance_id"] = selected->instanceId;
			rewardDetails["upgrade_level_before"] = selected->upgradeLevel;
			rewardDetails["upgrade_cost"] = upgradeCost;
		}
		if (upgradeCost < 0)
		{
			m_RewardState->SetMessage(UiText::BallAlreadyMax);
		}
		else if (m_RunController->Status().money < upgradeCost)
		{
			m_RewardState->SetMessage(UiText::UpgradeMoneyShortage);
		}
		else
		{
			int chargedCost = 0;
			rewardApplied = ApplyClearRewardUpgrade(
				m_RewardState->SelectedRewardBallIndex(),
				chargedCost);
			rewardDetails["upgrade_cost"] = chargedCost;
			m_RewardState->SetMessage(rewardApplied
				? UiText::UpgradeApplied
				: UiText::UpgradeUnavailable);
		}
		break;
	}
	case 2:
		rewardDetails["reward"] = "extra_money";
		m_RunController->AddMoney(kClearRewardExtraMoney);
		rewardApplied = true;
		m_RewardState->SetMessage(UiText::ExtraMoneyReceived);
		break;
	default:
		break;
	}

	if (rewardApplied)
	{
		rewardDetails["money_after"] = m_RunController->Status().money;
		rewardDetails["controller"] = "human";
		RecordBalanceEvent("clear_reward_choice", rewardDetails);
		m_RewardState->MarkChosen();
	}
}
// Begin Ball Selection の処理を実行
bool Game::BeginBallSelection()
{
	ResetShotRelicState();

	if (!m_RunController->Deck().PrepareOffer(GetBallOfferSize()))
	{
		return false;
	}

	int heldOfferIndex = -1;
	for (int index = 0;
		index < m_RunController->Deck().GetOfferCount();
		index++)
	{
		if (m_RunController->Deck().WasHeldOffer(index))
		{
			heldOfferIndex = index;
			break;
		}
	}
	m_BallSelectionState->Begin(
		m_RunController->Deck().GetOfferCount(),
		heldOfferIndex);

	ApplySelectedBallPreview();
	return true;
}

// Ball Selectionを更新
void Game::UpdateBallSelection()
{
	const int offerCount = m_RunController->Deck().GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	for (int index = 0; index < offerCount && index < 4; index++)
	{
		if (Input::GetKeyTrigger('1' + index))
		{
			ApplyBalanceAutoBallSelection(index);
		}
	}

	constexpr int HOLD_KEYS[] = { 'Q', 'W', 'E', 'R' };
	for (int index = 0; index < offerCount && index < 4; index++)
	{
		if (!Input::GetKeyTrigger(HOLD_KEYS[index]))
		{
			continue;
		}
		m_BallSelectionState->ToggleHold(index, offerCount);
	}
}

// Selected Ball Previewを適用
void Game::ApplySelectedBallPreview()
{
	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	if (players.empty() || players[0] == nullptr)
	{
		return;
	}

	ApplyPlayerStatusTo(players[0]);
}

// Ball Selection UIを描画
BallSelectionPresentationIntent GamePresentation::DrawBallSelection(
	const BallSelectionPresentationModel& model)
{
	BallSelectionPresentationIntent intent{
		model.selectedOfferIndex,
		model.selectedHoldIndex,
		false,
	};
	const int offerCount = static_cast<int>(model.offers.size());
	if (offerCount <= 0)
	{
		return intent;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	// 左下・右下の山札表示ぶんを空け、持ち札とHOLDは中央にまとめる。
	const float horizontalPadding = std::clamp(
		viewport->WorkSize.x * 0.105f,
		70.0f,
		132.0f);
	const float gap = 14.0f;
	const float collapsedCardHeight = 158.0f;
	const float expandedCardHeight = std::clamp(
		viewport->WorkSize.y * 0.43f,
		270.0f,
		330.0f);
	const float cardBottom =
		viewport->WorkPos.y + viewport->WorkSize.y - 12.0f;
	const float availableCardWidth =
		(viewport->WorkSize.x - horizontalPadding * 2.0f -
			gap * static_cast<float>((std::max)(0, offerCount - 1))) /
		static_cast<float>(offerCount);
	const float cardWidth = (std::min)(252.0f, availableCardWidth);
	const float cardsWidth = cardWidth * static_cast<float>(offerCount) +
		gap * static_cast<float>((std::max)(0, offerCount - 1));
	const float cardsStartX = viewport->WorkPos.x +
		(viewport->WorkSize.x - cardsWidth) * 0.5f;

	const auto easedExpansion = [](float value)
	{
		value = std::clamp(value, 0.0f, 1.0f);
		return value * value * (3.0f - 2.0f * value);
	};
	const auto cardHeightFor = [&](float expansion)
	{
		return collapsedCardHeight +
			(expandedCardHeight - collapsedCardHeight) *
			easedExpansion(expansion);
	};

	const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	const float animationStep =
		(std::min)(ImGui::GetIO().DeltaTime, 0.05f) * 8.5f;
	float tallestCardHeight = collapsedCardHeight;
	for (int index = 0;
		index < offerCount && index < static_cast<int>(m_BallCardExpansion.size());
		index++)
	{
		float& expansion = m_BallCardExpansion[static_cast<std::size_t>(index)];
		const float currentHeight = cardHeightFor(expansion);
		const float cardLeft = cardsStartX +
			static_cast<float>(index) * (cardWidth + gap);
		const bool hovered =
			mousePosition.x >= cardLeft &&
			mousePosition.x <= cardLeft + cardWidth &&
			mousePosition.y >= cardBottom - currentHeight &&
			mousePosition.y <= cardBottom;
		expansion = std::clamp(
			expansion + (hovered ? animationStep : -animationStep),
			0.0f,
			1.0f);
		tallestCardHeight = (std::max)(
			tallestCardHeight,
			cardHeightFor(expansion));
	}
	for (std::size_t index = static_cast<std::size_t>((std::min)(offerCount, 4));
		index < m_BallCardExpansion.size();
		index++)
	{
		m_BallCardExpansion[index] = 0.0f;
	}

	const float panelHeight = tallestCardHeight + 24.0f;
	const ImVec2 panelPosition(
		viewport->WorkPos.x,
		cardBottom - tallestCardHeight - 12.0f);

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(panelPosition, ImGuiCond_Always);
	ImGui::SetNextWindowSize(
		ImVec2(viewport->WorkSize.x, panelHeight),
		ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::Begin("##ball_card_hud", nullptr, flags);

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilledMultiColor(
		ImVec2(panelPosition.x, cardBottom - collapsedCardHeight - 16.0f),
		ImVec2(panelPosition.x + viewport->WorkSize.x, cardBottom + 12.0f),
		IM_COL32(5, 9, 15, 0),
		IM_COL32(5, 9, 15, 0),
		IM_COL32(5, 9, 15, 218),
		IM_COL32(5, 9, 15, 218));

	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball = model.offers[static_cast<std::size_t>(index)];
		if (ball == nullptr)
		{
			continue;
		}

		const bool selected = intent.selectedOfferIndex == index;
		const bool held = intent.selectedHoldIndex == index;
		const float expansion = index < static_cast<int>(m_BallCardExpansion.size())
			? m_BallCardExpansion[static_cast<std::size_t>(index)]
			: 0.0f;
		const float cardHeight = cardHeightFor(expansion);
		const ImVec2 cardPosition(
			cardsStartX + static_cast<float>(index) * (cardWidth + gap),
			cardBottom - cardHeight);
		const BallCardInteraction interaction = DrawBallSelectionCard(
			*ball,
			index,
			model.effectiveAttacks[static_cast<std::size_t>(index)],
			cardPosition,
			ImVec2(cardWidth, cardHeight),
			selected,
			held,
			expansion);

		if (interaction.openDetails)
		{
			m_BallDetailOfferIndex = index;
			ImGui::OpenPopup("ボール詳細");
		}

		if (interaction.select && !selected)
		{
			intent.selectedOfferIndex = index;
			if (held)
			{
				intent.selectedHoldIndex = -1;
			}
			intent.selectionChanged = true;
		}
		if (interaction.toggleHold && !selected)
		{
			intent.selectedHoldIndex = held ? -1 : index;
		}
	}

	ImGui::SetNextWindowSize(ImVec2(560.0f, 620.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("ボール詳細", nullptr, ImGuiWindowFlags_NoResize))
	{
		const PlayerBallData* detailBall =
			m_BallDetailOfferIndex >= 0 && m_BallDetailOfferIndex < offerCount
			? model.offers[static_cast<std::size_t>(m_BallDetailOfferIndex)]
			: nullptr;
		if (detailBall != nullptr)
		{
			DrawBallDetailPopup(
				*detailBall,
				model.effectiveAttacks[static_cast<std::size_t>(m_BallDetailOfferIndex)]);
		}
		if (ImGui::Button("閉じる", ImVec2(-1.0f, 38.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::End();
	ImGui::PopStyleVar(2);
	return intent;
}

// Clear Reward UIを描画
ClearRewardPresentationIntent GamePresentation::DrawClearReward(
	const ClearRewardPresentationModel& model)
{
	ClearRewardPresentationIntent intent{
		model.selectedRelicOfferIndex,
		model.selectedRewardIndex,
		model.selectedRewardBallIndex,
		false,
	};
	GameUi::PrepareWindow("clear_reward", ImVec2(290, 90), ImVec2(700, 550));
	ImGui::Begin(UiText::ClearWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	ImGui::TextUnformatted(UiText::StageClear);
	ImGui::Text(
		UiText::RewardMoneyFormat,
		model.stageRewardMoney);
	ImGui::Text(UiText::MoneyFormat, model.playerMoney);
	ImGui::Text("HP %d / %d", model.playerCurrentHp, model.playerMaxHp);
	ImGui::Separator();
	if (model.midBossRelicSelectionActive)
	{
		ImGui::TextUnformatted("中ボス撃破報酬：レリックを1つ選択");
		for (int index = 0; index < static_cast<int>(model.relicOffers.size()); ++index)
		{
			const auto* relic = model.relicOffers[static_cast<std::size_t>(index)];
			if (relic == nullptr) continue;
			ImGui::PushID(index);
			if (ImGui::Selectable(
				relic->name,
				index == intent.selectedRelicOfferIndex))
			{
				intent.selectedRelicOfferIndex = index;
			}
			ImGui::TextWrapped("%s", relic->description);
			ImGui::PopID();
		}
		if (ImGui::Button("選択したレリックを獲得", ImVec2(-1, 40)))
			intent.confirm = true;
	}
	else if (model.clearRewardChosen)
	{
		ImGui::TextWrapped("%s", model.rewardMessage.c_str());
		if (ImGui::Button("次のルートへ", ImVec2(-1, 44)))
			intent.confirm = true;
	}
	else
	{
		ImGui::TextUnformatted(UiText::ChooseReward);
		for (int index = 0; index < kClearRewardCount; ++index)
		{
			if (ImGui::RadioButton(
				kClearRewardNames[index],
				index == intent.selectedRewardIndex))
			{
				intent.selectedRewardIndex = index;
				intent.selectedRewardBallIndex = 0;
			}
		}
		ImGui::BeginChild("reward_targets", ImVec2(0, -96), ImGuiChildFlags_Borders);
		const bool upgrading = intent.selectedRewardIndex == 1;
		const int count = intent.selectedRewardIndex == 0
			? static_cast<int>(model.newBallOffers.size())
			: (upgrading ? static_cast<int>(model.upgradeTargets.size()) : 0);
		for (int index = 0; index < count; ++index)
		{
			const auto* ball = upgrading
				? model.upgradeTargets[static_cast<std::size_t>(index)].ball
				: model.newBallOffers[static_cast<std::size_t>(index)];
			if (ball == nullptr) continue;
			ImGui::PushID(index);
			if (PlayerBallUI::Select(
				*ball,
				index == intent.selectedRewardBallIndex))
			{
				intent.selectedRewardBallIndex = index;
			}
			if (index == intent.selectedRewardBallIndex)
			{
				ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball->definitionId));
				if (upgrading)
				{
					ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(*ball).c_str());
					const int upgradeCost =
						model.upgradeTargets[static_cast<std::size_t>(index)].upgradeCost;
					if (upgradeCost >= 0) ImGui::Text(UiText::UpgradeCostFormat, upgradeCost);
					if (upgradeCost > model.playerMoney) ImGui::TextUnformatted(UiText::UpgradeMoneyShortage);
				}
				else ImGui::TextWrapped("%s", PlayerBallText::GetStats(*ball, ball->status).c_str());
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (intent.selectedRewardIndex == 2)
			ImGui::Text("追加で %d Money を受け取ります。", model.extraMoneyAmount);
		ImGui::EndChild();
		const int cost = upgrading &&
			intent.selectedRewardBallIndex >= 0 &&
			intent.selectedRewardBallIndex < static_cast<int>(model.upgradeTargets.size())
			? model.upgradeTargets[static_cast<std::size_t>(intent.selectedRewardBallIndex)].upgradeCost
			: 0;
		const bool unavailable = upgrading
			? cost < 0 || cost > model.playerMoney
			: (intent.selectedRewardIndex == 0 && count == 0);
		ImGui::BeginDisabled(unavailable);
		if (ImGui::Button(
			upgrading ? "選択した個体を強化" : "選択した報酬を獲得",
			ImVec2(-1, 40)))
		{
			intent.confirm = true;
		}
		ImGui::EndDisabled();
		if (!model.rewardMessage.empty())
			ImGui::TextWrapped("%s", model.rewardMessage.c_str());
	}
	ImGui::End();
	return intent;
}
