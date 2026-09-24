#pragma once

#include <algorithm>

// ボールが持つ基本的な特殊能力の有無をまとめる
struct BallAbilities
{
	bool split = false;                 // 分裂能力を持つか
	bool pierce = false;                // 敵を貫通する能力を持つか
	bool anchor = false;                // アンカー特性を持つか
	bool refractAfterPierce = false;    // 貫通後に進行方向を変化させる能力を持つか
};

// ボール1個分の戦闘・物理性能と、カテゴリ固有シナジーの調整値を保持
// HPはこの構造体では管理せず、プレイヤーはラン状態、敵は敵データ側で管理
struct BallStatus
{
	int attack = 1;                     // 基本攻撃力
	// 敵データ互換用の防御力。プレイヤーデータの読み込み・保存では常に0として扱う
	int defense = 0;
	float mass = 1.0f;                  // 衝突計算に使用する質量
	float radius = 0.0f;                // 衝突判定に使用するボール半径
	float restitution = 0.8f;           // 衝突・壁反射で使用する反発係数
	float friction = 0.02f;             // 移動中の減速に使用する摩擦値
	// -------------------------
	// 基本カテゴリ固有性能
	// -------------------------

	float knockbackTransfer = 1.0f;     // 衝突時に相手へ伝える押し出し倍率
	int pierceMaxUses = 1;              // 1ショット中に使用できる基本貫通回数
	float pierceSpeedRetention = 0.75f; // 貫通後に維持する速度割合
	float anchorBrakeMultiplier = 1.0f; // アンカーボールの減速倍率
	float anchorStopSpeedSquared = 0.03f; // アンカー停止判定に使用する速度の二乗しきい値
	bool anchorKnockbackImmune = false; // 停止中などに外部からの押し戻しを無効化するか
	// -------------------------
	// カテゴリ内シナジー調整値
	// -------------------------

	// クッションスタックを強利用した際に反射後速度へ掛ける倍率
	// 1.0はクッション加速能力を持たない通常状態を表す
	float cushionChargeSpeedMultiplier = 1.0f;

	// 既存5カテゴリ内の派生ボール用効果。0の項目は対応効果を無効として扱う
	int stopShieldAmount = 0;           // 停止時に付与するシールド量
	float chainImpactRadius = 0.0f;     // 連鎖衝撃で周囲の敵へ影響する半径

	// 重量カテゴリ
	// 0を設定した項目は旧ボール・旧セーブに対して追加効果を発生させない
	float heavyFinisherDamagePerCollision = 0.0f; // 消費した重量カウント1個あたりの追加ダメージ
	int heavyCollisionConsumeAmount = 0;       // フィニッシャーで消費する重量カウント数(0は全消費)

	// 貫通カテゴリ
	int traceDurability = 0;						// 生成する貫通痕の耐久値(0では痕を生成しない)
	float traceUseAngleTolerance = 12.0f;			// 痕利用時に許容する進行方向との角度差(度)
	float traceUseDistance = 8.0f;					// 痕に沿って進み、利用成立とする累積距離
	float traceWidth = 2.0f;						// 痕に沿っていると判定する線からの許容距離
	float tracePierceSpeedMultiplier = 1.1f;		// 貫通カテゴリが痕を強利用した際の速度倍率
	int tracePierceAttackBonus = 1;					// 貫通カテゴリが痕を強利用した際の追加攻撃力
	int tracePierceMaxUsesBonus = 0;				// 最初の強利用時に追加する貫通可能回数
	float tracePierceSpeedRetentionBonus = 0.0f;	// 最初の強利用時に加算する貫通後速度維持率
	float traceNonPierceSpeedMultiplier = 1.0f;		// 非貫通カテゴリが痕を利用した際の速度倍率
	int pierceFinisherBaseBonus = 0;				// 貫通フィニッシャーが痕利用後に得る基本追加ダメージ
	int pierceFinisherMultiTargetBonus = 0;			// 複数の敵を貫通した際、2体目以降1体ごとの追加ダメージ

	// 反発カテゴリ
	int cushionStackGenerateAmount = 0;				// 対象壁区画へ新しく生成するクッションスタック数
	int cushionMaxStack = 3;						// 1区画に保持できるクッションスタック上限
	int cushionStackConsumeAmount = 1;				// 強利用1回で消費するクッションスタック数
	int cushionBounceAttackBonus = 0;				// 反発カテゴリがスタックを強利用した際の追加ダメージ
	float cushionNonBounceSpeedMultiplier = 1.0f;	// 非反発カテゴリがスタックを利用した際の速度倍率
	int ricochetFinisherBonusPerUse = 0;			// 反発フィニッシャーが強利用1回ごとに得る追加ダメージ

	// アンカーカテゴリ
	int anchorPlayerStackGenerate = 0;				// プレイヤー側へ生成する錨スタック数
	int anchorEnemyStackGenerate = 0;				// 敵側へ生成する錨スタック数
	float anchorStackRadius = 0.0f;					// 錨スタック生成・効果判定で使用する範囲
	int anchorStackMax = 99;						// 錨スタックの保持上限
	int anchorFinisherStackConsume = 0;				// アンカーフィニッシャーで消費するスタック数。0は全消費
	int anchorFinisherDamagePerStack = 0;			// 消費した錨スタック1個あたりの追加ダメージ
	int anchorFinisherAoeThreshold = 0;				// 範囲攻撃を発生させるために必要な消費スタック数
	float anchorFinisherAoeRadius = 0.0f;			// アンカーフィニッシャー範囲攻撃の半径

	BallAbilities abilities;						// このボールが持つ基本特殊能力
};

// BallStatusの各物理・シナジー調整値をゲーム内で許容する範囲へ補正して返す
// 引数は値渡しのため、呼び出し元のBallStatus自体は直接変更しない
inline BallStatus NormalizeBallStatus(BallStatus status)
{
	status.mass = (std::max)(0.0001f, status.mass);
	status.radius = (std::max)(0.0f, status.radius);
	status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);
	status.friction = (std::max)(0.0f, status.friction);
	status.knockbackTransfer = std::clamp(status.knockbackTransfer, 0.0f, 3.0f);
	status.pierceMaxUses = std::clamp(status.pierceMaxUses, 0, 16);
	status.pierceSpeedRetention = std::clamp(status.pierceSpeedRetention, 0.0f, 1.0f);
	status.anchorBrakeMultiplier = std::clamp(status.anchorBrakeMultiplier, 1.0f, 5.0f);
	status.anchorStopSpeedSquared = std::clamp(status.anchorStopSpeedSquared, 0.03f, 1.0f);
	status.cushionChargeSpeedMultiplier =
		std::clamp(status.cushionChargeSpeedMultiplier, 1.0f, 3.0f);
	status.stopShieldAmount = std::clamp(status.stopShieldAmount, 0, 100);
	status.chainImpactRadius = std::clamp(status.chainImpactRadius, 0.0f, 100.0f);
	status.heavyFinisherDamagePerCollision = std::clamp(
		status.heavyFinisherDamagePerCollision, 0.0f, 100.0f);
	status.heavyCollisionConsumeAmount = std::clamp(status.heavyCollisionConsumeAmount, 0, 9999);
	status.traceDurability = std::clamp(status.traceDurability, 0, 99);
	status.traceUseAngleTolerance = std::clamp(status.traceUseAngleTolerance, 1.0f, 89.0f);
	status.traceUseDistance = std::clamp(status.traceUseDistance, 0.1f, 200.0f);
	status.traceWidth = std::clamp(status.traceWidth, 0.1f, 50.0f);
	status.tracePierceSpeedMultiplier = std::clamp(
		status.tracePierceSpeedMultiplier, 1.0f, 3.0f);
	status.tracePierceAttackBonus = std::clamp(status.tracePierceAttackBonus, 0, 1000);
	status.tracePierceMaxUsesBonus = std::clamp(status.tracePierceMaxUsesBonus, 0, 16);
	status.tracePierceSpeedRetentionBonus = std::clamp(
		status.tracePierceSpeedRetentionBonus, 0.0f, 1.0f);
	status.traceNonPierceSpeedMultiplier = std::clamp(
		status.traceNonPierceSpeedMultiplier, 1.0f, 1.25f);
	status.pierceFinisherBaseBonus = std::clamp(status.pierceFinisherBaseBonus, 0, 1000);
	status.pierceFinisherMultiTargetBonus = std::clamp(
		status.pierceFinisherMultiTargetBonus, 0, 1000);
	status.cushionStackGenerateAmount = std::clamp(status.cushionStackGenerateAmount, 0, 99);
	status.cushionMaxStack = std::clamp(status.cushionMaxStack, 1, 99);
	status.cushionStackConsumeAmount = std::clamp(status.cushionStackConsumeAmount, 1, 99);
	status.cushionBounceAttackBonus = std::clamp(status.cushionBounceAttackBonus, 0, 1000);
	status.cushionNonBounceSpeedMultiplier = std::clamp(
		status.cushionNonBounceSpeedMultiplier, 1.0f, 1.25f);
	status.ricochetFinisherBonusPerUse = std::clamp(
		status.ricochetFinisherBonusPerUse, 0, 1000);
	status.anchorPlayerStackGenerate = std::clamp(status.anchorPlayerStackGenerate, 0, 99);
	status.anchorEnemyStackGenerate = std::clamp(status.anchorEnemyStackGenerate, 0, 99);
	status.anchorStackRadius = std::clamp(status.anchorStackRadius, 0.0f, 100.0f);
	status.anchorStackMax = std::clamp(status.anchorStackMax, 1, 9999);
	status.anchorFinisherStackConsume = std::clamp(status.anchorFinisherStackConsume, 0, 9999);
	status.anchorFinisherDamagePerStack = std::clamp(status.anchorFinisherDamagePerStack, 0, 1000);
	status.anchorFinisherAoeThreshold = std::clamp(status.anchorFinisherAoeThreshold, 0, 9999);
	status.anchorFinisherAoeRadius = std::clamp(status.anchorFinisherAoeRadius, 0.0f, 100.0f);
	return status;
}
