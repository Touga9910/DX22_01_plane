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
    TitlePhysicsWorld()
    {
        active.fill(true);
        for(std::size_t i=0;i<order.size();++i)order[i]=i;
        rotation.fill(DirectX::SimpleMath::Quaternion::Identity);
        for(const auto& p:TableConfig::GetPocketCenters())pockets.push_back({p,TableConfig::POCKET_RADIUS});
    }
    std::size_t Count()const{return balls.size();}
    bool IsActive(std::size_t i)const{return active[order[i]];}
    bool ShouldContinue()const{return true;}
    const auto& PhysicsBody(std::size_t i)const{return balls[order[i]];}
    const auto& Walls()const{return walls;}
    const auto& PocketSpheres()const{return pockets;}
    void BeginSubstep(){for(std::size_t i=0;i<Count();++i)start[i]=balls[i].position;}
    void Move(std::size_t i,float interval){auto& b=balls[order[i]];b.position+=b.velocity*interval;}
    void ResolveEnvironment(std::size_t i){ResolveEnvironmentSlot(order[i]);}
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
