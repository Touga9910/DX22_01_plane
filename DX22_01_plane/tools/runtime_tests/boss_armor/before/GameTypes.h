#pragma once

#include <array>
#include <cstddef>
#include <string>

enum class SceneType
{
	Title,
	Select,
	Battle,
	RestSite,
	Shop,
	Result,
	Max
};

// 通常ルートの進行値と、ラン終盤の特別区間を分離する。
// FinalBossは16番目の通常エリアとして数えない。
enum class RunPhase
{
	NormalRoute,
	BossPreparation,
	FinalBossReady,
	FinalBoss,
	Completed
};

inline const char* ToString(RunPhase phase)
{
	switch (phase)
	{
	case RunPhase::NormalRoute: return "normal_route";
	case RunPhase::BossPreparation: return "boss_preparation";
	case RunPhase::FinalBossReady: return "final_boss_ready";
	case RunPhase::FinalBoss: return "final_boss";
	case RunPhase::Completed: return "completed";
	default: return "normal_route";
	}
}

inline RunPhase RunPhaseFromString(const std::string& value)
{
	if (value == "boss_preparation") return RunPhase::BossPreparation;
	if (value == "final_boss_ready") return RunPhase::FinalBossReady;
	if (value == "final_boss") return RunPhase::FinalBoss;
	if (value == "completed") return RunPhase::Completed;
	return RunPhase::NormalRoute;
}

enum class RelicType
{
	AllBallAttackUp,
	AllBallDefenseUp,
	CollisionAttackUp,
	BankShot,
	EmergencyRepairKit,
	StandardBallScope,
	HeavyBallCore,
	PierceBallCharger,
	BounceBallSpring,
	AnchorBallChain,
	BountyList,
	ExpandedBallOffer,
	Count
};

enum class RelicRarity
{
	Common,
	Rare,
	Legendary
};

inline const char* ToString(RelicRarity rarity)
{
	switch (rarity)
	{
	case RelicRarity::Rare: return "rare";
	case RelicRarity::Legendary: return "legendary";
	default: return "common";
	}
}

struct RelicDefinition
{
	RelicType type;
	const char* name;
	const char* description;
	int price;
	RelicRarity rarity;
	int midBossWeight;
	int shopWeight;
};

inline const char* RelicUtf8(const char8_t* text) noexcept
{
	return reinterpret_cast<const char*>(text);
}

struct BalanceValidationVariant
{
	std::string id;
	bool disableDynamicBalance = true;
};

inline const std::array<
	RelicDefinition,
	static_cast<std::size_t>(RelicType::Count)> RelicCatalog =
{{
	{
		RelicType::AllBallAttackUp,
		RelicUtf8(u8"\u653b\u6483\u30b3\u30a2"),
		RelicUtf8(u8"\u6240\u6301\u3057\u3066\u3044\u308b\u3059\u3079\u3066\u306e\u30dc\u30fc\u30eb\u306e\u653b\u6483\u529b\u304c1\u4e0a\u304c\u308b\u3002"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::AllBallDefenseUp,
		RelicUtf8(u8"\u9632\u5fa1\u30b3\u30a2"),
		RelicUtf8(u8"\u6240\u6301\u3057\u3066\u3044\u308b\u3059\u3079\u3066\u306e\u30dc\u30fc\u30eb\u306e\u9632\u5fa1\u529b\u304c1\u4e0a\u304c\u308b\u3002"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::CollisionAttackUp,
		RelicUtf8(u8"\u885d\u6483\u52a0\u901f\u88c5\u7f6e"),
		RelicUtf8(u8"\u30dc\u30fc\u30eb\u540c\u58eb\u306e\u885d\u7a81\u5f8c\u3001\u6575\u306b\u4e0e\u3048\u308b\u30c0\u30e1\u30fc\u30b8\u304c1\u5897\u3048\u308b\u3002\u52b9\u679c\u306f\u30b7\u30e7\u30c3\u30c8\u3054\u3068\u306b\u30ea\u30bb\u30c3\u30c8\u3055\u308c\u308b\u3002"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::BankShot,
		RelicUtf8(u8"\u30d0\u30f3\u30af\u30b7\u30e7\u30c3\u30c8"),
		RelicUtf8(u8"\u58c1\u306b\u5f53\u305f\u3063\u305f\u5f8c\u3001\u6700\u521d\u306b\u6575\u3078\u76f4\u63a5\u5f53\u3066\u305f\u653b\u6483\u306e\u30c0\u30e1\u30fc\u30b8\u304c2\u500d\u306b\u306a\u308b\u3002"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::EmergencyRepairKit,
		RelicUtf8(u8"\u7dca\u6025\u4fee\u7406\u30ad\u30c3\u30c8"),
		RelicUtf8(u8"1\u56de\u306e\u30b7\u30e7\u30c3\u30c8\u3067\u30dc\u30fc\u30eb\u540c\u58eb\u304c3\u56de\u4ee5\u4e0a\u63a5\u89e6\u3059\u308b\u3068\u3001\u30b7\u30e7\u30c3\u30c8\u7d42\u4e86\u5f8c\u306bHP\u30921\u56de\u5fa9\u3059\u308b\u3002"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::StandardBallScope,
		RelicUtf8(u8"精密照準器"),
		RelicUtf8(u8"スタンダードボールが威力4以下で壁に当たらず敵に命中すると、ダメージ+1。"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::HeavyBallCore,
		RelicUtf8(u8"ヘビーインパクター"),
		RelicUtf8(u8"ヘビーボールが敵に与える衝突ダメージ+1。"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::PierceBallCharger,
		RelicUtf8(u8"貫通過給機"),
		RelicUtf8(u8"貫通ボールの貫通回数が1回から2回になり、貫通時に減速しなくなる。"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::BounceBallSpring,
		RelicUtf8(u8"高張力スプリング"),
		RelicUtf8(u8"バウンドボールは壁に当たるたびに次の命中ダメージ+1。最大+3。"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::AnchorBallChain,
		RelicUtf8(u8"アンカーチェーン"),
		RelicUtf8(u8"停止したアンカーボールに敵が衝突したとき、与えるダメージ+1。"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::BountyList,
		RelicUtf8(u8"賞金首リスト"),
		RelicUtf8(u8"各バトルで最初に倒した敵1体からMoneyを5追加で得る。"),
		20, RelicRarity::Common, 1, 1
	},
	{
		RelicType::ExpandedBallOffer,
		RelicUtf8(u8"拡張ボールラック"),
		RelicUtf8(u8"ショット前に提示されるボールの選択肢が3つから4つに増える。"),
		20, RelicRarity::Common, 1, 1
	}
}};

enum class DamageBallCollisionType
{
	PlayerEnemy,
	EnemyEnemy
};

enum class GameState
{
	AimingDirection,
	AimingPower,
	ConfirmShot,
	BallsMoving,
	EnemyAttack,
	TurnEnd,
	ClearReward,
	GameOver,
};
