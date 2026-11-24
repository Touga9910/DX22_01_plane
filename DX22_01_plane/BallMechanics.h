#pragma once

#include "BallStatus.h"
#include <cmath>
#include <utility>

// ボール衝突・貫通・方向補正・ポケットダメージで共通利用する計算処理をまとめる
namespace BallMechanics
{
    // 2つのボールが衝突した後の、衝突法線方向の速度を返す
    // 戻り値firstが第1ボール、secondが第2ボールの衝突後速度を表し、接線方向速度は変更しない
    // locked側のボールは速度を維持し、相手側だけを反発させる
    /// <summary>
    /// 衝突軸上の速度、質量、反発係数と固定状態から衝突後の速度を求める。
    /// </summary>
    inline std::pair<float, float> ResolveNormalImpact(
        float first, float second, float firstMass, float secondMass,
        float restitution, float firstTransfer, float secondTransfer,
        bool firstLocked, bool secondLocked)
    {
        if (firstLocked) return { first, -second * restitution };
        if (secondLocked) return { -first * restitution, second };
        const float totalMass = firstMass + secondMass;
        const float relative = first - second;
        return {
            first - (2.0f * secondMass / totalMass) * relative * restitution * secondTransfer,
            second + (2.0f * firstMass / totalMass) * relative * restitution * firstTransfer
        };
    }

    // ボールが使用できる貫通回数を返す
    // 貫通能力を持たない場合は0、chargedの場合は通常回数へ1回加算
    /// <summary>
    /// 貫通能力とチャージ状態から、このショットで使える貫通回数を求める。
    /// </summary>
    inline int PierceUses(const BallStatus& status, bool charged)
    {
        return status.abilities.pierce ? status.pierceMaxUses + (charged ? 1 : 0) : 0;
    }

    // 貫通後に維持する速度倍率を返す
    // chargedの場合は速度を100%維持するため1.0fを返す
    /// <summary>
    /// チャージ状態を考慮して貫通後の速度保持率を返す。
    /// </summary>
    inline float PierceRetention(const BallStatus& status, bool charged)
    {
        return charged ? 1.0f : status.pierceSpeedRetention;
    }

    // facingDotが0.5以上の場合にdamageへ倍率を適用して返す
    // 倍率適用時のダメージは最低1を保証し、条件外では元のdamageを返す
    /// <summary>
    /// 攻撃の方向が条件を満たす場合に正面ダメージ倍率を適用する。
    /// </summary>
    inline int DirectionalDamage(int damage, float facingDot, float multiplier)
    {
        return facingDot >= 0.5f
            ? (std::max)(1, static_cast<int>(std::ceil(damage * multiplier)))
            : damage;
    }

    // 最大HPと割合からポケット時のダメージを計算
    // ratioが0以下の場合は0、正の場合は最低1ダメージを返す
    /// <summary>
    /// 最大HPと割合から、ポケットによる整数ダメージを計算する。
    /// </summary>
    inline int PocketDamage(int maxHp, float ratio)
    {
        return ratio > 0.0f ? (std::max)(1, static_cast<int>(std::ceil(maxHp * ratio))) : 0;
    }
}
