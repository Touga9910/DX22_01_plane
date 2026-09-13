#pragma once

#include "PlayerBallData.h"

#include <string>
#include <array>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace PlayerBallText
{
	inline const char* Utf8(const char8_t* text) noexcept
	{
		return reinterpret_cast<const char*>(text);
	}

	inline std::array<float, 3> GetColor(const std::string& id)
	{
		if (id == "player_heavy") return { 0.85f, 0.45f, 0.16f };
		if (id == "player_pierce") return { 0.64f, 0.38f, 0.96f };
		if (id == "player_bounce" || id == "player_cushion_charge") return { 0.98f, 0.81f, 0.18f };
		if (id == "player_anchor") return { 0.20f, 0.78f, 0.64f };
		return { 0.25f, 0.63f, 1.0f };
	}

	inline std::array<float, 3> GetColor(const PlayerBallData& ball)
	{
		switch (ball.category)
		{
		case BallCategory::Heavy: return { 0.85f, 0.45f, 0.16f };
		case BallCategory::Pierce: return { 0.64f, 0.38f, 0.96f };
		case BallCategory::Bounce: return { 0.98f, 0.81f, 0.18f };
		case BallCategory::Anchor: return { 0.20f, 0.78f, 0.64f };
		default: return { 0.25f, 0.63f, 1.0f };
		}
	}

	inline const char* GetName(const std::string& definitionId)
	{
		if (definitionId == "player_standard")
		{
			return Utf8(u8"\u30b9\u30bf\u30f3\u30c0\u30fc\u30c9\u7403");
		}
		if (definitionId == "player_heavy")
		{
			return Utf8(u8"\u30d8\u30d3\u30fc\u7403");
		}
		if (definitionId == "player_pierce")
		{
			return Utf8(u8"\u8cab\u901a\u7403");
		}
		if (definitionId == "player_bounce")
		{
			return Utf8(u8"\u30d0\u30a6\u30f3\u30c9\u7403");
		}
		if (definitionId == "player_anchor")
		{
			return Utf8(u8"\u30a2\u30f3\u30ab\u30fc\u7403");
		}
		if (definitionId == "player_cushion_charge")
		{
			return Utf8(u8"\u30af\u30c3\u30b7\u30e7\u30f3\u84c4\u7a4d\u7403");
		}
		if (definitionId == "player_chain_impact") return Utf8(u8"\u9023\u9396\u885d\u6483\u7403");
		if (definitionId == "player_refractive_pierce") return Utf8(u8"\u5c48\u6298\u8cab\u901a\u7403");
		if (definitionId == "player_stop_shield") return Utf8(u8"\u505c\u6b62\u6642\u30b7\u30fc\u30eb\u30c9\u7403");
		return Utf8(u8"\u540d\u79f0\u672a\u8a2d\u5b9a\u306e\u30dc\u30fc\u30eb");
	}

	inline const char* GetDescription(const std::string& definitionId)
	{
		if (definitionId == "player_standard") return Utf8(u8"\u6271\u3044\u3084\u3059\u3044\u57fa\u672c\u306e\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u653b\u6483\u529b\u3068\u9632\u5fa1\u529b\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_heavy") return Utf8(u8"\u91cd\u3055\u3067\u6575\u3092\u62bc\u3057\u51fa\u3059\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u8cea\u91cf\u3068\u6575\u3078\u306e\u30ce\u30c3\u30af\u30d0\u30c3\u30af\u4f1d\u9054\u7387\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_pierce") return Utf8(u8"\u6575\u3092\u8cab\u901a\u3059\u308b\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u8cab\u901a\u56de\u6570\u304c\u5897\u3048\u3001\u8cab\u901a\u6642\u306e\u901f\u5ea6\u30ed\u30b9\u304c\u6e1b\u308b\u3002");
		if (definitionId == "player_bounce") return Utf8(u8"\u58c1\u53cd\u5c04\u304c\u5f97\u610f\u306a\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u53cd\u767a\u529b\u304c\u4e0a\u304c\u308a\u3001\u6469\u64e6\u306b\u3088\u308b\u6e1b\u901f\u304c\u6e1b\u308b\u3002");
		if (definitionId == "player_anchor") return Utf8(u8"\u6575\u3078\u529b\u3092\u4f1d\u3048\u305f\u5f8c\u3001\u305d\u306e\u5834\u3067\u505c\u6b62\u3002\u5f37\u5316\u3067\u6e1b\u901f\u304c\u5f37\u307e\u308a\u3001\u505c\u6b62\u4e2d\u306f\u62bc\u3057\u623b\u3055\u308c\u306a\u304f\u306a\u308b\u3002");
		if (definitionId == "player_cushion_charge") return Utf8(u8"\u89e6\u308c\u305f\u30af\u30c3\u30b7\u30e7\u30f3\u533a\u753b\u3092\u84c4\u7a4d\u72b6\u614b\u306b\u3059\u308b\u30dc\u30fc\u30eb\u3002\u5f8c\u7d9a\u306e\u5473\u65b9\u30dc\u30fc\u30eb\u304c\u89e6\u308c\u308b\u3068\u84c4\u7a4d\u3092\u6d88\u8cbb\u3057\u3066\u52a0\u901f\u3057\u3001\u5f37\u5316\u3067\u52a0\u901f\u500d\u7387\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_chain_impact") return Utf8(u8"\u76f4\u63a5\u547d\u4e2d\u3057\u305f\u6575\u306e\u5468\u56f2\u306b\u653b\u6483\u3092\u9023\u9396\u3055\u305b\u308b\u91cd\u91cf\u7403\u3002\u5f37\u5316\u3067\u9023\u9396\u7bc4\u56f2\u3068\u62bc\u3057\u51fa\u3059\u529b\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_refractive_pierce") return Utf8(u8"\u6575\u3092\u52d5\u304b\u3055\u305a\u8cab\u901a\u3057\u3001\u885d\u7a81\u9762\u3067\u81ea\u7403\u3060\u3051\u304c\u5c48\u6298\u3059\u308b\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u8cab\u901a\u56de\u6570\u3068\u901f\u5ea6\u7dad\u6301\u7387\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_stop_shield") return Utf8(u8"\u65e9\u304f\u505c\u6b62\u3057\u3001\u6b21\u306e\u6575\u653b\u6483\u4e2d\u3060\u3051\u30c0\u30e1\u30fc\u30b8\u3092\u5438\u53ce\u3059\u308b\u30b7\u30fc\u30eb\u30c9\u3092\u5f35\u308b\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u5438\u53ce\u91cf\u304c\u4e0a\u304c\u308b\u3002");
		return Utf8(u8"\u30dc\u30fc\u30eb\u306e\u8aac\u660e\u306f\u672a\u767b\u9332\u3067\u3059\u3002");
	}

	inline const char* GetTrait(const PlayerBallData& ball)
	{
		switch (ball.category)
		{
		case BallCategory::Heavy: return Utf8(u8"\u91cd\u91cf");
		case BallCategory::Pierce: return Utf8(u8"\u8cab\u901a");
		case BallCategory::Bounce: return Utf8(u8"\u53cd\u767a");
		case BallCategory::Anchor: return Utf8(u8"\u30a2\u30f3\u30ab\u30fc");
		default: return Utf8(u8"\u6c4e\u7528");
		}
	}
	inline std::string GetStats(const PlayerBallData& ball, const BallStatus& status)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(2);
		if (ball.definitionId == "player_cushion_charge")
			text << Utf8(u8"\u84c4\u7a4d\u30af\u30c3\u30b7\u30e7\u30f3\u52a0\u901f ") << static_cast<int>(std::lround(status.cushionChargeSpeedMultiplier * 100.0f)) << "%";
		else if (ball.definitionId == "player_chain_impact")
			text << Utf8(u8"\u9023\u9396\u7bc4\u56f2 ") << status.chainImpactRadius << Utf8(u8" / \u8cea\u91cf ") << status.mass << Utf8(u8" / \u62bc\u3057\u51fa\u3059\u529b ") << status.knockbackTransfer << Utf8(u8"\u500d");
		else if (ball.definitionId == "player_refractive_pierce")
			text << Utf8(u8"\u5c48\u6298\u8cab\u901a ") << status.pierceMaxUses << Utf8(u8"\u56de / \u901f\u5ea6\u7dad\u6301 ") << static_cast<int>(std::lround(status.pierceSpeedRetention * 100.0f)) << "%";
		else if (ball.definitionId == "player_stop_shield")
			text << Utf8(u8"\u505c\u6b62\u6642\u30b7\u30fc\u30eb\u30c9 ") << status.stopShieldAmount << Utf8(u8" / \u6469\u64e6 ") << std::setprecision(3) << status.friction;
		else if (ball.category == BallCategory::Heavy)
			text << Utf8(u8"\u8cea\u91cf ") << status.mass << Utf8(u8" / \u62bc\u3057\u51fa\u3059\u529b ") << status.knockbackTransfer << Utf8(u8"\u500d");
		else if (ball.category == BallCategory::Pierce)
			text << Utf8(u8"\u8cab\u901a ") << status.pierceMaxUses << Utf8(u8"\u56de / \u901f\u5ea6\u7dad\u6301 ") << static_cast<int>(std::lround(status.pierceSpeedRetention * 100.0f)) << "%";
		else if (ball.category == BallCategory::Bounce)
			text << Utf8(u8"\u58c1\u53cd\u5c04\u306e\u901f\u5ea6\u7dad\u6301 ") << static_cast<int>(std::lround(status.restitution * 100.0f)) << Utf8(u8"% / \u6469\u64e6 ") << std::setprecision(3) << status.friction;
		else if (ball.category == BallCategory::Anchor)
			text << Utf8(u8"\u6e1b\u901f ") << status.anchorBrakeMultiplier << Utf8(u8"\u500d / \u505c\u6b62\u5224\u5b9a\u901f\u5ea6 ") << std::sqrt(status.anchorStopSpeedSquared) << Utf8(u8" / \u505c\u6b62\u4e2d\u306e\u62bc\u3057\u623b\u3057 ") << (status.anchorKnockbackImmune ? Utf8(u8"\u7121\u52b9") : Utf8(u8"\u3042\u308a"));
		else
			text << Utf8(u8"\u653b\u6483 ") << status.attack << Utf8(u8" / \u9632\u5fa1 ") << status.defense;
		return text.str();
	}

	inline std::string GetUpgradePreview(const PlayerBallData& ball)
	{
		if (!ball.CanUpgrade()) return Utf8(u8"\u6700\u5927\u5f37\u5316");
		return std::string(Utf8(u8"\u73fe\u5728\uff1a")) + GetStats(ball, ball.status) + "\n" +
			Utf8(u8"\u5f37\u5316\u5f8c\uff1a") + GetStats(ball, ball.upgradeTable[ball.upgradeLevel]);
	}

}
