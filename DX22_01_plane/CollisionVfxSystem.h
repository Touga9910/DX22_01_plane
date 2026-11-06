#pragma once

#include "CollisionPresentationRules.h"

#include <SimpleMath.h>

#include <array>
#include <cstdint>

struct ImDrawList;
struct ImVec2;

struct CollisionVfxDiagnostics
{
	int impactEvents = 0;
	int sparksGenerated = 0;
	int sparksSkipped = 0;
	int wavesGenerated = 0;
	int wavesMerged = 0;
	int wavesSkipped = 0;
	int burstsGenerated = 0;
	int burstsSkipped = 0;
	int defeatRequests = 0;
	int defeatVfxGenerated = 0;
	int defeatVfxSkipped = 0;
	int maximumActiveSparks = 0;
	int maximumActiveWaves = 0;
	int maximumActiveBursts = 0;
	int activeSparks = 0;
	int activeWaves = 0;
	int activeBursts = 0;
	int trailsGenerated = 0;
	int trailsSkipped = 0;
	int maximumActiveTrails = 0;
	int activeTrails = 0;
	int primitiveSubmissionsLastFrame = 0;
	int maximumPrimitiveSubmissions = 0;
};

// Presentation-only fixed-capacity collision VFX owned by GamePresentation.
// It never feeds values back into physics, damage, or game-rule state.
class CollisionVfxSystem final
{
public:
	enum class Style
	{
		Neutral,
		Pierce,
		Reflection,
		Anchor,
	};

	struct SpawnRequest
	{
		DirectX::SimpleMath::Vector3 worldPosition{};
		DirectX::SimpleMath::Vector3 collisionAxis{};
		CollisionPresentationRules::Strength strength =
			CollisionPresentationRules::Strength::Weak;
		int chainTier = 0;
		int priority = 0;
		int density = 2;
		unsigned long long frame = 0;
		bool secondaryCollision = false;
		bool milestone = false;
		bool defeated = false;
		bool enhanced = false;
		bool finisher = false;
		float radius = 0.0f;
		Style style = Style::Neutral;
	};

	struct TrailRequest
	{
		DirectX::SimpleMath::Vector3 startPosition{};
		DirectX::SimpleMath::Vector3 endPosition{};
		Style style = Style::Pierce;
		float lifetime = 0.18f;
		float width = 3.0f;
		int priority = 1;
		unsigned long long frame = 0;
		std::uintptr_t sourceKey = 0;
		bool enhanced = false;
		bool finisher = false;
	};

	struct Tuning
	{
		int weakSparkCount = 4;
		int mediumSparkCount = 7;
		int strongSparkCount = 11;
		float sparkSizeScale = 1.0f;
		float sparkSpeedScale = 1.0f;
		float sparkLifetimeScale = 1.0f;
		float waveRadiusScale = 1.0f;
		float waveExpansionSpeed = 1.0f;
		float waveLifetimeScale = 1.0f;
		float impactCoreScale = 1.0f;
		float impactRayLengthScale = 1.0f;
		std::uint32_t directColor = 0xff4896ffu;
		std::uint32_t secondaryColor = 0xffffdf91u;
		std::uint32_t chainColor = 0xff3a9affu;
		std::uint32_t peakColor = 0xffeaffffu;
		std::uint32_t defeatColor = 0xffbdeaffu;
		std::uint32_t pierceColor = 0xffff5cb8u;
		std::uint32_t reflectionColor = 0xff3addeeu;
		std::uint32_t anchorColor = 0xffb7e14bu;
	};

	using ProjectWorldToScreen = bool (*)(
		const DirectX::SimpleMath::Vector3&,
		ImVec2&);

	void Reset();
	void ClearActive();
	void Update(float elapsedSeconds, bool enabled);
	void Spawn(const SpawnRequest& request);
	void SpawnTrail(const TrailRequest& request);
	void Draw(
		ImDrawList* drawList,
		ProjectWorldToScreen projector,
		bool enabled);
	void DrawDebugTuning();

	const CollisionVfxDiagnostics& GetDiagnostics() const noexcept
	{
		return m_Diagnostics;
	}

	Tuning& GetTuning() noexcept { return m_Tuning; }
	const Tuning& GetTuning() const noexcept { return m_Tuning; }

private:
	static constexpr int MaximumSparkCapacity = 96;
	static constexpr int MaximumWaveCapacity = 12;
	static constexpr int MaximumBurstCapacity = 8;
	static constexpr int MaximumTrailCapacity = 24;

	struct Spark
	{
		DirectX::SimpleMath::Vector3 worldPosition{};
		DirectX::SimpleMath::Vector3 velocity{};
		float lifetime = 0.0f;
		float totalLifetime = 0.0f;
		float size = 1.0f;
		std::uint32_t color = 0xffffffffu;
		int priority = 0;
		bool active = false;
		bool secondaryCollision = false;
		bool glint = false;
	};

	struct Shockwave
	{
		DirectX::SimpleMath::Vector3 worldPosition{};
		float lifetime = 0.0f;
		float totalLifetime = 0.0f;
		float startRadius = 0.5f;
		float endRadius = 6.0f;
		float thickness = 2.0f;
		std::uint32_t color = 0xffffffffu;
		int priority = 0;
		unsigned long long createdFrame = 0;
		bool active = false;
		bool secondaryCollision = false;
		bool doubleRing = false;
		Style style = Style::Neutral;
	};

	struct ImpactBurst
	{
		DirectX::SimpleMath::Vector3 worldPosition{};
		DirectX::SimpleMath::Vector3 collisionAxis{};
		float lifetime = 0.0f;
		float totalLifetime = 0.0f;
		float coreRadius = 12.0f;
		float rayLength = 42.0f;
		std::uint32_t color = 0xffffffffu;
		std::uint32_t accentColor = 0xffffffffu;
		int rayCount = 8;
		int priority = 0;
		bool active = false;
		bool secondaryCollision = false;
		bool milestone = false;
		Style style = Style::Neutral;
	};

	struct DirectionTrail
	{
		DirectX::SimpleMath::Vector3 startPosition{};
		DirectX::SimpleMath::Vector3 endPosition{};
		float lifetime = 0.0f;
		float totalLifetime = 0.0f;
		float width = 3.0f;
		std::uint32_t color = 0xffffffffu;
		int priority = 0;
		unsigned long long createdFrame = 0;
		std::uintptr_t sourceKey = 0;
		Style style = Style::Pierce;
		bool active = false;
		bool enhanced = false;
		bool finisher = false;
	};

	int DesiredSparkCount(const SpawnRequest& request) const;
	int FindSparkSlot(int priority, bool allowInactive);
	int FindWaveSlot(int maximumWaves, int priority, bool forcePriority);
	int FindBurstSlot(int maximumBursts, int priority, bool forcePriority);
	int FindTrailSlot(int priority);
	std::uint32_t SelectColor(const SpawnRequest& request) const;
	std::uint32_t SelectStyleColor(Style style) const;
	void RefreshActiveDiagnostics();

	std::array<Spark, MaximumSparkCapacity> m_Sparks{};
	std::array<Shockwave, MaximumWaveCapacity> m_Waves{};
	std::array<ImpactBurst, MaximumBurstCapacity> m_Bursts{};
	std::array<DirectionTrail, MaximumTrailCapacity> m_Trails{};
	CollisionVfxDiagnostics m_Diagnostics{};
	Tuning m_Tuning{};
	unsigned long long m_GenerationFrame = ~0ull;
	int m_GeneratedThisFrame = 0;
};

