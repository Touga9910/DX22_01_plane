#include "PlayerBall.h"
#include "GameShotPrediction.h"
#include "GameShotTraceView.h"
#include "GameRuntime.h"
#include "PlayerBallData.h"
#include "Renderer.h"
#include "TableConfig.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr int TraceGlow = 4;
    constexpr int TraceCore = 5;
    constexpr int TraceHighlight = 6;
}

// Preview caching and guide rendering, independent of runtime ball updates.
void PlayerBall::GeneratePreTrajectory(
	const DirectX::SimpleMath::Vector3& initialVelocity,
	std::uint64_t worldKey)
{
	const auto predictionStarted = std::chrono::steady_clock::now();
	const auto prediction =
		GameShotPrediction::Preview(*this, initialVelocity, true);
	const double predictionMilliseconds =
		std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - predictionStarted).count();
	GameRuntime::RecordTrajectoryPreviewPrediction(
		predictionMilliseconds,
		prediction.copiedBalls);
	if (GameRuntime::IsFrameTimingMeasurementActive())
		GameRuntime::RecordShotPredictionTiming(predictionMilliseconds);
	m_PrePositions.clear();
	const bool currentPowerReachesContact =
		prediction.reachedBlockingContact;
	auto pathDistance = [](const auto& path, std::size_t pointCount)
	{
		float distance = 0.0f;
		for (std::size_t index = 1; index < pointCount; ++index)
		{
			distance += (path[index] - path[index - 1]).Length();
		}
		return distance;
	};
	m_PreviewReachableDistance = pathDistance(
		prediction.path, prediction.path.size());

	const std::vector<Vector3>* displayedPath = &prediction.path;
	const auto* displayedContactGuide = &prediction.initialContactGuide;
	std::size_t displayedPoints = prediction.path.size();
	if (!currentPowerReachesContact && initialVelocity.LengthSquared() > 0.0001f)
	{
		Vector3 direction = initialVelocity;
		direction.Normalize();
		const bool reuseMaximumPowerPreview =
			m_MaximumPowerPreviewValid &&
			m_MaximumPowerPreviewWorldKey == worldKey &&
			fabs(m_MaximumPowerPreviewPower - m_MaxShotPower) <= 0.0001f &&
			direction == m_MaximumPowerPreviewDirection;
		if (reuseMaximumPowerPreview)
		{
			GameRuntime::RecordMaximumPowerPreviewCacheReuse();
		}
		else
		{
			const auto maximumPredictionStarted =
				std::chrono::steady_clock::now();
			auto maximumPowerPrediction = GameShotPrediction::Preview(
				*this,
				direction * m_MaxShotPower,
				true);
			GameRuntime::RecordTrajectoryPreviewPrediction(
				std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() -
					maximumPredictionStarted).count(),
				maximumPowerPrediction.copiedBalls);
			m_MaximumPowerPreviewPath =
				std::move(maximumPowerPrediction.path);
			m_MaximumPowerPreviewContactGuide =
				maximumPowerPrediction.initialContactGuide;
			m_MaximumPowerPreviewReachedBlockingContact =
				maximumPowerPrediction.reachedBlockingContact;
			m_MaximumPowerPreviewDistance = pathDistance(
				m_MaximumPowerPreviewPath,
				m_MaximumPowerPreviewPath.size());
			m_MaximumPowerPreviewWorldKey = worldKey;
			m_MaximumPowerPreviewDirection = direction;
			m_MaximumPowerPreviewPower = m_MaxShotPower;
			m_MaximumPowerPreviewValid = true;
		}
		if (m_MaximumPowerPreviewReachedBlockingContact ||
			m_MaximumPowerPreviewDistance > m_PreviewReachableDistance)
		{
			displayedPath = &m_MaximumPowerPreviewPath;
			displayedContactGuide = &m_MaximumPowerPreviewContactGuide;
			displayedPoints = m_MaximumPowerPreviewPath.size();
		}
	}
	for (std::size_t index = 0; index < displayedPoints; ++index)
	{
		m_PrePositions.push_back({ (*displayedPath)[index], 0, 1.0f });
	}
    // Keep the original aiming-only display: no reflected player trajectory,
    // just a short direction guide for the ball at the first blocking contact.
	// The guide and path must come from the same preview, including cache hits
	// while the current power cannot reach the displayed contact yet.
	m_PreviewHitBall = displayedContactGuide->hitBall;
	m_PreviewHitEnemy = displayedContactGuide->chainImpactCenter;
	m_PreviewGhostBallPosition = displayedContactGuide->playerPosition;
	m_PreviewHitBallPosition = displayedContactGuide->ballPosition;
	m_PreviewObjectBallDirection = displayedContactGuide->ballDirection;
	m_PreviewComplete = currentPowerReachesContact;
	m_LastPreviewWorldKey = worldKey;
}

void PlayerBall::InitTrajectoryVisualModel()
{
	// ★ PreviewMeshの初期化
	m_PreviewMesh.InitQuad();

	// ★ MeshRendererを初期化
	m_PreviewMeshRenderer.Init(m_PreviewMesh);

	auto addGuideMaterial = [this](const DirectX::SimpleMath::Color& color)
		{
			std::unique_ptr<Material> mat = std::make_unique<Material>();
			MATERIAL matData{};
			matData.Ambient = color;
			matData.Diffuse = color;
			matData.Specular = { 0.2f, 0.2f, 0.2f, 1.0f };
			matData.Emission = color;
			matData.Shiness = 4.0f;
			matData.TextureEnable = FALSE;
			mat->Create(matData);
			m_PreviewMaterials.push_back(std::move(mat));
		};

	addGuideMaterial({ 0.35f, 0.95f, 1.0f, 1.0f });   // 軌道予測線の色
	addGuideMaterial({ 1.0f, 1.0f, 1.0f, 0.9f });     // 接触時のプレイヤーボール半径の色
	addGuideMaterial({ 1.0f, 0.86f, 0.25f, 1.0f });   // 当たったボールが飛ぶ方向の色
	addGuideMaterial({ 0.78f, 0.34f, 1.0f, 1.0f });   // 連鎖衝撃の実寸範囲
	addGuideMaterial({ 0.52f, 0.24f, 0.85f, 0.18f }); // TraceGlow
	addGuideMaterial({ 0.73f, 0.49f, 1.0f, 0.58f });  // TraceCore
	addGuideMaterial({ 0.92f, 0.82f, 1.0f, 0.96f });  // TraceHighlight

	// サブセットを作成
	SUBSET subset;
	subset.MaterialIdx = 0;
	subset.IndexNum = static_cast<unsigned int>(m_PreviewMesh.GetIndices().size());
	subset.IndexBase = 0;
	subset.VertexBase = 0;
	m_PreviewSubsets.push_back(subset);
}

void PlayerBall::DrawTrajectoryLine()
{
	if (m_State == State::Simulation || m_PrePositions.size() < 2)
		return;

	// プレビュー用MeshRendererを準備
	Renderer::SetDepthEnable(true);
	m_PreviewMeshRenderer.BeforeDraw();

	const float Y_OFFSET = 0.8f;
	const float MAIN_LINE_THICKNESS = 0.32f;
	const float GHOST_LINE_THICKNESS = 0.2f;
	const float OBJECT_LINE_THICKNESS = 0.32f;
	const float OBJECT_LINE_LENGTH = 10.0f;

    constexpr float dashLength = 1.4f;
    constexpr float dashPeriod = 2.15f;
    const float startTrim = m_Ball->GetRadius() + MAIN_LINE_THICKNESS;
    float traveledDistance = 0.0f;
    for (std::size_t i = 0; i + 1 < m_PrePositions.size(); ++i)
    {
        const Vector3 start = m_PrePositions[i].position;
        const Vector3 delta = m_PrePositions[i + 1].position - start;
        const float length = delta.Length();
        const float segmentStart = traveledDistance;
        traveledDistance += length;
        if (length <= 0.0001f || traveledDistance <= startTrim) continue;

        const float visibleStart = (std::max)(segmentStart, startTrim);
        const float firstDash = startTrim +
            std::floor((visibleStart - startTrim) / dashPeriod) * dashPeriod;
        const Vector3 direction = delta / length;
        for (float dash = firstDash; dash < traveledDistance; dash += dashPeriod)
        {
            const float from = (std::max)(visibleStart, dash);
            const float to = (std::min)(traveledDistance, dash + dashLength);
            if (to <= from) continue;
            const bool directionOnly =
                !m_PreviewComplete && from >= m_PreviewReachableDistance - 0.001f;
            DrawGuideSegment(start + direction * (from - segmentStart),
                start + direction * (to - segmentStart),
                directionOnly ? MAIN_LINE_THICKNESS * 0.72f : MAIN_LINE_THICKNESS,
                Y_OFFSET, 0);
        }
    }

	if (m_PreviewHitBall)
	{
		DrawGuideCircle(
			m_PreviewGhostBallPosition,
			m_Ball->GetRadius(),
			GHOST_LINE_THICKNESS,
			Y_OFFSET + 0.03f,
			1);

		if (m_PreviewObjectBallDirection.LengthSquared() > 0.0001f)
		{
			Vector3 objectLineEnd =
				m_PreviewHitBallPosition +
				m_PreviewObjectBallDirection * OBJECT_LINE_LENGTH;

			DrawGuideSegment(
				m_PreviewHitBallPosition,
				objectLineEnd,
				OBJECT_LINE_THICKNESS,
				Y_OFFSET + 0.06f,
				2);
		}

		const float chainRadius = m_Ball->GetStatus().chainImpactRadius;
		if (m_PreviewHitEnemy && chainRadius > 0.0f)
		{
			DrawGuideCircle(
				m_PreviewHitBallPosition,
				chainRadius,
				0.24f,
				Y_OFFSET + 0.09f,
				3);
		}
	}

	Renderer::SetDepthEnable(true);
}

void PlayerBall::DrawPierceTraces()
{
    const auto& traces = GameShotTraceView::Current().traces;
    if (traces.empty()) return;

    const std::size_t firstHighlighted = traces.size() - GameShotTraceView::SelectedMinionTraceCount();
    constexpr float traceHeight = 0.05f;
    constexpr double pulsePeriod = 1.25;
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const float pulse = static_cast<float>(std::fmod(seconds, pulsePeriod) / pulsePeriod);

    Renderer::SetDepthEnable(true);
    m_PreviewMeshRenderer.BeforeDraw();
    for (std::size_t index = 0; index < traces.size(); ++index)
    {
        Vector3 start = traces[index].start;
        Vector3 end = traces[index].end;
        start.y = end.y = TableConfig::FIELD_HEIGHT;
        const bool highlighted = index >= firstHighlighted;

        DrawGuideSegment(start, end, 0.40f, traceHeight, TraceGlow);
        DrawGuideSegment(start, end, highlighted ? 0.18f : 0.13f,
            traceHeight + 0.01f, TraceCore);
        DrawGuideCircle(start, 0.58f, 0.12f, traceHeight + 0.02f,
            highlighted ? TraceHighlight : TraceCore, 16);

        if (highlighted)
        {
            // Clip both ends so the pulse travels from the launch ring to the trace tip.
            const Vector3 direction = end - start;
            const float head = (std::min)(1.0f, pulse * 1.14f);
            const float tail = (std::max)(0.0f, pulse * 1.14f - 0.14f);
            DrawGuideSegment(start + direction * tail, start + direction * head,
                0.24f, traceHeight + 0.03f, TraceHighlight);
        }
    }
}

void PlayerBall::DrawGuideSegment(
	const DirectX::SimpleMath::Vector3& start,
	const DirectX::SimpleMath::Vector3& end,
	float thickness,
	float yOffset,
	int materialIndex)
{
	if (materialIndex < 0 || materialIndex >= static_cast<int>(m_PreviewMaterials.size()))
	{
		return;
	}

	Vector3 startPos = start;                       // 描画開始位置
	Vector3 endPos = end;                         // 描画終了位置
	startPos.y += yOffset;                          // 床と重ならないように少し上げる
	endPos.y += yOffset;                            // 床と重ならないように少し上げる

	Vector3 forward = endPos - startPos;            // 線分の向き
	float distance = forward.Length();             // 線分の長さ
	if (distance <= 0.0001f)
	{
		return;
	}

	forward /= distance;                            // CreateWorldに渡すため方向だけにする

	Vector3 midPos = (startPos + endPos) * 0.5f;    // 線分の中央にメッシュを配置する
	Matrix s = Matrix::CreateScale(thickness, thickness, distance);
	Matrix rt = Matrix::CreateWorld(midPos, forward, Vector3::Up);
	Matrix worldmtx = s * rt;

	Renderer::SetWorldMatrix(&worldmtx);
	m_PreviewMaterials[materialIndex]->SetGPU();
	m_PreviewMeshRenderer.DrawSubset(
		m_PreviewSubsets[0].IndexNum,
		m_PreviewSubsets[0].IndexBase,
		m_PreviewSubsets[0].VertexBase);
}

void PlayerBall::DrawGuideCircle(
	const DirectX::SimpleMath::Vector3& center,
	float radius,
	float thickness,
	float yOffset,
	int materialIndex,
	int segmentCount)
{
	for (int i = 0; i < segmentCount; ++i)
	{
		float angle0 = DirectX::XM_2PI * static_cast<float>(i) / static_cast<float>(segmentCount);
		float angle1 = DirectX::XM_2PI * static_cast<float>(i + 1) / static_cast<float>(segmentCount);

		Vector3 p0 = center + Vector3(cos(angle0) * radius, 0.0f, sin(angle0) * radius);
		Vector3 p1 = center + Vector3(cos(angle1) * radius, 0.0f, sin(angle1) * radius);
		DrawGuideSegment(p0, p1, thickness, yOffset, materialIndex);
	}
}

