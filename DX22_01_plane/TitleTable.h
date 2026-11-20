#pragma once
#include "Component.h"
#include "BallPhysicsRules.h"
#include <array>
#include <memory>
class GameObject;
class TitleTable final:public Component
{
public:
    ~TitleTable() override;
    void Awake() override;
    void Release();
    void Draw() override;
    void Step(float dt,bool released);
    DirectX::SimpleMath::Vector2 ScreenBall(int index) const;
    bool RackHit() const{return m_Hit;}
    DirectX::SimpleMath::Vector2 PreviousScreenBall(int i)const{return m_Previous[i];}
    float Age()const{return m_Age;}
private:
    std::unique_ptr<GameObject> m_Table,m_Frame;
    std::array<std::unique_ptr<GameObject>,16> m_Visual;
    std::array<BallPhysicsRules::Body,16> m_Balls;
    mutable std::array<DirectX::SimpleMath::Vector2,16> m_Screen{},m_Previous{};
    float m_Accumulator=0,m_Age=0,m_ImpactAge=-1;
    bool m_Released=false,m_Hit=false;
};
