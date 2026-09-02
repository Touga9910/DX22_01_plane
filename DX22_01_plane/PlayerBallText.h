#pragma once

#include "PlayerBallData.h"

#include <string>

namespace PlayerBallText
{
	inline const char* Utf8(const char8_t* text) noexcept
	{
		return reinterpret_cast<const char*>(text);
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
		if (definitionId == "player_standard")
		{
			return Utf8(u8"\u6271\u3044\u3084\u3059\u304f\u3001\u653b\u6483\u3068\u9632\u5fa1\u306e\u30d0\u30e9\u30f3\u30b9\u304c\u3088\u3044\u57fa\u672c\u306e\u30dc\u30fc\u30eb\u3002");
		}
		if (definitionId == "player_heavy")
		{
			return Utf8(u8"\u91cd\u3055\u3068\u653b\u6483\u529b\u306b\u512a\u308c\u3001\u5f37\u3044\u885d\u7a81\u3067\u5927\u30c0\u30e1\u30fc\u30b8\u3092\u72d9\u3048\u308b\u3002");
		}
		if (definitionId == "player_pierce")
		{
			return Utf8(u8"\u6575\u3092\u8cab\u901a\u3057\u3001\u4e00\u76f4\u7dda\u306b\u8907\u6570\u306e\u6575\u3078\u30c0\u30e1\u30fc\u30b8\u3092\u4e0e\u3048\u3084\u3059\u3044\u3002");
		}
		if (definitionId == "player_bounce")
		{
			return Utf8(u8"\u53cd\u767a\u529b\u304c\u9ad8\u304f\u3001\u58c1\u3092\u4f7f\u3063\u305f\u9023\u7d9a\u30d2\u30c3\u30c8\u3084\u30d0\u30f3\u30af\u30b7\u30e7\u30c3\u30c8\u5411\u304d\u3002");
		}
		if (definitionId == "player_anchor")
		{
			return Utf8(u8"\u53cd\u767a\u3092\u6291\u3048\u3066\u6b62\u307e\u308a\u3084\u3059\u304f\u3001\u72d9\u3063\u305f\u4f4d\u7f6e\u3092\u7dad\u6301\u3057\u3084\u3059\u3044\u3002");
		}
		return Utf8(u8"\u3053\u306e\u30dc\u30fc\u30eb\u306e\u8aac\u660e\u306f\u307e\u3060\u767b\u9332\u3055\u308c\u3066\u3044\u307e\u305b\u3093\u3002");
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
}
