#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "BallStatus.h"

enum class BallCategory
{
	Standard,
	Heavy,
	Pierce,
	Bounce,
	Anchor,
};

inline const char* BallCategoryId(BallCategory category)
{
	switch (category)
	{
	case BallCategory::Heavy: return "heavy";
	case BallCategory::Pierce: return "pierce";
	case BallCategory::Bounce: return "bounce";
	case BallCategory::Anchor: return "anchor";
	default: return "standard";
	}
}

inline bool IsBallCategoryId(const std::string& categoryId)
{
	return categoryId == "standard" || categoryId == "heavy" ||
		categoryId == "pierce" || categoryId == "bounce" ||
		categoryId == "anchor";
}

inline BallCategory BallCategoryFromId(
	const std::string& categoryId,
	const std::string& definitionId = {})
{
	if (categoryId == "heavy") return BallCategory::Heavy;
	if (categoryId == "pierce") return BallCategory::Pierce;
	if (categoryId == "bounce") return BallCategory::Bounce;
	if (categoryId == "anchor") return BallCategory::Anchor;
	// categoryのない旧データは既存IDから安全に移行する。
	if (definitionId == "player_heavy") return BallCategory::Heavy;
	if (definitionId == "player_chain_impact") return BallCategory::Heavy;
	if (definitionId == "player_pierce" || definitionId == "player_refractive_pierce" ||
		definitionId == "player_trace_driver" || definitionId == "player_pierce_finisher")
		return BallCategory::Pierce;
	if (definitionId == "player_bounce" || definitionId == "player_cushion_charge" ||
		definitionId == "player_ricochet_finisher")
		return BallCategory::Bounce;
	if (definitionId == "player_anchor" || definitionId == "player_stop_shield" ||
		definitionId == "player_anchor_finisher")
		return BallCategory::Anchor;
	return BallCategory::Standard;
}

// 各段階のボール性能を丸ごと保持する。
using BallUpgradeStep = BallStatus;

// プレイヤーのデッキに含まれる、ボール1個分の実行時データ。
struct PlayerBallData
{
	static constexpr int MaxUpgradeLevel = 2;

	std::string definitionId = "player_default";
	BallCategory category = BallCategory::Standard;
	std::uint64_t instanceId = 0;
	BallStatus status{};
	std::array<BallUpgradeStep, MaxUpgradeLevel> upgradeTable{};
	int upgradeLevel = 0;

	bool CanUpgrade() const
	{
		return upgradeLevel >= 0 && upgradeLevel < MaxUpgradeLevel;
	}
};
