#include "../TitleSequence.h"
#include <cassert>
#include <iostream>
int main()
{
    TitleSequence configured; assert(configured.Begin(.4f));
    configured.phase=TitleSequence::Phase::Fade;configured.time=.2f;
    assert(std::abs(configured.FadeAlpha()-.5f)<.001f);
    configured.Advance(.1f);configured.Advance(.1f);
    assert(configured.phase==TitleSequence::Phase::Ready);
    TitleSequence instant;assert(instant.Begin(0));instant.phase=TitleSequence::Phase::Fade;
    assert(instant.FadeAlpha()==1);instant.Advance(0);assert(instant.ConsumeStart());
    TitleSequence idle;for(int i=0;i<600;++i)idle.Advance(.016f);assert(!idle.Active()&&!idle.ConsumeStart());
    TitleSequence camera;assert(camera.Begin());assert(camera.CameraBlend()==0);
    float previous=0;
    while(camera.phase==TitleSequence::Phase::Approach)
    {
        camera.Advance(1.0f/60);
        assert(camera.CameraBlend()>=previous&&camera.CameraBlend()<=1);
        assert(camera.phase!=TitleSequence::Phase::Break);
        previous=camera.CameraBlend();
    }
    assert(camera.phase==TitleSequence::Phase::Settle&&camera.CameraBlend()==1);
    camera.Advance(.1f);assert(camera.phase==TitleSequence::Phase::Settle);
    camera.Advance(.1f);assert(camera.phase==TitleSequence::Phase::Break&&camera.CameraBlend()==1);
    for(int skip=0;skip<5;++skip)
    {
        TitleSequence sequence;assert(sequence.Begin());assert(sequence.phase==TitleSequence::Phase::Approach);assert(!sequence.Begin());
        for(int i=0;i<1000;++i)
        {
            if((skip==1&&sequence.phase==TitleSequence::Phase::Approach)||(skip==2&&sequence.phase==TitleSequence::Phase::Settle)||
                (skip==3&&sequence.phase==TitleSequence::Phase::Break)||(skip==4&&sequence.phase==TitleSequence::Phase::Fade))sequence.Skip();
            sequence.Advance(1.0f/60,true);
            if(sequence.ConsumeStart())
            {
                assert(!sequence.ConsumeStart()&&!sequence.Begin());break;
            }
        }
        assert(sequence.phase==TitleSequence::Phase::Committed);
    }
    TitleSequence timeout;timeout.Begin();for(int i=0;i<500;++i)timeout.Advance(.016f,false);assert(timeout.ConsumeStart());
    std::cout<<"PASS: camera approach, stationary pause before shot, idle, double start, normal completion, every skip phase, collision timeout, one-shot commit\n";
}
