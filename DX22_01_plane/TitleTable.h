#pragma once
#include "Component.h"
#include "TitlePhysicsWorld.h"
#include "CollisionVfxSystem.h"
#include <array>
#include <memory>
class GameObject;
class TitleTable final:public Component
{
public:
    ~TitleTable() override;
    void Awake() override;
    /// <summary>
    /// オブジェクトの破棄時にタイトル台のリソースを解放する。
    /// </summary>
    void OnDestroy() override { Release(); }
    void Release();
    void Draw() override;
    void Step(float dt,bool released,float cameraBlend=0);
    void DrawEffects(ImDrawList* draw);
    bool Project(const DirectX::SimpleMath::Vector3& world, ImVec2& screen)const;
    /// <summary>
    /// タイトル演出の手球がラックに衝突したかを返す。
    /// </summary>
    bool RackHit() const{return m_Physics.hit;}
    /// <summary>
    /// タイトル台の演出開始からの経過時間を返す。
    /// </summary>
    float Age()const{return m_Age;}
private:
    std::unique_ptr<GameObject> m_Table,m_Frame;
    std::array<std::unique_ptr<GameObject>,16> m_Visual;
    TitlePhysicsWorld m_Physics;
    CollisionVfxSystem m_Vfx;
    unsigned long long m_EffectFrame=0;
    DirectX::SimpleMath::Matrix m_View{},m_Projection{};
    std::array<float,4> m_Viewport{};
    float m_RenderWidth=1,m_RenderHeight=1;
    float m_CameraBlend=0;
    bool m_ProjectionValid=false;
    float m_Accumulator=0,m_Age=0,m_ShakePhase=0,m_ShakeLifetime=0,m_ShakeStrength=0,m_ShakeCooldown=0;
    bool m_Released=false;
    bool m_Ready=false;
};
