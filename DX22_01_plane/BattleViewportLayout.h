#pragma once

#include "TableConfig.h"

#include <algorithm>
#include <cmath>

// Keeps the battle table inside the screen area below the persistent top HUD.
// The camera still renders to the full viewport, so picking and prediction keep
// using the same projection as the visible table.
struct BattleViewportLayout
{
	float viewportWidth = 1.0f;
	float viewportHeight = 1.0f;
	float hudHeight = 46.0f;
	float stagePadding = 8.0f;
	float stageTop = 54.0f;
	float stageBottom = 1.0f;
	float cameraDistance = 150.0f;
	float cameraTargetZ = 0.0f;

	static BattleViewportLayout Calculate(float width, float height) noexcept
	{
		BattleViewportLayout result{};
		result.viewportWidth = (std::max)(1.0f, width);
		result.viewportHeight = (std::max)(1.0f, height);
		result.hudHeight = std::clamp(
			result.viewportHeight * 0.052f, 46.0f, 56.0f);
		result.stagePadding = std::clamp(
			(std::min)(result.viewportWidth, result.viewportHeight) * 0.012f,
			8.0f, 14.0f);
		result.stageTop = result.hudHeight + result.stagePadding;
		result.stageBottom = result.viewportHeight - result.stagePadding;

		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kVerticalFieldOfView = 45.0f * kPi / 180.0f;
		const float tanHalfFov = std::tan(kVerticalFieldOfView * 0.5f);
		const float usableWidth = (std::max)(
			1.0f, result.viewportWidth - result.stagePadding * 2.0f);
		const float usableHeight = (std::max)(
			1.0f, result.stageBottom - result.stageTop);

		// A small world-space margin keeps rails, pocket rims, and projected
		// enemy rings from touching the viewport edges.
		constexpr float kWorldMargin = 4.0f;
		const float stageWidth = TableConfig::TABLE_OUTER_WIDTH + kWorldMargin;
		const float stageDepth = TableConfig::TABLE_OUTER_DEPTH + kWorldMargin;
		const float widthDistance = stageWidth * result.viewportHeight /
			(2.0f * tanHalfFov * usableWidth);
		const float heightDistance = stageDepth * result.viewportHeight /
			(2.0f * tanHalfFov * usableHeight);
		result.cameraDistance = std::clamp(
			(std::max)(widthDistance, heightDistance), 80.0f, 220.0f);

		const float pixelsPerWorldUnit = result.viewportHeight /
			(2.0f * result.cameraDistance * tanHalfFov);
		const float desiredStageCenterY =
			(result.stageTop + result.stageBottom) * 0.5f;
		result.cameraTargetZ =
			(desiredStageCenterY - result.viewportHeight * 0.5f) /
			pixelsPerWorldUnit;
		return result;
	}
};
