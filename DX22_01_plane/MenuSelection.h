#pragma once

class MenuSelection final
{
public:
	bool UpdateVertical(int itemCount, bool allowSpace = true);
	int GetIndex() const { return m_Index; }
	void SetIndex(int index, int itemCount);
	bool WasMoved() const { return m_WasMoved; }

private:
	int m_Index = 0;
	bool m_WasMoved = false;
};
