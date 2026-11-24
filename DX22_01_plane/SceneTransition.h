#pragma once
#include "SceneTransitionTiming.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

// Visual-time state machine. Scene mutation only happens after a full black
// frame has been presented; the caller must freeze gameplay while Active().
class SceneTransition
{
public:
    enum class Phase { Idle, FadeOut, Black, FadeIn };
    /// <summary>
    /// 遷移の開始前にフェード時間を正規化して設定する。
    /// </summary>
    void Configure(SceneTransitionTiming timing)
    {
        if (Active()) return;
        timing.Normalize();
        m_Timing = timing;
    }

    /// <summary>
    /// 遷移中でなければ確定用の処理を保持し、フェードアウトを開始する。
    /// </summary>
    bool Request(std::function<void()> action, bool alreadyBlack = false)
    {
        if (Active() || !action) return false;
        m_Preview = false;
        m_Action = std::move(action);
        m_Phase = alreadyBlack ? Phase::Black : Phase::FadeOut;
        m_Time = 0;
        return true;
    }
    /// <summary>
    /// 現在の遷移がなければ、指定タイミングでフェードのプレビューを始める。
    /// </summary>
    bool RequestPreview(SceneTransitionTiming timing)
    {
        if (Active()) return false;
        Configure(timing);
        if (!Request([] {})) return false;
        m_Preview = true;
        return true;
    }
    /// <summary>
    /// 画面遷移がプレビューとして進行中かを返す。
    /// </summary>
    bool IsPreviewing() const { return m_Preview && Active(); }
    /// <summary>
    /// 進行中のフェードプレビューを取り消して待機状態に戻す。
    /// </summary>
    bool CancelPreview()
    {
        if (!IsPreviewing()) return false;
        Reset();
        return true;
    }
    /// <summary>
    /// 画面を表示するフェードイン段階に切り替え、経過時間をリセットする。
    /// </summary>
    void BeginFadeIn() { m_Phase = Phase::FadeIn; m_Time = 0; }
    /// <summary>
    /// 遷移時の処理、段階、プレビューと時間の状態を初期化する。
    /// </summary>
    void Reset() { m_Action = {}; m_Phase = Phase::Idle; m_Time = 0; m_Committing = false; m_Preview = false; }
    /// <summary>
    /// フェードまたは暗転状態で画面遷移が進行しているかを返す。
    /// </summary>
    bool Active() const { return m_Phase != Phase::Idle; }
    /// <summary>
    /// 遷移先を確定する処理の実行中かを返す。
    /// </summary>
    bool IsCommitting() const { return m_Committing; }
    /// <summary>
    /// 画面遷移の現在の段階を返す。
    /// </summary>
    Phase GetPhase() const { return m_Phase; }
    /// <summary>
    /// フェード段階と経過時間に対応する暗転の不透明度を返す。
    /// </summary>
    float Alpha() const
    {
        if (m_Phase == Phase::Idle) return 0;
        if (m_Phase == Phase::Black) return 1;
        const float duration = m_Phase == Phase::FadeOut ? m_Timing.fadeOutSeconds : m_Timing.fadeInSeconds;
        const float t = duration > 0 ? std::clamp(m_Time / duration, 0.0f, 1.0f) : 1;
        const float eased = t * t * (3 - 2 * t);
        return m_Phase == Phase::FadeOut ? eased : 1 - eased;
    }
    // True also on the final fade-in frame: no input/physics catches up then.
    /// <summary>
    /// 経過時間を反映してフェードと遷移先の確定を進め、このフレームで遷移を処理したかを返す。
    /// </summary>
    bool Advance(double seconds)
    {
        if (!Active()) return false;
        const float dt = std::isfinite(seconds) ? static_cast<float>(std::clamp(seconds, 0.0, .1)) : 0;
        if (m_Phase == Phase::Black)
        {
            m_Time += dt;
            if (m_Time < m_Timing.blackHoldSeconds) return true;
            auto action = std::move(m_Action);
            m_Committing = true;
            try { if (action) action(); }
            catch (...) { m_Committing = false; BeginFadeIn(); throw; }
            m_Committing = false;
            BeginFadeIn();
            return true;
        }
        m_Time += dt;
        if (m_Phase == Phase::FadeOut && m_Time >= m_Timing.fadeOutSeconds)
        {
            m_Phase = Phase::Black; m_Time = 0;
        }
        else if (m_Phase == Phase::FadeIn && m_Time >= m_Timing.fadeInSeconds)
        {
            m_Phase = Phase::Idle; m_Time = 0; m_Preview = false;
        }
        return true;
    }
private:
    SceneTransitionTiming m_Timing;
    Phase m_Phase = Phase::Idle;
    float m_Time = 0;
    bool m_Committing = false;
    bool m_Preview = false;
    std::function<void()> m_Action;
};
