#include "../GameRunDefaults.h"

#include <cassert>

/// <summary>
/// ラン開始時に既定の所持金が設定され、セーブを復元しても開始分が重複しないことを検証する。
/// </summary>
int main()
{
	GameRunDefaults defaults;
	assert(defaults.PlayerStatus().money == GameRunDefaults::InitialMoney);
	assert(GameRunDefaults::InitialMoney == 50);

	// Loading assigns the serialized status; the starting grant is not added.
	PlayerRunStatus restored = defaults.PlayerStatus();
	restored.money = 17;
	defaults.PlayerStatus() = restored;
	assert(defaults.PlayerStatus().money == 17);
	return 0;
}
