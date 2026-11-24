#include "BalanceLogger.h"
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

using json = nlohmann::json;

/// <summary>
/// 保存待ちのランログを所定のディレクトリへ書き出す。
/// </summary>
void BalanceLogger::Save()
{
	if (!m_RunActive || m_LogPath.empty() || !m_SavePending)
	{
		return;
	}

	std::error_code error;
	std::filesystem::create_directories(
		m_LogPath.parent_path(),
		error);
	if (error)
	{
		std::cerr
			<< "[BalanceLogger] Failed to create log directory: "
			<< error.message() << std::endl;
		return;
	}

	std::filesystem::path temporaryPath = m_LogPath;
	temporaryPath += ".tmp";

	{
		std::ofstream file(
			temporaryPath,
			std::ios::out | std::ios::trunc);
		if (!file)
		{
			std::cerr
				<< "[BalanceLogger] Failed to open log file: "
				<< temporaryPath.string() << std::endl;
			return;
		}

		file << std::setw(2) << m_Root << '\n';
		if (!file)
		{
			std::cerr
				<< "[BalanceLogger] Failed to write log file: "
				<< temporaryPath.string() << std::endl;
			return;
		}
	}

	std::filesystem::remove(m_LogPath, error);
	error.clear();
	std::filesystem::rename(
		temporaryPath,
		m_LogPath,
		error);

	if (error)
	{
		std::cerr
			<< "[BalanceLogger] Failed to replace log file: "
			<< error.message() << std::endl;
		return;
	}

	m_SavePending = false;
}

/// <summary>
/// 現在時刻をUnixエポックからのミリ秒数で返す。
/// </summary>
long long BalanceLogger::GetEpochMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

/// <summary>
/// 現在時刻をUTCのタイムスタンプ文字列に変換する。
/// </summary>
std::string BalanceLogger::MakeUtcTimestamp()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t nowTime =
		std::chrono::system_clock::to_time_t(now);

	std::tm utcTime{};
#if defined(_WIN32)
	gmtime_s(&utcTime, &nowTime);
#else
	gmtime_r(&nowTime, &utcTime);
#endif

	std::ostringstream stream;
	stream << std::put_time(
		&utcTime,
		"%Y-%m-%dT%H:%M:%SZ");
	return stream.str();
}

/// <summary>
/// 現在時刻を使ってログ用のラン識別子を生成する。
/// </summary>
std::string BalanceLogger::MakeRunId()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t nowTime =
		std::chrono::system_clock::to_time_t(now);
	const long long milliseconds =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()).count() % 1000;

	std::tm localTime{};
#if defined(_WIN32)
	localtime_s(&localTime, &nowTime);
#else
	localtime_r(&nowTime, &localTime);
#endif

	std::ostringstream stream;
	stream
		<< "run_"
		<< std::put_time(&localTime, "%Y%m%d_%H%M%S")
		<< '_'
		<< std::setw(3)
		<< std::setfill('0')
		<< milliseconds;
	return stream.str();
}
