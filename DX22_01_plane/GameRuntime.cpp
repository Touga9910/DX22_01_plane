#include "Game.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "LocalPerformanceLogger.h"
#include "EnemyBall.h"
#include "RunController.h"
#include "PlayerRunStatus.h"
#include "Camera.h"

#pragma execution_character_set("utf-8")

#include <algorithm>

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

void GameRuntime::RecordFrameTiming(
	double frameMilliseconds,
	double updateMilliseconds,
	double drawMilliseconds,
	double presentMilliseconds)
{
	Game& game = CurrentGame();
	const BattleState state = game.GetBattleState();
	game.m_RuntimeState->RecordAimingFrameTiming(
		frameMilliseconds,
		updateMilliseconds,
		state == BattleState::AimingDirection ||
		state == BattleState::AimingPower);
	if (game.IsFrameTimeMeasurementEnabled() &&
		LocalPerformanceLogger::IsAimingSequencePending(
			game.m_RuntimeState->CompletedAimingSequence()))
	{
		const AimingPerformanceSnapshot snapshot =
			game.m_RuntimeState->CaptureAimingPerformance();
		if (LocalPerformanceLogger::IsAimingSnapshotPending(snapshot))
		{
			const auto enemies =
				GameRuntime::World().GetComponents<EnemyBall>();
			const int livingEnemyCount = static_cast<int>(std::count_if(
				enemies.begin(), enemies.end(),
				[](const EnemyBall* enemy)
				{
					return enemy != nullptr && !enemy->IsDefeated();
				}));
			LocalPerformanceLogger::TryWriteAimingSnapshot(
				snapshot,
				livingEnemyCount,
				game.m_RunController->Status().GetSelectedStageId());
		}
	}
	if (!game.IsFrameTimeMeasurementEnabled() ||
		!game.m_RuntimeState->IsFrameTimingMeasurementActive())
	{
		return;
	}
	game.m_RuntimeState->RecordFrameTiming(
		frameMilliseconds,
		updateMilliseconds,
		drawMilliseconds,
		presentMilliseconds,
		game.m_RuntimeState->CapturePhysicsTiming().stepsLastFrame,
		game.GetBattleState() == BattleState::BallsMoving);
}

void GameRuntime::RecordTrajectoryPreviewPrediction(
	double milliseconds,
	std::size_t copiedBalls)
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewPrediction(
		milliseconds,
		copiedBalls);
}

void GameRuntime::RecordTrajectoryPreviewWorldKey(double milliseconds)
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewWorldKey(
		milliseconds);
}

void GameRuntime::RecordTrajectoryPreviewCacheReuse()
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewCacheReuse();
}

void GameRuntime::RecordTrajectoryPreviewRefresh()
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewRefresh();
}

void GameRuntime::RecordMaximumPowerPreviewCacheReuse()
{
	CurrentGame().m_RuntimeState->RecordMaximumPowerPreviewCacheReuse();
}

bool GameRuntime::IsFrameTimingMeasurementActive()
{
	Game& game = CurrentGame();
	return game.IsFrameTimeMeasurementEnabled() &&
		game.m_RuntimeState->IsFrameTimingMeasurementActive();
}

void GameRuntime::RecordShotPredictionTiming(double milliseconds)
{
	Game& game = CurrentGame();
	if (!game.IsFrameTimeMeasurementEnabled() ||
		!game.m_RuntimeState->IsFrameTimingMeasurementActive())
	{
		return;
	}
	game.m_RuntimeState->RecordShotPredictionTiming(milliseconds);
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
