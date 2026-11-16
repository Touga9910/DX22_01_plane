#include "EnemyInfoLayout.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
constexpr float kEnemyFramePadding = 4.0f;
constexpr float kEnemyFrameGap = 2.0f;
constexpr float kEnemyVerticalLimit = 56.0f;
constexpr float kEnemyHorizontalLimit = 30.0f;
constexpr float kEnemyVerticalStep = 4.0f;
constexpr float kEnemyHorizontalStep = 6.0f;
}

ImVec4 EnemyInfoLayout::MakeFrameRect(const ImVec2& pos, float width, float height) const
{
	return ImVec4(
		pos.x - kEnemyFramePadding,
		pos.y - kEnemyFramePadding,
		pos.x + width + kEnemyFramePadding,
		pos.y + height + kEnemyFramePadding
	);
}

ImVec2 EnemyInfoLayout::ClampHoverPosition(const ImVec2& base, float width, float height) const
{
	const float left = m_ViewportPosition.x + kEnemyFramePadding + 2.0f;
	const float top = m_SafeTop + kEnemyFramePadding;
	const float right = m_ViewportPosition.x + m_ViewportSize.x -
		kEnemyFramePadding - width - 2.0f;
	const float bottom = m_ViewportPosition.y + m_ViewportSize.y -
		kEnemyFramePadding - height - 2.0f;
	return ImVec2(
		std::clamp(base.x, left, (std::max)(left, right)),
		std::clamp(base.y, top, (std::max)(top, bottom))
	);
}

EnemyInfoLink EnemyInfoLayout::MakeLink(const ImVec2& pos, float width, float height, bool upper, const ImVec2& ball, float radius) const
{
	const ImVec4 frame = MakeFrameRect(pos, width, height);
	const ImVec2 destination(
		pos.x + width * 0.5f,
		pos.y + height * 0.5f
	);

	const float dx = destination.x - ball.x;
	const float dy = destination.y - ball.y;
	const float length =
		(std::max)(1.0f, std::sqrt(dx * dx + dy * dy));

	const ImVec2 start(
		ball.x + dx / length * (radius + 2.0f),
		ball.y + dy / length * (radius + 2.0f)
	);

	const ImVec2 end(
		std::clamp(start.x, frame.x, frame.z),
		upper ? frame.w : frame.y
	);

	return { start, end };
}

float EnemyInfoLayout::PointSegmentDistanceSquared(const ImVec2& point, const ImVec2& a, const ImVec2& b)
{
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	const float denom = dx * dx + dy * dy;

	const float t = denom > 0.0001f
		? std::clamp(
			((point.x - a.x) * dx + (point.y - a.y) * dy) / denom,
			0.0f, 1.0f)
		: 0.0f;

	const float ex = point.x - (a.x + dx * t);
	const float ey = point.y - (a.y + dy * t);

	return ex * ex + ey * ey;
}

EnemyInfoPlacement EnemyInfoLayout::Resolve(const ImVec2& base, float width, float height, bool upper, std::uintptr_t owner, const ImVec2& ball, float radius)
{
	EnemyInfoPlacement result{};
	result.position = base;

	float fallbackScore = FLT_MAX;
	ImVec2 fallbackPosition = base;
	bool found = false;

	// 指定位置にウィンドウを置けるか評価
	const auto examine =
		[&](const ImVec2& pos, bool& valid, float& score)
		{
			const ImVec4 frame =
				MakeFrameRect(pos, width, height);

			valid = true;
			score = 0.0f;

			// 画面外判定
			if (frame.x < m_ViewportPosition.x + 2.0f ||
				frame.y < m_SafeTop ||
				frame.z > m_ViewportPosition.x + m_ViewportSize.x - 2.0f ||
				frame.w > m_ViewportPosition.y + m_ViewportSize.y - 2.0f)
			{
				valid = false;
				score += 100000.0f;
			}

			// 1. 他の上部・下部ウィンドウとの重なり
			for (const ImVec4& used : m_OccupiedFrames)
			{
				const float w = (std::max)(
					0.0f,
					(std::min)(frame.z, used.z + kEnemyFrameGap) -
					(std::max)(frame.x, used.x - kEnemyFrameGap)
					);

				const float h = (std::max)(
					0.0f,
					(std::min)(frame.w, used.w + kEnemyFrameGap) -
					(std::max)(frame.y, used.y - kEnemyFrameGap)
					);

				if (w > 0.0f && h > 0.0f)
				{
					valid = false;
					score += w * h * 20.0f;
				}
			}

			// 2. ウィンドウが既存の点線に重ならないか
			for (const EnemyInfoLink& previous : m_OccupiedLinks)
			{
				const float dx = previous.end.x - previous.start.x;
				const float dy = previous.end.y - previous.start.y;
				const float length = std::sqrt(dx * dx + dy * dy);

				const int samples = (std::max)(
					1,
					static_cast<int>(std::ceil(length / 3.0f))
					);

				for (int i = 0; i <= samples; ++i)
				{
					const float t = static_cast<float>(i) / samples;

					const ImVec2 p(
						previous.start.x + dx * t,
						previous.start.y + dy * t
					);

					if (p.x >= frame.x - 2.0f &&
						p.x <= frame.z + 2.0f &&
						p.y >= frame.y - 2.0f &&
						p.y <= frame.w + 2.0f)
					{
						valid = false;
						score += 400.0f;
					}
				}
			}

			// 3. 他の敵ボールとの重なり
			for (const auto& [key, body] : m_BodyRegions)
			{
				if (key == owner)
					continue;

				const float bx = (body.x + body.z) * 0.5f;
				const float by = (body.y + body.w) * 0.5f;
				const float br = (body.z - body.x) * 0.5f;

				const float nx = std::clamp(bx, frame.x, frame.z);
				const float ny = std::clamp(by, frame.y, frame.w);

				const float dx = bx - nx;
				const float dy = by - ny;

				const float penetration =
					br - std::sqrt(dx * dx + dy * dy);

				if (penetration > 0.0f)
				{
					valid = false;
					score += penetration * penetration * 30.0f;
				}
			}

			// 4. 移動した場合の点線経路を確認
			if (std::abs(pos.x - base.x) +
				std::abs(pos.y - base.y) > 0.01f)
			{
				const EnemyInfoLink link = MakeLink(
					pos, width, height, upper, ball, radius
				);

				const float dx = link.end.x - link.start.x;
				const float dy = link.end.y - link.start.y;
				const float length = std::sqrt(dx * dx + dy * dy);

				const int samples = (std::max)(
					1,
					static_cast<int>(std::ceil(length / 3.0f))
					);

				for (int i = 0; i <= samples; ++i)
				{
					const float t = static_cast<float>(i) / samples;

					const ImVec2 p(
						link.start.x + dx * t,
						link.start.y + dy * t
					);

					// 点線が他の敵ボールを横切らないか
					for (const auto& [key, body] : m_BodyRegions)
					{
						if (key == owner)
							continue;

						const float bx = (body.x + body.z) * 0.5f;
						const float by = (body.y + body.w) * 0.5f;
						const float br =
							(body.z - body.x) * 0.5f + 2.0f;

						const float ex = p.x - bx;
						const float ey = p.y - by;

						if (ex * ex + ey * ey < br * br)
						{
							valid = false;
							score += 500.0f;
						}
					}

					// 点線が他のウィンドウを横切らないか
					for (const ImVec4& used : m_OccupiedFrames)
					{
						if (p.x >= used.x - 2.0f &&
							p.x <= used.z + 2.0f &&
							p.y >= used.y - 2.0f &&
							p.y <= used.w + 2.0f)
						{
							valid = false;
							score += 400.0f;
						}
					}

					// 点線同士が接近・交差しないか
					for (const EnemyInfoLink& previous : m_OccupiedLinks)
					{
						if (PointSegmentDistanceSquared(
							p, previous.start, previous.end) < 16.0f)
						{
							valid = false;
							score += 200.0f;
						}
					}
				}
			}
		};

	// 上下方向の候補を評価
	const auto consider = [&](const ImVec2& candidate) -> bool
		{
			bool valid = false;
			float score = 0.0f;

			examine(candidate, valid, score);

			score += std::abs(candidate.x - base.x) * 0.5f +
				std::abs(candidate.y - base.y) * 0.1f;

			if (score < fallbackScore)
			{
				fallbackScore = score;
				fallbackPosition = candidate;
			}

			if (valid)
			{
				result.position = candidate;
				found = true;
			}

			return valid;
		};

	// 第1段階：上下方向のみ探索
	for (float y = 0.0f;
		y <= kEnemyVerticalLimit && !found;
		y += kEnemyVerticalStep)
	{
		consider(ImVec2(
			base.x,
			base.y + (upper ? -y : y)
		));
	}

	// 第2段階：上下で解決しなければ左右移動も許可
	if (!found)
	{
		float bestTravel = FLT_MAX;

		for (float x = kEnemyHorizontalStep;
			x <= kEnemyHorizontalLimit;
			x += kEnemyHorizontalStep)
		{
			for (float sign : { -1.0f, 1.0f })
			{
				for (float y = 0.0f;
					y <= kEnemyVerticalLimit;
					y += kEnemyVerticalStep)
				{
					const ImVec2 candidate(
						base.x + sign * x,
						base.y + (upper ? -y : y)
					);

					bool valid = false;
					float score = 0.0f;
					examine(candidate, valid, score);

					// 左右移動には少し大きいコストを設定
					const float travel = x * 2.0f + y;

					if (valid && travel < bestTravel)
					{
						bestTravel = travel;
						result.position = candidate;
						found = true;
					}

					score += travel * 0.1f;

					if (score < fallbackScore)
					{
						fallbackScore = score;
						fallbackPosition = candidate;
					}
				}
			}
		}
	}

	// どの候補も解決できない場合
	if (!found)
	{
		result.hidden = true;
		result.position = fallbackPosition;
	}

	result.moved =
		std::abs(result.position.x - base.x) +
		std::abs(result.position.y - base.y) > 0.01f;

	if (result.moved)
	{
		result.link = MakeLink(
			result.position, width, height,
			upper, ball, radius
		);
	}

	// ホバー状態では配置予約を増やさない。通常配置だけを占有領域へ記録する。
	if (!result.hidden)
	{
		m_OccupiedFrames.push_back(
			MakeFrameRect(
				result.position, width, height
			)
		);

		if (result.moved)
		{
			m_OccupiedLinks.push_back(result.link);
		}
	}

	return result;
}
