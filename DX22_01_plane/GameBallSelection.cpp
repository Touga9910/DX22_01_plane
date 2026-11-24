#include "Game.h"
#include "GameBallSelectionState.h"
#include "GamePresentation.h"
#include "GameDebugController.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "RunController.h"
#include "PlayerDeck.h"
#include "PlayerBall.h"
#include "input.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

/// <summary>
/// 現在選択している球候補の位置を返す。
/// </summary>
int Game::GetSelectedBallOfferIndex() const
{
	return m_BallSelectionState->SelectedOfferIndex();
}

/// <summary>
/// 球候補の保持機能廃止後の互換値を取得する。
/// </summary>
int Game::GetHeldBallOfferIndex() const
{
	return m_BallSelectionState->HeldOfferIndex();
}

/// <summary>
/// 戦闘の進行や操作の制限を確認し、ショット準備の入力が可能かを返す。
/// </summary>
bool Game::IsShotPreparationInputEnabled() const
{
	return m_BallSelectionState != nullptr &&
		m_BallSelectionState->IsShotReady() &&
		m_GamePresentation != nullptr &&
		!m_GamePresentation->IsBattleInteractionBlocked() &&
		!IsPaused() &&
		!IsClearRewardActive() &&
		!m_DebugController->IsEditorOpen();
}

/// <summary>
/// デッキから球候補を用意し、球選択とプレビューを初期化する。
/// </summary>
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

/// <summary>
/// 球選択の表示状態を進め、操作入力に応じて選択やショット準備を処理する。
/// </summary>
void Game::UpdateBallSelection(float deltaSeconds)
{
	const int offerCount = m_RunController->Deck().GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	m_BallSelectionState->Update(deltaSeconds);
	if (m_BallSelectionState->IsShotReady() &&
		m_GamePresentation != nullptr &&
		!m_GamePresentation->IsBattleInteractionBlocked() &&
		!ImGui::GetIO().WantCaptureMouse &&
		Input::GetKeyTrigger(VK_RBUTTON))
	{
		for (PlayerBall* player :
			GameRuntime::World().GetComponents<PlayerBall>())
		{
			if (player != nullptr)
			{
				player->CancelShotPreparationInput();
			}
		}
		m_BallSelectionState->CancelShotPreparation();
		return;
	}
	if (!m_BallSelectionState->IsSelectionInteractive())
	{
		return;
	}

	for (int index = 0; index < offerCount && index < 7; index++)
	{
		if (Input::GetKeyTrigger('1' + index))
		{
			ApplyBalanceAutoBallSelection(index);
			m_BallSelectionState->ConfirmSelection(offerCount);
			return;
		}
	}

}

/// <summary>
/// 選択中の球の能力値をプレイヤー球へ反映する。
/// </summary>
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
