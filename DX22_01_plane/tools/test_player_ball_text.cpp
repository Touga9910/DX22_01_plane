#include "../PlayerBallDataLoader.h"
#include "../PlayerBallText.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

namespace
{
	/// <summary>
	/// 球の定義一覧から指定IDのデータを探し、見つからなければテストを失敗させる。
	/// </summary>
	const PlayerBallData& FindBall(
		const std::vector<PlayerBallData>& definitions,
		const std::string& definitionId)
	{
		const auto found = std::find_if(
			definitions.begin(),
			definitions.end(),
			[&definitionId](const PlayerBallData& ball)
			{
				return ball.definitionId == definitionId;
			});
		assert(found != definitions.end());
		return *found;
	}

	/// <summary>
	/// 球の定義をコピーし、指定の強化段階に対応する状態を設定する。
	/// </summary>
	PlayerBallData AtLevel(const PlayerBallData& definition, int level)
	{
		PlayerBallData ball = definition;
		ball.upgradeLevel = level;
		ball.status = level == 0
			? definition.status
			: definition.upgradeTable[static_cast<std::size_t>(level - 1)];
		return ball;
	}

	/// <summary>
	/// 指定球と強化段階の短い説明に、期待するUTF-8文字列が含まれることを確認する。
	/// </summary>
	void ExpectContains(
		const std::vector<PlayerBallData>& definitions,
		const char* definitionId,
		int level,
		const char8_t* expected)
	{
		const std::string description = PlayerBallText::GetShortDescription(
			AtLevel(FindBall(definitions, definitionId), level));
		assert(description.find(PlayerBallText::Utf8(expected)) != std::string::npos);
	}

	/// <summary>
	/// 説明文の各文末が句点で終わり、次の文が改行で区切られることを確認する。
	/// </summary>
	void ExpectSentenceBreaks(const std::string& description)
	{
		const std::string period = PlayerBallText::Utf8(u8"。");
		assert(!description.empty());
		assert(description.ends_with(period));
		std::size_t offset = 0;
		while (true)
		{
			const std::size_t periodOffset = description.find(period, offset);
			if (periodOffset == std::string::npos) break;
			const std::size_t next = periodOffset + period.size();
			assert(next == description.size() || description[next] == '\n');
			offset = next;
		}
	}
}

/// <summary>
/// 定義データを読み込み、各球や強化段階の日本語説明を検証する。
/// </summary>
int main()
{
	const PlayerBallDataLoadResult loaded = PlayerBallDataLoader::Load(
		"assets/data/player_status.json",
		{},
		{});
	assert(loaded.ballDefinitions.size() == 18);

	for (const PlayerBallData& definition : loaded.ballDefinitions)
	{
		for (int level = 0; level <= PlayerBallData::MaxUpgradeLevel; ++level)
		{
			ExpectSentenceBreaks(PlayerBallText::GetShortDescription(
				AtLevel(definition, level)));
		}
	}

	ExpectContains(loaded.ballDefinitions, "player_collision_boost", 0, u8"×1.25");
	ExpectContains(loaded.ballDefinitions, "player_collision_boost", 1, u8"×1.50");
	ExpectContains(loaded.ballDefinitions, "player_collision_boost", 2, u8"×1.75");
	ExpectContains(loaded.ballDefinitions, "player_reflection_setup", 0, u8"1生成");
	ExpectContains(loaded.ballDefinitions, "player_reflection_setup", 1, u8"2生成");
	ExpectContains(loaded.ballDefinitions, "player_reflection_setup", 2, u8"3生成");
	ExpectContains(loaded.ballDefinitions, "player_stop_shield", 0, u8"1個につき1シールド");
	ExpectContains(loaded.ballDefinitions, "player_stop_shield", 1, u8"1個につき2シールド");
	ExpectContains(loaded.ballDefinitions, "player_stop_shield", 2, u8"1個につき3シールド");
	ExpectContains(loaded.ballDefinitions, "player_ricochet_finisher", 0, u8"衝突3回");
	ExpectContains(loaded.ballDefinitions, "player_ricochet_finisher", 1, u8"衝突2回");
	ExpectContains(loaded.ballDefinitions, "player_ricochet_finisher", 2, u8"速度98%");
	ExpectContains(loaded.ballDefinitions, "player_pierce_finisher", 0, u8"+1/+2/+3");
	ExpectContains(loaded.ballDefinitions, "player_pierce_finisher", 1, u8"+2/+3/+4");
	ExpectContains(loaded.ballDefinitions, "player_pierce_finisher", 2, u8"+2/+4/+6");
	ExpectContains(loaded.ballDefinitions, "player_anchor_finisher", 0, u8"5以上消費");
	ExpectContains(loaded.ballDefinitions, "player_anchor_finisher", 1, u8"5以上消費");
	ExpectContains(loaded.ballDefinitions, "player_anchor_finisher", 2, u8"4以上消費");

	std::cout << "player ball text tests passed\n";
	return 0;
}
