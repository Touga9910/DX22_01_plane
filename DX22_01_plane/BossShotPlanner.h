#pragma once

#include "json/json.hpp"

#include <string>

class Game;

// Owns boss-shot search results and chooses a requested planned shot.
class BossShotPlanner final
{
public:
	nlohmann::json Evaluate(Game& game);
	bool Fire(
		Game& game,
		const std::string& candidateId,
		const std::string& stateKey);
	/// <summary>
	/// ボスのショット候補のキャッシュと識別キーを消去する。
	/// </summary>
	void Reset()
	{
		m_BossShotCache = nlohmann::json{};
		m_BossShotCacheKey.clear();
	}

private:
	nlohmann::json m_BossShotCache;
	std::string m_BossShotCacheKey;
};
