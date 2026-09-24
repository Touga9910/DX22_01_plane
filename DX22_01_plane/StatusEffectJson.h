#pragma once

#include "StatusEffect.h"
#include "json/json.hpp"

// JSON配列から状態効果一覧を読み込む
// 配列でない場合は空のStatusEffectCollectionを返し、不正な要素は読み飛ばす
inline StatusEffectCollection ReadStatusEffects(const nlohmann::json& value)
{
	StatusEffectCollection effects;
	if (!value.is_array()) return effects;

	for (const auto& entry : value)
	{
		if (!entry.is_object() || !entry.contains("type") ||
			!entry["type"].is_string() || !entry.contains("magnitude") ||
			!entry["magnitude"].is_number_integer())
		{
			continue;
		}

		StatusEffectType type{};
		if (!TryParseStatusEffectType(entry["type"].get<std::string>(), type))
		{
			continue;
		}
		effects.Set(type, entry["magnitude"].get<int>());
	}
	return effects;
}

// 状態効果一覧をJSON配列へ変換
// 効果量が0の状態効果は保存対象に含めない
inline nlohmann::json WriteStatusEffects(const StatusEffectCollection& effects)
{
	nlohmann::json result = nlohmann::json::array();
	for (const StatusEffectType type : AllStatusEffectTypes)
	{
		const int magnitude = effects.GetMagnitude(type);
		if (magnitude > 0)
		{
			result.push_back({ { "type", ToString(type) }, { "magnitude", magnitude } });
		}
	}
	return result;
}
