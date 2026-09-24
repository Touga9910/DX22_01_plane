#pragma once

#include "GameTypes.h"
#include <algorithm>

// 1ショット中だけ必要なレリック効果の状態と判定をまとめる
// 予測処理はこの構造体を複製して使用し、報酬やラン全体のログはGame側で管理
struct ShotRelicRules
{
    std::array<bool, static_cast<std::size_t>(RelicType::Count)> relics{};	    // 現在所持しているレリックの有無
    std::string ballId;														    // このショットで使用しているボール定義ID
    int collisionBonus = 0, playerEnemyContacts = 0, enemyEnemyContacts = 0;	// 衝突系レリックの加算値と接触回数
    int wallContacts = 0, bounceBonus = 0;									    // 壁接触回数とバウンドボール用の蓄積ダメージ
    bool bankReady = false, bankConsumed = false, anchorStopped = false;		// バンクショット準備・消費状態とアンカー停止状態
    float launchPower = 0.0f;											    	// ショット開始時の発射威力

    bool Has(RelicType type) const { return relics[static_cast<std::size_t>(type)]; }	// 指定したレリックを所持していればtrueを返す

    // 壁接触を記録し、バウンド強化とバンクショットの準備状態を更新
    void Wall()
    {
        ++wallContacts;
        if (Has(RelicType::BounceBallSpring) && ballId == "player_bounce")
            bounceBonus = (std::min)(3, bounceBonus + 1);
        if (Has(RelicType::BankShot) && !bankConsumed) bankReady = true;
    }

    // バンクショットによるダメージ倍率を取得して消費
    // 発動条件を満たす場合は2、満たさない場合は1を返す
    int ConsumeBankShotDamageMultiplier()
    {
        if (!Has(RelicType::BankShot) || !bankReady || bankConsumed) return 1;
        bankReady = false;
        bankConsumed = true;
        return 2;
    }

    // プレイヤーボールが敵へ命中した際のレリック追加ダメージ合計を返す
    // BounceBallSpringの蓄積値は取得時に0へ戻す
    int ConsumePlayerEnemyRelicDamageBonus()
    {
        int bonus = 0;
        if (Has(RelicType::StandardBallScope) && ballId == "player_standard" &&
            launchPower <= 4.0001f && wallContacts == 0) ++bonus;
        if (Has(RelicType::HeavyBallCore) && ballId == "player_heavy") ++bonus;
        if (Has(RelicType::BounceBallSpring) && ballId == "player_bounce")
        {
            bonus += bounceBonus;
            bounceBonus = 0;
        }
        if (Has(RelicType::AnchorBallChain) && ballId == "player_anchor" && anchorStopped) ++bonus;
        return bonus;
    }

    void Anchor() { if (ballId == "player_anchor") anchorStopped = true; }	// アンカーボールが停止状態へ入ったことを記録

    // ボール同士の接触を記録
    // playerEnemyがtrueならプレイヤー対敵、falseなら敵同士の接触として数える
    void Contact(bool playerEnemy)
    {
        if (playerEnemy) ++playerEnemyContacts;
        else ++enemyEnemyContacts;
        if (Has(RelicType::CollisionAttackUp)) ++collisionBonus;
    }

    int GetCurrentShotCollisionAttackBonus() const { return collisionBonus; }	// 現在のショットで蓄積した衝突追加ダメージを返す
};
