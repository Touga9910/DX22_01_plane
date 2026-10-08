#include "Game.h"

#include "GameBattleState.h"
#include "GameBalanceRuntime.h"
#include "GamePersistenceRuntime.h"
#include "GameRunSaveState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunProgressController.h"
#include "StageSelector.h"
#include "RestSiteScene.h"
#include "UiText.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

GameRunSaveSnapshot Game::CaptureRunSaveSnapshot() const
{
	GameRunSaveSnapshot snapshot{};
	snapshot.status = m_RunController->Status();
	snapshot.progress = m_RunController->Progress().Capture();
	snapshot.relics = m_RunController->Relics();
	snapshot.deck = m_RunController->Deck().CaptureSnapshot();
	snapshot.stageSelector = m_RunController->StageSelection().CaptureSnapshot();
	snapshot.shopBallOffers = m_RunController->ShopBallOffers();
	snapshot.shopRelicOffers = m_RunController->ShopRelicOffers();
	snapshot.statistics = m_PersistenceRuntime->Statistics().GetState();
	snapshot.activeAscension = m_PersistenceRuntime->ActiveAscension();
	snapshot.runSeed = m_BalanceRuntime->runRandomSeed;
	snapshot.stageSelectionSeed = m_BalanceRuntime->stageSelectionSeed;
	snapshot.routeSelectionSeed = m_BalanceRuntime->routeSelectionSeed;
	snapshot.routeSelectionCounter = m_BalanceRuntime->routeSelectionCounter;
	snapshot.pocketRandomEngine =
		m_BattleState->controller.PocketRandomEngine();
	snapshot.relicRandomEngine = m_RunController->RelicRandomEngine();
	return snapshot;
}

void Game::RestoreRunSaveSnapshot(GameRunRestoreRequest request)
{
	GameRunSaveSnapshot& snapshot = request.snapshot;
	const std::mt19937 savedDeckRandomEngine = snapshot.deck.randomEngine;
	StartNewRun("human", "save_load", "", "", snapshot.runSeed);

	m_PersistenceRuntime->ActiveAscension() = snapshot.activeAscension;
	m_RunController->RestHealRatio() = (std::max)(
		0.05f,
		m_PersistenceRuntime->DefaultRestHealRatio() -
			ProgressionProfile::RestHealPenalty(snapshot.activeAscension));
	m_RunController->Status() = std::move(snapshot.status);
	m_RunController->Progress().Restore(std::move(snapshot.progress));
	m_RunController->Relics() = snapshot.relics;
	m_BalanceRuntime->runRandomSeed = snapshot.runSeed;
	m_BalanceRuntime->stageSelectionSeed = snapshot.stageSelectionSeed;
	m_BalanceRuntime->routeSelectionSeed = snapshot.routeSelectionSeed;
	m_BalanceRuntime->routeSelectionCounter = snapshot.routeSelectionCounter;
	m_RunController->StageSelection().RestoreSnapshot(
		std::move(snapshot.stageSelector));
	m_BattleState->controller.PocketRandomEngine() =
		snapshot.pocketRandomEngine;
	m_RunController->RelicRandomEngine() = snapshot.relicRandomEngine;
	m_PersistenceRuntime->Statistics().Restore(snapshot.statistics);
	ActivateRunSession();

	if (!m_RunController->Deck().RestoreSnapshot(std::move(snapshot.deck)))
	{
		throw std::runtime_error(UiText::InvalidSaveData);
	}

	m_PersistenceRuntime->SetRestoringRunSave(true);
	try
	{
		ChangeScene(request.resumeScene);
	}
	catch (...)
	{
		m_PersistenceRuntime->SetRestoringRunSave(false);
		throw;
	}
	m_PersistenceRuntime->SetRestoringRunSave(false);

	if (request.hasRelicRandomState)
	{
		// Undo the temporary shop roll so the next roll stays deterministic.
		m_RunController->RelicRandomEngine() =
			snapshot.relicRandomEngine;
	}
	if (request.resumeScene == SceneType::Shop &&
		!snapshot.shopBallOffers.empty())
	{
		// ShopSceneの一時抽選を取り消し、保存時の3枠と乱数状態を復元する。
		m_RunController->ShopBallOffers() =
			std::move(snapshot.shopBallOffers);
		m_RunController->Deck().RandomEngine() = savedDeckRandomEngine;
	}
	if (request.resumeScene == SceneType::Shop &&
		!snapshot.shopRelicOffers.empty())
	{
		m_RunController->ShopRelicOffers() =
			std::move(snapshot.shopRelicOffers);
	}
	if (request.migratedToBossPreparation)
	{
		RecordBalanceEvent(
			"run_endpoint_save_migrated",
			{
				{ "area_progress", m_RunController->Progress().GetAreaProgress() },
				{ "run_phase", ToString(m_RunController->Progress().GetPhase()) },
			});
	}
	if (!request.migratedToBossPreparation &&
		request.resumeScene == SceneType::RestSite &&
		request.restActionUsed)
	{
		if (RestSiteScene* rest =
			dynamic_cast<RestSiteScene*>(GetCurrentScene()))
		{
			rest->MarkActionUsed();
		}
	}
}
