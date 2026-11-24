#pragma once
#include <algorithm>
#include "SceneTransitionTiming.h"
// No Game, rendering, physics or save dependencies. Start is committed once.
class TitleSequence
{
public:
    enum class Phase { Idle, Approach, Settle, Break, Fade, Ready, Committed };
    static constexpr float ApproachDuration = .8f;
    static constexpr float SettleDuration = .2f;
    float fadeDuration = SceneTransitionTiming{}.fadeOutSeconds;
    Phase phase=Phase::Idle;
    float time=0;
    /// <summary>
    /// フェード時間を設定し、待機中のタイトル演出を開始する。
    /// </summary>
    bool Begin(float fadeSeconds = SceneTransitionTiming{}.fadeOutSeconds)
    {
        if(phase!=Phase::Idle)return false;
        fadeDuration=SceneTransitionTiming::ClampSeconds(fadeSeconds, SceneTransitionTiming{}.fadeOutSeconds);
        phase=Phase::Approach;time=0;return true;
    }
    /// <summary>
    /// 経過時間とフェード時間から現在のフェード率を求める。
    /// </summary>
    float FadeAlpha()const { return fadeDuration>0?std::clamp(time/fadeDuration,0.0f,1.0f):1.0f; }
    /// <summary>
    /// タイトル演出の段階と経過時間からカメラの補間率を求める。
    /// </summary>
    float CameraBlend()const
    {
        if(phase==Phase::Idle)return 0;
        if(phase!=Phase::Approach)return 1;
        const float remaining=1-std::clamp(time/ApproachDuration,0.0f,1.0f);
        return 1-remaining*remaining*remaining;
    }
    /// <summary>
    /// 経過時間とラックへの衝突に応じてタイトル演出を次の段階に進める。
    /// </summary>
    void Advance(float dt,bool rackHit=false)
    {
        if(phase==Phase::Idle||phase==Phase::Ready||phase==Phase::Committed)return;
        time+=std::clamp(dt,0.0f,.1f);
        if(phase==Phase::Approach&&time>=ApproachDuration){phase=Phase::Settle;time=0;}
        else if(phase==Phase::Settle&&time>=SettleDuration){phase=Phase::Break;time=0;}
        else if(phase==Phase::Break&&((rackHit&&time>=3.0f)||time>=5.0f)){phase=Phase::Fade;time=0;}
        else if(phase==Phase::Fade&&time>=fadeDuration){phase=Phase::Ready;time=0;}
    }
    /// <summary>
    /// 進行中のタイトル演出をスキップし、開始を確定できる状態にする。
    /// </summary>
    void Skip(){if(phase==Phase::Approach||phase==Phase::Settle||phase==Phase::Break||phase==Phase::Fade)phase=Phase::Ready;}
    /// <summary>
    /// 開始可能な状態を確定済みに変更し、開始が受理されたかを返す。
    /// </summary>
    bool ConsumeStart(){if(phase!=Phase::Ready)return false;phase=Phase::Committed;return true;}
    /// <summary>
    /// タイトル演出が待機状態から進んでいるかを返す。
    /// </summary>
    bool Active()const{return phase!=Phase::Idle;}
};
