#include "TitleTable.h"
#include "GameObject.h"
#include "Ground.h"
#include "GroundRenderComponent.h"
#include "TableFrameRenderComponent.h"
#include "BallRenderComponent.h"
#include "TagComponent.h"
#include "TransformComponent.h"
#include "RenderViewOverride.h"
#include "Renderer.h"
#include "Application.h"
#include "Camera.h"
#include "GameSceneCommands.h"
#include <cmath>
#include "PlayerBallDataLoader.h"
#include "StageDataLoader.h"
#include "ShotPowerRules.h"
#include "imgui/imgui.h"
using namespace DirectX::SimpleMath;
/// <summary>
/// タイトルのビリヤード台と演出に使うリソースを解放する。
/// </summary>
TitleTable::~TitleTable(){Release();}
/// <summary>
/// ビリヤード台、球、演出のリソースを解放し、投影情報を無効にする。
/// </summary>
void TitleTable::Release()
{
    m_Ready=false;
    m_Physics.impact={};
    for(auto& visual:m_Visual)visual.reset();
    m_Table.reset();m_Frame.reset();m_Vfx.ClearActive();m_ProjectionValid=false;
}
/// <summary>
/// タイトル演出の台、ラック、手球、物理とエフェクトを初期化する。
/// </summary>
void TitleTable::Awake()
{
    Release();
    m_Physics=TitlePhysicsWorld{};m_ShakePhase=m_ShakeLifetime=m_ShakeStrength=m_ShakeCooldown=0;m_Age=0;m_Released=false;m_Accumulator=0;m_EffectFrame=0;m_Vfx.Reset();
    m_CameraBlend=0;m_Physics.breakFanEnabled=true;
    BallStatus fallback;fallback.radius=2.4f;fallback.mass=2;fallback.restitution=.8f;fallback.friction=.02f;
    const auto data=PlayerBallDataLoader::Load("assets/data/player_status.json",fallback,PlayerRunStatus{});
    auto cue=data.defaultBallStatus;
    for(const auto& ball:data.ballDefinitions)if(ball.definitionId=="player_standard")cue=ball.status;
    auto numbered=cue;
    for(const auto& enemy:StageDataLoader::LoadEnemyDefinitions("assets/data/enemy_data.json"))if(enemy.id=="enemy_normal")numbered=enemy.status;
    // Title-only numbered balls coast farther after the break.
    constexpr float NumberedBallFrictionScale = .65f;
    numbered.friction *= NumberedBallFrictionScale;
    m_Physics.impact=[this](const CollisionImpactEvent& event)
    {
        GameSceneCommands::PlayTitleCollision(event);
        if(event.collisionKind==CombatCollisionKind::Wall)return;
        if(m_ShakeCooldown<=0&&GameSceneCommands::TitleShakeEnabled())
        {
            const auto shake=CollisionPresentationRules::ImpactShake(CollisionPresentationRules::Classify(event.relativeApproachSpeed),event.collisionKind==CombatCollisionKind::EnemyEnemy);
            if(shake.lifetime>0)
            {
                m_ShakeLifetime=(std::max)(m_ShakeLifetime,shake.lifetime);
                m_ShakeStrength=(std::max)(m_ShakeStrength,shake.strength);m_ShakeCooldown=.08f;
            }
        }
        if(!GameSceneCommands::TitleEffectsEnabled())return;
        CollisionVfxSystem::SpawnRequest vfx;
        vfx.worldPosition=event.worldPosition;
        vfx.collisionAxis=event.firstTargetPosition-event.secondTargetPosition;
        vfx.strength=CollisionPresentationRules::Classify(event.relativeApproachSpeed);
        vfx.priority=vfx.strength==CollisionPresentationRules::Strength::Strong?2:vfx.strength==CollisionPresentationRules::Strength::Medium?1:0;
        vfx.secondaryCollision=event.collisionKind==CombatCollisionKind::EnemyEnemy;
        vfx.frame=m_EffectFrame;vfx.density=GameSceneCommands::TitleEffectDensity();
        m_Vfx.Spawn(vfx);
    };
    m_Table=std::make_unique<GameObject>("TitleTableVisual");
    m_Table->AddComponent<GroundRenderComponent>();
    m_Table->AddComponent<Ground>();
    // Frame geometry is already in table coordinates; keep it on its own object.
    for(int i=0;i<16;++i)
    {
        auto& b=m_Physics.balls[i]; b.id=i+1;b.status=i==0?cue:numbered;b.player=i==0;b.enemy=i!=0;
        b.position={-44,TableConfig::FIELD_HEIGHT,0};
        m_Visual[i]=std::make_unique<GameObject>("TitleBall");
        if(i) m_Visual[i]->AddComponent<TagComponent>(GameObjectTag::Enemy);
        auto* render=m_Visual[i]->AddComponent<BallRenderComponent>();
        render->LoadModel("assets/model/golf_ball.obj","assets/model/");
        render->SetSpecularStrength(.8f);render->SetEnvironmentStrength(.25f);
    }
    // Approach the rack from the side rather than straight down its centerline.
    m_Physics.balls[0].position.z=-12.0f;
    const float spacing=numbered.radius*2+.02f;
    int i=1;
    for(int row=0;row<5;++row) for(int col=0;col<=row;++col)
        m_Physics.balls[i++].position={19+row*spacing*.87f,TableConfig::FIELD_HEIGHT,(col-row*.5f)*spacing};
    m_Ready=true;
}
/// <summary>
/// タイトル演出の時間と表示を更新し、手球の発射後は球の物理を固定刻みで進める。
/// </summary>
void TitleTable::Step(float dt,bool released,float cameraBlend)
{
    if(!m_Ready)return;
    m_CameraBlend=std::clamp(cameraBlend,0.0f,1.0f);
    m_Age+=dt;++m_EffectFrame;
    m_ShakeCooldown=(std::max)(0.0f,m_ShakeCooldown-dt);
    m_ShakeLifetime=GameSceneCommands::TitleShakeEnabled()?(std::max)(0.0f,m_ShakeLifetime-dt):0;
    if(m_ShakeLifetime>0)m_ShakePhase+=1.65f;
    m_Vfx.Update(dt,GameSceneCommands::TitleEffectsEnabled());
    if(!released)return;
    if(!m_Released)
    {
        m_Released=true;
        auto direction=m_Physics.balls[1].position-m_Physics.balls[0].position;direction.y=0;direction.Normalize();
        m_Physics.balls[0].velocity=direction*ShotPowerRules::AtFraction(.95f);
        GameSceneCommands::PlayTitleCue(true,ShotPowerRules::AtFraction(.95f));
    }
    m_Accumulator+=std::clamp(dt,0.0f,.1f);
    while(m_Accumulator>=1.0f/60)
    {m_Accumulator-=1.0f/60;m_Physics.Tick();}
}
/// <summary>
/// タイトルのカメラを設定してビリヤード台と球を描き、演出用の投影情報を保存する。
/// </summary>
void TitleTable::Draw()
{
    auto* context=Renderer::GetDeviceContext();
    if(!m_Table)return;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    context->OMGetRenderTargets(1,target.GetAddressOf(),nullptr);
    if(target){const float navy[]={.018f,.028f,.052f,1};context->ClearRenderTargetView(target.Get(),navy);}
    UINT count=1;D3D11_VIEWPORT previous{};context->RSGetViewports(&count,&previous);
    const float w=previous.Width,h=previous.Height;
    m_RenderWidth=w;m_RenderHeight=h;
    D3D11_VIEWPORT viewport{w*.35f,0,w*.65f,h,0,1};context->RSSetViewports(1,&viewport);
    const float aspect=viewport.Width/viewport.Height;
    const float distance=(230.0f-50.0f*m_CameraBlend)*(std::max)(1.0f,1.0f/aspect);
    Vector3 eye(-distance*.60f+std::sin(m_Age*.08f)*1.3f*(1-m_CameraBlend),distance*.62f,-distance*.56f);
    const auto shake=CollisionPresentationRules::ShakeOffset(m_ShakePhase,m_ShakeLifetime,m_ShakeStrength);
    const Vector3 targetPoint(shake.x,1,shake.z);
    RenderViewOverride::View view{DirectX::XMMatrixLookAtLH(eye,targetPoint,Vector3::UnitY),
        DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(45),aspect,1,1000),eye};
    m_View=view.view;m_Projection=view.projection;
    m_Viewport={viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height};m_ProjectionValid=true;
    {
        RenderViewOverride::Scope scope(view);
        m_Table->Draw();
        // Title-only frame object: no collision or charge gameplay components.
        if(!m_Frame){m_Frame=std::make_unique<GameObject>("TitleFrame");m_Frame->AddComponent<TableFrameRenderComponent>()->SetChargeIndicatorsEnabled(false);}
        m_Frame->Draw();
        for(int i=0;i<16;++i)
        {
            if(!m_Physics.active[i])continue;
            auto* r=m_Visual[i]->GetComponent<BallRenderComponent>();
            const float scale=m_Physics.balls[i].status.radius/(std::max)(.001f,r->GetModelBaseRadius());
            const auto rotation=Matrix::CreateFromQuaternion(m_Physics.rotation[i]);
            Matrix world=Matrix::CreateScale(scale)*rotation*Matrix::CreateTranslation(m_Physics.balls[i].position);
            r->DrawMesh(world);

        }
    }
    context->RSSetViewports(count,&previous);
    Camera::GetInstance().SetCamera();
    // Following renderers always bind their own camera as before.
}

namespace { const TitleTable* activeProjector=nullptr;
    /// <summary>
    /// 現在のタイトル台の投影処理を使い、ワールド座標を画面座標へ変換する。
    /// </summary>
    bool ProjectTitle(const Vector3& world,ImVec2& screen){return activeProjector&&activeProjector->Project(world,screen);}
}
/// <summary>
/// タイトル表示の投影情報からワールド座標を画面座標に変換し、投影後の深度を確認する。
/// </summary>
bool TitleTable::Project(const Vector3& world,ImVec2& screen)const
{
    if(!m_ProjectionValid)return false;
    const auto projected=DirectX::XMVector3Project(world,m_Viewport[0],m_Viewport[1],m_Viewport[2],m_Viewport[3],0,1,m_Projection,m_View,Matrix::Identity);
    Vector3 p;DirectX::XMStoreFloat3(&p,projected);
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.z<0||p.z>1)return false;
    const auto* vp=ImGui::GetMainViewport();
    screen={vp->Pos.x+p.x/m_RenderWidth*vp->Size.x,vp->Pos.y+p.y/m_RenderHeight*vp->Size.y};return true;
}
/// <summary>
/// タイトル画面の台の表示範囲に、座標を投影したエフェクトを描く。
/// </summary>
void TitleTable::DrawEffects(ImDrawList* draw)
{
    if(!m_ProjectionValid)return;
    const auto* previous=activeProjector;activeProjector=this;
    const auto* vp=ImGui::GetMainViewport();
    draw->PushClipRect(ImVec2(vp->Pos.x+vp->Size.x*.35f,vp->Pos.y),ImVec2(vp->Pos.x+vp->Size.x,vp->Pos.y+vp->Size.y),true);
    m_Vfx.Draw(draw,&ProjectTitle,GameSceneCommands::TitleEffectsEnabled());
    draw->PopClipRect();activeProjector=previous;
}
