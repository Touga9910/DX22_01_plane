#pragma once

#include "BallPhysicsRules.h"
#include "SynchronizedBallStepper.h"
#include <vector>

// 全ボール・壁・ポケットの中から最も早い衝突時刻を求め、その時刻まで一括で進めるCCDステッパー
// 接触解決後の速度で次の最短時刻を再計算
// Bodyが保持する接触継続状態と、このTOI反復回数は別の概念として管理
namespace ContinuousBallStepper
{
    using Result = SynchronizedBallStepper::Result;
    inline constexpr int MaxIterations = 256; // 1tick内で許可する衝突時刻反復回数の上限

    // 位置・速度が有限で、半径と質量が正の有効な物理Bodyか判定
    inline bool Valid(const BallPhysicsRules::Body& body)
    {
        return BallCcdGeometry::Finite(body.position) && BallCcdGeometry::Finite(body.velocity) &&
            std::isfinite(body.status.radius) && body.status.radius > 0 &&
            std::isfinite(body.status.mass) && body.status.mass > 0;
    }

    // 現在時刻で発生している環境接触とボール同士の接触を解決
    // 同時刻ではポケットを含む環境処理を先に行い、その後でボール同士を処理
    template<class Adapter>
    void ResolveContacts(Adapter& world)
    {
        // 同一時刻ではポケット処理を壁・ボール同士の処理より優先
        for (std::size_t i = 0; i < world.Count(); ++i)
            if (world.IsActive(i)) world.ResolveEnvironment(i);
        for (std::size_t i = 0; i < world.Count(); ++i)
            for (std::size_t j = i + 1; j < world.Count(); ++j)
            {
                if (!world.IsActive(i)) break;
                if (world.IsActive(j)) world.ResolvePair(i, j);
            }
    }

    // 1tick内の最短衝突時刻を反復探索し、未衝突区間を飛び越えずにworldを進める
    // 不正なBody・壁・ポケットを検出した場合はlimitReachedをtrueにして終了
    template<class Adapter>
    Result Step(Adapter& world)
    {
        Result result;
        if (world.Count() == 0) return result;
        for (std::size_t i = 0; i < world.Count(); ++i)
            if (world.IsActive(i) && !Valid(world.PhysicsBody(i))) { result.limitReached = true; return result; }
        for (const auto& wall : world.Walls())
            if (!BallCcdGeometry::Finite(wall.start) || !BallCcdGeometry::Finite(wall.end))
            {
                result.limitReached = true; return result;
            }
        for (const auto& pocket : world.PocketSpheres())
            if (!BallCcdGeometry::Finite(pocket.center) || !std::isfinite(pocket.radius) || pocket.radius < 0)
            {
                result.limitReached = true; return result;
            }
        // 開始時点ですでに重なっている物体を、時間を進めずに解消
        // 開始時点のポケット重なりもここで処理するが、ダメージ発生には接近方向の相対運動が必要
        world.BeginSubstep();
        ResolveContacts(world);
        if (!world.ShouldContinue()) return result;

        double remaining = 1.0;
        std::vector<BallPhysicsRules::Body> bodies;
        std::vector<bool> active;
        while (remaining > 1.0e-9 && result.substeps < MaxIterations)
        {
            // TOI反復間で各Bodyの接触管理vectorの容量を再利用
            bodies.resize(world.Count());
            active.resize(world.Count());
            for (std::size_t i = 0; i < world.Count(); ++i)
            {
                bodies[i] = world.PhysicsBody(i);
                active[i] = world.IsActive(i);
                const auto& b = bodies[i];
                if (active[i] && !Valid(b))
                {
                    result.limitReached = true;
                    return result;
                }
            }
            double earliest = remaining;
            for (std::size_t i = 0; i < bodies.size(); ++i)
            {
                if (!active[i]) continue;
                const auto& a = bodies[i];
                if (!a.boss) for (const auto& pocket : world.PocketSpheres())
                    earliest = (std::min)(earliest, BallCcdGeometry::PocketTime(a.position, a.velocity,
                        a.status.radius, pocket, remaining));
                if (a.boss) for (const auto& wall : BallPhysicsRules::BossWalls())
                    earliest = (std::min)(earliest, BallCcdGeometry::WallTime(a.position, a.velocity,
                        a.status.radius, wall, remaining));
                for (const auto& wall : world.Walls())
                    earliest = (std::min)(earliest, BallCcdGeometry::WallTime(a.position, a.velocity,
                        a.status.radius, wall, remaining));
                for (std::size_t j = i + 1; j < bodies.size(); ++j)
                {
                    if (!active[j]) continue;
                    const auto& b = bodies[j];
                    if (BallPhysicsRules::IgnoresPiercedPair(a, b)) continue;
                    earliest = (std::min)(earliest, BallCcdGeometry::SphereTime(a.position - b.position,
                        a.velocity - b.velocity, static_cast<double>(a.status.radius) + b.status.radius, remaining));
                }
            }
            world.BeginSubstep();
            for (std::size_t i = 0; i < world.Count(); ++i)
                if (world.IsActive(i)) world.Move(i, static_cast<float>(earliest));
            ResolveContacts(world);
            remaining -= earliest;
            result.advancedFraction += earliest;
            ++result.substeps;
            if (!world.ShouldContinue()) return result;
        }
        // 接触群が反復上限まで収束しない場合、未検証区間を強制移動して形状をすり抜けさせない
        // 未処理時間が残ったことをlimitReachedで通知し、実行時計測や予測の未完了判定に利用
        result.limitReached = remaining > 1.0e-9;
        return result;
    }
}
