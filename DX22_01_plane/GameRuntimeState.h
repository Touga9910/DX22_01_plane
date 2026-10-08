#pragma once

#include "FixedStepClock.h"
#include "GameWorld.h"
#include "SceneManager.h"

#include <cstdint>

// Game's mutable loop infrastructure. Game keeps orchestration decisions while
// this type owns scene lifetime and fixed-step bookkeeping details.
class GameRuntimeState final
{
public:
	struct PhysicsTiming final
	{
		int substepsLastTick = 0;
		std::uint64_t substepLimitCount = 0;
		std::uint64_t tick = 0;
		int stepsLastFrame = 0;
		double droppedSeconds = 0.0;
	};

	void SetBattleSceneFactory(SceneManager::BattleSceneFactory factory);
	bool InitializeScene(SceneType sceneType);
	bool ChangeScene(SceneType sceneType);
	void ResetScene();
	Scene* CurrentScene() const;
	SceneType CurrentSceneType() const;
	GameWorld& World();

	void ResetPhysicsTiming();
	int BeginPhysicsFrame(double elapsedSeconds);
	void RecordPhysicsStep(int substeps, bool limitReached);
	void StopPhysicsCatchUp();
	PhysicsTiming CapturePhysicsTiming() const;

private:
	GameWorld m_World;
	SceneManager m_Scenes;
	FixedStepClock m_PhysicsClock;
	bool m_ResetPhysicsElapsed = true;
	std::uint64_t m_PhysicsTickCount = 0;
	int m_PhysicsStepsLastFrame = 0;
	int m_PhysicsSubstepsLastTick = 0;
	std::uint64_t m_PhysicsSubstepLimitCount = 0;
};
