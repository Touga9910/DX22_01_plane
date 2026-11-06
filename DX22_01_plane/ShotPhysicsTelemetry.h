#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// One-shot observation state only. Physics and game rules decide which movement
// and contact episodes are valid before forwarding facts to this collector.
class ShotPhysicsTelemetry final
{
public:
	struct EnemyDescriptor final
	{
		std::uintptr_t key = 0;
		std::size_t stableIndex = 0;
		std::string enemyId;
		int hp = 0;
		int maxHp = 0;
		float positionX = 0.0f;
		float positionZ = 0.0f;
	};

	struct EnemyDistance final
	{
		EnemyDescriptor enemy;
		float distance = 0.0f;
	};

	struct CollisionEvent final
	{
		std::size_t firstEnemyIndex = 0;
		std::size_t secondEnemyIndex = 0;
		float approachSpeed = 0.0f;
		float positionX = 0.0f;
		float positionZ = 0.0f;
	};

	struct PairSummary final
	{
		std::size_t firstEnemyIndex = 0;
		std::size_t secondEnemyIndex = 0;
		int collisionCount = 0;
	};

	struct WallReflectionEvent final
	{
		int timelineEventIndex = -1;
		int wallIndex = 0;
		std::string wallGroup;
		std::string ballKind;
		float positionX = 0.0f;
		float positionZ = 0.0f;
		float velocityBeforeX = 0.0f;
		float velocityBeforeZ = 0.0f;
		float velocityAfterPhysicsX = 0.0f;
		float velocityAfterPhysicsZ = 0.0f;
		float velocityAfterEffectsX = 0.0f;
		float velocityAfterEffectsZ = 0.0f;
	};

	struct Snapshot final
	{
		std::vector<EnemyDistance> enemyDistances;
		std::vector<CollisionEvent> collisions;
		std::vector<PairSummary> pairSummaries;
		std::vector<WallReflectionEvent> wallReflections;
		float totalEnemyDistance = 0.0f;
		float maximumEnemyDistance = 0.0f;
		int involvedEnemyCount = 0;
		int maximumCollisionsForOnePair = 0;
		long long physicsProcessingMicroseconds = 0;
		int physicsStepCount = 0;
	};

	void BeginShot(const std::vector<EnemyDescriptor>& enemies)
	{
		Reset();
		m_Active = true;
		for (const EnemyDescriptor& enemy : enemies)
		{
			RegisterEnemy(enemy);
		}
	}

	void Reset()
	{
		m_Active = false;
		m_Enemies.clear();
		m_EnemyLookup.clear();
		m_Collisions.clear();
		m_WallReflections.clear();
		m_PairCounts.clear();
		m_InvolvedEnemies.clear();
		m_NextStableIndex = 0;
		m_PhysicsProcessingMicroseconds = 0;
		m_PhysicsStepCount = 0;
	}

	bool IsActive() const { return m_Active; }

	void RecordEnemyMovement(
		std::uintptr_t enemyKey,
		const std::string& enemyId,
		float distance)
	{
		if (!m_Active || enemyKey == 0 || distance <= 0.0f)
		{
			return;
		}
		const std::size_t slot = EnsureEnemy(enemyKey, enemyId);
		m_Enemies[slot].distance += distance;
	}

	void RecordEnemyEnemyCollision(
		std::uintptr_t firstEnemyKey,
		const std::string& firstEnemyId,
		std::uintptr_t secondEnemyKey,
		const std::string& secondEnemyId,
		float approachSpeed,
		float positionX,
		float positionZ)
	{
		if (!m_Active || firstEnemyKey == 0 || secondEnemyKey == 0 ||
			firstEnemyKey == secondEnemyKey)
		{
			return;
		}
		const std::size_t firstSlot =
			EnsureEnemy(firstEnemyKey, firstEnemyId);
		const std::size_t secondSlot =
			EnsureEnemy(secondEnemyKey, secondEnemyId);
		const std::size_t firstIndex =
			m_Enemies[firstSlot].enemy.stableIndex;
		const std::size_t secondIndex =
			m_Enemies[secondSlot].enemy.stableIndex;
		m_Collisions.push_back({
			firstIndex,
			secondIndex,
			(std::max)(0.0f, approachSpeed),
			positionX,
			positionZ,
		});
		m_InvolvedEnemies.insert(firstEnemyKey);
		m_InvolvedEnemies.insert(secondEnemyKey);
		const auto pair = (std::minmax)(firstIndex, secondIndex);
		++m_PairCounts[{ pair.first, pair.second }];
	}

	void RecordPhysicsStep(long long processingMicroseconds)
	{
		if (!m_Active)
		{
			return;
		}
		m_PhysicsProcessingMicroseconds +=
			(std::max)(0LL, processingMicroseconds);
		++m_PhysicsStepCount;
	}

	void RecordWallReflection(
		int wallIndex,
		const std::string& wallGroup,
		const std::string& ballKind,
		float positionX,
		float positionZ,
		float velocityBeforeX,
		float velocityBeforeZ,
		float velocityAfterPhysicsX,
		float velocityAfterPhysicsZ,
		float velocityAfterEffectsX,
		float velocityAfterEffectsZ,
		int timelineEventIndex = -1)
	{
		if (!m_Active) return;
		m_WallReflections.push_back({
			timelineEventIndex,
			wallIndex,
			wallGroup,
			ballKind,
			positionX,
			positionZ,
			velocityBeforeX,
			velocityBeforeZ,
			velocityAfterPhysicsX,
			velocityAfterPhysicsZ,
			velocityAfterEffectsX,
			velocityAfterEffectsZ,
		});
	}

	Snapshot EndShot()
	{
		Snapshot snapshot;
		if (!m_Active)
		{
			return snapshot;
		}
		m_Active = false;
		snapshot.enemyDistances = m_Enemies;
		snapshot.collisions = m_Collisions;
		snapshot.wallReflections = m_WallReflections;
		snapshot.involvedEnemyCount =
			static_cast<int>(m_InvolvedEnemies.size());
		snapshot.physicsProcessingMicroseconds =
			m_PhysicsProcessingMicroseconds;
		snapshot.physicsStepCount = m_PhysicsStepCount;
		for (const EnemyDistance& enemy : m_Enemies)
		{
			snapshot.totalEnemyDistance += enemy.distance;
			snapshot.maximumEnemyDistance = (std::max)(
				snapshot.maximumEnemyDistance,
				enemy.distance);
		}
		for (const auto& [pair, count] : m_PairCounts)
		{
			snapshot.pairSummaries.push_back({
				pair.first,
				pair.second,
				count,
			});
			snapshot.maximumCollisionsForOnePair = (std::max)(
				snapshot.maximumCollisionsForOnePair,
				count);
		}
		return snapshot;
	}

private:
	void RegisterEnemy(const EnemyDescriptor& enemy)
	{
		if (enemy.key == 0 || m_EnemyLookup.contains(enemy.key))
		{
			return;
		}
		m_EnemyLookup.emplace(enemy.key, m_Enemies.size());
		m_Enemies.push_back({ enemy, 0.0f });
		m_NextStableIndex = (std::max)(
			m_NextStableIndex,
			enemy.stableIndex + 1);
	}

	std::size_t EnsureEnemy(
		std::uintptr_t enemyKey,
		const std::string& enemyId)
	{
		const auto found = m_EnemyLookup.find(enemyKey);
		if (found != m_EnemyLookup.end())
		{
			return found->second;
		}
		EnemyDescriptor enemy;
		enemy.key = enemyKey;
		enemy.stableIndex = m_NextStableIndex;
		enemy.enemyId = enemyId;
		RegisterEnemy(enemy);
		return m_EnemyLookup.at(enemyKey);
	}

private:
	bool m_Active = false;
	std::vector<EnemyDistance> m_Enemies;
	std::unordered_map<std::uintptr_t, std::size_t> m_EnemyLookup;
	std::vector<CollisionEvent> m_Collisions;
	std::vector<WallReflectionEvent> m_WallReflections;
	std::map<std::pair<std::size_t, std::size_t>, int> m_PairCounts;
	std::unordered_set<std::uintptr_t> m_InvolvedEnemies;
	std::size_t m_NextStableIndex = 0;
	long long m_PhysicsProcessingMicroseconds = 0;
	int m_PhysicsStepCount = 0;
};
