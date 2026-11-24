#include "LocalPerformanceLogger.h"

#include "json/json.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
	std::uint64_t g_LastHandledSequence = 0;

	/// <summary>
	/// 呼び出し回数から処理時間の平均をミリ秒単位で求める。
	/// </summary>
	double Average(const TimingAggregate& timing)
	{
		return timing.callCount > 0
			? timing.totalMilliseconds / static_cast<double>(timing.callCount)
			: 0.0;
	}

	/// <summary>
	/// ログのファイル名に使うローカル日時を整形する。
	/// </summary>
	std::string Timestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t value = std::chrono::system_clock::to_time_t(now);
		std::tm local{};
		localtime_s(&local, &value);
		std::ostringstream result;
		result << std::put_time(&local, "%Y%m%d_%H%M%S");
		return result.str();
	}

	/// <summary>
	/// 既存のJSONログと名前が重複しない出力パスを探す。
	/// </summary>
	std::filesystem::path UniqueLogPath(
		const std::filesystem::path& directory,
		const std::string& stem)
	{
		for (int suffix = 0; suffix < 1000; ++suffix)
		{
			const std::string name = suffix == 0
				? stem + ".json"
				: stem + "_" + std::to_string(suffix) + ".json";
			const std::filesystem::path path = directory / name;
			if (!std::filesystem::exists(path)) return path;
		}
		return {};
	}
}

/// <summary>
/// 照準の計測番号が有効で、処理済み番号と異なるかを返す。
/// </summary>
bool LocalPerformanceLogger::IsAimingSequencePending(std::uint64_t sequence)
{
	return sequence != 0 && sequence != g_LastHandledSequence;
}

/// <summary>
/// 照準の計測が完了し、まだ記録対象であるかを判定する。
/// </summary>
bool LocalPerformanceLogger::IsAimingSnapshotPending(
	const AimingPerformanceSnapshot& snapshot)
{
	return snapshot.active && snapshot.complete && IsAimingSequencePending(snapshot.sequence);
}

/// <summary>
/// 計測条件を満たす照準の処理時間とキャッシュ統計をJSONログに記録する。
/// </summary>
void LocalPerformanceLogger::TryWriteAimingSnapshot(
	const AimingPerformanceSnapshot& snapshot,
	int livingEnemyCount,
	const std::string& stageId)
{
	if (!IsAimingSnapshotPending(snapshot))
	{
		return;
	}
	g_LastHandledSequence = snapshot.sequence;
	if (livingEnemyCount != 6 && livingEnemyCount != 10 &&
		livingEnemyCount != 12)
	{
		return;
	}

	const std::uint64_t cacheRequests =
		snapshot.previewCacheReuseFrames + snapshot.previewRefreshes;
	const double cacheHitRate = cacheRequests > 0
		? static_cast<double>(snapshot.previewCacheReuseFrames) /
			static_cast<double>(cacheRequests)
		: 0.0;
	const nlohmann::json document = {
		{ "schema_version", 1 },
		{ "captured_at_local", Timestamp() },
		{ "condition", {
			{ "enemy_count", livingEnemyCount },
			{ "stage_id", stageId },
#if defined(NDEBUG)
			{ "build", "Release" },
#else
			{ "build", "Debug" },
#endif
		} },
		{ "aiming_frame", {
			{ "sequence", snapshot.sequence },
			{ "sample_count", snapshot.sampleCount },
			{ "duration_seconds", snapshot.durationSeconds },
			{ "average_ms", snapshot.averageFrameMilliseconds },
			{ "p95_ms", snapshot.p95FrameMilliseconds },
			{ "p99_ms", snapshot.p99FrameMilliseconds },
			{ "maximum_ms", snapshot.maximumFrameMilliseconds },
			{ "average_fps", snapshot.averageFps },
			{ "average_update_ms", snapshot.averageUpdateMilliseconds },
		} },
		{ "trajectory_preview", {
			{ "refresh_average_ms", Average(snapshot.previewPrediction) },
			{ "refresh_maximum_ms", snapshot.previewPrediction.maximumMilliseconds },
			{ "refresh_count", snapshot.previewPrediction.callCount },
			{ "world_key_average_ms", Average(snapshot.previewWorldKey) },
			{ "world_key_maximum_ms", snapshot.previewWorldKey.maximumMilliseconds },
			{ "world_key_count", snapshot.previewWorldKey.callCount },
			{ "cache_reuse_count", snapshot.previewCacheReuseFrames },
			{ "cache_refresh_count", snapshot.previewRefreshes },
			{ "cache_hit_rate", cacheHitRate },
			{ "maximum_power_cache_hits", snapshot.maximumPowerPreviewCacheHits },
			{ "copied_ball_count", snapshot.previewBallCopies },
		} },
		{ "physics", {
			{ "fixed_tick_count", snapshot.physicsFixedTickCount },
			{ "ccd_limit_reached_count", snapshot.ccdLimitReachedCount },
		} },
	};

	std::error_code error;
	const std::filesystem::path directory =
		std::filesystem::path("logs") / "performance";
	std::filesystem::create_directories(directory, error);
	if (error)
	{
		std::cerr << "[Performance] Cannot create log directory: "
			<< error.message() << '\n';
		return;
	}
	const std::filesystem::path path = UniqueLogPath(
		directory,
		"aiming_" + Timestamp() + "_enemies" +
		std::to_string(livingEnemyCount));
	if (path.empty()) return;
	std::ofstream file(path, std::ios::binary);
	if (!file)
	{
		std::cerr << "[Performance] Cannot open log: " << path << '\n';
		return;
	}
	file << std::setw(2) << document << '\n';
	if (!file)
	{
		std::cerr << "[Performance] Cannot write log: " << path << '\n';
		return;
	}
	std::cout << "[Performance] Aiming log written: " << path << '\n';
}
