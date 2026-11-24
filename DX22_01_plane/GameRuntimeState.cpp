#include "GameRuntimeState.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace
{
	/// <summary>
	/// 計測時間を0以上に補正して累積値と最大値を更新する。
	/// </summary>
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

/// <summary>
/// 戦闘シーンの生成に使用するファクトリを設定する。
/// </summary>
void GameRuntimeState::SetBattleSceneFactory(
	SceneManager::BattleSceneFactory factory)
{
	m_Scenes.SetBattleSceneFactory(std::move(factory));
}

/// <summary>
/// 指定したシーンを初期化し、成功した場合はシーン遷移の状態をリセットする。
/// </summary>
bool GameRuntimeState::InitializeScene(SceneType sceneType)
{
	const bool initialized = m_Scenes.Initialize(sceneType);
	if (initialized) m_Transition.BeginFadeIn();
	return initialized;
}

/// <summary>
/// 実行中のシーンを指定した種別へ変更し、成否を返す。
/// </summary>
bool GameRuntimeState::ChangeScene(SceneType sceneType)
{
	return m_Scenes.Change(sceneType);
}

/// <summary>
/// シーン遷移の状態と現在のシーンをクリアする。
/// </summary>
void GameRuntimeState::ResetScene()
{
	m_Transition.Reset();
	m_Scenes.Reset();
}

/// <summary>
/// 現在使用中のシーンを返す。
/// </summary>
Scene* GameRuntimeState::CurrentScene() const
{
	return m_Scenes.Get();
}

/// <summary>
/// 現在使用中のシーンの種別を返す。
/// </summary>
SceneType GameRuntimeState::CurrentSceneType() const
{
	return m_Scenes.GetType();
}

/// <summary>
/// 実行時のワールドを変更可能な参照として返す。
/// </summary>
GameWorld& GameRuntimeState::World()
{
	return m_World;
}

/// <summary>
/// 物理更新用の時計と直近フレームの計測状態を初期化する。
/// </summary>
void GameRuntimeState::ResetPhysicsTiming()
{
	m_PhysicsClock.Reset();
	m_ResetPhysicsElapsed = true;
	m_PhysicsStepsLastFrame = 0;
	m_PhysicsDroppedSecondsLastFrame = 0.0;
	m_PhysicsRemainderSecondsLastFrame = 0.0;
}

/// <summary>
/// 経過時間を物理更新用の時計へ加え、このフレームで実行する固定ステップ数を返す。
/// </summary>
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

/// <summary>
/// 固定物理ステップのサブステップ数や上限到達状況を計測に加える。
/// </summary>
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

/// <summary>
/// 物理更新用の時計をリセットし、未消化の更新時間を破棄する。
/// </summary>
void GameRuntimeState::StopPhysicsCatchUp()
{
	m_PhysicsClock.Reset();
}

/// <summary>
/// 物理ステップとサブステップの最新の計測値をまとめて返す。
/// </summary>
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

/// <summary>
/// 新しいフレーム時間計測を開始し、集計とサンプルの状態を初期化する。
/// </summary>
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

/// <summary>
/// 計測中の場合は、更新処理の各工程にかかった時間を集計する。
/// </summary>
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

/// <summary>
/// 計測中の場合は、固定物理更新のワールド処理と詳細工程の時間を集計する。
/// </summary>
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

/// <summary>
/// 計測中の場合は、ショット予測にかかった時間を集計する。
/// </summary>
void GameRuntimeState::RecordShotPredictionTiming(double milliseconds)
{
	if (!m_FrameTiming.active) return;
	AddTiming(m_FrameTiming.shotPrediction, milliseconds);
}

/// <summary>
/// 照準中のフレーム時間を記録し、照準の開始と終了に合わせて計測状態を切り替える。
/// </summary>
void GameRuntimeState::RecordAimingFrameTiming(
	double frameMilliseconds,
	double updateMilliseconds,
	bool aiming)
{
	if (!aiming)
	{
		m_AimingTimingActive = false;
		return;
	}
	if (!m_AimingTimingActive)
	{
		m_AimingTimingSamples.fill({});
		m_AimingTimingWriteIndex = 0;
		m_AimingTimingSampleCount = 0;
		m_AimingPreviewPrediction = {};
		m_AimingPreviewWorldKey = {};
		m_AimingPreviewCacheReuseFrames = 0;
		m_AimingPreviewRefreshes = 0;
		m_AimingMaximumPowerPreviewCacheHits = 0;
		m_AimingPreviewBallCopies = 0;
		m_AimingElapsedMilliseconds = 0.0;
		++m_AimingTimingSequence;
		m_AimingPhysicsTickStart = m_PhysicsTickCount;
		m_AimingCcdLimitStart = m_PhysicsSubstepLimitCount;
		m_AimingTimingActive = true;
	}
	AimingFrameSample& sample =
		m_AimingTimingSamples[m_AimingTimingWriteIndex];
	sample.frameMilliseconds = (std::max)(0.0, frameMilliseconds);
	sample.updateMilliseconds = (std::max)(0.0, updateMilliseconds);
	m_AimingElapsedMilliseconds += sample.frameMilliseconds;
	m_AimingTimingWriteIndex =
		(m_AimingTimingWriteIndex + 1) % AimingTimingCapacity;
	m_AimingTimingSampleCount = (std::min)(
		m_AimingTimingSampleCount + 1,
		AimingTimingCapacity);
}

/// <summary>
/// 照準中の場合は、予測処理にかかった時間と予測回数などを集計する。
/// </summary>
void GameRuntimeState::RecordTrajectoryPreviewPrediction(
	double milliseconds,
	std::size_t copiedBalls)
{
	if (!m_AimingTimingActive) return;
	AddTiming(m_AimingPreviewPrediction, milliseconds);
	m_AimingPreviewBallCopies += copiedBalls;
}

/// <summary>
/// 照準中の場合は、弾道プレビューの盤面キーの計算時間を記録する。
/// </summary>
void GameRuntimeState::RecordTrajectoryPreviewWorldKey(
	double milliseconds)
{
	if (!m_AimingTimingActive) return;
	AddTiming(m_AimingPreviewWorldKey, milliseconds);
}

/// <summary>
/// 照準中の場合は、弾道プレビューのキャッシュを再利用したフレーム数を増やす。
/// </summary>
void GameRuntimeState::RecordTrajectoryPreviewCacheReuse()
{
	if (m_AimingTimingActive)
	{
		++m_AimingPreviewCacheReuseFrames;
	}
}

/// <summary>
/// 照準中の場合は、弾道プレビューを更新した回数を増やす。
/// </summary>
void GameRuntimeState::RecordTrajectoryPreviewRefresh()
{
	if (m_AimingTimingActive)
	{
		++m_AimingPreviewRefreshes;
	}
}

/// <summary>
/// 照準中の場合は、最大威力プレビューでキャッシュを再利用した回数を増やす。
/// </summary>
void GameRuntimeState::RecordMaximumPowerPreviewCacheReuse()
{
	if (m_AimingTimingActive)
	{
		++m_AimingMaximumPowerPreviewCacheHits;
	}
}

/// <summary>
/// 照準時のフレーム時間と弾道プレビューの計測結果をスナップショットにまとめる。
/// </summary>
AimingPerformanceSnapshot
GameRuntimeState::CaptureAimingPerformance() const
{
	AimingPerformanceSnapshot snapshot;
	snapshot.active = m_AimingTimingActive;
	snapshot.complete =
		m_AimingTimingSampleCount == AimingTimingCapacity;
	snapshot.sequence = m_AimingTimingSequence;
	snapshot.sampleCount =
		static_cast<int>(m_AimingTimingSampleCount);
	snapshot.previewPrediction = m_AimingPreviewPrediction;
	snapshot.previewWorldKey = m_AimingPreviewWorldKey;
	snapshot.previewCacheReuseFrames =
		m_AimingPreviewCacheReuseFrames;
	snapshot.previewRefreshes = m_AimingPreviewRefreshes;
	snapshot.maximumPowerPreviewCacheHits =
		m_AimingMaximumPowerPreviewCacheHits;
	snapshot.previewBallCopies = m_AimingPreviewBallCopies;
	snapshot.physicsFixedTickCount =
		m_PhysicsTickCount - m_AimingPhysicsTickStart;
	snapshot.ccdLimitReachedCount =
		m_PhysicsSubstepLimitCount - m_AimingCcdLimitStart;
	if (m_AimingTimingSampleCount == 0) return snapshot;

	std::vector<double> frameTimes;
	frameTimes.reserve(m_AimingTimingSampleCount);
	double frameTotal = 0.0;
	double updateTotal = 0.0;
	for (std::size_t index = 0;
		index < m_AimingTimingSampleCount;
		++index)
	{
		const AimingFrameSample& sample = m_AimingTimingSamples[index];
		frameTimes.push_back(sample.frameMilliseconds);
		frameTotal += sample.frameMilliseconds;
		updateTotal += sample.updateMilliseconds;
	}
	std::sort(frameTimes.begin(), frameTimes.end());
	const double count = static_cast<double>(frameTimes.size());
	const std::size_t p95Index = (std::min)(
		frameTimes.size() - 1,
		static_cast<std::size_t>(std::ceil(0.95 * count)) - 1);
	const std::size_t p99Index = (std::min)(
		frameTimes.size() - 1,
		static_cast<std::size_t>(std::ceil(0.99 * count)) - 1);
	snapshot.durationSeconds = m_AimingElapsedMilliseconds / 1000.0;
	snapshot.averageFrameMilliseconds = frameTotal / count;
	snapshot.maximumFrameMilliseconds = frameTimes.back();
	snapshot.p95FrameMilliseconds = frameTimes[p95Index];
	snapshot.p99FrameMilliseconds = frameTimes[p99Index];
	snapshot.averageFps = snapshot.averageFrameMilliseconds > 0.0
		? 1000.0 / snapshot.averageFrameMilliseconds
		: 0.0;
	snapshot.averageUpdateMilliseconds = updateTotal / count;
	return snapshot;
}

/// <summary>
/// 計測中のフレーム時間と各工程の時間をサンプルに保存する。
/// </summary>
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

/// <summary>
/// 収集したサンプルからフレーム時間の統計を集計し、計測を終了する。
/// </summary>
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
