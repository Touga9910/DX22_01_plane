#include "../GameRunDefaults.h"

#include <cassert>

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
