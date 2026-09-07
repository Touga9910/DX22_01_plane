#include "MenuSelection.h"

#include "input.h"

#include <algorithm>

bool MenuSelection::UpdateVertical(int itemCount, bool allowSpace)
{
	m_WasMoved = false;
	const bool mouseConfirmed = m_MouseConfirmed;
	m_MouseConfirmed = false;
	itemCount = (std::max)(1, itemCount);
	const bool moveUp = Input::GetKeyTrigger(VK_W) ||
		Input::GetKeyTrigger(VK_UP) ||
		Input::GetButtonTrigger(XINPUT_UP);
	const bool moveDown = Input::GetKeyTrigger(VK_S) ||
		Input::GetKeyTrigger(VK_DOWN) ||
		Input::GetButtonTrigger(XINPUT_DOWN);
	if (moveUp)
	{
		m_Index = (m_Index + itemCount - 1) % itemCount;
		m_WasMoved = true;
	}
	if (moveDown)
	{
		m_Index = (m_Index + 1) % itemCount;
		m_WasMoved = true;
	}
	return mouseConfirmed || Input::GetKeyTrigger(VK_RETURN) ||
		(allowSpace && Input::GetKeyTrigger(VK_SPACE)) ||
		Input::GetButtonTrigger(XINPUT_A);
}

void MenuSelection::SetIndex(int index, int itemCount)
{
	itemCount = (std::max)(1, itemCount);
	m_Index = std::clamp(index, 0, itemCount - 1);
}
