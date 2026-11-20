#pragma once
#include <algorithm>
// No Game, rendering, physics or save dependencies. Start is committed once.
class TitleSequence
{
public:
    enum class Phase { Idle, Break, Fade, Ready, Committed };
    Phase phase=Phase::Idle;
    float time=0;
    bool Begin() { if(phase!=Phase::Idle)return false; phase=Phase::Break;time=0;return true; }
    void Advance(float dt,bool rackHit=false)
    {
        if(phase==Phase::Idle||phase==Phase::Ready||phase==Phase::Committed)return;
        time+=std::clamp(dt,0.0f,.1f);
        if(phase==Phase::Break&&((rackHit&&time>=3.0f)||time>=5.0f)){phase=Phase::Fade;time=0;}
        else if(phase==Phase::Fade&&time>=.65f){phase=Phase::Ready;time=0;}
    }
    void Skip(){if(phase==Phase::Break||phase==Phase::Fade)phase=Phase::Ready;}
    bool ConsumeStart(){if(phase!=Phase::Ready)return false;phase=Phase::Committed;return true;}
    bool Active()const{return phase!=Phase::Idle;}
};
