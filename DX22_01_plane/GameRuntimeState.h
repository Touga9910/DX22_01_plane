#pragma once

#include "FixedStepClock.h"
#include "GameWorld.h"
#include "SceneManager.h"

#include <array>
#include <cstdint>

struct TimingAggregate final
{
	double totalMilliseconds = 0.0;
	double maximumMilliseconds = 0.0;
	std::uint64_t callCount = 0;
};

struct UpdateTimingFrame final
{
	double audioFeedbackMilliseconds = 0.0;
	double presentationMilliseconds = 0.0;
	double gameMcpMilliseconds = 0.0;
	double sceneMilliseconds = 0.0;
	double cameraMilliseconds = 0.0;
	double worldUpdateMilliseconds = 0.0;
	double fixedPhysicsMilliseconds = 0.0;
	double worldLateUpdateMilliseconds = 0.0;
	double battleControllerMilliseconds = 0.0;
	double otherMilliseconds = 0.0;
};

struct FrameTimingSnapshot final
{
	bool active = false;
	bool completed = false;
	std::uint64_t sequence = 0;
	int sampleCount = 0;
	int physicsStepCount = 0;
	double averageFrameMilliseconds = 0.0;
	double maximumFrameMilliseconds = 0.0;
	double p95FrameMilliseconds = 0.0;
	double p99FrameMilliseconds = 0.0;
	double averageFps = 0.0;
	double averageUpdateMilliseconds = 0.0;
	double maximumUpdateMilliseconds = 0.0;
	double averageDrawMilliseconds = 0.0;
	double maximumDrawMilliseconds = 0.0;
	double averagePresentMilliseconds = 0.0;
	double maximumPresentMilliseconds = 0.0;
	double onePercentLowFps = 0.0;
	double shotDurationSeconds = 0.0;
	int maximumPhysicsStepsPerFrame = 0;
	int framesAtMaximumPhysicsSteps = 0;
	int framesAtPhysicsStepCap = 0;
	int zeroPhysicsStepFrames = 0;
	int maximumConsecutiveDelayedFrames = 0;
	double droppedSimulationSeconds = 0.0;
	double maximumRemainderSeconds = 0.0;
	std::array<int, FixedStepClock::MaxStepsPerFrame + 1>
		physicsStepsPerFrameHistogram{};
	TimingAggregate presentationUpdate;
	TimingAggregate audioFeedbackUpdate;
	TimingAggregate gameMcpUpdate;
	TimingAggregate sceneUpdate;
	TimingAggregate cameraUpdate;
	TimingAggregate worldUpdate;
	TimingAggregate fixedPhysicsUpdate;
	TimingAggregate worldLateUpdate;
	TimingAggregate battleControllerUpdate;
	TimingAggregate otherUpdate;
	TimingAggregate worldFixedUpdate;
	TimingAggregate ballPhysicsWorldStep;
	TimingAggregate fixedBattleController;
	TimingAggregate shotPrediction;
};

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

	void BeginFrameTimingMeasurement();
	void RecordUpdateTiming(const UpdateTimingFrame& timing);
	void RecordFixedPhysicsDetail(
		double worldFixedMilliseconds,
		double ballPhysicsMilliseconds,
		double battleControllerMilliseconds);
	void RecordShotPredictionTiming(double milliseconds);
	void RecordFrameTiming(
		double frameMilliseconds,
		double updateMilliseconds,
		double drawMilliseconds,
		double presentMilliseconds,
		int physicsSteps,
		bool shotMoving);
	bool IsFrameTimingMeasurementActive() const
	{
		return m_FrameTiming.active;
	}
	const FrameTimingSnapshot& CaptureFrameTiming() const
	{
		return m_FrameTiming;
	}

private:
	struct FrameTimingSample final
	{
		double frameMilliseconds = 0.0;
		double updateMilliseconds = 0.0;
		double drawMilliseconds = 0.0;
		double presentMilliseconds = 0.0;
		int physicsSteps = 0;
		double droppedSimulationSeconds = 0.0;
		double remainderSeconds = 0.0;
	};
	static constexpr std::size_t FrameTimingCapacity = 512;
	void CompleteFrameTimingMeasurement();

	GameWorld m_World;
	SceneManager m_Scenes;
	FixedStepClock m_PhysicsClock;
	bool m_ResetPhysicsElapsed = true;
	std::uint64_t m_PhysicsTickCount = 0;
	int m_PhysicsStepsLastFrame = 0;
	int m_PhysicsSubstepsLastTick = 0;
	std::uint64_t m_PhysicsSubstepLimitCount = 0;
	double m_PhysicsDroppedSecondsLastFrame = 0.0;
	double m_PhysicsRemainderSecondsLastFrame = 0.0;
	std::array<FrameTimingSample, FrameTimingCapacity> m_FrameTimingSamples{};
	std::size_t m_FrameTimingWriteIndex = 0;
	std::size_t m_FrameTimingSampleCount = 0;
	bool m_FrameTimingSawMoving = false;
	FrameTimingSnapshot m_FrameTiming{};
};
