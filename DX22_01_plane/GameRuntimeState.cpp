#include "GameRuntimeState.h"

#include <utility>

void GameRuntimeState::SetBattleSceneFactory(
	SceneManager::BattleSceneFactory factory)
{
	m_Scenes.SetBattleSceneFactory(std::move(factory));
}

bool GameRuntimeState::InitializeScene(SceneType sceneType)
{
	return m_Scenes.Initialize(sceneType);
}

bool GameRuntimeState::ChangeScene(SceneType sceneType)
{
	return m_Scenes.Change(sceneType);
}

void GameRuntimeState::ResetScene()
{
	m_Scenes.Reset();
}

Scene* GameRuntimeState::CurrentScene() const
{
	return m_Scenes.Get();
}

SceneType GameRuntimeState::CurrentSceneType() const
{
	return m_Scenes.GetType();
}

GameWorld& GameRuntimeState::World()
{
	return m_World;
}

void GameRuntimeState::ResetPhysicsTiming()
{
	m_PhysicsClock.Reset();
	m_ResetPhysicsElapsed = true;
	m_PhysicsStepsLastFrame = 0;
}

int GameRuntimeState::BeginPhysicsFrame(double elapsedSeconds)
{
	m_PhysicsStepsLastFrame = 0;
	if (m_ResetPhysicsElapsed)
	{
		elapsedSeconds = FixedStepClock::StepSeconds;
		m_ResetPhysicsElapsed = false;
	}
	return m_PhysicsClock.Advance(elapsedSeconds);
}

void GameRuntimeState::RecordPhysicsStep(
	int substeps,
	bool limitReached)
{
	m_PhysicsSubstepsLastTick = substeps;
	if (limitReached)
	{
		++m_PhysicsSubstepLimitCount;
	}
	++m_PhysicsTickCount;
	++m_PhysicsStepsLastFrame;
}

void GameRuntimeState::StopPhysicsCatchUp()
{
	m_PhysicsClock.Reset();
}

GameRuntimeState::PhysicsTiming
GameRuntimeState::CapturePhysicsTiming() const
{
	return PhysicsTiming{
		m_PhysicsSubstepsLastTick,
		m_PhysicsSubstepLimitCount,
		m_PhysicsTickCount,
		m_PhysicsStepsLastFrame,
		m_PhysicsClock.DroppedSeconds(),
	};
}
