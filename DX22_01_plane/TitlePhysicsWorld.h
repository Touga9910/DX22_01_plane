#pragma once
#include "ContinuousBallStepper.h"
#include "TableFrameCollisionComponent.h"
#include "TableGeometry.h"
#include "GameEvent.h"
#include <array>
#include <functional>

// Disposable scene-local world. Same tick units, CCD, contact and friction rules
// as live gameplay/prediction; deliberately no HP, AI, rewards or run events.
struct TitlePhysicsWorld
{
    using Vector3=DirectX::SimpleMath::Vector3;
    std::array<BallPhysicsRules::Body,16> balls{};
    std::array<bool,16> active{};
    std::array<Vector3,16> start{};
    std::array<DirectX::SimpleMath::Quaternion,16> rotation{};
    std::vector<Collision::Segment> walls=TableFrameCollisionComponent::BuildLocalWalls();
    std::vector<Collision::Sphere> pockets;
    std::function<void(const CollisionImpactEvent&)> impact;
    std::array<std::size_t,16> order{};
    int stopCount=0;
    bool hit=false;
    bool breakFanEnabled=false;
    bool breakFanApplied=false;
    /// <summary>
    /// タイトル演出で使う球の初期状態、処理順、ポケットを設定する。
    /// </summary>
    TitlePhysicsWorld()
    {
        active.fill(true);
        for(std::size_t i=0;i<order.size();++i)order[i]=i;
        rotation.fill(DirectX::SimpleMath::Quaternion::Identity);
        for(const auto& p:TableConfig::GetPocketCenters())pockets.push_back({p,TableConfig::POCKET_RADIUS});
    }
    /// <summary>
    /// タイトルの盤面で管理している球の総数を返す。
    /// </summary>
    std::size_t Count()const{return balls.size();}
    /// <summary>
    /// 処理順の指定位置に対応する球が有効かを返す。
    /// </summary>
    bool IsActive(std::size_t i)const{return active[order[i]];}
    /// <summary>
    /// タイトル演出で物理ステップの継続を許可する。
    /// </summary>
    bool ShouldContinue()const{return true;}
    /// <summary>
    /// 処理順の指定位置に対応する球の物理データを返す。
    /// </summary>
    const auto& PhysicsBody(std::size_t i)const{return balls[order[i]];}
    /// <summary>
    /// タイトルの盤面で使う壁の衝突線分を返す。
    /// </summary>
    const auto& Walls()const{return walls;}
    /// <summary>
    /// タイトルの盤面で使うポケットの当たり判定を返す。
    /// </summary>
    const auto& PocketSpheres()const{return pockets;}
    /// <summary>
    /// サブステップ開始時の球の座標を保存する。
    /// </summary>
    void BeginSubstep(){for(std::size_t i=0;i<Count();++i)start[i]=balls[i].position;}
    /// <summary>
    /// 処理順で指定した球を、その速度と時間刻みに従って移動する。
    /// </summary>
    void Move(std::size_t i,float interval){auto& b=balls[order[i]];b.position+=b.velocity*interval;}
    /// <summary>
    /// 処理順の指定位置を球の番号に変換し、盤面との接触を解決する。
    /// </summary>
    void ResolveEnvironment(std::size_t i){ResolveEnvironmentSlot(order[i]);}
    /// <summary>
    /// 指定した球のポケット進入と壁との衝突を処理する。
    /// </summary>
    void ResolveEnvironmentSlot(std::size_t i)
    {
        if(!active[i])return;
        auto& b=balls[i];
        for(const auto& p:pockets)if(BallPhysicsRules::PocketHit(start[i],b.position,b.status.radius,p))
        {active[i]=false;b.velocity=Vector3::Zero;return;}
        for(const auto& wall:walls)
        {
            const float speed=b.velocity.Length();Vector3 contact;
            if(BallPhysicsRules::Wall(b,wall,Vector3::Zero,&contact)&&impact)
            {
                CollisionImpactEvent e{};e.worldPosition=contact;e.relativeApproachSpeed=speed;
                e.collisionKind=CombatCollisionKind::Wall;impact(e);
            }
        }
    }
    /// <summary>
    /// 球同士の接触で位置と速度を調整し、条件に応じてブレイクの効果を通知する。
    /// </summary>
    void ResolvePair(std::size_t i,std::size_t j)
    {
        i=order[i];j=order[j];
        auto& a=balls[i];auto& b=balls[j];
        const Vector3 first=a.position,second=b.position;
        Vector3 normal=first-second;normal.y=0;
        const float distance=normal.Length();if(distance>.0001f)normal/=distance;
        const float speed=(std::max)(0.0f,-(a.velocity-b.velocity).Dot(normal));
        if(BallPhysicsRules::Pair(a,b))
        {
            hit=true;
            if(breakFanEnabled&&!breakFanApplied&&(i==0||j==0)&&speed>.1f)
            {
                // One title-only break accent. The second-row flank balls have
                // an open outward lane, including a small component toward the cue.
                breakFanApplied=true;
                const float fanSpeed=(std::min)(speed*.32f,2.8f);
                if(active[2])balls[2].velocity=Vector3(-.45f,0,-.893f)*fanSpeed;
                if(active[3])balls[3].velocity=Vector3(-.45f,0,.893f)*fanSpeed;
            }
            if(impact)
            {
                CollisionImpactEvent e{};e.worldPosition=(first+second)*.5f;
                e.firstTargetPosition=first;e.secondTargetPosition=second;e.relativeApproachSpeed=speed;
                e.collisionKind=i==0||j==0?CombatCollisionKind::PlayerEnemy:CombatCollisionKind::EnemyEnemy;
                impact(e);
            }
        }
        ResolveEnvironmentSlot(i);ResolveEnvironmentSlot(j);
    }
    /// <summary>
    /// 球に摩擦を適用して物理を一刻み進め、移動距離に応じた回転を更新する。
    /// </summary>
    ContinuousBallStepper::Result Tick()
    {
        std::array<Vector3,16> before;
        for(std::size_t i=0;i<Count();++i)
        {
            auto& b=balls[i];before[i]=b.position;
            if(!active[i])continue;
            b.velocity.y=0;
            if(i==0){Vector3 acceleration;BallPhysicsRules::PlayerFriction(b.velocity,acceleration,b.status,b.status.friction,stopCount);}
            else BallPhysicsRules::EnemyFriction(b.velocity,b.status.friction);
        }
        std::stable_sort(order.begin(),order.end(),[&](std::size_t i,std::size_t j){
            return BallPhysicsRules::OrderingKey(balls[i],0,0,std::string{}) < BallPhysicsRules::OrderingKey(balls[j],0,0,std::string{});
        });
        const auto result=ContinuousBallStepper::Step(*this);
        for(std::size_t i=0;i<Count();++i)
        {
            if(!active[i])continue;
            auto move=balls[i].position-before[i];move.y=0;const float distance=move.Length();
            if(distance>.0001f)
            {
                auto axis=Vector3::UnitY.Cross(move/distance);axis.Normalize();
                rotation[i]=rotation[i]*DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(axis,distance/balls[i].status.radius);
            }
        }
        return result;
    }
};
