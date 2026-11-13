#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "BallStatusJson.h"
#include "AnchorStackRules.h"
#include "GameBallSelectionState.h"
#include "GameObject.h"
#include "BallRenderComponent.h"
#include "PlayerBallText.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "PlayerBall.h"

#include <algorithm>

void Game::ApplyPlayerStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	const PlayerBallData* selectedBall = m_RunController->Deck().GetCurrent();
	if (selectedBall == nullptr)
	{
		selectedBall = m_RunController->Deck().GetOffer(
			GetSelectedBallOfferIndex());
	}

	if (selectedBall == nullptr)
	{
		ApplyPlayerRunStatusTo(player);
		return;
	}

	BallStatus playerStatus = selectedBall->status;
	playerStatus.defense = 0;
	player->SetStatus(playerStatus);
	if (auto* render = player->GetGameObject()->GetComponent<BallRenderComponent>())
	{
		const auto color = PlayerBallText::GetColor(*selectedBall);
		render->SetTint(DirectX::SimpleMath::Color(color[0], color[1], color[2], 1.0f));
	}

	// 物理半径だけでなく、描画モデルの大きさも選択したボールへ合わせる。
	if (selectedBall->status.radius > 0.0f && player->GetBall() != nullptr)
	{
		const float visualScale = selectedBall->status.radius;
		player->GetBall()->SetScale(DirectX::SimpleMath::Vector3(
			visualScale,
			visualScale,
			visualScale));
	}

	ApplyPlayerRunStatusTo(player);
}

// Player Run Status Toを適用する。

void Game::ApplyPlayerRunStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	m_RunController->Status() = NormalizePlayerRunStatus(m_RunController->Status());
	player->SetMaxHP(m_RunController->Status().maxHp);
	player->SetHP(m_RunController->Status().currentHp);
	ApplyRelicModifiersTo(player);
	InvalidateDebugCombatForecast("プレイヤー状態変更");
}

// Relic Modifiers Toを適用する。

void Game::CapturePlayerStatusFrom(const PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	BallStatus updatedStatus =
		NormalizeBallStatus(player->GetStatus());

	PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall != nullptr)
	{
		currentBall->status = updatedStatus;
	}

	m_RunController->Status() = NormalizePlayerRunStatus(m_RunController->Status());
	m_RunController->Status().currentHp = std::clamp(
		player->GetHP(),
		0,
		m_RunController->Status().maxHp
	);
}
// Current Player Statusを取得して保持する。

void Game::CaptureCurrentPlayerStatus()
{
	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	if (players.empty())
	{
		return;
	}
	CapturePlayerStatusFrom(players[0]);
}

// Next Player Ballを準備する。

bool Game::PrepareNextPlayerBall()
{
	DiscardCurrentPlayerBall();
	DirectX::SimpleMath::Vector3 returnPosition;
	if (AnchorStackRules::ConsumeReturnPosition(
		m_BattleState->anchorStacks, returnPosition))
	{
		const auto players = GameRuntime::World().GetComponents<PlayerBall>();
		if (!players.empty() && players.front() != nullptr &&
			players.front()->GetBall() != nullptr)
		{
			players.front()->GetBall()->ResetAtPosition(returnPosition);
		}
		if (m_BattleState->originAnchorCrossAxisBridgePending &&
			m_BattleState->crossAxisSupportGeneratedThisTurn <
				GameBattleState::CrossAxisSupportLimitPerTurn &&
			m_BattleState->pierceTraces.stackCount < PierceTraceRules::MaxStackCount)
		{
			++m_BattleState->pierceTraces.stackCount;
			++m_BattleState->crossAxisSupportGeneratedThisTurn;
			RecordBalanceEvent("support_cross_axis_generated", {
				{ "source", "player_origin_anchor" },
				{ "target_axis", "pierce" }, { "amount", 1 },
			});
		}
		m_BattleState->originAnchorCrossAxisBridgePending = false;
	}

	if (m_RunController->Deck().HasCurrent())
	{
		return true;
	}

	return BeginBallSelection();
}

// Stage Reward Moneyを計算する。

void Game::DiscardCurrentPlayerBall()
{
	if (!m_RunController->Deck().HasCurrent())
	{
		m_RunController->Deck().ClearCurrentUsed();
		return;
	}

	if (!m_RunController->Deck().IsCurrentUsed())
	{
		return;
	}

	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	if (!players.empty() && players[0] != nullptr)
	{
		CapturePlayerStatusFrom(players[0]);
	}

	m_RunController->Deck().DiscardCurrentIfUsed();
}

