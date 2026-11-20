#pragma once
#include <SimpleMath.h>
// A render-call-local camera. Does not mutate the gameplay singleton camera.
namespace RenderViewOverride
{
    struct View { DirectX::SimpleMath::Matrix view,projection; DirectX::SimpleMath::Vector3 eye; };
    inline const View* current=nullptr;
    struct Scope
    {
        const View* previous;
        explicit Scope(const View& value):previous(current){current=&value;}
        ~Scope(){current=previous;}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
}
