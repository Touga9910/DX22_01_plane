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
        /// <summary>
        /// 現在の描画ビューを退避し、このスコープで使うビューに切り替える。
        /// </summary>
        explicit Scope(const View& value):previous(current){current=&value;}
        /// <summary>
        /// スコープ終了時に描画ビューを直前の状態へ戻す。
        /// </summary>
        ~Scope(){current=previous;}
        /// <summary>
        /// 描画ビューを復元する責務が重複しないよう、スコープのコピー構築を禁止する。
        /// </summary>
        Scope(const Scope&)=delete;
        /// <summary>
        /// 描画ビューの退避状態を上書きできないよう、スコープのコピー代入を禁止する。
        /// </summary>
        Scope& operator=(const Scope&)=delete;
    };
}
