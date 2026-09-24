#pragma once

#include <algorithm>
#include <cmath>

// 物理更新を60Hzの固定時間刻みで進めるためのクロック
// 1tickは従来の速度・摩擦の単位である1/60秒を維持
// 入力、コマンド、ターン処理はこのクラスの管理対象外
class FixedStepClock final
{
public:
    static constexpr double StepSeconds = 1.0 / 60.0;   // 物理更新1回分の秒数
    static constexpr int MaxStepsPerFrame = 8;          // 1描画フレームで実行する物理更新の上限回数
    static constexpr double MaxFrameSeconds = 0.25;     // 1回のAdvanceで受け入れる経過時間の上限

    // 実時間の経過秒数を受け取り、このフレームで実行すべき固定更新回数を返す
    // 0以下または有限でない値の場合は0を返す
    // 上限を超えた経過時間や実行しきれないtickはDroppedSecondsへ加算する
    int Advance(double elapsedSeconds)
    {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0)
        {
            return 0;
        }
        const double accepted = (std::min)(elapsedSeconds, MaxFrameSeconds);
        m_DroppedSeconds += elapsedSeconds - accepted;
        m_RemainderSeconds += accepted;
        // 144Hzで1秒進めた場合など、丸め誤差で正確な1tickを失わないよう微小値を加える
        const int due = static_cast<int>(
            std::floor((m_RemainderSeconds + 1.0e-10) / StepSeconds));
        m_RemainderSeconds = (std::max)(0.0,
            m_RemainderSeconds - due * StepSeconds);
        const int steps = (std::min)(due, MaxStepsPerFrame);
        m_DroppedSeconds += (due - steps) * StepSeconds;
        return steps;
    }

    // ポーズ、シーン切替、新しいショット開始時などに未処理時間を破棄する
    void Reset() { m_RemainderSeconds = 0.0; }

    double RemainderSeconds() const { return m_RemainderSeconds; }  // 次回以降へ持ち越す未満tickの秒数
    double DroppedSeconds() const { return m_DroppedSeconds; }      // 上限処理によって物理更新へ使用しなかった累積秒数

private:
    double m_RemainderSeconds = 0.0;   // まだ固定更新1回分に達していない持ち越し時間
    double m_DroppedSeconds = 0.0;     // 処理上限によって破棄した経過時間の累積値
};
