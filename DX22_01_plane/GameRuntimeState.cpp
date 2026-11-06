#include "GameRuntimeState.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace
{
	void AddTiming(
		TimingAggregate& aggregate,
		double milliseconds)
	{
		const double clamped = (std::max)(0.0, milliseconds);
		aggregate.totalMilliseconds += clamped;
		aggregate.maximumMilliseconds = (std::max)(
			aggregate.maximumMilliseconds,
			clamped);
		++aggregate.callCount;
	}
}

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
	m_PhysicsDroppedSecondsLastFrame = 0.0;
	m_PhysicsRemainderSecondsLastFrame = 0.0;
}

int GameRuntimeState::BeginPhysicsFrame(double elapsedSeconds)
{
	m_PhysicsStepsLastFrame = 0;
	m_PhysicsDroppedSecondsLastFrame = 0.0;
	if (m_ResetPhysicsElapsed)
	{
		elapsedSeconds = FixedStepClock::StepSeconds;
		m_ResetPhysicsElapsed = false;
	}
	const double droppedBefore = m_PhysicsClock.DroppedSeconds();
	const int steps = m_PhysicsClock.Advance(elapsedSeconds);
	m_PhysicsDroppedSecondsLastFrame =
		m_PhysicsClock.DroppedSeconds() - droppedBefore;
	m_PhysicsRemainderSecondsLastFrame =
		m_PhysicsClock.RemainderSeconds();
	return steps;
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

void GameRuntimeState::BeginFrameTimingMeasurement()
{
	const std::uint64_t nextSequence = m_FrameTiming.sequence + 1;
	m_FrameTimingSamples.fill({});
	m_FrameTimingWriteIndex = 0;
	m_FrameTimingSampleCount = 0;
	m_FrameTimingSawMoving = false;
	m_FrameTiming = {};
	m_FrameTiming.active = true;
	m_FrameTiming.sequence = nextSequence;
}

void GameRuntimeState::RecordUpdateTiming(
	const UpdateTimingFrame& timing)
{
	if (!m_FrameTiming.active) return;
	AddTiming(
		m_FrameTiming.audioFeedbackUpdate,
		timing.audioFeedbackMilliseconds);
	AddTiming(
		m_FrameTiming.presentationUpdate,
		timing.presentationMilliseconds);
	AddTiming(m_FrameTiming.gameMcpUpdate, timing.gameMcpMilliseconds);
	AddTiming(m_FrameTiming.sceneUpdate, timing.sceneMilliseconds);
	AddTiming(m_FrameTiming.cameraUpdate, timing.cameraMilliseconds);
	AddTiming(m_FrameTiming.worldUpdate, timing.worldUpdateMilliseconds);
	AddTiming(
		m_FrameTiming.fixedPhysicsUpdate,
		timing.fixedPhysicsMilliseconds);
	AddTiming(
		m_FrameTiming.worldLateUpdate,
		timing.worldLateUpdateMilliseconds);
	AddTiming(
		m_FrameTiming.battleControllerUpdate,
		timing.battleControllerMilliseconds);
	AddTiming(m_FrameTiming.otherUpdate, timing.otherMilliseconds);
}

void GameRuntimeState::RecordFixedPhysicsDetail(
	double worldFixedMilliseconds,
	double ballPhysicsMilliseconds,
	double battleControllerMilliseconds)
{
	if (!m_FrameTiming.active) return;
	AddTiming(m_FrameTiming.worldFixedUpdate, worldFixedMilliseconds);
	AddTiming(m_FrameTiming.ballPhysicsWorldStep, ballPhysicsMilliseconds);
	AddTiming(
		m_FrameTiming.fixedBattleController,
		battleControllerMilliseconds);
}

void GameRuntimeState::RecordShotPredictionTiming(double milliseconds)
{
	if (!m_FrameTiming.active) return;
	AddTiming(m_FrameTiming.shotPrediction, milliseconds);
}

void GameRuntimeState::RecordFrameTiming(
	double frameMilliseconds,
	double updateMilliseconds,
	double drawMilliseconds,
	double presentMilliseconds,
	int physicsSteps,
	bool shotMoving)
{
	if (!m_FrameTiming.active) return;
	FrameTimingSample& sample = m_FrameTimingSamples[m_FrameTimingWriteIndex];
	sample.frameMilliseconds = (std::max)(0.0, frameMilliseconds);
	sample.updateMilliseconds = (std::max)(0.0, updateMilliseconds);
	sample.drawMilliseconds = (std::max)(0.0, drawMilliseconds);
	sample.presentMilliseconds = (std::max)(0.0, presentMilliseconds);
	sample.physicsSteps = (std::clamp)(
		physicsSteps,
		0,
		FixedStepClock::MaxStepsPerFrame);
	sample.droppedSimulationSeconds = (std::max)(
		0.0,
		m_PhysicsDroppedSecondsLastFrame);
	sample.remainderSeconds = (std::max)(
		0.0,
		m_PhysicsRemainderSecondsLastFrame);
	m_FrameTimingWriteIndex =
		(m_FrameTimingWriteIndex + 1) % FrameTimingCapacity;
	m_FrameTimingSampleCount = (std::min)(
		m_FrameTimingSampleCount + 1,
		FrameTimingCapacity);
	m_FrameTiming.sampleCount = static_cast<int>(m_FrameTimingSampleCount);
	m_FrameTiming.physicsStepCount += (std::max)(0, physicsSteps);
	m_FrameTimingSawMoving = m_FrameTimingSawMoving || shotMoving;
	if (m_FrameTimingSawMoving && !shotMoving)
	{
		CompleteFrameTimingMeasurement();
	}
}

void GameRuntimeState::CompleteFrameTimingMeasurement()
{
	if (!m_FrameTiming.active || m_FrameTimingSampleCount == 0) return;
	std::vector<double> frameTimes;
	frameTimes.reserve(m_FrameTimingSampleCount);
	double updateTotal = 0.0;
	double drawTotal = 0.0;
	double presentTotal = 0.0;
	int consecutiveDelayedFrames = 0;
	for (std::size_t index = 0; index < m_FrameTimingSampleCount; ++index)
	{
		const FrameTimingSample& sample = m_FrameTimingSamples[index];
		frameTimes.push_back(sample.frameMilliseconds);
		updateTotal += sample.updateMilliseconds;
		drawTotal += sample.drawMilliseconds;
		presentTotal += sample.presentMilliseconds;
		if (sample.frameMilliseconds > FixedStepClock::StepSeconds * 1000.0)
		{
			++consecutiveDelayedFrames;
			m_FrameTiming.maximumConsecutiveDelayedFrames = (std::max)(
				m_FrameTiming.maximumConsecutiveDelayedFrames,
				consecutiveDelayedFrames);
		}
		else
		{
			consecutiveDelayedFrames = 0;
		}
		m_FrameTiming.maximumPhysicsStepsPerFrame = (std::max)(
			m_FrameTiming.maximumPhysicsStepsPerFrame,
			sample.physicsSteps);
		++m_FrameTiming.physicsStepsPerFrameHistogram[
			static_cast<std::size_t>(sample.physicsSteps)];
		if (sample.physicsSteps == 0)
		{
			++m_FrameTiming.zeroPhysicsStepFrames;
		}
		if (sample.physicsSteps == FixedStepClock::MaxStepsPerFrame)
		{
			++m_FrameTiming.framesAtPhysicsStepCap;
		}
		m_FrameTiming.droppedSimulationSeconds +=
			sample.droppedSimulationSeconds;
		m_FrameTiming.maximumRemainderSeconds = (std::max)(
			m_FrameTiming.maximumRemainderSeconds,
			sample.remainderSeconds);
		m_FrameTiming.maximumUpdateMilliseconds = (std::max)(
			m_FrameTiming.maximumUpdateMilliseconds,
			sample.updateMilliseconds);
		m_FrameTiming.maximumDrawMilliseconds = (std::max)(
			m_FrameTiming.maximumDrawMilliseconds,
			sample.drawMilliseconds);
		m_FrameTiming.maximumPresentMilliseconds = (std::max)(
			m_FrameTiming.maximumPresentMilliseconds,
			sample.presentMilliseconds);
	}
	std::sort(frameTimes.begin(), frameTimes.end());
	const double count = static_cast<double>(frameTimes.size());
	const auto percentile = [&](double value)
	{
		const std::size_t index = (std::min)(
			frameTimes.size() - 1,
			static_cast<std::size_t>(std::ceil(value * count)) - 1);
		return frameTimes[index];
	};
	const double total = std::accumulate(
		frameTimes.begin(),
		frameTimes.end(),
		0.0);
	m_FrameTiming.sampleCount = static_cast<int>(frameTimes.size());
	m_FrameTiming.averageFrameMilliseconds = total / count;
	m_FrameTiming.maximumFrameMilliseconds = frameTimes.back();
	m_FrameTiming.p95FrameMilliseconds = percentile(0.95);
	m_FrameTiming.p99FrameMilliseconds = percentile(0.99);
	m_FrameTiming.averageFps = m_FrameTiming.averageFrameMilliseconds > 0.0
		? 1000.0 / m_FrameTiming.averageFrameMilliseconds
		: 0.0;
	m_FrameTiming.averageUpdateMilliseconds = updateTotal / count;
	m_FrameTiming.averageDrawMilliseconds = drawTotal / count;
	m_FrameTiming.averagePresentMilliseconds = presentTotal / count;
	m_FrameTiming.onePercentLowFps =
		m_FrameTiming.p99FrameMilliseconds > 0.0
		? 1000.0 / m_FrameTiming.p99FrameMilliseconds
		: 0.0;
	m_FrameTiming.shotDurationSeconds = total / 1000.0;
	m_FrameTiming.framesAtMaximumPhysicsSteps =
		m_FrameTiming.physicsStepsPerFrameHistogram[
			static_cast<std::size_t>(
				m_FrameTiming.maximumPhysicsStepsPerFrame)];
	m_FrameTiming.active = false;
	m_FrameTiming.completed = true;
}
