#include "GamePresentationPrimitives.h"
#include "Application.h"
#include "Camera.h"
#include "GameEvent.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cmath>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

namespace GamePresentationPrimitives
{
/// <summary>
/// UTF-8の文字列を、描画APIが受け取る文字ポインターとして返す。
/// </summary>
const char* PresentationUtf8(const char8_t* text) noexcept
{
	return reinterpret_cast<const char*>(text);
}

/// <summary>
/// 色成分を保ったまま、不透明度を指定された割合で置き換える。
/// </summary>
ImU32 WithAlpha(ImU32 color, float alpha)
{
	const unsigned int clamped = static_cast<unsigned int>(
		std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
	return (color & 0x00ffffffu) | (clamped << 24u);
}

/// <summary>
/// 能力軸の種別を画面に表示するラベルに変換する。
/// </summary>
const char* AxisResourceLabel(AxisResourceKind axis)
{
	switch (axis)
	{
	case AxisResourceKind::Heavy: return PresentationUtf8(u8"重量");
	case AxisResourceKind::Pierce: return PresentationUtf8(u8"貫通");
	case AxisResourceKind::Reflection: return PresentationUtf8(u8"反射");
	case AxisResourceKind::Anchor: return "Anchor";
	default: return PresentationUtf8(u8"軸");
	}
}

/// <summary>
/// 能力軸に対応する演出の色を返す。
/// </summary>
ImU32 AxisResourceColor(AxisResourceKind axis)
{
	switch (axis)
	{
	case AxisResourceKind::Heavy: return IM_COL32(255, 146, 61, 255);
	case AxisResourceKind::Pierce: return IM_COL32(184, 92, 255, 255);
	case AxisResourceKind::Reflection: return IM_COL32(255, 221, 58, 255);
	case AxisResourceKind::Anchor: return IM_COL32(75, 225, 183, 255);
	default: return IM_COL32_WHITE;
	}
}

/// <summary>
/// 能力軸のリソースが変化した理由を表示用ラベルに変換する。
/// </summary>
const char* AxisResourceReasonLabel(AxisResourceChangeReason reason)
{
	switch (reason)
	{
	case AxisResourceChangeReason::AbilityCost: return PresentationUtf8(u8"能力コスト");
	case AxisResourceChangeReason::Collision: return PresentationUtf8(u8"衝突");
	case AxisResourceChangeReason::TravelDistance: return PresentationUtf8(u8"移動連鎖");
	case AxisResourceChangeReason::PierceHit: return PresentationUtf8(u8"貫通命中");
	case AxisResourceChangeReason::WallReflection: return PresentationUtf8(u8"壁反射");
	case AxisResourceChangeReason::ShieldConversion: return PresentationUtf8(u8"盾変換");
	case AxisResourceChangeReason::DefeatConversion: return PresentationUtf8(u8"撃破回収");
	case AxisResourceChangeReason::EndOfShot: return PresentationUtf8(u8"ショット終了");
	case AxisResourceChangeReason::Finisher: return "FINISHER";
	default: return "";
	}
}

/// <summary>
/// ワールド座標をカメラとビューポートに合わせて画面座標に投影する。
/// </summary>
bool TryProjectToScreen(
	const Vector3& worldPosition,
	ImVec2& screenPosition)
{
	const float width = static_cast<float>(Application::GetWidth());
	const float height = static_cast<float>(Application::GetHeight());
	if (width <= 0.0f || height <= 0.0f)
	{
		return false;
	}

	const Matrix projection = DirectX::XMMatrixPerspectiveFovLH(
		DirectX::XMConvertToRadians(45.0f),
		width / height,
		1.0f,
		1000.0f);
	const Matrix view = Camera::GetInstance().GetViewMatrix();
	const DirectX::XMVECTOR projected = DirectX::XMVector3Project(
		worldPosition,
		0.0f,
		0.0f,
		width,
		height,
		0.0f,
		1.0f,
		projection,
		view,
		Matrix::Identity);
	Vector3 result;
	DirectX::XMStoreFloat3(&result, projected);
	if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
		result.z < 0.0f || result.z > 1.0f)
	{
		return false;
	}
	const ImVec2 origin = ImGui::GetMainViewport()->Pos;
	screenPosition = ImVec2(origin.x + result.x, origin.y + result.y);
	return true;
}
}
