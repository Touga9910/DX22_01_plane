#pragma once

#include "json/json.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

enum class BalanceCollisionType
{
	PlayerEnemy,
	EnemyEnemy,
};

struct BalanceBallSnapshot
{
	std::string id;
	int upgradeLevel = 0;
	int attack = 0;
	int defense = 0;
	float mass = 0.0f;
	float radius = 0.0f;
	float restitution = 0.0f;
	float friction = 0.0f;
	bool split = false;
	bool pierce = false;
};

struct BalanceEnemySnapshot
{
	std::string id;
	int maxHp = 0;
	int attack = 0;
	int defense = 0;
	float mass = 0.0f;
	float radius = 0.0f;
	float restitution = 0.0f;
	float friction = 0.0f;
	float positionX = 0.0f;
	float positionY = 0.0f;
	float positionZ = 0.0f;
};

class BalanceLogger final
{
public:
	static BalanceLogger& GetInstance();

	void BeginRun(
		int playerMaxHp,
		int playerCurrentHp,
		const std::vector<BalanceBallSnapshot>& deck,
		const std::string& controllerType);

	void BeginStage(
		const std::string& stageId,
		const std::string& stageType,
		int difficulty,
		int playerCurrentHp,
		int playerMaxHp,
		const std::vector<BalanceEnemySnapshot>& enemies);

	void BeginShot(
		const std::string& ballId,
		int upgradeLevel,
		float power,
		float positionX,
		float positionY,
		float positionZ,
		float velocityX,
		float velocityY,
		float velocityZ,
		int playerHp,
		int enemiesAlive,
		int enemiesDefeatedTotal);

	void RecordDamageCollision(BalanceCollisionType collisionType);

	void EndShot(
		int playerHp,
		int enemiesAlive,
		int enemiesDefeatedTotal);

	void EndStage(
		const std::string& result,
		int playerHp,
		int playerMaxHp,
		int enemiesDefeatedTotal);

	void EndRun(
		const std::string& result,
		int playerHp,
		int playerMaxHp,
		int clearedStageCount);

	bool IsRunActive() const { return m_RunActive; }
	bool IsStageActive() const { return m_StageActive; }
	bool IsShotActive() const { return m_ShotActive; }
	const std::filesystem::path& GetLogPath() const { return m_LogPath; }

private:
	BalanceLogger() = default;

	void Save();
	nlohmann::json* GetCurrentStage();

	static long long GetEpochMilliseconds();
	static std::string MakeUtcTimestamp();
	static std::string MakeRunId();
	static double CalculateShotScore(int collisionEffect);

private:
	static constexpr std::size_t NoStage =
		static_cast<std::size_t>(-1);

	nlohmann::json m_Root;
	nlohmann::json m_PendingShot;
	std::filesystem::path m_LogPath;

	std::size_t m_CurrentStageIndex = NoStage;
	long long m_RunStartedMs = 0;
	long long m_StageStartedMs = 0;
	long long m_ShotStartedMs = 0;

	int m_PlayerEnemyHitCount = 0;
	int m_EnemyEnemyHitCount = 0;
	int m_DefeatedAtShotStart = 0;

	bool m_RunActive = false;
	bool m_StageActive = false;
	bool m_ShotActive = false;
};
