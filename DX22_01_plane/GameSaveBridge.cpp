#include "Game.h"

#include "GameRunSaveState.h"
#include "RestSiteScene.h"
#include "UiText.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

GameRunSaveSnapshot Game::CaptureRunSaveSnapshot() const
{
	GameRunSaveSnapshot snapshot{};
	snapshot.status = m_RunController.Status();
	snapshot.progress = m_RunController.Progress().Capture();
	snapshot.relics = m_RunController.Relics();
	snapshot.deck = m_RunController.Deck().CaptureSnapshot();
	snapshot.stageSelector = m_RunController.StageSelection().CaptureSnapshot();
	snapshot.shopRelicOffers = m_RunController.ShopRelicOffers();
	snapshot.statistics = m_RunStatistics.GetState();
	snapshot.activeAscension = m_ActiveAscension;
	snapshot.runSeed = m_RunRandomSeed;
	snapshot.stageSelectionSeed = m_StageSelectionSeed;
	snapshot.routeSelectionSeed = m_RouteSelectionSeed;
	snapshot.routeSelectionCounter = m_RouteSelectionCounter;
	snapshot.pocketRandomEngine =
		m_BattleController.PocketRandomEngine();
	snapshot.relicRandomEngine = m_RunController.RelicRandomEngine();
	return snapshot;
}

void Game::RestoreRunSaveSnapshot(GameRunRestoreRequest request)
{
	GameRunSaveSnapshot& snapshot = request.snapshot;
	StartNewRun("human", "save_load", "", "", snapshot.runSeed);

	m_ActiveAscension = snapshot.activeAscension;
	m_RunController.RestHealRatio() = (std::max)(
		0.05f,
		m_DefaultRestHealRatio -
			ProgressionProfile::RestHealPenalty(snapshot.activeAscension));
	m_RunController.Status() = std::move(snapshot.status);
	m_RunController.Progress().Restore(std::move(snapshot.progress));
	m_RunController.Relics() = snapshot.relics;
	m_RunRandomSeed = snapshot.runSeed;
	m_StageSelectionSeed = snapshot.stageSelectionSeed;
	m_RouteSelectionSeed = snapshot.routeSelectionSeed;
	m_RouteSelectionCounter = snapshot.routeSelectionCounter;
	m_RunController.StageSelection().RestoreSnapshot(
		std::move(snapshot.stageSelector));
	m_BattleController.PocketRandomEngine() =
		snapshot.pocketRandomEngine;
	m_RunController.RelicRandomEngine() = snapshot.relicRandomEngine;
	m_RunStatistics.Restore(snapshot.statistics);
	m_RunActive = true;

	if (!m_RunController.Deck().RestoreSnapshot(std::move(snapshot.deck)))
	{
		throw std::runtime_error(UiText::InvalidSaveData);
	}

	m_IsRestoringRunSave = true;
	try
	{
		ChangeScene(request.resumeScene);
	}
	catch (...)
	{
		m_IsRestoringRunSave = false;
		throw;
	}
	m_IsRestoringRunSave = false;

	if (request.hasRelicRandomState)
	{
		// Undo the temporary shop roll so the next roll stays deterministic.
		m_RunController.RelicRandomEngine() =
			snapshot.relicRandomEngine;
	}
	if (request.resumeScene == SceneType::Shop &&
		!snapshot.shopRelicOffers.empty())
	{
		m_RunController.ShopRelicOffers() =
			std::move(snapshot.shopRelicOffers);
	}
	if (request.migratedToBossPreparation)
	{
		RecordBalanceEvent(
			"run_endpoint_save_migrated",
			{
				{ "area_progress", m_RunController.Progress().GetAreaProgress() },
				{ "run_phase", ToString(m_RunController.Progress().GetPhase()) },
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
