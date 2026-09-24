#pragma once

#include "Component.h"

// GameObjectの用途や種類を識別するためのタグを表す。
// 衝突判定やオブジェクト種別の判定などで使用
enum class GameObjectTag
{
    None,       // タグ未設定
    Player,     // プレイヤー
    Enemy,      // 敵
    Ground,     // 地面・フィールド
    Rail,       // 壁・レール
    Pocket,     // ポケット
    Goal,       // ゴール
    WorldUi,    // ワールド空間上に表示するUI
    ScreenUi,   // 画面空間に表示するUI
};

// GameObjectへタグ情報を付与するためのコンポーネント。
// 1つのGameObjectに対して、現在設定されているGameObjectTagを保持
class TagComponent final : public Component
{
public:
    // 指定したタグを初期値としてTagComponentを生成
    // 引数を省略した場合はNoneとして生成
    explicit TagComponent(GameObjectTag tag = GameObjectTag::None)
        : m_Tag(tag)
    {
    }

    // -------------------------
    // タグ取得・変更
    // -------------------------

    GameObjectTag GetTag() const { return m_Tag; }        // 現在設定されているタグを返す
    void SetTag(GameObjectTag tag) { m_Tag = tag; }       // このGameObjectのタグを指定した値へ変更する

private:
    // -------------------------
    // メンバー変数
    // -------------------------

    GameObjectTag m_Tag = GameObjectTag::None;            // このGameObjectに設定されているタグ
};
