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
using namespace DirectX::SimpleMath;
TitleTable::~TitleTable()=default;
void TitleTable::Release()
{
    for(auto& visual:m_Visual)visual.reset();
    m_Table.reset();m_Frame.reset();
}
void TitleTable::Awake()
{
    m_Table=std::make_unique<GameObject>("TitleTableVisual");
    m_Table->AddComponent<GroundRenderComponent>();
    m_Table->AddComponent<Ground>();
    // Frame geometry is already in table coordinates; keep it on its own object.
    for(int i=0;i<16;++i)
    {
        auto& b=m_Balls[i]; b.id=i+1;b.status.radius=2;b.status.mass=1;b.status.restitution=.94f;
        b.position={-44,TableConfig::FIELD_HEIGHT+2,0};
        m_Visual[i]=std::make_unique<GameObject>("TitleBall");
        if(i) m_Visual[i]->AddComponent<TagComponent>(GameObjectTag::Enemy);
        auto* render=m_Visual[i]->AddComponent<BallRenderComponent>();
        render->LoadModel("assets/model/golf_ball.obj","assets/model/");
        render->SetSpecularStrength(.8f);render->SetEnvironmentStrength(.25f);
    }
    int i=1;
    for(int row=0;row<5;++row) for(int col=0;col<=row;++col)
        m_Balls[i++].position={19+row*3.52f,TableConfig::FIELD_HEIGHT+2,(col-row*.5f)*4.06f};
}
void TitleTable::Step(float dt,bool released)
{
    m_Age+=dt;
    if(!released)return; // Absolutely no physics drift while idle.
    if(!m_Released){m_Released=true;m_Balls[0].velocity={110,0,.17f};GameSceneCommands::PlayTitleCue(true,2);}
    m_Accumulator+=std::clamp(dt,0.0f,.1f);
    while(m_Accumulator>=1.0f/60)
    {
        m_Accumulator-=1.0f/60;
        for(int sub=0;sub<8;++sub)
        {
            for(auto& b:m_Balls)
            {
                b.touching.clear(); b.position+=b.velocity*(1.0f/480);
                b.velocity*=.9995f;
                for(const auto& wall:BallPhysicsRules::BossWalls()) BallPhysicsRules::Wall(b,wall,Vector3::Zero);
            }
            for(int i=0;i<16;++i)for(int j=i+1;j<16;++j)
                if(BallPhysicsRules::Pair(m_Balls[i],m_Balls[j]))
                {
                    if(!m_Hit){GameSceneCommands::PlayTitleCue(false,4);m_ImpactAge=m_Age;}
                    m_Hit=true;
                }
        }
    }
}
Vector2 TitleTable::ScreenBall(int index)const{return m_Screen[index];}
void TitleTable::Draw()
{
    auto* context=Renderer::GetDeviceContext();
    if(!m_Table)return;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    context->OMGetRenderTargets(1,target.GetAddressOf(),nullptr);
    if(target){const float navy[]={.018f,.028f,.052f,1};context->ClearRenderTargetView(target.Get(),navy);}
    UINT count=1;D3D11_VIEWPORT previous{};context->RSGetViewports(&count,&previous);
    const float w=static_cast<float>(Application::GetWidth()),h=static_cast<float>(Application::GetHeight());
    D3D11_VIEWPORT viewport{w*.35f,0,w*.65f,h,0,1};context->RSSetViewports(1,&viewport);
    const float aspect=viewport.Width/viewport.Height;
    const float distance=230.0f*(std::max)(1.0f,1.0f/aspect);
    Vector3 eye(-distance*.60f+std::sin(m_Age*.08f)*1.3f,distance*.62f,-distance*.56f);
    if(m_ImpactAge>=0 && m_Age-m_ImpactAge<.22f && GameSceneCommands::TitleShakeEnabled())
        eye.x+=std::sin((m_Age-m_ImpactAge)*110)*.65f*(1-(m_Age-m_ImpactAge)/.22f);
    RenderViewOverride::View view{DirectX::XMMatrixLookAtLH(eye,Vector3(0,1,0),Vector3::UnitY),
        DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(45),aspect,1,1000),eye};
    {
        RenderViewOverride::Scope scope(view);
        m_Table->Draw();
        // Title-only frame object: no collision or charge gameplay components.
        if(!m_Frame){m_Frame=std::make_unique<GameObject>("TitleFrame");m_Frame->AddComponent<TableFrameRenderComponent>()->SetChargeIndicatorsEnabled(false);}
        m_Frame->Draw();
        for(int i=0;i<16;++i)
        {
            auto* r=m_Visual[i]->GetComponent<BallRenderComponent>();
            const float scale=2.0f/(std::max)(.001f,r->GetModelBaseRadius());
            const auto rotation=Matrix::CreateRotationZ(m_Released?m_Balls[i].position.x*.12f:0);
            Matrix world=Matrix::CreateScale(scale)*rotation*Matrix::CreateTranslation(m_Balls[i].position);
            r->DrawMesh(world);
            const auto p=Vector3::Transform(m_Balls[i].position,view.view*view.projection);
            m_Previous[i]=m_Screen[i];
            m_Screen[i]={viewport.TopLeftX+(p.x+1)*viewport.Width*.5f,(1-p.y)*h*.5f};
        }
    }
    context->RSSetViewports(count,&previous);
    Camera::GetInstance().SetCamera();
    // Following renderers always bind their own camera as before.
}
