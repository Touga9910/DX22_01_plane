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

int Game::GetSelectedBallOfferIndex() const
{
	return m_BallSelectionState->SelectedOfferIndex();
}

int Game::GetHeldBallOfferIndex() const
{
	return m_BallSelectionState->HeldOfferIndex();
}

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
