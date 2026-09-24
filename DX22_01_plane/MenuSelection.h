#pragma once

// キーボード・ゲームパッド・マウスによる縦並びメニューの選択状態を管理
class MenuSelection final
{
public:
	// 上下入力で選択位置を循環させ、決定入力があった場合はtrueを返す
	// itemCountは最低1として扱い、allowSpaceがfalseの場合はSPACEを決定入力として扱わない
	bool UpdateVertical(int itemCount, bool allowSpace = true);

	int GetIndex() const { return m_Index; }	// 現在選択している項目番号を返す

	// 選択位置を0～itemCount-1の範囲へ補正して設定
	void SetIndex(int index, int itemCount);

	// マウスなどから指定項目を選択し、次回UpdateVerticalで決定済みとして扱う
	void Confirm(int index, int itemCount) { SetIndex(index, itemCount); m_MouseConfirmed = true; }

	bool WasMoved() const { return m_WasMoved; }	// 直前のUpdateVerticalで上下移動が発生していればtrueを返す

private:
	int m_Index = 0;					// 現在選択している項目番号
	bool m_WasMoved = false;			// 直前の更新で選択位置が上下に移動したか
	bool m_MouseConfirmed = false;		// Confirmによる決定入力を次回更新まで保持
};
