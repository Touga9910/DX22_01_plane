#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

// 全ボールを同じ時間幅ずつ移動させてから接触判定を行う同期型ステッパー
// ゲーム固有の移動・接触処理はAdapter側へ委譲
// 時間は秒ではなく、従来の1/60秒物理tickを1.0とした割合で扱う
namespace SynchronizedBallStepper
{
    inline constexpr int MaxSubsteps = 256; // 1tick内で許可する分割更新回数の上限
    // 1tick分のステップ処理結果。
    struct Result
    {
        int substeps = 0;               // 実行した分割更新回数
        double advancedFraction = 0.0; // 実際に進められたtick割合
        bool limitReached = false;     // 不正値または分割上限により最後まで処理できなかったか
    };

    // world内の全有効ボールを安全な距離幅で同期移動し、環境・ボール同士の接触を順に解決
    // ボールが存在しない場合は初期値のResultを返す
    template<class Adapter>
    Result Step(Adapter& world)
    {
        Result result;
        if (world.Count() == 0) return result;
        double remaining = 1.0;
        while (remaining > 1.0e-9 && result.substeps < MaxSubsteps)
        {
            // 重量衝突などで速度が変化するため、接触解決後は毎回最大速度を再計算
            double maxSpeed = 0.0;
            double minRadius = (std::numeric_limits<double>::max)();
            for (std::size_t i = 0; i < world.Count(); ++i)
            {
                if (!world.IsActive(i)) continue;
                const double speed = world.Velocity(i).Length();
                const double radius = world.Radius(i);
                if (!std::isfinite(speed) || !std::isfinite(radius) || radius <= 0.0)
                {
                    result.limitReached = true;
                    return result;
                }
                maxSpeed = (std::max)(maxSpeed, speed);
                minRadius = (std::min)(minRadius, radius);
            }
            // 2倍の最大速度を、任意の2球間で起こりうる相対速度の上限として扱う
            // 不正な移動状態では未検証のまま1tick全体を進めない
            if (!std::isfinite(maxSpeed) || minRadius <= 0.0)
            {
                result.limitReached = true;
                break;
            }
            const double interval = maxSpeed > 1.0e-8
                ? (std::min)(remaining, minRadius * 0.5 / (2.0 * maxSpeed))
                : remaining;
            if (!(interval > 0.0) || !std::isfinite(interval))
            {
                result.limitReached = true;
                break;
            }
            world.BeginSubstep();
            for (std::size_t i = 0; i < world.Count(); ++i)
                if (world.IsActive(i)) world.Move(i, static_cast<float>(interval));
            // すべての有効ボールを同一時刻まで移動し終えるまで接触判定を行わない
            for (std::size_t i = 0; i < world.Count(); ++i)
                if (world.IsActive(i)) world.ResolveEnvironment(i);
            for (std::size_t i = 0; i < world.Count(); ++i)
            {
                for (std::size_t j = i + 1; j < world.Count(); ++j)
                {
                    if (!world.IsActive(i)) break;
                    if (world.IsActive(j)) world.ResolvePair(i, j);
                }
            }
            remaining -= interval;
            result.advancedFraction += interval;
            ++result.substeps;
            if (!world.ShouldContinue()) break;
        }
        result.limitReached = result.limitReached ||
            (result.substeps == MaxSubsteps && remaining > 1.0e-9);
        return result;
    }
}
