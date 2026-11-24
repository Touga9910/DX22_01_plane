#include "Game.h"
#include "GameBattleState.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameShotPrediction.h"
#include "BallPhysicsWorld.h"
#include "BalanceLogger.h"

#pragma execution_character_set("utf-8")

#include <chrono>

using DirectX::SimpleMath::Vector3;

/// <summary>
/// ゲームが初期化済みなら、固定物理ステップの時間計測をリセットする。
/// </summary>
void Game::ResetFrameTiming()
{
	if (m_Instance != nullptr)
	{
		m_Instance->m_RuntimeState->ResetPhysicsTiming();
	}
}

/// <summary>
/// 経過時間に応じて物理ステップを進め、戦闘状態の変化と処理時間を記録する。
/// </summary>
void Game::UpdateFixedPhysics(double elapsedSeconds)
{
	const int steps =
		m_RuntimeState->BeginPhysicsFrame(elapsedSeconds);

	for (int step = 0; step < steps; ++step)
	{
		const bool measureDetailedPhysics =
			m_RuntimeState->IsFrameTimingMeasurementActive();
		const auto worldFixedStarted = measureDetailedPhysics
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		const BattleState previousState =
			m_BattleState->controller.GetState();

		m_RuntimeState->World().FixedUpdate();
		const auto worldFixedFinished = measureDetailedPhysics
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};

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
		BalanceLogger& balanceLogger = BalanceLogger::GetInstance();
		const bool measurePhysics = balanceLogger.IsShotActive();
		const auto physicsStarted =
			(measurePhysics || measureDetailedPhysics)
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		const auto physicsResult = BallPhysicsWorld::Step(physicsContext);
		const auto physicsFinished =
			(measurePhysics || measureDetailedPhysics)
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		if (measurePhysics)
		{
			balanceLogger.RecordPhysicsStep(
				std::chrono::duration_cast<std::chrono::microseconds>(
					physicsFinished - physicsStarted).count());
		}

		m_RuntimeState->RecordPhysicsStep(
			physicsResult.substeps,
			physicsResult.limitReached);

		const auto fixedControllerStarted = measureDetailedPhysics
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		const bool stopCatchUp =
			m_BattleState->controller.OnFixedStepCompleted();
		const auto fixedControllerFinished = measureDetailedPhysics
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		if (measureDetailedPhysics)
		{
			const auto milliseconds = [](const auto& start, const auto& end)
			{
				return std::chrono::duration<double, std::milli>(
					end - start).count();
			};
			m_RuntimeState->RecordFixedPhysicsDetail(
				milliseconds(worldFixedStarted, worldFixedFinished),
				milliseconds(physicsStarted, physicsFinished),
				milliseconds(
					fixedControllerStarted,
					fixedControllerFinished));
		}

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
