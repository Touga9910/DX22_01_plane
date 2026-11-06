#include "CollisionVfxSystem.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>

using DirectX::SimpleMath::Vector3;

namespace
{
	constexpr float kFrameEpsilon = 0.000001f;

	float Clamp01(float value)
	{
		return std::clamp(value, 0.0f, 1.0f);
	}

	ImU32 WithAlpha(ImU32 color, float alpha)
	{
		return (color & 0x00ffffffu) |
			(static_cast<ImU32>(255.0f * Clamp01(alpha)) << 24);
	}

	float DeterministicJitter(unsigned long long frame, int index)
	{
		std::uint32_t value = static_cast<std::uint32_t>(frame) ^
			(static_cast<std::uint32_t>(index + 1) * 0x9e3779b9u);
		value ^= value >> 16;
		value *= 0x7feb352du;
		value ^= value >> 15;
		return static_cast<float>(value & 0xffffu) / 65535.0f - 0.5f;
	}

	void DrawWorldRing(
		ImDrawList* drawList,
		CollisionVfxSystem::ProjectWorldToScreen projector,
		const Vector3& center,
		float radius,
		ImU32 color,
		float thickness)
	{
		constexpr int segmentCount = 32;
		std::array<ImVec2, segmentCount> points{};
		for (int index = 0; index < segmentCount; ++index)
		{
			const float angle = DirectX::XM_2PI *
				static_cast<float>(index) / static_cast<float>(segmentCount);
			const Vector3 point = center + Vector3(
				std::cos(angle) * radius,
				0.18f,
				std::sin(angle) * radius);
			if (!projector(point, points[index]))
			{
				return;
			}
		}
		drawList->AddPolyline(
			points.data(),
			segmentCount,
			color,
			ImDrawFlags_Closed,
			thickness);
	}

	void ColorToFloats(std::uint32_t color, float values[4])
	{
		values[0] = static_cast<float>(color & 0xffu) / 255.0f;
		values[1] = static_cast<float>((color >> 8) & 0xffu) / 255.0f;
		values[2] = static_cast<float>((color >> 16) & 0xffu) / 255.0f;
		values[3] = static_cast<float>((color >> 24) & 0xffu) / 255.0f;
	}

	std::uint32_t FloatsToColor(const float values[4])
	{
		return IM_COL32(
			static_cast<int>(Clamp01(values[0]) * 255.0f),
			static_cast<int>(Clamp01(values[1]) * 255.0f),
			static_cast<int>(Clamp01(values[2]) * 255.0f),
			static_cast<int>(Clamp01(values[3]) * 255.0f));
	}
}

void CollisionVfxSystem::Reset()
{
	ClearActive();
	m_Diagnostics = {};
	m_GenerationFrame = ~0ull;
	m_GeneratedThisFrame = 0;
}

void CollisionVfxSystem::ClearActive()
{
	for (Spark& spark : m_Sparks) spark.active = false;
	for (Shockwave& wave : m_Waves) wave.active = false;
	for (ImpactBurst& burst : m_Bursts) burst.active = false;
	for (DirectionTrail& trail : m_Trails) trail.active = false;
	RefreshActiveDiagnostics();
}

void CollisionVfxSystem::Update(float elapsedSeconds, bool enabled)
{
	if (!enabled)
	{
		ClearActive();
		return;
	}
	const float delta = std::clamp(elapsedSeconds, 0.0f, 0.05f);
	for (Spark& spark : m_Sparks)
	{
		if (!spark.active) continue;
		spark.lifetime -= delta;
		if (spark.lifetime <= 0.0f)
		{
			spark.active = false;
			continue;
		}
		spark.worldPosition += spark.velocity * delta;
		spark.velocity *= (std::max)(0.0f, 1.0f - 3.8f * delta);
	}
	for (Shockwave& wave : m_Waves)
	{
		if (!wave.active) continue;
		wave.lifetime -= delta;
		if (wave.lifetime <= 0.0f) wave.active = false;
	}
	for (ImpactBurst& burst : m_Bursts)
	{
		if (!burst.active) continue;
		burst.lifetime -= delta;
		if (burst.lifetime <= 0.0f) burst.active = false;
	}
	for (DirectionTrail& trail : m_Trails)
	{
		if (!trail.active) continue;
		trail.lifetime -= delta;
		if (trail.lifetime <= 0.0f) trail.active = false;
	}
	RefreshActiveDiagnostics();
}

int CollisionVfxSystem::DesiredSparkCount(const SpawnRequest& request) const
{
	int count = m_Tuning.weakSparkCount;
	if (request.strength == CollisionPresentationRules::Strength::Medium)
		count = m_Tuning.mediumSparkCount;
	else if (request.strength == CollisionPresentationRules::Strength::Strong)
		count = m_Tuning.strongSparkCount;
	if (request.secondaryCollision)
		count += (std::min)(2, request.chainTier);
	if (request.milestone) ++count;
	if (request.defeated) count += 3;
	if (request.density == 1)
		count = (std::max)(2, static_cast<int>(std::ceil(count * 0.60f)));
	else if (request.density == 3)
		count = static_cast<int>(std::ceil(count * 1.10f));
	return std::clamp(count, 0, 14);
}

int CollisionVfxSystem::FindSparkSlot(int priority, bool allowInactive)
{
	if (allowInactive)
	{
		for (int index = 0; index < MaximumSparkCapacity; ++index)
		{
			if (!m_Sparks[index].active) return index;
		}
	}
	int lowestIndex = -1;
	for (int index = 0; index < MaximumSparkCapacity; ++index)
	{
		if (m_Sparks[index].active &&
			m_Sparks[index].priority < priority &&
			(lowestIndex < 0 ||
				m_Sparks[index].priority < m_Sparks[lowestIndex].priority))
		{
			lowestIndex = index;
		}
	}
	return lowestIndex;
}

int CollisionVfxSystem::FindWaveSlot(
	int maximumWaves,
	int priority,
	bool forcePriority)
{
	int activeCount = 0;
	for (int index = 0; index < maximumWaves; ++index)
	{
		if (!m_Waves[index].active) return index;
		++activeCount;
	}
	if (activeCount < maximumWaves) return activeCount;
	int lowestIndex = -1;
	for (int index = 0; index < maximumWaves; ++index)
	{
		if (m_Waves[index].priority < priority ||
			(forcePriority && m_Waves[index].priority == priority))
		{
			if (lowestIndex < 0 ||
				m_Waves[index].priority < m_Waves[lowestIndex].priority)
				lowestIndex = index;
		}
	}
	return lowestIndex;
}

int CollisionVfxSystem::FindBurstSlot(
	int maximumBursts,
	int priority,
	bool forcePriority)
{
	for (int index = 0; index < maximumBursts; ++index)
	{
		if (!m_Bursts[index].active) return index;
	}
	int lowestIndex = -1;
	for (int index = 0; index < maximumBursts; ++index)
	{
		if (m_Bursts[index].priority < priority ||
			(forcePriority && m_Bursts[index].priority == priority))
		{
			if (lowestIndex < 0 ||
				m_Bursts[index].priority < m_Bursts[lowestIndex].priority)
				lowestIndex = index;
		}
	}
	return lowestIndex;
}

int CollisionVfxSystem::FindTrailSlot(int priority)
{
	for (int index = 0; index < MaximumTrailCapacity; ++index)
	{
		if (!m_Trails[index].active) return index;
	}
	int lowestIndex = -1;
	for (int index = 0; index < MaximumTrailCapacity; ++index)
	{
		if (m_Trails[index].priority < priority &&
			(lowestIndex < 0 ||
				m_Trails[index].priority < m_Trails[lowestIndex].priority))
		{
			lowestIndex = index;
		}
	}
	return lowestIndex;
}

std::uint32_t CollisionVfxSystem::SelectStyleColor(Style style) const
{
	switch (style)
	{
	case Style::Pierce: return m_Tuning.pierceColor;
	case Style::Reflection: return m_Tuning.reflectionColor;
	case Style::Anchor: return m_Tuning.anchorColor;
	default: return m_Tuning.directColor;
	}
}

std::uint32_t CollisionVfxSystem::SelectColor(
	const SpawnRequest& request) const
{
	if (request.defeated) return m_Tuning.defeatColor;
	if (request.style != Style::Neutral) return SelectStyleColor(request.style);
	if (!request.secondaryCollision)
		return request.chainTier >= 3 ? m_Tuning.chainColor : m_Tuning.directColor;
	if (request.chainTier >= 4 && request.milestone) return m_Tuning.peakColor;
	return m_Tuning.secondaryColor;
}

void CollisionVfxSystem::Spawn(const SpawnRequest& request)
{
	++m_Diagnostics.impactEvents;
	if (request.defeated) ++m_Diagnostics.defeatRequests;
	if (request.density <= 0)
	{
		if (request.defeated) ++m_Diagnostics.defeatVfxSkipped;
		return;
	}
	bool defeatVisualAllocated = false;
	if (request.frame != m_GenerationFrame)
	{
		m_GenerationFrame = request.frame;
		m_GeneratedThisFrame = 0;
	}

	if (!request.milestone && !request.defeated && request.priority <= 1)
	{
		for (Shockwave& existing : m_Waves)
		{
			Vector3 distance = existing.worldPosition - request.worldPosition;
			distance.y = 0.0f;
			if (existing.active &&
				existing.style == request.style &&
				existing.secondaryCollision == request.secondaryCollision &&
				existing.lifetime > existing.totalLifetime * 0.45f &&
				distance.LengthSquared() <= 16.0f)
			{
				existing.endRadius = (std::max)(
					existing.endRadius,
					6.5f * m_Tuning.waveRadiusScale);
				existing.thickness = (std::max)(existing.thickness, 2.3f);
				existing.lifetime = (std::max)(
					existing.lifetime,
					existing.totalLifetime * 0.82f);
				++m_Diagnostics.wavesMerged;
				m_Diagnostics.sparksSkipped += DesiredSparkCount(request);
				return;
			}
		}
	}

	const int maximumWaves = CollisionPresentationRules::MaximumDecorations(
		request.density);
	const int waveSlot = FindWaveSlot(
		maximumWaves,
		request.priority,
		request.milestone || request.defeated);
	if (waveSlot >= 0)
	{
		Shockwave wave;
		wave.active = true;
		wave.worldPosition = request.worldPosition;
		wave.priority = request.priority;
		wave.createdFrame = request.frame;
		wave.secondaryCollision = request.secondaryCollision;
		wave.doubleRing =
			request.strength == CollisionPresentationRules::Strength::Strong ||
			request.milestone || request.defeated || request.finisher;
		wave.style = request.style;
		wave.color = SelectColor(request);
		if (request.strength == CollisionPresentationRules::Strength::Strong)
		{
			wave.startRadius = 0.9f;
			wave.endRadius = 9.5f;
			wave.thickness = 3.2f;
			wave.totalLifetime = 0.38f;
		}
		else if (request.strength == CollisionPresentationRules::Strength::Medium)
		{
			wave.startRadius = 0.65f;
			wave.endRadius = 6.5f;
			wave.thickness = 2.3f;
			wave.totalLifetime = 0.30f;
		}
		else
		{
			wave.startRadius = 0.45f;
			wave.endRadius = 4.2f;
			wave.thickness = 1.5f;
			wave.totalLifetime = 0.22f;
		}
		if (request.defeated)
		{
			wave.startRadius = 1.1f;
			wave.endRadius = 10.5f;
			wave.thickness = 3.2f;
			wave.totalLifetime = 0.38f;
		}
		wave.endRadius *= m_Tuning.waveRadiusScale *
			(1.0f + 0.08f * static_cast<float>(request.chainTier));
		wave.totalLifetime *= m_Tuning.waveLifetimeScale;
		if (request.milestone)
		{
			wave.endRadius *= 1.18f;
			wave.thickness += 0.8f;
			wave.totalLifetime += 0.08f;
		}
		if (request.radius > 0.0f)
		{
			wave.endRadius = (std::max)(wave.endRadius, request.radius);
			wave.totalLifetime = (std::max)(wave.totalLifetime, 0.48f);
		}
		if (request.enhanced) wave.thickness += 0.8f;
		if (request.finisher)
		{
			wave.endRadius *= 1.12f;
			wave.thickness += 1.2f;
			wave.totalLifetime += 0.16f;
		}
		wave.lifetime = wave.totalLifetime;
		m_Waves[waveSlot] = wave;
		++m_Diagnostics.wavesGenerated;
		defeatVisualAllocated = defeatVisualAllocated || request.defeated;
	}
	else
	{
		++m_Diagnostics.wavesSkipped;
	}

	Vector3 axis = request.collisionAxis;
	axis.y = 0.0f;
	if (axis.LengthSquared() <= kFrameEpsilon) axis = Vector3::UnitX;
	else axis.Normalize();

	const bool shouldBurst =
		request.strength != CollisionPresentationRules::Strength::Weak ||
		request.milestone || request.defeated || request.enhanced ||
		request.finisher;
	if (shouldBurst)
	{
		const int maximumBursts = std::clamp(request.density * 2, 0, 6);
		const int burstSlot = FindBurstSlot(
			maximumBursts,
			request.priority,
			request.milestone || request.defeated);
		if (burstSlot >= 0)
		{
			ImpactBurst burst;
			burst.active = true;
			burst.worldPosition = request.worldPosition + Vector3(0.0f, 0.28f, 0.0f);
			burst.collisionAxis = axis;
			burst.priority = request.priority;
			burst.secondaryCollision = request.secondaryCollision;
			burst.milestone = request.milestone;
			burst.style = request.style;
			burst.color = SelectColor(request);
			burst.accentColor = request.style != Style::Neutral
				? (request.enhanced || request.finisher
					? m_Tuning.peakColor
					: burst.color)
				: request.milestone
				? m_Tuning.chainColor
				: request.defeated ? m_Tuning.peakColor : burst.color;
			if (request.strength == CollisionPresentationRules::Strength::Strong)
			{
				burst.totalLifetime = 0.32f;
				burst.coreRadius = 22.0f;
				burst.rayLength = 80.0f;
				burst.rayCount = 12;
			}
			else
			{
				burst.totalLifetime = 0.22f;
				burst.coreRadius = 13.0f;
				burst.rayLength = 48.0f;
				burst.rayCount = 7;
			}
			if (request.defeated)
			{
				burst.totalLifetime = 0.36f;
				burst.coreRadius = 24.0f;
				burst.rayLength = 78.0f;
				burst.rayCount = 12;
			}
			if (request.milestone)
			{
				burst.totalLifetime += 0.12f;
				burst.coreRadius *= 1.35f;
				burst.rayLength *= 1.30f;
				burst.rayCount += 4;
			}
			if (request.enhanced)
			{
				burst.coreRadius *= 1.18f;
				burst.rayLength *= 1.12f;
				burst.rayCount += 2;
			}
			if (request.finisher)
			{
				burst.totalLifetime += 0.18f;
				burst.coreRadius *= 1.42f;
				burst.rayLength *= 1.35f;
				burst.rayCount += 6;
			}
			burst.coreRadius *= m_Tuning.impactCoreScale;
			burst.rayLength *= m_Tuning.impactRayLengthScale;
			burst.lifetime = burst.totalLifetime;
			m_Bursts[burstSlot] = burst;
			++m_Diagnostics.burstsGenerated;
			defeatVisualAllocated = defeatVisualAllocated || request.defeated;
		}
		else
		{
			++m_Diagnostics.burstsSkipped;
		}
	}

	const int maximumSparks = CollisionPresentationRules::MaximumSparks(
		request.density);
	const int perFrameBudget =
		CollisionPresentationRules::MaximumSparkGenerationPerFrame(
			request.density);
	const int desired = DesiredSparkCount(request);
	const int allowed = (std::max)(
		0,
		(std::min)(desired, perFrameBudget - m_GeneratedThisFrame));
	if (allowed < desired) m_Diagnostics.sparksSkipped += desired - allowed;

	const float axisAngle = std::atan2(axis.z, axis.x);
	const std::uint32_t color = SelectColor(request);
	int sparksSpawned = 0;
	for (int index = 0; index < allowed; ++index)
	{
		const int slot = FindSparkSlot(
			request.priority,
			m_Diagnostics.activeSparks < maximumSparks);
		if (slot < 0)
		{
			m_Diagnostics.sparksSkipped += allowed - index;
			break;
		}
		const float jitter = DeterministicJitter(request.frame, index);
		float angle = DirectX::XM_2PI *
			static_cast<float>(index) / static_cast<float>((std::max)(1, allowed));
		if (request.secondaryCollision)
		{
			const int side = index % 2 == 0 ? 1 : -1;
			const float fan = allowed > 2
				? static_cast<float>(index / 2) /
					static_cast<float>((std::max)(1, allowed / 2 - 1)) - 0.5f
				: 0.0f;
			angle = axisAngle + (side < 0 ? DirectX::XM_PI : 0.0f) +
				fan * 1.25f + jitter * 0.18f;
		}
		else
		{
			angle += jitter * 0.34f;
		}
		float speed = request.strength == CollisionPresentationRules::Strength::Strong
			? 40.0f
			: request.strength == CollisionPresentationRules::Strength::Medium
				? 28.0f
				: 18.0f;
		if (request.defeated) speed = 46.0f;
		speed *= m_Tuning.sparkSpeedScale *
			(1.0f + 0.06f * static_cast<float>(request.chainTier)) *
			(0.88f + 0.24f * (jitter + 0.5f));
		float lifetime = request.strength == CollisionPresentationRules::Strength::Strong
			? 0.34f
			: request.strength == CollisionPresentationRules::Strength::Medium
				? 0.26f
				: 0.18f;
		if (request.defeated) lifetime = 0.38f;
		lifetime = (std::min)(
			0.48f,
			(lifetime + 0.015f * static_cast<float>(request.chainTier)) *
			m_Tuning.sparkLifetimeScale);
		float size = request.strength == CollisionPresentationRules::Strength::Strong
			? 3.4f
			: request.strength == CollisionPresentationRules::Strength::Medium
				? 2.5f
				: 1.8f;
		if (request.defeated) size = 3.8f;
		Spark spark;
		spark.active = true;
		spark.worldPosition = request.worldPosition + Vector3(0.0f, 0.24f, 0.0f);
		spark.velocity = Vector3(std::cos(angle) * speed, 0.0f, std::sin(angle) * speed);
		spark.lifetime = lifetime;
		spark.totalLifetime = lifetime;
		spark.size = size * m_Tuning.sparkSizeScale *
			(1.0f + 0.05f * static_cast<float>(request.chainTier));
		spark.color = color;
		spark.priority = request.priority;
		spark.secondaryCollision = request.secondaryCollision;
		spark.glint = request.secondaryCollision && request.chainTier >= 3 &&
			(request.strength == CollisionPresentationRules::Strength::Strong ||
				request.milestone) && index % 4 == 0;
		if (spark.glint) spark.color = m_Tuning.peakColor;
		m_Sparks[slot] = spark;
		++m_GeneratedThisFrame;
		++m_Diagnostics.sparksGenerated;
		++sparksSpawned;
		RefreshActiveDiagnostics();
	}
	if (request.defeated)
	{
		defeatVisualAllocated = defeatVisualAllocated || sparksSpawned > 0;
		if (defeatVisualAllocated) ++m_Diagnostics.defeatVfxGenerated;
		else ++m_Diagnostics.defeatVfxSkipped;
	}
	RefreshActiveDiagnostics();
}

void CollisionVfxSystem::SpawnTrail(const TrailRequest& request)
{
	Vector3 delta = request.endPosition - request.startPosition;
	delta.y = 0.0f;
	if (delta.LengthSquared() <= kFrameEpsilon || request.lifetime <= 0.0f)
	{
		++m_Diagnostics.trailsSkipped;
		return;
	}
	if (request.sourceKey != 0)
	{
		for (DirectionTrail& existing : m_Trails)
		{
			if (existing.active && existing.sourceKey == request.sourceKey &&
				existing.style == request.style &&
				existing.createdFrame == request.frame)
			{
				existing.endPosition = request.endPosition;
				existing.lifetime = (std::max)(existing.lifetime, request.lifetime);
				existing.totalLifetime = (std::max)(existing.totalLifetime, request.lifetime);
				existing.enhanced = existing.enhanced || request.enhanced;
				existing.finisher = existing.finisher || request.finisher;
				return;
			}
		}
	}
	const int slot = FindTrailSlot(request.priority);
	if (slot < 0)
	{
		++m_Diagnostics.trailsSkipped;
		return;
	}
	DirectionTrail trail;
	trail.active = true;
	trail.startPosition = request.startPosition;
	trail.endPosition = request.endPosition;
	trail.totalLifetime = std::clamp(request.lifetime, 0.08f, 0.72f);
	trail.lifetime = trail.totalLifetime;
	trail.width = std::clamp(request.width, 1.0f, 8.0f);
	trail.priority = request.priority;
	trail.createdFrame = request.frame;
	trail.sourceKey = request.sourceKey;
	trail.style = request.style;
	trail.color = SelectStyleColor(request.style);
	trail.enhanced = request.enhanced;
	trail.finisher = request.finisher;
	m_Trails[slot] = trail;
	++m_Diagnostics.trailsGenerated;
	RefreshActiveDiagnostics();
}

void CollisionVfxSystem::Draw(
	ImDrawList* drawList,
	ProjectWorldToScreen projector,
	bool enabled)
{
	m_Diagnostics.primitiveSubmissionsLastFrame = 0;
	if (!enabled || drawList == nullptr || projector == nullptr) return;
	for (const DirectionTrail& trail : m_Trails)
	{
		if (!trail.active || trail.totalLifetime <= kFrameEpsilon) continue;
		ImVec2 start;
		ImVec2 end;
		if (!projector(trail.startPosition + Vector3(0.0f, 0.30f, 0.0f), start) ||
			!projector(trail.endPosition + Vector3(0.0f, 0.30f, 0.0f), end))
			continue;
		ImVec2 direction(end.x - start.x, end.y - start.y);
		const float length = std::sqrt(
			direction.x * direction.x + direction.y * direction.y);
		if (length <= 1.0f) continue;
		direction.x /= length;
		direction.y /= length;
		const ImVec2 normal(-direction.y, direction.x);
		const float life = Clamp01(trail.lifetime / trail.totalLifetime);
		const float pulse = 0.82f + 0.18f *
			std::sin((1.0f - life) * DirectX::XM_2PI * 2.0f);
		const float width = trail.width *
			(trail.enhanced ? 1.35f : 1.0f) * pulse;
		drawList->AddLine(
			start,
			end,
			WithAlpha(trail.color, life * 0.16f),
			width * (trail.finisher ? 4.8f : 3.2f));
		drawList->AddLine(
			start,
			end,
			WithAlpha(trail.color, life * 0.88f),
			width);
		m_Diagnostics.primitiveSubmissionsLastFrame += 2;
		if (trail.enhanced || trail.finisher)
		{
			drawList->AddLine(
				start,
				end,
				WithAlpha(m_Tuning.peakColor, life * 0.72f),
				(std::max)(1.0f, width * 0.28f));
			++m_Diagnostics.primitiveSubmissionsLastFrame;
		}
		const int markerCount = trail.finisher ? 5 : trail.enhanced ? 4 : 3;
		for (int marker = 1; marker <= markerCount; ++marker)
		{
			const float along = static_cast<float>(marker) /
				static_cast<float>(markerCount + 1);
			const ImVec2 tip(
				start.x + (end.x - start.x) * along,
				start.y + (end.y - start.y) * along);
			const float arrow = trail.style == Style::Anchor ? 7.0f : 5.0f;
			const ImVec2 base(
				tip.x - direction.x * arrow,
				tip.y - direction.y * arrow);
			drawList->AddLine(
				ImVec2(base.x + normal.x * arrow * 0.55f,
					base.y + normal.y * arrow * 0.55f),
				tip,
				WithAlpha(trail.color, life * 0.82f),
				1.7f);
			drawList->AddLine(
				ImVec2(base.x - normal.x * arrow * 0.55f,
					base.y - normal.y * arrow * 0.55f),
				tip,
				WithAlpha(trail.color, life * 0.82f),
				1.7f);
			m_Diagnostics.primitiveSubmissionsLastFrame += 2;
		}
	}
	for (const Shockwave& wave : m_Waves)
	{
		if (!wave.active || wave.totalLifetime <= kFrameEpsilon) continue;
		const float progress = Clamp01(1.0f - wave.lifetime / wave.totalLifetime);
		const float expansion = Clamp01(progress * m_Tuning.waveExpansionSpeed);
		const float eased = 1.0f - std::pow(1.0f - expansion, 2.2f);
		const float radius = wave.startRadius +
			(wave.endRadius - wave.startRadius) * eased;
		const float alpha = std::pow(1.0f - progress, 1.35f);
		DrawWorldRing(
			drawList,
			projector,
			wave.worldPosition,
			radius,
			WithAlpha(wave.color, alpha * 0.92f),
			wave.thickness * (0.72f + 0.28f * (1.0f - progress)));
		++m_Diagnostics.primitiveSubmissionsLastFrame;
		if (wave.doubleRing)
		{
			DrawWorldRing(
				drawList,
				projector,
				wave.worldPosition,
				radius * 0.68f,
				WithAlpha(wave.color, alpha * 0.48f),
				(std::max)(1.0f, wave.thickness - 1.0f));
			++m_Diagnostics.primitiveSubmissionsLastFrame;
		}
	}

	for (const ImpactBurst& burst : m_Bursts)
	{
		if (!burst.active || burst.totalLifetime <= kFrameEpsilon) continue;
		ImVec2 center;
		ImVec2 axisPoint;
		if (!projector(burst.worldPosition, center)) continue;
		if (!projector(
			burst.worldPosition + burst.collisionAxis * 3.0f,
			axisPoint))
		{
			axisPoint = ImVec2(center.x + 1.0f, center.y);
		}
		ImVec2 axis(axisPoint.x - center.x, axisPoint.y - center.y);
		const float axisLength = std::sqrt(axis.x * axis.x + axis.y * axis.y);
		if (axisLength > 0.001f)
		{
			axis.x /= axisLength;
			axis.y /= axisLength;
		}
		else
		{
			axis = ImVec2(1.0f, 0.0f);
		}
		const ImVec2 normal(-axis.y, axis.x);
		const float progress = Clamp01(1.0f - burst.lifetime / burst.totalLifetime);
		const float life = 1.0f - progress;
		const float flash = std::pow(life, 1.7f);
		const float pulse = 0.78f + 0.34f * std::sin(progress * DirectX::XM_PI);
		const float core = burst.coreRadius * pulse;

		for (int index = 0; index < burst.rayCount; ++index)
		{
			const float evenAngle = DirectX::XM_2PI *
				static_cast<float>(index) /
				static_cast<float>((std::max)(1, burst.rayCount));
			const float angle = evenAngle +
				DeterministicJitter(
					static_cast<unsigned long long>(burst.priority + 17),
					index) * 0.34f;
			const float directionX = std::cos(angle);
			const float directionY = std::sin(angle);
			const float length = burst.rayLength *
				(0.68f + 0.52f * std::abs(std::sin(angle * 1.7f))) *
				(0.72f + 0.28f * life);
			const ImVec2 start(
				center.x + directionX * core * 0.34f,
				center.y + directionY * core * 0.34f);
			const ImVec2 end(
				center.x + directionX * length,
				center.y + directionY * length);
			drawList->AddLine(
				start,
				end,
				WithAlpha(burst.accentColor, flash * 0.72f),
				burst.milestone ? 2.2f : 1.45f);
			++m_Diagnostics.primitiveSubmissionsLastFrame;
		}

		drawList->AddCircleFilled(
			center,
			core * 2.25f,
			WithAlpha(burst.color, flash * 0.10f),
			32);
		drawList->AddCircleFilled(
			center,
			core * 1.35f,
			WithAlpha(burst.accentColor, flash * 0.24f),
			24);
		drawList->AddCircleFilled(
			center,
			core * 0.58f,
			WithAlpha(m_Tuning.peakColor, flash * 0.92f),
			20);
		m_Diagnostics.primitiveSubmissionsLastFrame += 3;

		if (burst.secondaryCollision)
		{
			for (int side = -1; side <= 1; side += 2)
			{
				for (int step = 0; step < 2; ++step)
				{
					const float along = 24.0f + static_cast<float>(step) * 15.0f;
					const float direction = static_cast<float>(side);
					const ImVec2 tip(
						center.x + axis.x * along * direction,
						center.y + axis.y * along * direction);
					const ImVec2 base(
						tip.x - axis.x * 9.0f * direction,
						tip.y - axis.y * 9.0f * direction);
					const ImU32 cueColor = WithAlpha(
						m_Tuning.secondaryColor,
						flash * (0.82f - static_cast<float>(step) * 0.18f));
					drawList->AddLine(
						ImVec2(base.x + normal.x * 5.0f, base.y + normal.y * 5.0f),
						tip,
						cueColor,
						2.0f);
					drawList->AddLine(
						ImVec2(base.x - normal.x * 5.0f, base.y - normal.y * 5.0f),
						tip,
						cueColor,
						2.0f);
					m_Diagnostics.primitiveSubmissionsLastFrame += 2;
				}
			}
		}
	}

	for (const Spark& spark : m_Sparks)
	{
		if (!spark.active || spark.totalLifetime <= kFrameEpsilon) continue;
		ImVec2 head;
		Vector3 direction = spark.velocity;
		direction.y = 0.0f;
		if (direction.LengthSquared() <= kFrameEpsilon) continue;
		direction.Normalize();
		const float life = Clamp01(spark.lifetime / spark.totalLifetime);
		const float size = spark.size * (0.30f + 0.70f * life);
		const Vector3 tailWorld = spark.worldPosition -
			direction * (0.8f + size * 0.42f);
		ImVec2 tail;
		if (!projector(spark.worldPosition, head) || !projector(tailWorld, tail))
			continue;
		const ImU32 color = WithAlpha(spark.color, life * life);
		drawList->AddLine(tail, head, color, (std::max)(1.0f, size * 0.58f));
		++m_Diagnostics.primitiveSubmissionsLastFrame;
		if (spark.secondaryCollision)
		{
			ImVec2 delta(head.x - tail.x, head.y - tail.y);
			const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
			if (length > 0.001f)
			{
				delta.x /= length;
				delta.y /= length;
				const ImVec2 normal(-delta.y, delta.x);
				const ImVec2 triangle[3] = {
					head,
					ImVec2(tail.x + normal.x * size, tail.y + normal.y * size),
					ImVec2(tail.x - normal.x * size, tail.y - normal.y * size),
				};
				drawList->AddConvexPolyFilled(triangle, 3, WithAlpha(spark.color, life * 0.58f));
				++m_Diagnostics.primitiveSubmissionsLastFrame;
			}
		}
		else
		{
			drawList->AddCircleFilled(head, (std::max)(1.0f, size * 0.34f), color, 8);
			++m_Diagnostics.primitiveSubmissionsLastFrame;
		}
		if (spark.glint)
		{
			const float ray = 3.5f + size * 1.8f;
			drawList->AddLine(
				ImVec2(head.x - ray, head.y),
				ImVec2(head.x + ray, head.y),
				WithAlpha(spark.color, life),
				1.4f);
			drawList->AddLine(
				ImVec2(head.x, head.y - ray),
				ImVec2(head.x, head.y + ray),
				WithAlpha(spark.color, life),
				1.4f);
			m_Diagnostics.primitiveSubmissionsLastFrame += 2;
		}
	}
	m_Diagnostics.maximumPrimitiveSubmissions = (std::max)(
		m_Diagnostics.maximumPrimitiveSubmissions,
		m_Diagnostics.primitiveSubmissionsLastFrame);
}

void CollisionVfxSystem::DrawDebugTuning()
{
	if (!ImGui::Begin("Collision VFX Tuning"))
	{
		ImGui::End();
		return;
	}
	ImGui::SliderInt("Weak sparks", &m_Tuning.weakSparkCount, 0, 12);
	ImGui::SliderInt("Medium sparks", &m_Tuning.mediumSparkCount, 0, 14);
	ImGui::SliderInt("Strong sparks", &m_Tuning.strongSparkCount, 0, 14);
	ImGui::SliderFloat("Spark size", &m_Tuning.sparkSizeScale, 0.25f, 2.5f);
	ImGui::SliderFloat("Spark speed", &m_Tuning.sparkSpeedScale, 0.25f, 2.5f);
	ImGui::SliderFloat("Spark lifetime", &m_Tuning.sparkLifetimeScale, 0.25f, 2.0f);
	ImGui::SliderFloat("Wave radius", &m_Tuning.waveRadiusScale, 0.25f, 2.5f);
	ImGui::SliderFloat("Wave expansion", &m_Tuning.waveExpansionSpeed, 0.25f, 2.5f);
	ImGui::SliderFloat("Wave lifetime", &m_Tuning.waveLifetimeScale, 0.25f, 2.0f);
	ImGui::SliderFloat("Impact core", &m_Tuning.impactCoreScale, 0.35f, 2.2f);
	ImGui::SliderFloat("Impact rays", &m_Tuning.impactRayLengthScale, 0.35f, 2.2f);
	const struct ColorBinding
	{
		const char* label;
		std::uint32_t* color;
	} colors[] = {
		{ "Direct color", &m_Tuning.directColor },
		{ "Secondary color", &m_Tuning.secondaryColor },
		{ "CHAIN color", &m_Tuning.chainColor },
		{ "Peak color", &m_Tuning.peakColor },
		{ "Defeat color", &m_Tuning.defeatColor },
		{ "Pierce color", &m_Tuning.pierceColor },
		{ "Reflection color", &m_Tuning.reflectionColor },
		{ "Anchor color", &m_Tuning.anchorColor },
	};
	for (const ColorBinding& binding : colors)
	{
		float values[4]{};
		ColorToFloats(*binding.color, values);
		if (ImGui::ColorEdit4(binding.label, values))
			*binding.color = FloatsToColor(values);
	}
	ImGui::Text(
		"active sparks=%d waves=%d bursts=%d trails=%d  max=%d/%d/%d/%d",
		m_Diagnostics.activeSparks,
		m_Diagnostics.activeWaves,
		m_Diagnostics.activeBursts,
		m_Diagnostics.activeTrails,
		m_Diagnostics.maximumActiveSparks,
		m_Diagnostics.maximumActiveWaves,
		m_Diagnostics.maximumActiveBursts,
		m_Diagnostics.maximumActiveTrails);
	ImGui::End();
}

void CollisionVfxSystem::RefreshActiveDiagnostics()
{
	m_Diagnostics.activeSparks = static_cast<int>(std::count_if(
		m_Sparks.begin(),
		m_Sparks.end(),
		[](const Spark& spark) { return spark.active; }));
	m_Diagnostics.activeWaves = static_cast<int>(std::count_if(
		m_Waves.begin(),
		m_Waves.end(),
		[](const Shockwave& wave) { return wave.active; }));
	m_Diagnostics.activeBursts = static_cast<int>(std::count_if(
		m_Bursts.begin(),
		m_Bursts.end(),
		[](const ImpactBurst& burst) { return burst.active; }));
	m_Diagnostics.activeTrails = static_cast<int>(std::count_if(
		m_Trails.begin(),
		m_Trails.end(),
		[](const DirectionTrail& trail) { return trail.active; }));
	m_Diagnostics.maximumActiveSparks = (std::max)(
		m_Diagnostics.maximumActiveSparks,
		m_Diagnostics.activeSparks);
	m_Diagnostics.maximumActiveWaves = (std::max)(
		m_Diagnostics.maximumActiveWaves,
		m_Diagnostics.activeWaves);
	m_Diagnostics.maximumActiveBursts = (std::max)(
		m_Diagnostics.maximumActiveBursts,
		m_Diagnostics.activeBursts);
	m_Diagnostics.maximumActiveTrails = (std::max)(
		m_Diagnostics.maximumActiveTrails,
		m_Diagnostics.activeTrails);
}
