#pragma once

#include "Collision.h"
#include <algorithm>
#include <cmath>
#include <limits>

// ボールの連続衝突判定（CCD）で使用する幾何計算をまとめる
// 時刻は1/60秒の物理tickを1.0とした割合で表し、位置・速度はゲーム内単位をそのまま使用
namespace BallCcdGeometry
{
    using DirectX::SimpleMath::Vector3;
    inline constexpr float ContactSlop = 0.0001f;      // 接触判定の丸め誤差を吸収する許容幅
    inline constexpr float ReleaseSlop = 0.001f;       // 接触状態を解除したとみなすための余白
    inline constexpr float ApproachEpsilon = 0.00001f; // 接近中か判定する際の微小しきい値
    inline constexpr double NoHit = (std::numeric_limits<double>::infinity)(); // 指定区間内に衝突しないことを表す特殊値

    // Vector3の全成分が有限値であればtrueを返す
    inline bool Finite(const Vector3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    // 2つのVector3の内積をdouble精度で計算
    inline double Dot(const Vector3& a, const Vector3& b)
    {
        return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
    }

    // 相対位置offset・相対速度velocityから、半径radiusの球面へ最初に接触する時刻を返す
    // remaining以内に接触しない場合はNoHitを返す。すでに食い込んでいる場合やtrigger指定時は0.0を返せる
    inline double SphereTime(const Vector3& offset, const Vector3& velocity, double radius,
        double remaining, bool trigger = false)
    {
        const double lengthSq = Dot(offset, offset);
        const double speedSq = Dot(velocity, velocity);
        const double approach = Dot(offset, velocity);
        const double c = lengthSq - radius * radius;
        const double outer = radius + ContactSlop;
        const double inner = (std::max)(0.0, radius - ContactSlop);
        if (lengthSq <= outer * outer)
        {
            if (trigger || lengthSq < inner * inner || approach < -ApproachEpsilon * radius) return 0.0;
            return NoHit; // 接触中でも離反・接線移動ならt=0衝突を繰り返さない
        }
        if (speedSq <= 1.0e-20 || approach >= 0.0) return NoHit;
        const double discriminant = approach * approach - speedSq * c;
        if (discriminant < 0.0) return NoHit;
        // 二次方程式の減算による桁落ちを避ける形で最初の接触時刻を計算
        const double time = c / (-approach + std::sqrt(discriminant));
        return time >= 0.0 && time <= remaining ? time : NoHit;
    }

    // XZ平面上でpointから線分segmentへ最も近い点を返す
    // projectionが指定されている場合は、線分始点を0・終点を1とした投影係数も格納
    inline Vector3 ClosestXZ(const Vector3& point, const Collision::Segment& segment, float* projection = nullptr)
    {
        Vector3 direction = segment.end - segment.start;
        direction.y = 0;
        Vector3 offset = point - segment.start;
        offset.y = 0;
        const double denominator = Dot(direction, direction);
        const float t = denominator > 1.0e-20 ? static_cast<float>(Dot(offset, direction) / denominator) : 0.0f;
        if (projection) *projection = t;
        Vector3 closest = segment.start + direction * std::clamp(t, 0.0f, 1.0f);
        closest.y = point.y;
        return closest;
    }

    // 壁線分に対してinterior側を向くXZ平面上の法線を返す
    // 長さを持たない線分の場合はUnitXを返す
    inline Vector3 InwardNormal(const Collision::Segment& wall, const Vector3& interior)
    {
        Vector3 direction = wall.end - wall.start;
        direction.y = 0;
        if (direction.LengthSquared() <= 1.0e-12f) return Vector3::UnitX;
        direction.Normalize();
        Vector3 normal(-direction.z, 0, direction.x);
        if ((interior - wall.start).Dot(normal) < 0) normal = -normal;
        return normal;
    }

    // 移動する球と壁線分がremaining以内に最初に接触する時刻を返す
    // すでに食い込んでいる、または接触状態で壁へ接近している場合は0を返す
    // 衝突しない場合はNoHitを返す
    inline double WallTime(const Vector3& position, Vector3 velocity, float radius,
        const Collision::Segment& wall, double remaining)
    {
        velocity.y = 0;
        const Vector3 closest = ClosestXZ(position, wall);
        const Vector3 offset = position - closest;
        const double distance = std::sqrt(Dot(offset, offset));
        if (distance < radius - ContactSlop) return 0;
        if (distance <= radius + ContactSlop && Dot(offset, velocity) < -ApproachEpsilon * radius) return 0;

        Vector3 first = position - wall.start, second = position - wall.end;
        first.y = second.y = 0;
        double earliest = (std::min)(SphereTime(first, velocity, radius, remaining),
            SphereTime(second, velocity, radius, remaining));
        Vector3 direction = wall.end - wall.start;
        direction.y = 0;
        const double length = std::sqrt(Dot(direction, direction));
        if (length <= 1.0e-12) return earliest; // 長さ0の線分は端点1個として判定
        direction /= static_cast<float>(length);
        const Vector3 normal(-direction.z, 0, direction.x);
        const double distanceToLine = Dot(first, normal);
        const double normalSpeed = Dot(velocity, normal);
        if (std::abs(normalSpeed) <= 1.0e-12) return earliest;
        for (const double side : { -1.0, 1.0 })
        {
            if (normalSpeed * side >= -ApproachEpsilon) continue;
            const double time = (side * radius - distanceToLine) / normalSpeed;
            if (time < 0 || time > remaining) continue;
            const double along = Dot(first, direction) + Dot(velocity, direction) * time;
            if (along >= 0 && along <= length) earliest = (std::min)(earliest, time);
        }
        return earliest;
    }

    // ボール中心とポケット中心の連続判定から、ポケットへ接触する時刻を返す
    // 判定半径にはボール半径とポケット半径の合計を使用
    inline double PocketTime(Vector3 position, Vector3 velocity, float radius,
        const Collision::Sphere& pocket, double remaining)
    {
        position -= pocket.center;
        position.y = velocity.y = 0;
        return SphereTime(position, velocity, static_cast<double>(radius) + pocket.radius, remaining, true);
    }
}
