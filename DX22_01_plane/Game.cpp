#include "Game.h"
#include "BallShotPrediction.h"
#include "GameUi.h"
#include "BallMechanics.h"

#pragma execution_character_set("utf-8")
#include "Renderer.h"
#include "BalanceLogger.h"
#include "GameMcpBridge.h"
#include "GamePresentation.h"
#include "GameSaveManager.h"
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
#include "UiText.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <stdexcept>

using DirectX::SimpleMath::Vector3;

Game* Game::m_Instance;//ゲームインスタンス

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
	constexpr int kExtraRewardMoney = 10;
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
		return PlayerBallText::Utf8(u8"狙い：配置に合わせて確実に連鎖を始める");
	}

	struct BallCardInteraction
	{
		bool select = false;
		bool toggleHold = false;
	};

	BallCardInteraction DrawBallSelectionCard(
		const PlayerBallData& ball,
		int index,
		int attack,
		int defense,
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
		// 十分な高さができてから詳細とホールド操作を表示する。
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
			const ImVec2 ballCenter(position.x + 30.0f, cardMax.y - 36.0f);
			drawList->AddCircleFilled(ballCenter, 18.0f, IM_COL32(3, 7, 12, 210), 28);
			drawList->AddCircleFilled(ballCenter, 14.0f, accent, 28);
			drawList->AddCircle(ballCenter, 18.0f, accent, 28, 2.0f);
			drawList->AddText(
				font,
				fontSize * 0.95f,
				ImVec2(position.x + 56.0f, cardMax.y - 47.0f),
				IM_COL32(255, 255, 255, 255),
				PlayerBallText::GetName(ball.definitionId));
			const char* stateText = selected
				? PlayerBallText::Utf8(u8"選択中")
				: (held ? "HOLD" : PlayerBallText::Utf8(u8"手札"));
			const ImVec2 stateSize = ImGui::CalcTextSize(stateText);
			drawList->AddText(
				ImVec2(cardMax.x - stateSize.x - 10.0f, cardMax.y - 21.0f),
				selected
					? IM_COL32(255, 221, 118, 255)
					: (held ? IM_COL32(91, 222, 205, 255) : IM_COL32(145, 156, 174, 230)),
				stateText);
			drawList->PopClipRect();
			ImGui::PopID();
			return interaction;
		}

		sprintf_s(text, "%d  %s", index + 1, GetPlayerBallAbilityName(ball));
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
			PlayerBallText::Utf8(u8"攻撃 %d   防御 %d"),
			attack,
			defense);
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
			enemy->IsPocketed())
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
		case BattleState::ConfirmShot:     return "ConfirmShot";
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
		case BattleState::ConfirmShot:
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
		case BattleState::ConfirmShot:
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
{
}

// デストラクタ
Game::~Game()
{
	m_SceneManager.Reset();
	DeleteAllGameObjects();
}

// Debug Combat Forecastを無効化する。
void GameDebugController::InvalidateCombatForecast(const char* reason)
{
	m_DebugCombatForecastDirty = true;
	if (reason != nullptr && reason[0] != '\0')
	{
		m_DebugCombatForecastPendingReason = reason;
	}
}

// Debug Combat Forecastを更新する。
void GameDebugController::RefreshCombatForecast(Game& game)
{
	if (!m_DebugCombatForecastDirty)
	{
		return;
	}

	CombatForecastSnapshot next{};
	next.updateRevision = m_DebugCombatForecast.updateRevision + 1;
	next.updateReason = m_DebugCombatForecastPendingReason;

	const std::vector<PlayerBall*> players = game.GetComponents<PlayerBall>();
	const std::vector<EnemyBall*> enemies = game.GetComponents<EnemyBall>();
	const PlayerBall* player = players.empty() ? nullptr : players.front();
	if (player != nullptr)
	{
		next.hasPlayer = true;
		next.playerCurrentHp = player->GetHP();
		next.playerMaxHp = player->GetMaxHP();
		next.playerDefense = player->GetDefense();
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
			enemySnapshot.damageBeforeMinimum =
				enemy->GetAttack() - player->GetDefense();
			enemySnapshot.expectedDamage =
				player->CalculateDamageTaken(enemy->GetAttack());
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

// Debug Player Damageを記録する。
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
	m_DebugController.InvalidateCombatForecast(reason);
}

void Game::RefreshDebugCombatForecast()
{
	m_DebugController.RefreshCombatForecast(*this);
}

void Game::RecordDebugPlayerDamage(
	const std::string& source,
	const std::string& sourceId,
	int damage,
	int hpBefore,
	int hpAfter)
{
	m_DebugController.RecordPlayerDamage(
		source,
		sourceId,
		damage,
		hpBefore,
		hpAfter);
}

// Pauseが可能か判定する。
bool Game::CanPause() const
{
	return m_RunActive &&
		(dynamic_cast<BattleScene*>(m_SceneManager.Get()) != nullptr ||
		 dynamic_cast<StageSelectScene*>(m_SceneManager.Get()) != nullptr ||
		 dynamic_cast<RestSiteScene*>(m_SceneManager.Get()) != nullptr ||
		 dynamic_cast<ShopScene*>(m_SceneManager.Get()) != nullptr);
}

// And Return To Titleを保存する。
bool Game::SaveAndReturnToTitle()
{
	if (IsDebugMode()) { m_DebugController.RequestExit(); return true; }
	if (m_IsClearRewardActive && !m_IsClearRewardChosen)
	{
		SetSaveLoadMessage(
			"報酬を選択して次のルートへ進んでから保存してください。");
		return false;
	}

	CaptureCurrentPlayerStatus();
	SceneType resumeScene = SceneType::Max;
	bool sceneAlreadyActive = true;
	if (dynamic_cast<BattleScene*>(m_SceneManager.Get()) != nullptr)
	{
		resumeScene = m_IsClearRewardActive
			? SceneType::Select
			: SceneType::Battle;
		sceneAlreadyActive = false;
	}
	else if (dynamic_cast<StageSelectScene*>(m_SceneManager.Get()) != nullptr)
	{
		resumeScene = SceneType::Select;
	}
	else if (dynamic_cast<RestSiteScene*>(m_SceneManager.Get()) != nullptr)
	{
		resumeScene = SceneType::RestSite;
	}
	else if (dynamic_cast<ShopScene*>(m_SceneManager.Get()) != nullptr)
	{
		resumeScene = SceneType::Shop;
	}

	if (resumeScene == SceneType::Max ||
		!SaveRunCheckpoint(resumeScene, sceneAlreadyActive, true))
	{
		return false;
	}

	m_IsPaused = false;
	m_PauseConfirmTitle = false;
	m_RunActive = false;
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
	m_Instance->m_SettingsManager.Load();
	m_Instance->m_SettingsManager.ApplyDisplaySettings();

	// 入力処理を初期化
	Input::Create();

	// カメラを初期化
	m_Instance->m_Camera.Init();

	m_Instance->m_ProgressionProfile.Load();
	m_Instance->LoadPlayerStatusFromJson();
	m_Instance->m_BalanceAutoPlayer.LoadConfig();
	m_Instance->LoadDifficultyProfileConfig();
	m_Instance->m_DynamicBalanceController.LoadConfig();
	m_Instance->m_BalanceValidationController.LoadConfig(
		"assets/data/balance_validation.json",
		m_Instance->m_BaselineDifficultyProfile);
	m_Instance->LoadEncounterBalanceConfig();
	m_Instance->LoadPocketRulesConfig();
	m_Instance->InitializeBattleController();

	// 最初のシーンを読み込む
	m_Instance->m_SceneManager.Initialize(SceneType::Title);
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
		if (m_Instance->LoadDebugPreset()) m_Instance->m_DebugController.RequestStart();
	}
}

// 更新
void Game::Update(double elapsedSeconds)
{
	m_Instance->RemoveDestroyedGameObjects();

	// 入力処理を更新
	Input::Update();
	if (m_Instance->UpdateDebugMode()) return;
	if (m_Instance->m_PendingDisplayApply)
	{
		m_Instance->m_SettingsManager.ApplyDisplaySettings();
		m_Instance->m_PendingDisplayApply = false;
	}
	if (std::exchange(m_Instance->m_MouseFullscreenToggle, false) || Input::GetKeyTrigger(VK_F11))
	{
		GameSettings& settings = m_Instance->m_SettingsManager.Edit();
		settings.fullscreen = !settings.fullscreen;
		m_Instance->m_SettingsManager.MarkDirty();
		m_Instance->m_SettingsManager.ApplyDisplaySettings();
		m_Instance->m_SettingsManager.SaveIfDirty();
	}
	if ((std::exchange(m_Instance->m_MousePauseToggle, false) || Input::GetKeyTrigger(VK_ESCAPE)) &&
		m_Instance->CanPause() &&
		!m_Instance->m_BalanceAutoPlayer.IsEnabled())
	{
		const bool wasPaused = m_Instance->m_IsPaused;
		m_Instance->m_IsPaused = !m_Instance->m_IsPaused;
		m_Instance->m_PauseConfirmTitle = false;
		if (wasPaused)
		{
			m_Instance->m_SettingsManager.SaveIfDirty();
		}
	}
	if ((std::exchange(m_Instance->m_MouseSaveRequested, false) || Input::GetKeyTrigger(VK_F6)) &&
		!m_Instance->m_BalanceAutoPlayer.IsEnabled())
	{
		m_Instance->SaveCurrentRun();
	}
	if (m_Instance->m_IsPaused)
	{
		ResetFrameTiming();
		return;
	}
	if (m_Instance->m_RunActive)
	{
		m_Instance->m_RunStatistics.AdvanceFrame();
	}
	if (m_Instance->m_SaveLoadMessageFrames > 0)
	{
		--m_Instance->m_SaveLoadMessageFrames;
		if (m_Instance->m_SaveLoadMessageFrames == 0)
		{
			m_Instance->m_SaveLoadMessage.clear();
		}
	}
	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Update(*m_Instance);
	}

	if (m_Instance->m_GameMcpBridge != nullptr)
	{
		m_Instance->m_GameMcpBridge->Update(*m_Instance);
	}

	if (m_Instance->m_BalanceAutoPlayer.Update(*m_Instance))
	{
		ResetFrameTiming();
		return;
	}

	// ==========================
	// ClearReward中はゲーム本編を更新しない
	// ==========================
	if (m_Instance->m_IsClearRewardActive)
	{
		ResetFrameTiming();
		m_Instance->UpdateClearReward();
		return;
	}

	if (!m_Instance->m_BalanceAutoPlayer.IsEnabled() &&
		dynamic_cast<BattleScene*>(m_Instance->m_SceneManager.Get()) != nullptr &&
		m_Instance->m_RunController.Deck().GetOfferCount() > 0)
	{
		m_Instance->UpdateBallSelection();
	}

	// シーンを更新
	if (Scene* scene = m_Instance->m_SceneManager.Get())
	{
		scene->Update();
	}

	// カメラを更新
	m_Instance->m_Camera.Update();

	// オブジェクトを更新
	m_Instance->m_World.Update();

	// Input/AI/MCP have run once. Only physical motion may catch up here.
	m_Instance->UpdateFixedPhysics(elapsedSeconds);

	m_Instance->m_World.LateUpdate();
	m_Instance->RemoveDestroyedGameObjects();
	m_Instance->m_BattleController.Update();

	const BattleResult battleResult =
		m_Instance->m_BattleController.ConsumeResult();

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
		m_Instance->m_PhysicsClock.Reset();
		m_Instance->m_ResetPhysicsElapsed = true;
		m_Instance->m_PhysicsStepsLastFrame = 0;
	}
}

// Fixed Physicsを更新する。
void Game::UpdateFixedPhysics(double elapsedSeconds)
{
	m_PhysicsStepsLastFrame = 0;

	if (m_ResetPhysicsElapsed)
	{
		elapsedSeconds = FixedStepClock::StepSeconds;
		m_ResetPhysicsElapsed = false;
	}

	const int steps =
		m_PhysicsClock.Advance(elapsedSeconds);

	for (int step = 0; step < steps; ++step)
	{
		const BattleState previousState =
			m_BattleController.GetState();

		m_World.FixedUpdate();

		const auto physicsResult =
			BallPhysicsWorld::Step(*this);

		m_PhysicsSubstepsLastTick =
			physicsResult.substeps;

		if (physicsResult.limitReached)
		{
			++m_PhysicsSubstepLimitCount;
		}

		++m_PhysicsTickCount;
		++m_PhysicsStepsLastFrame;

		const bool stopCatchUp =
			m_BattleController.OnFixedStepCompleted();

		const BattleState currentState =
			m_BattleController.GetState();

		if (currentState != previousState ||
			stopCatchUp ||
			m_BattleController.IsFinished())
		{
			if (previousState == BattleState::BallsMoving)
			{
				BallShotPrediction::WriteVerificationActual(*this);
			}

			m_PhysicsClock.Reset();
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
		game.GetComponents<PlayerBall>();
	const std::vector<EnemyBall*> enemies =
		game.GetComponents<EnemyBall>();
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
			game.m_IsClearRewardActive));
	if (forecast.hasPlayer)
	{
		ImGui::Text(
			"プレイヤーHP: %d / %d  防御: %d  シールド: %d",
			forecast.playerCurrentHp,
			forecast.playerMaxHp,
			forecast.playerDefense,
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
				"    攻撃%d - 防御%d = %d%s",
				enemy.attack,
				forecast.playerDefense,
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
		game.m_IsClearRewardActive
		? "true"
		: "false");

	ImGui::Text("AreAllBallsStopped = %s",
		game.AreAllBallsStopped() ? "true" : "false");

	ImGui::Text("AreAllEnemiesDefeated = %s",
		game.AreAllEnemiesDefeated() ? "true" : "false");

	ImGui::Text("Player Run HP = %d / %d",
		game.m_RunController.Status().currentHp,
		game.m_RunController.Status().maxHp);
	ImGui::Text("Draw Pile Count = %d", game.GetPlayerDeckCount());
	ImGui::Text("Discard Pile Count = %d", game.GetPlayerDiscardCount());
	ImGui::Text("Offer Count = %d", game.m_RunController.Deck().GetOfferCount());
	ImGui::Text("Total Deck Count = %d", game.m_RunController.Deck().GetRewardTargetCount());
	ImGui::Text("Current Ball Used = %s",
		game.m_RunController.Deck().IsCurrentUsed() ? "true" : "false");

	const PlayerBallData* currentDebugBall =
		game.m_RunController.Deck().GetCurrent();
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

	ImGui::BeginDisabled(game.IsDebugMode());
	if (ImGui::Button(
		game.m_BalanceAutoPlayer.IsEnabled()
			? "Stop Balance Auto Play"
			: "Start Balance Auto Play"))
	{
		const bool startAutoPlay =
			!game.m_BalanceAutoPlayer.IsEnabled();
		game.m_BalanceAutoPlayer.SetEnabled(startAutoPlay);
		game.m_BalanceAutoPlayer.SetStopAfterCurrentRunRequested(
			startAutoPlay &&
			game.m_BalanceAutoPlayer.IsStopAfterCurrentRunDefault());
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text(
		"F8 / Runs: %d%s",
		game.m_BalanceAutoPlayer.GetRunCount(),
		game.m_BalanceAutoPlayer.GetMaximumRuns() > 0
			? " (limited)"
			: " (unlimited)");
	ImGui::BeginDisabled(!game.m_BalanceAutoPlayer.IsEnabled());
	if (ImGui::Button(
		game.m_BalanceAutoPlayer.IsStopAfterCurrentRunRequested()
			? "Continue After This Run (F9)"
			: "Stop At This Run End (F9)"))
	{
		game.m_BalanceAutoPlayer.SetStopAfterCurrentRunRequested(
			!game.m_BalanceAutoPlayer.IsStopAfterCurrentRunRequested());
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextUnformatted(
		game.m_BalanceAutoPlayer.IsStopAfterCurrentRunRequested()
			? "Stops on game over / game clear"
			: "Continuous runs");

	if (ImGui::Button("Save Debug Snapshot"))
	{
		game.SaveDebugSnapshot();
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
		game.m_RunController.Status().money
	);

	ImGui::End();

	}


}

void Game::Draw()
{
	Renderer::DrawStart();

	m_Instance->m_World.Draw();

	ImGui::BeginDisabled(m_Instance->m_IsPaused || m_Instance->m_DebugController.IsEditorOpen());
	if (m_Instance->m_SceneManager.Get() != nullptr)
	{
		m_Instance->m_SceneManager.Get()->DrawUI();
	}

	m_Instance->m_DebugController.DrawDiagnostics(*m_Instance);
	if (dynamic_cast<BattleScene*>(m_Instance->m_SceneManager.Get()) != nullptr &&
		m_Instance->m_RunController.Deck().GetOfferCount() > 0)
	{
		m_Instance->DrawBallSelectionUI();
	}

	// ==========================
	// 報酬UIを最後に重ねる
	// ==========================
	if (m_Instance->m_IsClearRewardActive)
	{
		m_Instance->DrawClearRewardUI();
	}

	ImGui::EndDisabled();

	if (m_Instance->m_GamePresentation != nullptr)
	{
		m_Instance->m_GamePresentation->Draw(*m_Instance);
	}

	m_Instance->DrawDebugMode();
	if (m_Instance->m_IsPaused && !m_Instance->m_DebugController.IsEditorOpen())
	{
		m_Instance->DrawPauseUI();
	}

	if (!m_Instance->m_SaveLoadMessage.empty())
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
		ImGui::TextUnformatted(m_Instance->m_SaveLoadMessage.c_str());
		ImGui::End();
	}

	// Application owns ImGui rendering and presentation, including detached windows.
}

// Pause UIを描画する。
void GamePresentation::DrawPause(Game& game)
{
	GameSettings& settings = game.m_SettingsManager.Edit();
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
		game.m_SettingsManager.SaveIfDirty();
		game.m_IsPaused = false;
		game.m_PauseConfirmTitle = false;
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
		game.m_PendingDisplayApply = true;
		game.m_SettingsManager.SaveIfDirty();
	}

	ImGui::SeparatorText("ランを中断");
	if (game.IsDebugMode())
	{
		if (ImGui::Button("デバッグを終了してタイトルへ", ImVec2(-1, 40))) game.m_DebugController.RequestExit();
	}
	else if (!game.m_PauseConfirmTitle)
	{
		if (ImGui::Button("セーブしてタイトルへ戻る", ImVec2(-1.0f, 40.0f)))
		{
			game.m_PauseConfirmTitle = true;
		}
	}
	else
	{
		ImGui::TextWrapped(
			"ランのセーブデータは1個だけです。現在のセーブを上書きしてタイトルへ戻ります。");
		if (dynamic_cast<BattleScene*>(game.m_SceneManager.Get()) != nullptr &&
			!game.m_IsClearRewardActive)
		{
			ImGui::TextDisabled("戦闘中のランは、この戦闘の最初から再開します。");
		}
		if (ImGui::Button("上書きして戻る", ImVec2(260.0f, 38.0f)))
		{
			game.SaveAndReturnToTitle();
		}
		ImGui::SameLine();
		if (ImGui::Button("キャンセル", ImVec2(260.0f, 38.0f)))
		{
			game.m_PauseConfirmTitle = false;
		}
	}

	if (settingsChanged)
	{
		game.m_SettingsManager.MarkDirty();
	}
	ImGui::End();
}

// 終了処理
void Game::Uninit()
{
	if (m_Instance != nullptr)
	{
		m_Instance->m_SettingsManager.SaveIfDirty();
		m_Instance->m_GamePresentation.reset();
		if (m_Instance->m_GameMcpBridge != nullptr)
		{
			m_Instance->m_GameMcpBridge->Shutdown(*m_Instance);
		}

		const std::vector<PlayerBall*> players =
			m_Instance->GetComponents<PlayerBall>();
		const std::vector<EnemyBall*> enemies =
			m_Instance->GetComponents<EnemyBall>();
		const int playerHp =
			players.empty() || players[0] == nullptr
			? m_Instance->m_RunController.Status().currentHp
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
			m_Instance->m_RunController.Status().maxHp,
			CountDefeatedEnemies(enemies));
		logger.EndRun(
			"application_exit",
			playerHp,
			m_Instance->m_RunController.Status().maxHp,
			m_Instance->m_RunController.Progress().GetClearedBattleCount());
	}

	// カメラの終了処理
	m_Instance->m_Camera.Uninit();

	// オブジェクトの終了処理は所有者であるGameWorldへ委譲する。
	m_Instance->m_World.Clear();


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

// GameObject生成要求を専用のGameWorldへ委譲する。
GameObject* Game::CreateGameObject(const std::string& name)
{
	return m_World.Create(name);
}

// シーン遷移前後のゲーム固有処理を調停し、シーン寿命管理をSceneManagerへ委譲する。
void Game::ChangeScene(SceneType sceneType)
{
	if (sceneType < SceneType::Title || sceneType >= SceneType::Max)
	{
		std::cerr << "[Game] 無効なシーンへの遷移要求を拒否しました" << std::endl;
		return;
	}

	ResetFrameTiming();
	int score = 0;
	Scene* currentScene = m_Instance->m_SceneManager.Get();

	if (currentScene != nullptr)
	{
		m_Instance->CaptureCurrentPlayerStatus();
		if (!m_Instance->m_IsRestoringRunSave &&
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
		m_IsClearRewardActive = false;
		m_LastBattleResult = BattleResult::None;

		m_BattleController.StartBattle();

		m_RunController.Deck().Reset();

		if (!BeginBallSelection())
		{
			m_BattleController.NotifyPlayerDefeated();
		}

		m_RunController.BeginStageReward();
		m_RewardMessage.clear();
	}
	else
	{
		m_IsClearRewardActive = false;
		m_BattleController.Reset();
	}

	if (sceneType == SceneType::Title && IsDebugMode())
	{
		EndDebugMode();
	}

	if (!m_Instance->m_SceneManager.Change(sceneType))
	{
		if (sceneType == SceneType::Battle)
		{
			m_BattleController.Reset();
		}
		std::cerr << "[Game] シーンの生成に失敗しました" << std::endl;
		return;
	}

	if (sceneType == SceneType::Result)
	{
		if (ResultScene* resultScene = dynamic_cast<ResultScene*>(
			m_Instance->m_SceneManager.Get()))
		{
			resultScene->SetScore(score);
		}
	}

	m_Instance->InvalidateDebugCombatForecast("シーン変更");
}

// GameObject削除要求を専用のGameWorldへ委譲する。
void Game::DeleteGameObject(GameObject* gameObject)
{
	m_World.RequestDestroy(gameObject);
}

// 削除要求済みGameObjectの破棄を専用のGameWorldへ委譲する。
void Game::RemoveDestroyedGameObjects()
{
	m_World.RemoveDestroyed();
}

// ゲーム空間に存在するすべてのGameObjectを終了して破棄する。
void Game::DeleteAllGameObjects()
{
	m_World.Clear();
}

// 指定したGameObjectが現在のGameWorldに存在するかを返す。
bool Game::ContainsGameObject(const GameObject* gameObject) const
{
	return m_World.Contains(gameObject);
}

// All Enemies Defeatedかどうかを判定する。
bool Game::AreAllEnemiesDefeated() const
{
	return m_BattleController.AreAllEnemiesDefeated();
}

// Current Pocket Finisher Ratioを取得する。
float Game::GetCurrentPocketFinisherRatio() const
{
	switch (m_BattleController.GetStageType())
	{
	case StageType::MidBoss:
		return m_BattleController.PocketRules().midBossFinisherRatio;
	case StageType::Boss:
		return m_BattleController.PocketRules().bossFinisherRatio;
	case StageType::Normal:
	default:
		return m_BattleController.PocketRules().normalFinisherRatio;
	}
}

// Enemy Pocket Finisher Eligibleかどうかを判定する。
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

// Player Pocket Damage Amountを取得する。
int Game::GetPlayerPocketDamageAmount() const
{
	return (std::max)(1, static_cast<int>(std::ceil(
		static_cast<float>(m_RunController.Status().maxHp) *
		m_BattleController.PocketRules().playerDamageRatio)));
}

// Enemy Pocketを処理する。
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
			NotifyEnemyDefeated(enemy->GetEnemyId());
		}
		enemy->RemoveFromFieldAfterPocket();
		RecordBalanceEvent(
			"enemy_pocket_finisher",
			{
				{ "enemy_id", enemy->GetEnemyId() },
				{ "stage_type", ToString(m_BattleController.GetStageType()) },
				{ "hp_before", hpBefore },
				{ "max_hp", maxHp },
				{ "hp_ratio", hpRatio },
				{ "finisher_ratio", finisherRatio },
				{ "already_defeated", wasAlreadyDefeated },
			});
		return;
	}

	enemy->EnterPocketQueue();
	NotifyPocketFeedback(
		pocketPosition,
		false,
		false,
		pocketDamage);
	m_BattleController.QueuePocketedEnemy(enemy);
	RecordBalanceEvent(
		"enemy_pocket_controlled",
		{
			{ "enemy_id", enemy->GetEnemyId() },
			{ "stage_type", ToString(m_BattleController.GetStageType()) },
			{ "hp", enemy->GetHP() },
			{ "max_hp", maxHp },
			{ "hp_ratio", hpRatio },
			{ "finisher_ratio", finisherRatio },
			{ "queue_size", m_BattleController.GetPocketQueueSize() },
		});
}

// Pocket Queue Indexを取得する。
int Game::GetPocketQueueIndex(const EnemyBall* enemy) const
{
	return m_BattleController.GetPocketQueueIndex(enemy);
}

// Player Pocket Return Positionを検索する。
Vector3 Game::FindPlayerPocketReturnPosition(const PlayerBall* player)
{
	std::uniform_real_distribution<float> xDistribution(
		-m_BattleController.PocketRules().playerReturnHalfWidth,
		m_BattleController.PocketRules().playerReturnHalfWidth);
	std::uniform_real_distribution<float> zDistribution(
		-m_BattleController.PocketRules().playerReturnHalfDepth,
		m_BattleController.PocketRules().playerReturnHalfDepth);
	const float playerRadius = player != nullptr && player->GetBall() != nullptr
		? player->GetBall()->GetRadius()
		: 2.4f;

	for (int attempt = 0; attempt < 24; attempt++)
	{
		const Vector3 candidate(
			xDistribution(m_BattleController.PocketRandomEngine()),
			TableConfig::FIELD_HEIGHT,
			zDistribution(m_BattleController.PocketRandomEngine()));
		bool blocked = false;
		for (BallComponent* ball : GetComponents<BallComponent>())
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

// Pocketed Playerを復元する。
void Game::RestorePocketedPlayer()
{
	for (PlayerBall* player : GetComponents<PlayerBall>())
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

// Enemy Pocket Return Positionを検索する。
Vector3 Game::FindEnemyPocketReturnPosition(
	const EnemyBall* returningEnemy) const
{
	const float baseZ = TableConfig::GetFieldDepth() * 0.5f -
		m_BattleController.PocketRules().enemyReturnTopEdgeOffset;
	const float radius = returningEnemy != nullptr
		? returningEnemy->GetRadius()
		: 2.4f;
	const float spacing = radius * 2.0f + 1.0f;
	const std::array<int, 9> offsets{ 0, -1, 1, -2, 2, -3, 3, -4, 4 };
	for (int offset : offsets)
	{
		const Vector3 candidate(
			m_BattleController.PocketRules().enemyReturnX + static_cast<float>(offset) * spacing,
			TableConfig::FIELD_HEIGHT,
			baseZ);
		bool blocked = false;
		for (BallComponent* ball : m_Instance->GetComponents<BallComponent>())
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
	return Vector3(
		m_BattleController.PocketRules().enemyReturnX,
		TableConfig::FIELD_HEIGHT,
		baseZ);
}

// Next Pocketed Enemyを復元する。
void Game::RestoreNextPocketedEnemy()
{
	while (m_BattleController.GetPocketQueueSize() > 0)
	{
		EnemyBall* enemy = m_BattleController.PopPocketedEnemy();
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
				{ "position_x", returnPosition.x },
				{ "position_z", returnPosition.z },
				{ "remaining_queue_size", m_BattleController.GetPocketQueueSize() },
			});
		break;
	}
}

// Finalize Run Result の処理を実行する。
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
	m_LastRunResult = m_RunStatistics.CreateSnapshot(
		completed,
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp,
		acquiredRelics);
	m_LastRunResult.clearedBattles =
		m_RunController.Progress().GetClearedBattleCount();
	RecordPersistentProgress();
}

// Persistent Progressを記録する。
void Game::RecordPersistentProgress()
{
	m_LastProgressionUnlocks.clear();
	if (!m_PersistentProgressEligible) return;
	try
	{
		m_LastProgressionUnlocks = m_ProgressionProfile.RecordRun(m_LastRunResult, m_ActiveAscension);
		m_ProgressionProfile.Save();
	}
	catch (const std::exception& e)
	{
		m_LastProgressionUnlocks.push_back(std::string("恒久進行を保存できませんでした：") + e.what());
	}
}

// Selected Ascensionを設定する。
void Game::SetSelectedAscension(int level)
{
	if (HasValidRunSave()) return;
	m_ProgressionProfile.selectedAscension = std::clamp(level, 0, m_ProgressionProfile.highestUnlockedAscension);
	try { m_ProgressionProfile.Save(); } catch (...) {}
}

// Normal Route Areaを完了する。
void Game::CompleteNormalRouteArea(const char* areaType)
{
	// 現在の通常エリアを完了し、ラン進行と関連ログを同期する。
	const bool longValidationRun =
		m_BalanceValidationController.UsesExtendedRoute(
			kNormalRouteAreaGoal);
	const AreaCompletionResult completion =
		m_RunController.Progress().CompleteNormalArea(
			longValidationRun,
			m_BalanceValidationController.GetMaximumClearedStages(),
			m_RouteSelectionSeed + static_cast<std::uint32_t>(
				m_RunController.Progress().GetAreaProgress() + 1));
	if (completion == AreaCompletionResult::Rejected)
	{
		return;
	}

	const int areaProgress = m_RunController.Progress().GetAreaProgress();
	m_RunController.Status().progress = (std::min)(
		kNormalRouteAreaGoal,
		areaProgress + 1);
	m_RunStatistics.CompleteArea(areaProgress);
	RecordBalanceEvent(
		"route_area_completed",
		{
			{ "map_path", m_RunController.Progress().GetMap().Path() },
			{ "area_type", areaType != nullptr ? areaType : "unknown" },
			{ "area_progress", areaProgress },
			{ "area_goal", kNormalRouteAreaGoal },
		});

	if (completion == AreaCompletionResult::MapExtended)
	{
		RecordBalanceEvent(
			"run_map_extended",
			m_RunController.Progress().GetMap().Snapshot());
	}
	if (completion == AreaCompletionResult::BossPreparationEntered)
	{
		m_RunController.Status().progress = kNormalRouteAreaGoal;
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
	// 現在のランフェーズに応じて次のルート選択先へ遷移する。
	if (m_RunController.Progress().IsBossPreparation())
	{
		ChangeScene(SceneType::RestSite);
	}
	else
	{
		ChangeScene(SceneType::Select);
	}
}

// Shopから退出する。
void Game::LeaveShop()
{
	CompleteNormalRouteArea("shop");
	EnterNextRouteAfterArea();
}

// Rest Siteから退出する。
void Game::LeaveRestSite()
{
	// 休憩所の利用完了を記録して次のルートへ進める。
	if (m_RunController.Progress().IsBossPreparation())
	{
		if (!m_RunController.Progress().CompleteBossPreparation())
		{
			return;
		}
		RecordBalanceEvent(
			"boss_preparation_completed",
			{
				{ "area_progress", m_RunController.Progress().GetAreaProgress() },
				{ "next_phase", ToString(m_RunController.Progress().GetPhase()) },
			});
		ChangeScene(SceneType::Select);
		return;
	}

	CompleteNormalRouteArea("rest_site");
	EnterNextRouteAfterArea();
}

// Continue After Clear Reward の処理を実行する。
void Game::ContinueAfterClearReward()
{
	m_IsClearRewardActive = false;
	EnterNextRouteAfterArea();
}

// Final Boss Runを完了する。
void Game::CompleteFinalBossRun()
{
	// 最終ボス撃破時のラン完了処理と永続結果を確定する。
	m_RunController.Progress().RecordBattleCleared();
	m_RunStatistics.DefeatFinalBoss();
	m_RunController.Progress().CompleteFinalBoss();
	RecordBalanceEvent(
		"final_boss_defeated",
		{
			{ "stage_id", m_RunController.Status().GetSelectedStageId() },
			{ "area_progress", m_RunController.Progress().GetAreaProgress() },
			{ "cleared_battle_count", m_RunController.Progress().GetClearedBattleCount() },
		});
	BalanceLogger::GetInstance().EndRun(
		"completed",
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp,
		m_RunController.Progress().GetClearedBattleCount());
	m_RunActive = false;
	FinalizeRunResult(true);
	GameSaveManager::Remove();
	ChangeScene(SceneType::Result);
}

// Game Overを処理する。
void Game::ProcessGameOver()
{
	m_IsClearRewardActive = false;
	if (IsDebugMode()) { FinishDebugBattle(false); return; }
	DiscardCurrentPlayerBall();

	const std::vector<EnemyBall*> enemies =
		GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_RunController.Status().currentHp,
		CountAliveEnemies(enemies),
		CountDefeatedEnemies(enemies));
	logger.EndStage(
		"game_over",
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp,
		CountDefeatedEnemies(enemies));
	m_DynamicBalanceController.OnBattleFinished(
		false,
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp);
	RecordBalanceEvent(
		"dynamic_balance_evaluation",
		{
			{ "battle_result", "game_over" },
			{ "result", m_DynamicBalanceController.GetLastResult() },
			{ "reason", m_DynamicBalanceController.GetLastReason() },
			{ "level_change", m_DynamicBalanceController.GetLastLevelChange() },
			{ "next_level", m_DynamicBalanceController.GetLevel() },
			{ "remaining_hp_ratio", m_DynamicBalanceController.GetLastHpRatio() },
			{ "no_hit_rate", m_DynamicBalanceController.GetLastNoHitRate() },
			{ "shots_per_enemy", m_DynamicBalanceController.GetLastShotsPerEnemy() },
		});
	logger.EndRun(
		"game_over",
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp,
		m_RunController.Progress().GetClearedBattleCount());
	m_RunActive = false;
	FinalizeRunResult(false);
	GameSaveManager::Remove();

	ChangeScene(SceneType::Result);
}

// Clear Rewardを開始する。
void Game::StartClearReward()
{
	if (IsDebugMode()) { FinishDebugBattle(true); return; }
	DiscardCurrentPlayerBall();

	const std::vector<EnemyBall*> enemies =
		GetComponents<EnemyBall>();
	BalanceLogger& logger = BalanceLogger::GetInstance();
	logger.EndShot(
		m_RunController.Status().currentHp,
		CountAliveEnemies(enemies),
		CountDefeatedEnemies(enemies));
	logger.EndStage(
		"clear",
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp,
		CountDefeatedEnemies(enemies));
	m_DynamicBalanceController.OnBattleFinished(
		true,
		m_RunController.Status().currentHp,
		m_RunController.Status().maxHp);
	RecordBalanceEvent(
		"dynamic_balance_evaluation",
		{
			{ "battle_result", "clear" },
			{ "result", m_DynamicBalanceController.GetLastResult() },
			{ "reason", m_DynamicBalanceController.GetLastReason() },
			{ "level_change", m_DynamicBalanceController.GetLastLevelChange() },
			{ "next_level", m_DynamicBalanceController.GetLevel() },
			{ "remaining_hp_ratio", m_DynamicBalanceController.GetLastHpRatio() },
			{ "no_hit_rate", m_DynamicBalanceController.GetLastNoHitRate() },
			{ "shots_per_enemy", m_DynamicBalanceController.GetLastShotsPerEnemy() },
		});

	if (m_RunController.Progress().GetPhase() == RunPhase::FinalBoss &&
		m_BattleController.GetStageType() == StageType::Boss)
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

	m_SelectedRewardIndex = 0;
	m_SelectedRewardBallIndex = 0;
	m_IsClearRewardChosen = false;
	m_ClearRewardMouseConfirmed = false;
	m_RewardMessage = UiText::ChooseClearReward;
	m_IsMidBossRelicSelectionActive = false;
	m_RunController.MidBossRelicOffers().clear();
	if (m_BattleController.GetStageType() == StageType::MidBoss)
	{
		RollMidBossRelicOffers();
	}
	m_RunController.Progress().RecordBattleCleared();
	if (m_BattleController.GetStageType() == StageType::MidBoss)
	{
		m_RunStatistics.DefeatMidBoss();
	}
	CompleteNormalRouteArea(
		m_BattleController.GetStageType() == StageType::MidBoss
		? "midboss_battle"
		: "normal_battle");
	if (m_BalanceValidationController.HasReachedMaximumClearedStages(
		m_RunController.Progress().GetClearedBattleCount(),
		kNormalRouteAreaGoal))
	{
		RecordBalanceEvent(
			"balance_validation_run_completed",
			{
				{ "reason", "maximum_cleared_stages_reached" },
				{ "cleared_stage_count", m_RunController.Progress().GetClearedBattleCount() },
				{ "maximum_cleared_stages",
					m_BalanceValidationController.GetMaximumClearedStages() },
			});
		logger.EndRun(
			"validation_complete",
			m_RunController.Status().currentHp,
			m_RunController.Status().maxHp,
			m_RunController.Progress().GetClearedBattleCount());
		m_RunActive = false;
		FinalizeRunResult(true);
		GameSaveManager::Remove();
		ChangeScene(SceneType::Result);
		return;
	}
	nlohmann::json newBallCandidates = nlohmann::json::array();
	for (int index = 0; index < m_RunController.Deck().GetCatalogCount(); index++)
	{
		const PlayerBallData* ball = m_RunController.Deck().GetCatalogBall(index);
		if (ball != nullptr)
		{
			newBallCandidates.push_back(
				{
					{ "catalog_index", index },
					{ "ball_id", ball->definitionId },
				});
		}
	}
	nlohmann::json upgradeCandidates = nlohmann::json::array();
	for (int index = 0;
		index < m_RunController.Deck().GetRewardTargetCount();
		index++)
	{
		const PlayerBallData* ball = m_RunController.Deck().GetRewardTarget(index);
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
						m_RunController.Status().money >= upgradeCost },
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
			{ "extra_money_amount", kExtraRewardMoney },
		});
	m_IsClearRewardActive = true;
}

// Current Stageを完了する。
void Game::CompleteCurrentStage()
{
	if (!m_IsClearRewardActive)
	{
		StartClearReward();
	}
}

// Scheduled Stage Typeを取得する。
StageType Game::GetScheduledStageType() const
{
	// 現在のランフェーズから次に開始すべき戦闘種別を返す。
	if (m_RunController.Progress().GetPhase() == RunPhase::FinalBossReady ||
		m_RunController.Progress().GetPhase() == RunPhase::FinalBoss)
	{
		return StageType::Boss;
	}
	return StageType::Normal;
}

// Next Battleを開始する。
void Game::StartNextBattle()
{
	StartNextBattle(GetScheduledStageType());
}

// Next Battleを開始する。
void Game::StartNextBattle(StageType stageType)
{
	// 選択された戦闘種別に対応するステージを確定して戦闘へ遷移する。
	const bool enteringFinalBoss = m_RunController.Progress().BeginFinalBoss();
	if (enteringFinalBoss)
	{
		stageType = StageType::Boss;
	}
	const std::string previousStageId =
		m_RunController.Status().GetSelectedStageId().empty()
		? m_RunController.Status().GetLastStageId()
		: m_RunController.Status().GetSelectedStageId();

	if (m_McpNextStageOverride.has_value())
	{
		m_McpCurrentStageOverride =
			std::move(m_McpNextStageOverride.value());
		m_McpNextStageOverride.reset();
		m_RunController.Status().SetLastStageId(previousStageId);
		m_RunController.Status().SetSelectedStageId(
			m_McpCurrentStageOverride->id);
		if (enteringFinalBoss)
		{
			m_McpCurrentStageOverride->stageType = StageType::Boss;
		}
		ChangeScene(SceneType::Battle);
		return;
	}

	m_McpCurrentStageOverride.reset();
	const std::vector<StageData> stages = StageDataLoader::LoadAll(
		"assets/data/stage_01.json",
		"assets/data/enemy_data.json");

	const StageData* selectedStage = m_RunController.StageSelection().SelectStage(
		stages,
		stageType,
		m_RunController.Status().progress,
		previousStageId);
	if (selectedStage == nullptr)
	{
		m_RunController.Progress().CancelActiveNode();
		if (enteringFinalBoss)
		{
			m_RunController.Progress().CancelFinalBossStart();
		}
		std::cerr << "[Game] 戦闘ステージを選択できなかったため、"
			"シーン遷移を中止します" << std::endl;
		return;
	}

	m_RunController.Status().SetLastStageId(previousStageId);
	m_RunController.Status().SetSelectedStageId(selectedStage->id);
	ChangeScene(SceneType::Battle);
}

// Clear Rewardを更新する。
void Game::UpdateClearReward()
{
	const bool confirmed = std::exchange(m_ClearRewardMouseConfirmed, false) ||
		Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE);
	if (m_IsMidBossRelicSelectionActive)
	{
		const int offerCount = GetMidBossRelicOfferCount();
		if (offerCount <= 0)
		{
			m_IsMidBossRelicSelectionActive = false;
			return;
		}
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A) ||
			Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
		{
			m_SelectedRelicOfferIndex =
				(m_SelectedRelicOfferIndex + offerCount - 1) % offerCount;
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D) ||
			Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
		{
			m_SelectedRelicOfferIndex =
				(m_SelectedRelicOfferIndex + 1) % offerCount;
		}
		if (confirmed)
		{
			AcquireMidBossRelicOffer(m_SelectedRelicOfferIndex);
		}
		return;
	}

	if (m_IsClearRewardChosen)
	{
		if (confirmed)
		{
			ContinueAfterClearReward();
		}
		return;
	}

	if (Input::GetKeyTrigger(VK_UP) || Input::GetKeyTrigger(VK_W))
	{
		m_SelectedRewardIndex =
			(m_SelectedRewardIndex + kClearRewardCount - 1) % kClearRewardCount;
		m_SelectedRewardBallIndex = 0;
	}
	if (Input::GetKeyTrigger(VK_DOWN) || Input::GetKeyTrigger(VK_S))
	{
		m_SelectedRewardIndex = (m_SelectedRewardIndex + 1) % kClearRewardCount;
		m_SelectedRewardBallIndex = 0;
	}

	int targetCount = 0;
	if (m_SelectedRewardIndex == 0)
	{
		targetCount = m_RunController.Deck().GetCatalogCount();
	}
	else if (m_SelectedRewardIndex == 1)
	{
		targetCount = m_RunController.Deck().GetRewardTargetCount();
	}

	if (targetCount > 0)
	{
		if (Input::GetKeyTrigger(VK_LEFT) || Input::GetKeyTrigger(VK_A))
		{
			m_SelectedRewardBallIndex =
				(m_SelectedRewardBallIndex + targetCount - 1) % targetCount;
		}
		if (Input::GetKeyTrigger(VK_RIGHT) || Input::GetKeyTrigger(VK_D))
		{
			m_SelectedRewardBallIndex = (m_SelectedRewardBallIndex + 1) % targetCount;
		}
	}

	if (!confirmed)
	{
		return;
	}

	bool rewardApplied = false;
	nlohmann::json rewardDetails =
	{
		{ "money_before", m_RunController.Status().money },
	};
	switch (m_SelectedRewardIndex)
	{
	case 0:
	{
		std::string acquiredBallId;
		if (const PlayerBallData* selected =
			m_RunController.Deck().GetCatalogBall(m_SelectedRewardBallIndex))
		{
			acquiredBallId = selected->definitionId;
			rewardDetails["reward"] = "new_ball";
			rewardDetails["ball_id"] = selected->definitionId;
		}
		rewardApplied = m_RunController.Deck().AddCatalogBall(m_SelectedRewardBallIndex);
		if (rewardApplied)
		{
			PublishGameEvent(BallAcquiredEvent{ acquiredBallId });
		}
		m_RewardMessage = rewardApplied
			? UiText::NewBallAdded
			: UiText::NoBallAvailable;
		break;
	}
	case 1:
	{
		const PlayerBallData* selected =
			m_RunController.Deck().GetRewardTarget(m_SelectedRewardBallIndex);
		const int upgradeCost =
			GetClearRewardUpgradeCost(m_SelectedRewardBallIndex);
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
			m_RewardMessage = UiText::BallAlreadyMax;
		}
		else if (m_RunController.Status().money < upgradeCost)
		{
			m_RewardMessage = UiText::UpgradeMoneyShortage;
		}
		else
		{
			int chargedCost = 0;
			rewardApplied = ApplyClearRewardUpgrade(
				m_SelectedRewardBallIndex,
				chargedCost);
			rewardDetails["upgrade_cost"] = chargedCost;
			m_RewardMessage = rewardApplied
				? UiText::UpgradeApplied
				: UiText::UpgradeUnavailable;
		}
		break;
	}
	case 2:
		rewardDetails["reward"] = "extra_money";
		m_RunController.AddMoney(kExtraRewardMoney);
		rewardApplied = true;
		m_RewardMessage = UiText::ExtraMoneyReceived;
		break;
	default:
		break;
	}

	if (rewardApplied)
	{
		rewardDetails["money_after"] = m_RunController.Status().money;
		rewardDetails["controller"] = "human";
		RecordBalanceEvent("clear_reward_choice", rewardDetails);
		m_IsClearRewardChosen = true;
	}
}
// Begin Ball Selection の処理を実行する。
bool Game::BeginBallSelection()
{
	ResetShotRelicState();

	if (!m_RunController.Deck().PrepareOffer(GetBallOfferSize()))
	{
		return false;
	}

	m_SelectedOfferIndex = 0;
	m_SelectedHoldIndex = -1;

	for (int index = 0;
		index < m_RunController.Deck().GetOfferCount();
		index++)
	{
		if (m_RunController.Deck().WasHeldOffer(index))
		{
			m_SelectedHoldIndex = index;
			break;
		}
	}

	if (m_SelectedHoldIndex >= 0 &&
		m_RunController.Deck().GetOfferCount() > 1)
	{
		m_SelectedOfferIndex = 1;
	}
	else if (m_SelectedHoldIndex ==
		m_SelectedOfferIndex)
	{
		m_SelectedHoldIndex = -1;
	}

	ApplySelectedBallPreview();
	return true;
}

// Ball Selectionを更新する。
void Game::UpdateBallSelection()
{
	const int offerCount = m_RunController.Deck().GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	bool selectionChanged = false;
	for (int index = 0; index < offerCount && index < 4; index++)
	{
		if (Input::GetKeyTrigger('1' + index))
		{
			m_SelectedOfferIndex = index;
			selectionChanged = true;
			if (m_SelectedHoldIndex == index)
			{
				m_SelectedHoldIndex = -1;
			}
		}
	}

	constexpr int HOLD_KEYS[] = { 'Q', 'W', 'E', 'R' };
	for (int index = 0; index < offerCount && index < 4; index++)
	{
		if (!Input::GetKeyTrigger(HOLD_KEYS[index]) ||
			index == m_SelectedOfferIndex)
		{
			continue;
		}

		m_SelectedHoldIndex =
			m_SelectedHoldIndex == index ? -1 : index;
	}

	if (selectionChanged)
	{
		ApplySelectedBallPreview();
	}
}

// Selected Ball Previewを適用する。
void Game::ApplySelectedBallPreview()
{
	std::vector<PlayerBall*> players = GetComponents<PlayerBall>();
	if (players.empty() || players[0] == nullptr)
	{
		return;
	}

	ApplyPlayerStatusTo(players[0]);
}

// Ball Selection UIを描画する。
void GamePresentation::DrawBallSelection(Game& game)
{
	const int offerCount = game.m_RunController.Deck().GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	// 左下・右下の山札表示ぶんを空け、持ち札とHOLDは中央にまとめる。
	const float horizontalPadding = std::clamp(
		viewport->WorkSize.x * 0.105f,
		70.0f,
		132.0f);
	const float gap = 14.0f;
	const float collapsedCardHeight = 76.0f;
	const float expandedCardHeight = std::clamp(
		viewport->WorkSize.y * 0.37f,
		218.0f,
		280.0f);
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

	bool selectionChanged = false;
	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball = game.m_RunController.Deck().GetOffer(index);
		if (ball == nullptr)
		{
			continue;
		}

		const bool selected = game.m_SelectedOfferIndex == index;
		const bool held = game.m_SelectedHoldIndex == index;
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
			game.GetEffectivePlayerBallAttack(ball),
			game.GetEffectivePlayerBallDefense(ball),
			cardPosition,
			ImVec2(cardWidth, cardHeight),
			selected,
			held,
			expansion);

		if (interaction.select && !selected)
		{
			game.m_SelectedOfferIndex = index;
			if (held)
			{
				game.m_SelectedHoldIndex = -1;
			}
			selectionChanged = true;
		}
		if (interaction.toggleHold && !selected)
		{
			game.m_SelectedHoldIndex = held ? -1 : index;
		}
	}

	if (selectionChanged)
	{
		game.ApplySelectedBallPreview();
	}

	ImGui::End();
	ImGui::PopStyleVar(2);
}

// Clear Reward UIを描画する。
void GamePresentation::DrawClearReward(Game& game)
{
	GameUi::PrepareWindow("clear_reward", ImVec2(290, 90), ImVec2(700, 550));
	ImGui::Begin(UiText::ClearWindow, nullptr, ImGuiWindowFlags_NoCollapse);
	ImGui::TextUnformatted(UiText::StageClear);
	ImGui::Text(
		UiText::RewardMoneyFormat,
		game.m_RunController.GetCurrentStageRewardMoney());
	ImGui::Text(UiText::MoneyFormat, game.m_RunController.Status().money);
	ImGui::Text("HP %d / %d", game.m_RunController.Status().currentHp, game.m_RunController.Status().maxHp);
	ImGui::Separator();
	if (game.m_IsMidBossRelicSelectionActive)
	{
		ImGui::TextUnformatted("中ボス撃破報酬：レリックを1つ選択");
		for (int index = 0; index < game.GetMidBossRelicOfferCount(); ++index)
		{
			const auto* relic = game.GetMidBossRelicOffer(index);
			if (relic == nullptr) continue;
			ImGui::PushID(index);
			if (ImGui::Selectable(relic->name, index == game.m_SelectedRelicOfferIndex)) game.m_SelectedRelicOfferIndex = index;
			ImGui::TextWrapped("%s", relic->description);
			ImGui::PopID();
		}
		if (ImGui::Button("選択したレリックを獲得", ImVec2(-1, 40))) game.m_ClearRewardMouseConfirmed = true;
	}
	else if (game.m_IsClearRewardChosen)
	{
		ImGui::TextWrapped("%s", game.m_RewardMessage.c_str());
		if (ImGui::Button("次のルートへ", ImVec2(-1, 44))) game.m_ClearRewardMouseConfirmed = true;
	}
	else
	{
		ImGui::TextUnformatted(UiText::ChooseReward);
		for (int index = 0; index < kClearRewardCount; ++index)
		{
			if (ImGui::RadioButton(kClearRewardNames[index], index == game.m_SelectedRewardIndex))
			{
				game.m_SelectedRewardIndex = index;
				game.m_SelectedRewardBallIndex = 0;
			}
		}
		ImGui::BeginChild("reward_targets", ImVec2(0, -96), ImGuiChildFlags_Borders);
		const bool upgrading = game.m_SelectedRewardIndex == 1;
		const int count = game.m_SelectedRewardIndex == 0 ? game.m_RunController.Deck().GetCatalogCount() :
			(upgrading ? game.m_RunController.Deck().GetRewardTargetCount() : 0);
		for (int index = 0; index < count; ++index)
		{
			const auto* ball = upgrading ? game.m_RunController.Deck().GetRewardTarget(index) : game.m_RunController.Deck().GetCatalogBall(index);
			if (ball == nullptr) continue;
			ImGui::PushID(index);
			if (PlayerBallUI::Select(*ball, index == game.m_SelectedRewardBallIndex)) game.m_SelectedRewardBallIndex = index;
			if (index == game.m_SelectedRewardBallIndex)
			{
				ImGui::TextWrapped("%s", PlayerBallText::GetDescription(ball->definitionId));
				if (upgrading)
				{
					ImGui::TextWrapped("%s", PlayerBallText::GetUpgradePreview(*ball).c_str());
					const int upgradeCost = game.GetClearRewardUpgradeCost(index);
					if (upgradeCost >= 0) ImGui::Text(UiText::UpgradeCostFormat, upgradeCost);
					if (upgradeCost > game.m_RunController.Status().money) ImGui::TextUnformatted(UiText::UpgradeMoneyShortage);
				}
				else ImGui::TextWrapped("%s", PlayerBallText::GetStats(*ball, ball->status).c_str());
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (game.m_SelectedRewardIndex == 2) ImGui::Text("追加で %d Money を受け取ります。", kExtraRewardMoney);
		ImGui::EndChild();
		const int cost = upgrading ? game.GetClearRewardUpgradeCost(game.m_SelectedRewardBallIndex) : 0;
		const bool unavailable = upgrading ? cost < 0 || cost > game.m_RunController.Status().money :
			(game.m_SelectedRewardIndex == 0 && count == 0);
		ImGui::BeginDisabled(unavailable);
		if (ImGui::Button(upgrading ? "選択した個体を強化" : "選択した報酬を獲得", ImVec2(-1, 40))) game.m_ClearRewardMouseConfirmed = true;
		ImGui::EndDisabled();
		if (!game.m_RewardMessage.empty()) ImGui::TextWrapped("%s", game.m_RewardMessage.c_str());
	}
	ImGui::End();
}
