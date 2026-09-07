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
		if (id == "player_bounce") return { 0.98f, 0.81f, 0.18f };
		if (id == "player_anchor") return { 0.20f, 0.78f, 0.64f };
		return { 0.25f, 0.63f, 1.0f };
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
		return Utf8(u8"\u540d\u79f0\u672a\u8a2d\u5b9a\u306e\u30dc\u30fc\u30eb");
	}

	inline const char* GetDescription(const std::string& definitionId)
	{
		if (definitionId == "player_standard") return Utf8(u8"\u6271\u3044\u3084\u3059\u3044\u57fa\u672c\u306e\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u653b\u6483\u529b\u3068\u9632\u5fa1\u529b\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_heavy") return Utf8(u8"\u91cd\u3055\u3067\u6575\u3092\u62bc\u3057\u51fa\u3059\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u8cea\u91cf\u3068\u6575\u3078\u306e\u30ce\u30c3\u30af\u30d0\u30c3\u30af\u4f1d\u9054\u7387\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_pierce") return Utf8(u8"\u6575\u3092\u8cab\u901a\u3059\u308b\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u8cab\u901a\u56de\u6570\u304c\u5897\u3048\u3001\u8cab\u901a\u6642\u306e\u901f\u5ea6\u30ed\u30b9\u304c\u6e1b\u308b\u3002");
		if (definitionId == "player_bounce") return Utf8(u8"\u58c1\u53cd\u5c04\u304c\u5f97\u610f\u306a\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u53cd\u767a\u529b\u304c\u4e0a\u304c\u308a\u3001\u6469\u64e6\u306b\u3088\u308b\u6e1b\u901f\u304c\u6e1b\u308b\u3002");
		if (definitionId == "player_anchor") return Utf8(u8"\u6575\u3078\u529b\u3092\u4f1d\u3048\u305f\u5f8c\u3001\u305d\u306e\u5834\u3067\u505c\u6b62\u3002\u5f37\u5316\u3067\u6e1b\u901f\u304c\u5f37\u307e\u308a\u3001\u505c\u6b62\u4e2d\u306f\u62bc\u3057\u623b\u3055\u308c\u306a\u304f\u306a\u308b\u3002");
		return Utf8(u8"\u30dc\u30fc\u30eb\u306e\u8aac\u660e\u306f\u672a\u767b\u9332\u3067\u3059\u3002");
	}

	inline const char* GetTrait(const PlayerBallData& ball)
	{
		if (ball.status.abilities.pierce)
		{
			return Utf8(u8"\u6575\u3092\u8cab\u901a");
		}
		if (ball.status.abilities.anchor)
		{
			return Utf8(u8"\u505c\u6b62\u3057\u3084\u3059\u3044");
		}
		if (ball.definitionId == "player_bounce")
		{
			return Utf8(u8"\u9ad8\u53cd\u767a");
		}
		if (ball.definitionId == "player_heavy")
		{
			return Utf8(u8"\u91cd\u91cf");
		}
		return Utf8(u8"\u30d0\u30e9\u30f3\u30b9\u578b");
	}
	inline std::string GetStats(const PlayerBallData& ball, const BallStatus& status)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(2);
		if (ball.definitionId == "player_heavy")
			text << Utf8(u8"\u8cea\u91cf ") << status.mass << Utf8(u8" / \u62bc\u3057\u51fa\u3059\u529b ") << status.knockbackTransfer << Utf8(u8"\u500d");
		else if (ball.definitionId == "player_pierce")
			text << Utf8(u8"\u8cab\u901a ") << status.pierceMaxUses << Utf8(u8"\u56de / \u901f\u5ea6\u7dad\u6301 ") << static_cast<int>(std::lround(status.pierceSpeedRetention * 100.0f)) << "%";
		else if (ball.definitionId == "player_bounce")
			text << Utf8(u8"\u58c1\u53cd\u5c04\u306e\u901f\u5ea6\u7dad\u6301 ") << static_cast<int>(std::lround(status.restitution * 100.0f)) << Utf8(u8"% / \u6469\u64e6 ") << std::setprecision(3) << status.friction;
		else if (ball.definitionId == "player_anchor")
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
