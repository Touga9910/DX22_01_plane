#pragma once

class MenuSelection final
{
public:
	bool UpdateVertical(int itemCount, bool allowSpace = true);
	int GetIndex() const { return m_Index; }
	void SetIndex(int index, int itemCount);
	void Confirm(int index, int itemCount) { SetIndex(index, itemCount); m_MouseConfirmed = true; }
	bool WasMoved() const { return m_WasMoved; }

private:
	int m_Index = 0;
	bool m_WasMoved = false;
	bool m_MouseConfirmed = false;
};
