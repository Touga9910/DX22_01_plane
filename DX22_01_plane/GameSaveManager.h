#pragma once

#include "GameTypes.h"

#include <string>

class Game;

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
		Game& game,
		SceneType resumeScene,
		bool sceneAlreadyActive,
		std::string& message);
	static bool Load(Game& game, std::string& message);
	static bool Remove(std::string* error = nullptr);
};
