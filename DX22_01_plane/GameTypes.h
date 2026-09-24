#pragma once

#include <array>
#include <cstddef>
#include <string>

// ゲーム内で使用するシーン種別を表す。
// SceneManagerなどで遷移先や現在のシーンを識別するために使用





enum class SceneType
{
	Title,		// タイトル画面
	Select,		// ステージ・進行先などの選択画面
	Battle,		// 戦闘画面
	RestSite,	// 休憩所
	Shop,		// ショップ
	Result,		// ラン終了後のリザルト画面
	Max			// SceneTypeの要素数を表す終端値
};

// ラン全体の進行段階を表す。
// 通常ルートの進行値と、ラン終盤の特別区間を分離して管理
// FinalBossは16番目の通常エリアとして数えない。
enum class RunPhase
{
	NormalRoute,		// 通常ルートを進行している状態
	BossPreparation,	// 最終ボス前の準備区間
	FinalBossReady,		// 最終ボスへ進入可能な状態
	FinalBoss,			// 最終ボス戦を進行している状態
	Completed			// 最終ボスを終え、ランが完了した状態
};

// RunPhaseをセーブデータなどで扱う文字列へ変換
// 未定義の値が渡された場合は"normal_route"を返す。
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

// 文字列からRunPhaseを復元
// 対応する文字列がない場合はNormalRouteを返す。
inline RunPhase RunPhaseFromString(const std::string& value)
{
	if (value == "boss_preparation") return RunPhase::BossPreparation;
	if (value == "final_boss_ready") return RunPhase::FinalBossReady;
	if (value == "final_boss") return RunPhase::FinalBoss;
	if (value == "completed") return RunPhase::Completed;
	return RunPhase::NormalRoute;
}

// ラン中に取得できるレリックの種類を表す。
enum class RelicType
{
	AllBallAttackUp,		// 所持するすべてのボールの攻撃力を上げる
	CollisionAttackUp,		// ボール同士の衝突後に敵へ与えるダメージを増やす
	BankShot,				// 壁反射後の最初の直接攻撃を強化する
	EmergencyRepairKit,		// 1ショット中の一定回数以上のボール接触でHPを回復する
	StandardBallScope,		// 条件を満たしたスタンダードボールの命中ダメージを増やす
	HeavyBallCore,			// ヘビーボールの敵への衝突ダメージを増やす
	PierceBallCharger,		// 貫通ボールの貫通回数を増やし、貫通時の減速をなくす
	BounceBallSpring,		// バウンドボールの壁接触回数に応じて次の命中ダメージを増やす
	AnchorBallChain,		// 停止中のアンカーボールへ敵が衝突した際のダメージを増やす
	BountyList,				// 各バトルで最初に倒した敵から追加Moneyを得る
	ExpandedBallOffer,		// ショット前に提示されるボールの選択肢を増やす
	Count					// RelicTypeの要素数。レリック定義配列のサイズとして使用する
};

// レリックのレア度を表す。
enum class RelicRarity
{
	Common,		// 通常レア度
	Rare,		// レア
	Legendary	// 最高レア度
};

// RelicRarityを保存・表示用の文字列へ変換
// Commonおよび未定義の値は"common"を返す。
inline const char* ToString(RelicRarity rarity)
{
	switch (rarity)
	{
	case RelicRarity::Rare: return "rare";
	case RelicRarity::Legendary: return "legendary";
	default: return "common";
	}
}

// レリック1種類分の定義情報を保持
struct RelicDefinition
{
	RelicType type;				// レリックの種類
	const char* name;			// プレイヤーへ表示するレリック名
	const char* description;	// プレイヤーへ表示する効果説明
	int price;					// ショップでの購入価格
	RelicRarity rarity;			// レリックのレア度
	int midBossWeight;			// 中ボス報酬の抽選で使用する重み
	int shopWeight;				// ショップの抽選で使用する重み
};

// char8_tで記述したUTF-8文字列を、既存のconst char*として扱うために変換
inline const char* RelicUtf8(const char8_t* text) noexcept
{
	return reinterpret_cast<const char*>(text);
}

// バランス検証時に使用する検証条件の識別情報を保持
struct BalanceValidationVariant
{
	std::string id;	// 検証条件を識別するID
};

// RelicTypeごとの表示情報、価格、レア度、抽選重みをまとめた定義一覧。
// RelicType::Countを配列サイズとして使用し、列挙値を対応する定義の参照に利用
inline const std::array<
	RelicDefinition,
	static_cast<std::size_t>(RelicType::Count)> RelicCatalog =
{ {
	{
		RelicType::AllBallAttackUp,
		RelicUtf8(u8"\u653b\u6483\u30b3\u30a2"),
		RelicUtf8(u8"\u6240\u6301\u3057\u3066\u3044\u308b\u3059\u3079\u3066\u306e\u30dc\u30fc\u30eb\u306e\u653b\u6483\u529b\u304c1\u4e0a\u304c\u308b\u3002"),
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
} };

// ダメージ計算対象となるボール同士の衝突種別を表す。
enum class DamageBallCollisionType
{
	PlayerEnemy,	// プレイヤーボールと敵ボールの衝突
	EnemyEnemy		// 敵ボール同士の衝突
};
