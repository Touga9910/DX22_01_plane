#include "../TitleSequence.h"
#include <cassert>
#include <iostream>
int main()
{
    TitleSequence idle;for(int i=0;i<600;++i)idle.Advance(.016f);assert(!idle.Active()&&!idle.ConsumeStart());
    for(int skip=0;skip<4;++skip)
    {
        TitleSequence sequence;assert(sequence.Begin());assert(!sequence.Begin());
        for(int i=0;i<1000;++i)
        {
            if((skip==1&&sequence.phase==TitleSequence::Phase::Link)||(skip==2&&sequence.phase==TitleSequence::Phase::Break)||(skip==3&&sequence.phase==TitleSequence::Phase::Fade))sequence.Skip();
            sequence.Advance(1.0f/60,true);
            if(sequence.ConsumeStart())
            {
                assert(!sequence.ConsumeStart()&&!sequence.Begin());break;
            }
        }
        assert(sequence.phase==TitleSequence::Phase::Committed);
    }
    TitleSequence timeout;timeout.Begin();for(int i=0;i<500;++i)timeout.Advance(.016f,false);assert(timeout.ConsumeStart());
    std::cout<<"PASS: idle, double start, normal completion, every skip phase, collision timeout, one-shot commit\n";
}
