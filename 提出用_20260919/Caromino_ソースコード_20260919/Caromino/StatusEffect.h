#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

// Display- and ball-type-independent core for active stat effects.
// Enemies own this per instance. A future player-wide buff can instead be
// owned by the player/run and applied to every active player ball.
enum class StatusEffectType
{
	AttackUp,
	AttackDown,
	DefenseUp,
	DefenseDown,
	Count,
};

inline constexpr std::array<StatusEffectType, 4> AllStatusEffectTypes = {
	StatusEffectType::AttackUp,
	StatusEffectType::AttackDown,
	StatusEffectType::DefenseUp,
	StatusEffectType::DefenseDown,
};

inline constexpr const char* ToString(StatusEffectType type)
{
	switch (type)
	{
	case StatusEffectType::AttackUp: return "attack_up";
	case StatusEffectType::AttackDown: return "attack_down";
	case StatusEffectType::DefenseUp: return "defense_up";
	case StatusEffectType::DefenseDown: return "defense_down";
	default: return "unknown";
	}
}

inline bool TryParseStatusEffectType(std::string_view value, StatusEffectType& result)
{
	for (const StatusEffectType type : AllStatusEffectTypes)
	{
		if (value == ToString(type))
		{
			result = type;
			return true;
		}
	}
	return false;
}

inline constexpr bool IsAttackStatusEffect(StatusEffectType type)
{
	return type == StatusEffectType::AttackUp || type == StatusEffectType::AttackDown;
}

inline constexpr bool IsPositiveStatusEffect(StatusEffectType type)
{
	return type == StatusEffectType::AttackUp || type == StatusEffectType::DefenseUp;
}

class StatusEffectCollection
{
public:
	static constexpr int MaxMagnitude = 999;

	void Set(StatusEffectType type, int magnitude)
	{
		const std::size_t index = static_cast<std::size_t>(type);
		if (index >= m_Magnitudes.size()) return;
		m_Magnitudes[index] = std::clamp(magnitude, 0, MaxMagnitude);
	}

	void Remove(StatusEffectType type) { Set(type, 0); }
	void Clear() { m_Magnitudes.fill(0); }

	int GetMagnitude(StatusEffectType type) const
	{
		const std::size_t index = static_cast<std::size_t>(type);
		return index < m_Magnitudes.size() ? m_Magnitudes[index] : 0;
	}

	bool Has(StatusEffectType type) const { return GetMagnitude(type) > 0; }

	int GetAttackModifier() const
	{
		return GetMagnitude(StatusEffectType::AttackUp) -
			GetMagnitude(StatusEffectType::AttackDown);
	}

	int GetDefenseModifier() const
	{
		return GetMagnitude(StatusEffectType::DefenseUp) -
			GetMagnitude(StatusEffectType::DefenseDown);
	}

	int GetActiveCount() const
	{
		return static_cast<int>(std::count_if(
			m_Magnitudes.begin(), m_Magnitudes.end(),
			[](int magnitude) { return magnitude > 0; }));
	}

	bool Empty() const { return GetActiveCount() == 0; }

private:
	std::array<int, static_cast<std::size_t>(StatusEffectType::Count)> m_Magnitudes{};
};
