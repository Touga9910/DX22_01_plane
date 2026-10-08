#pragma once

#include "GameTypes.h"

#include <string>

struct GameRunRestoreRequest;
struct GameRunSaveSnapshot;

// Values required to validate and migrate a save document.  Keeping this
// separate from Game prevents the persistence layer from reaching into the
// runtime coordinator while preserving the existing migration rules.
struct GameRunLoadRules
{
	bool enduranceMode = false;
	int normalRouteAreaGoal = 0;
	int relicCount = 0;
	int ballCatalogCount = 0;
};

struct RunSaveInfo
{
	bool exists = false;
	bool valid = false;
	int floor = 1;
	int currentHp = 0;
	int maxHp = 0;
	int money = 0;
	std::string savedAt;
	std::string error;
};

class GameSaveManager final
{
public:
	static RunSaveInfo Inspect();
	static bool Save(
		const GameRunSaveSnapshot& snapshot,
		SceneType resumeScene,
		bool debugMode,
		bool restActionUsed,
		std::string& message);
	static bool Load(
		const GameRunLoadRules& rules,
		GameRunRestoreRequest& request,
		std::string& message);
	static bool Remove(std::string* error = nullptr);
};
