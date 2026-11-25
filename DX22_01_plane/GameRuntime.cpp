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

/// <summary>
/// ゲームの実行時リソースを初期化する。
/// </summary>
bool GameRuntime::Initialize()
{
	return Game::Init();
}

/// <summary>
/// 経過秒数を渡してゲームの1フレームを更新する。
/// </summary>
void GameRuntime::Update(double elapsedSeconds)
{
	Game::Update(elapsedSeconds);
}

/// <summary>
/// ゲームのフレーム時間計測を初期状態へ戻す。
/// </summary>
void GameRuntime::ResetFrameTiming()
{
	Game::ResetFrameTiming();
}

/// <summary>
/// フレーム全体と更新、描画、画面提示の時間を現在のゲーム状態に関連付けて記録する。
/// </summary>
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

/// <summary>
/// 弾道プレビューで予測計算にかかった時間と計算内容を計測に加える。
/// </summary>
void GameRuntime::RecordTrajectoryPreviewPrediction(
	double milliseconds,
	std::size_t copiedBalls)
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewPrediction(
		milliseconds,
		copiedBalls);
}

/// <summary>
/// 弾道プレビューの盤面キーの生成時間を計測に加える。
/// </summary>
void GameRuntime::RecordTrajectoryPreviewWorldKey(double milliseconds)
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewWorldKey(
		milliseconds);
}

/// <summary>
/// 弾道プレビューでキャッシュを再利用したことを計測に記録する。
/// </summary>
void GameRuntime::RecordTrajectoryPreviewCacheReuse()
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewCacheReuse();
}

/// <summary>
/// 弾道プレビューの更新が発生したことを計測に記録する。
/// </summary>
void GameRuntime::RecordTrajectoryPreviewRefresh()
{
	CurrentGame().m_RuntimeState->RecordTrajectoryPreviewRefresh();
}

/// <summary>
/// 最大威力の弾道プレビューでキャッシュを再利用したことを計測に記録する。
/// </summary>
void GameRuntime::RecordMaximumPowerPreviewCacheReuse()
{
	CurrentGame().m_RuntimeState->RecordMaximumPowerPreviewCacheReuse();
}

/// <summary>
/// フレーム時間の計測が有効で、実際に計測中かを返す。
/// </summary>
bool GameRuntime::IsFrameTimingMeasurementActive()
{
	Game& game = CurrentGame();
	return game.IsFrameTimeMeasurementEnabled() &&
		game.m_RuntimeState->IsFrameTimingMeasurementActive();
}

/// <summary>
/// フレーム時間の計測が有効な場合、ショット予測に要した時間を記録する。
/// </summary>
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

/// <summary>
/// ゲームの現在のフレームを描画する。
/// </summary>
void GameRuntime::Draw()
{
	Game::Draw();
}

/// <summary>
/// ゲームの終了処理を実行し、実行時リソースを解放する。
/// </summary>
void GameRuntime::Shutdown()
{
	Game::Uninit();
}

/// <summary>
/// メインのカメラを参照として返す。
/// </summary>
Camera& GameRuntime::MainCamera()
{
	return Camera::GetInstance();
}

/// <summary>
/// 実行中のゲームが保持するワールドを参照として返す。
/// </summary>
GameWorld& GameRuntime::World()
{
	return Game::GetInstance()->m_RuntimeState->World();
}

/// <summary>
/// 実行中のゲームの共有インスタンスを参照として返す。
/// </summary>
Game& GameRuntime::CurrentGame()
{
	return *Game::GetInstance();
}

/// <summary>
/// 指定したゲームに指定名のゲームオブジェクトを作成する。
/// </summary>
GameObject* GameRuntime::CreateObject(
	Game& game,
	const std::string& name)
{
	return game.CreateGameObject(name);
}

/// <summary>
/// 実行中のゲームに指定名のゲームオブジェクトを作成する。
/// </summary>
GameObject* GameRuntime::CreateObject(const std::string& name)
{
	return CurrentGame().CreateGameObject(name);
}

/// <summary>
/// 実行中のゲームから指定したゲームオブジェクトを削除する。
/// </summary>
void GameRuntime::DestroyObject(GameObject* object)
{
	Game::GetInstance()->DeleteGameObject(object);
}
