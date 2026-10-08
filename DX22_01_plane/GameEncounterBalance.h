#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct EncounterThreatInput
{
	std::string stageId;
	std::vector<std::string> enemyIds;
	bool usesMcpOverride = false;
};

struct EncounterThreatSnapshot
{
	std::string layoutSource;
	float baseBudget = 0.0f;
	float layoutMultiplier = 1.0f;
	float actualBudget = 0.0f;
	std::optional<float> targetBudget;
};

// Owns encounter-threat tuning and evaluates the immutable values written to
// balance logs. It does not decide stage selection or modify combat values.
class GameEncounterBalance final
{
public:
	void Load(const std::string& filePath);
	EncounterThreatSnapshot Evaluate(const EncounterThreatInput& input) const;

private:
	std::unordered_map<std::string, float> m_EnemyThreatCosts;
	std::unordered_map<std::string, float> m_StageThreatTargets;
	float m_StageDataLayoutMultiplier = 1.0f;
	float m_DenseLayoutMultiplier = 1.25f;
	float m_McpLayoutMultiplier = 1.0f;
};
