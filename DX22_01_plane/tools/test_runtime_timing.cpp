#include "../GameRuntimeState.h"
#include "../GameObject.h"
#include "../LocalPerformanceLogger.h"
#include "../Scene.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include "test_checks.h"

// Scene creation is outside this CPU-only timing fixture. The production
// World, timing ring, aggregations, and performance logger are linked below.
SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;
void SceneManager::SetBattleSceneFactory(BattleSceneFactory) { assert(false); }
bool SceneManager::Initialize(SceneType) { assert(false); return false; }
bool SceneManager::Change(SceneType) { assert(false); return false; }
void SceneManager::Reset() { assert(false); }

int main()
{
    using Clock = std::chrono::steady_clock;
    GameRuntimeState runtime;
    assert(runtime.CompletedAimingSequence() == 0);
    for (int i = 0; i < 299; ++i) runtime.RecordAimingFrameTiming(16, 2, true);
    assert(runtime.CompletedAimingSequence() == 0);
    runtime.RecordAimingFrameTiming(16, 2, true);
    auto first = runtime.CaptureAimingPerformance();
    assert(first.complete && first.active && first.sampleCount == 300);
    assert(first.averageFrameMilliseconds == 16 && first.p99FrameMilliseconds == 16);
    assert(first.averageUpdateMilliseconds == 2);
    assert(runtime.CompletedAimingSequence() == first.sequence);

    constexpr int frames = 20000;
    int capturedBefore = 0, capturedAfter = 0;
    const auto before = Clock::now();
    for (int i = 0; i < frames; ++i)
    {
        const auto snapshot = runtime.CaptureAimingPerformance();
        ++capturedBefore;
        if (LocalPerformanceLogger::IsAimingSnapshotPending(snapshot))
            LocalPerformanceLogger::TryWriteAimingSnapshot(snapshot, 6, "timing_fixture");
    }
    const double beforeMs = std::chrono::duration<double, std::milli>(Clock::now()-before).count();
    assert(!LocalPerformanceLogger::IsAimingSequencePending(first.sequence));
    runtime.RecordAimingFrameTiming(16, 2, false);
    assert(runtime.CompletedAimingSequence() == 0);
    for (int i = 0; i < 300; ++i) runtime.RecordAimingFrameTiming(16, 2, true);
    assert(runtime.CompletedAimingSequence() != first.sequence);
    const auto after = Clock::now();
    for (int i = 0; i < frames; ++i)
    {
        if (!LocalPerformanceLogger::IsAimingSequencePending(runtime.CompletedAimingSequence())) continue;
        const auto snapshot = runtime.CaptureAimingPerformance();
        ++capturedAfter;
        assert(snapshot.averageFrameMilliseconds == first.averageFrameMilliseconds);
        assert(snapshot.p99FrameMilliseconds == first.p99FrameMilliseconds);
        LocalPerformanceLogger::TryWriteAimingSnapshot(snapshot, 6, "timing_fixture");
    }
    const double afterMs = std::chrono::duration<double, std::milli>(Clock::now()-after).count();
    assert(capturedBefore == frames && capturedAfter == 1);
    int logs = 0;
    for (const auto& file : std::filesystem::directory_iterator("logs/performance"))
        if (file.path().extension() == ".json") ++logs;
    assert(logs == 2);
    // The ring must still refresh statistics after logging has been handled.
    for (int i = 0; i < 300; ++i) runtime.RecordAimingFrameTiming(20, 3, true);
    assert(runtime.CaptureAimingPerformance().averageFrameMilliseconds == 20);
    assert(!LocalPerformanceLogger::IsAimingSequencePending(runtime.CompletedAimingSequence()));
    std::cout << "PASS: incomplete/complete/inactive/new sequence, once-only logging, rolling statistics\n"
        << "20000-frame logging gate CPU ms: before=" << beforeMs << " after=" << afterMs
        << "; snapshots=" << capturedBefore << " -> " << capturedAfter << '\n';
}
