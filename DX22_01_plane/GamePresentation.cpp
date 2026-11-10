#include "GamePresentation.h"

#pragma execution_character_set("utf-8")

#include "Application.h"
#include "CollisionPresentationRules.h"
#include "GameObject.h"
#include "BattleScene.h"
#include "Camera.h"
#include "Game.h"
#include "GameEvent.h"
#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameSettingsRuntime.h"
#include "GameRewardState.h"
#include "GameBallSelectionState.h"
#include "GamePauseState.h"
#include "PlayerBallData.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "GameDebugController.h"
#include "GameUi.h"
#include "EnemyBall.h"
#include "EnemyAttackComponent.h"
#include "BreakBall.h"
#include "PlayerBall.h"
#include "PlayerBallText.h"
#include "Renderer.h"
#include "input.h"
#include "imgui/imgui.h"
#include "json/json.hpp"
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <wrl/client.h>

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;
using nlohmann::json;

namespace
{
	constexpr const char* kTutorialProgressPath =
		"runtime/tutorial_progress.json";
	constexpr const char* kBalanceReportPath =
		"logs/balance/balance_report.json";

	const char* PresentationUtf8(const char8_t* text) noexcept
	{
		return reinterpret_cast<const char*>(text);
	}

	ImU32 WithAlpha(ImU32 color, float alpha)
	{
		const unsigned int clamped = static_cast<unsigned int>(
			std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
		return (color & 0x00ffffffu) | (clamped << 24u);
	}

	struct CombatIconTexture
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		int width = 0;
		int height = 0;

		bool IsValid() const noexcept
		{
			return view != nullptr && width > 0 && height > 0;
		}
	};

	enum class CombatIconKind : std::size_t
	{
		BuffAttack,
		BuffDefense,
		IntentAttack,
		IntentChargeStart,
		IntentChargeAttack,
		ResourceHeavy,
		ResourcePierce,
		ResourceReflect,
		ResourceAnchor,
		ResourceEnemyAnchor,
		EnemySwarm,
		EnemyResonance,
		EnemyBrittle,
		Count,
	};

	class CombatIconResources final
	{
	public:
		void Load()
		{
			if (m_LoadAttempted)
				return;
			m_LoadAttempted = true;
			static constexpr std::array<const char*, static_cast<std::size_t>(CombatIconKind::Count)> paths = {
				"assets/UI/Combat/buff_attack_sword.png",
				"assets/UI/Combat/buff_defense_shield.png",
				"assets/UI/Combat/intent_attack_sword.png",
				"assets/UI/Combat/intent_charge_start.png",
				"assets/UI/Combat/intent_charge_hammer.png",
				"assets/UI/Combat/resource_heavy.png",
				"assets/UI/Combat/resource_pierce.png",
				"assets/UI/Combat/resource_reflect.png",
				"assets/UI/Combat/resource_anchor.png",
				"assets/UI/Combat/resource_enemy_anchor.png",
				"assets/UI/Combat/enemy_swarm_ring_swords.png",
				"assets/UI/Combat/enemy_resonance_ring_shield.png",
				"assets/UI/Combat/enemy_brittle_cracked_ball.png",
			};
			for (std::size_t index = 0; index < paths.size(); ++index)
			{
				LoadTexture(paths[index], m_Textures[index]);
			}
		}

		const CombatIconTexture* Get(CombatIconKind kind) const noexcept
		{
			const CombatIconTexture& texture = m_Textures[static_cast<std::size_t>(kind)];
			return texture.IsValid() ? &texture : nullptr;
		}

	private:
		static bool LoadTexture(const char* path, CombatIconTexture& destination)
		{
			int channels = 0;
			unsigned char* pixels = stbi_load(path, &destination.width, &destination.height, &channels, STBI_rgb_alpha);
			if (pixels == nullptr)
			{
				std::cerr << "Combat UI icon load failed: " << path << '\n';
				destination = {};
				return false;
			}

			ID3D11Device* device = Renderer::GetDevice();
			if (device == nullptr)
			{
				stbi_image_free(pixels);
				destination = {};
				return false;
			}

			D3D11_TEXTURE2D_DESC description{};
			description.Width = static_cast<UINT>(destination.width);
			description.Height = static_cast<UINT>(destination.height);
			description.MipLevels = 1;
			description.ArraySize = 1;
			description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.SampleDesc.Count = 1;
			description.Usage = D3D11_USAGE_IMMUTABLE;
			description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			D3D11_SUBRESOURCE_DATA initialData{};
			initialData.pSysMem = pixels;
			initialData.SysMemPitch = description.Width * 4;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			HRESULT result = device->CreateTexture2D(&description, &initialData, texture.GetAddressOf());
			if (SUCCEEDED(result))
			{
				result = device->CreateShaderResourceView(texture.Get(), nullptr, destination.view.GetAddressOf());
			}
			stbi_image_free(pixels);
			if (FAILED(result))
			{
				std::cerr << "Combat UI icon GPU upload failed: " << path << '\n';
				destination = {};
				return false;
			}
			return true;
		}

		bool m_LoadAttempted = false;
		std::array<CombatIconTexture, static_cast<std::size_t>(CombatIconKind::Count)> m_Textures{};
	};

	CombatIconResources& GetCombatIconResources()
	{
		static CombatIconResources resources;
		return resources;
	}

	ImTextureRef MakeCombatTextureReference(const CombatIconTexture& texture)
	{
		return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.view.Get())));
	}

	void DrawCombatIcon(ImDrawList* drawList, const CombatIconTexture* texture, const ImVec2& minimum,
		const ImVec2& maximum, ImU32 tint = IM_COL32_WHITE)
	{
		if (drawList == nullptr || texture == nullptr || !texture->IsValid())
			return;
		const float areaWidth = (std::max)(1.0f, maximum.x - minimum.x);
		const float areaHeight = (std::max)(1.0f, maximum.y - minimum.y);
		const float scale = (std::min)(areaWidth / static_cast<float>(texture->width),
			areaHeight / static_cast<float>(texture->height));
		const ImVec2 size(static_cast<float>(texture->width) * scale, static_cast<float>(texture->height) * scale);
		const ImVec2 offset((areaWidth - size.x) * 0.5f, (areaHeight - size.y) * 0.5f);
		drawList->AddImage(MakeCombatTextureReference(*texture), ImVec2(minimum.x + offset.x, minimum.y + offset.y),
			ImVec2(minimum.x + offset.x + size.x, minimum.y + offset.y + size.y), ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f), tint);
	}

	void DrawOutlinedValue(ImDrawList* drawList, const ImVec2& position, const char* value,
		ImU32 color = IM_COL32(255, 255, 255, 255), float fontScale = 0.76f)
	{
		if (drawList == nullptr || value == nullptr || value[0] == '\0')
			return;
		ImFont* font = ImGui::GetFont();
		const float size = ImGui::GetFontSize() * fontScale;
		drawList->AddText(font, size, ImVec2(position.x + 2.0f, position.y + 2.0f), IM_COL32(0, 0, 0, 190), value);
		for (const ImVec2 offset : {ImVec2(-1.0f, 0.0f), ImVec2(1.0f, 0.0f), ImVec2(0.0f, -1.0f), ImVec2(0.0f, 1.0f)})
		{
			drawList->AddText(font, size, ImVec2(position.x + offset.x, position.y + offset.y), IM_COL32(0, 0, 0, 255),
				value);
		}
		drawList->AddText(font, size, position, color, value);
	}

	constexpr float kCombatIconSize = 26.0f;
	constexpr float kCombatIconSpacing = 3.0f;

	ImU32 EnemyHpColor(float hpRatio, float opacity)
	{
		if (hpRatio <= 0.30f)
		{
			return WithAlpha(IM_COL32(255, 74, 72, 255), opacity);
		}
		if (hpRatio <= 0.50f)
		{
			return WithAlpha(IM_COL32(255, 207, 64, 255), opacity);
		}
		return WithAlpha(IM_COL32(72, 225, 108, 255), opacity);
	}

	void DrawArc(
		ImDrawList* drawList,
		const ImVec2& center,
		float radius,
		float startAngle,
		float endAngle,
		ImU32 color,
		float thickness)
	{
		if (drawList == nullptr || endAngle <= startAngle || thickness <= 0.0f)
		{
			return;
		}
		drawList->PathArcTo(
			center,
			radius,
			startAngle,
			endAngle,
			40);
		drawList->PathStroke(color, 0, thickness);
	}

	void DrawEnemyHpRing(
		const EnemyHpDisplaySettings& settings,
		const ImVec2& projectedCenter,
		float projectedRadius,
		float hpRatio,
		float trailingRatio,
		float damageHold)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		// World-linked information stays below interactive windows. In particular,
		// an expanded card must never be obscured by an enemy ring.
		ImDrawList* drawList = ImGui::GetBackgroundDrawList(viewport);
		(void)settings.ringDrawLayer;
		const float visualScale = std::clamp(
			projectedRadius / 28.0f,
			0.55f,
			2.25f);
		const ImVec2 center(
			projectedCenter.x + settings.ringOffsetX * visualScale,
			projectedCenter.y + settings.ringOffsetY * visualScale);
		const float radius = std::clamp(
			projectedRadius * settings.ringDiameterScale,
			9.0f,
			64.0f);
		const float thickness = std::clamp(
			settings.ringThickness * std::sqrt(visualScale),
			1.5f,
			10.0f);
		const float opacity = std::clamp(settings.ringOpacity, 0.35f, 1.0f);
		constexpr float startAngle = -3.92699082f;
		constexpr float sweepAngle = 4.71238898f;
		const float endAngle = startAngle + sweepAngle;

		DrawArc(
			drawList,
			center,
			radius,
			startAngle,
			endAngle,
			WithAlpha(IM_COL32(5, 9, 14, 255), opacity * 0.86f),
			thickness + 2.4f);
		if (trailingRatio > 0.0f)
		{
			DrawArc(
				drawList,
				center,
				radius,
				startAngle,
				startAngle + sweepAngle * trailingRatio,
				WithAlpha(IM_COL32(255, 151, 54, 255), opacity * 0.94f),
				thickness);
		}
		if (hpRatio > 0.0f)
		{
			DrawArc(
				drawList,
				center,
				radius,
				startAngle,
				startAngle + sweepAngle * hpRatio,
				EnemyHpColor(hpRatio, opacity),
				thickness);
		}

		const float hitPulse = std::clamp(damageHold / 0.28f, 0.0f, 1.0f);
		if (hitPulse > 0.0f)
		{
			DrawArc(
				drawList,
				center,
				radius + thickness * 0.85f + hitPulse * 1.5f,
				startAngle,
				endAngle,
				WithAlpha(IM_COL32(255, 255, 255, 255),
					opacity * hitPulse * 0.72f),
				1.2f + hitPulse * 1.3f);
		}
	}

	const char* JudgementLabel(const std::string& judgement)
	{
		if (judgement == "balanced") return "目標範囲内";
		if (judgement == "too_easy") return "易しすぎる傾向";
		if (judgement == "too_difficult") return "難しすぎる傾向";
		if (judgement == "insufficient_data") return "データ不足";
		return "評価待ち";
	}

	ImVec4 JudgementColor(const std::string& judgement)
	{
		if (judgement == "balanced") return ImVec4(0.25f, 0.85f, 0.48f, 1.0f);
		if (judgement == "too_easy") return ImVec4(0.30f, 0.72f, 1.0f, 1.0f);
		if (judgement == "too_difficult") return ImVec4(1.0f, 0.43f, 0.30f, 1.0f);
		return ImVec4(0.95f, 0.78f, 0.28f, 1.0f);
	}

	const char* StatusEffectName(StatusEffectType type)
	{
		switch (type)
		{
		case StatusEffectType::AttackUp: return "攻撃上昇";
		case StatusEffectType::AttackDown: return "攻撃低下";
		case StatusEffectType::DefenseUp: return "防御上昇";
		case StatusEffectType::DefenseDown: return "防御低下";
		default: return "不明な状態効果";
		}
	}

	const char* EnemyActionBadgeLabel(EnemyActionType action)
	{
		switch (action)
		{
		case EnemyActionType::NormalAttack: return PresentationUtf8(u8"攻");
		case EnemyActionType::ChargeStart: return PresentationUtf8(u8"溜");
		case EnemyActionType::ChargeAttack: return PresentationUtf8(u8"強");
		default: return PresentationUtf8(u8"待");
		}
	}

	const char* EnemyActionDisplayName(EnemyActionType action)
	{
		switch (action)
		{
		case EnemyActionType::NormalAttack: return PresentationUtf8(u8"通常攻撃");
		case EnemyActionType::ChargeStart: return PresentationUtf8(u8"Charge開始");
		case EnemyActionType::ChargeAttack: return PresentationUtf8(u8"Charge攻撃");
		default: return PresentationUtf8(u8"待機");
		}
	}

	ImU32 EnemyActionColor(EnemyActionType action)
	{
		switch (action)
		{
		case EnemyActionType::NormalAttack: return IM_COL32(255, 92, 76, 255);
		case EnemyActionType::ChargeStart: return IM_COL32(255, 204, 64, 255);
		case EnemyActionType::ChargeAttack: return IM_COL32(210, 84, 255, 255);
		default: return IM_COL32(145, 155, 170, 255);
		}
	}

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

	bool RectanglesOverlap(const ImVec4& first, const ImVec4& second)
	{
		return first.x < second.z && first.z > second.x &&
			first.y < second.w && first.w > second.y;
	}

	void DrawEnemyActionBadge(ImDrawList* drawList, const ImVec2& minimum, EnemyActionType action)
	{
		const ImVec2 maximum(minimum.x + kCombatIconSize, minimum.y + kCombatIconSize);
		const CombatIconTexture* texture = nullptr;
		switch (action)
		{
		case EnemyActionType::NormalAttack:
			texture = GetCombatIconResources().Get(CombatIconKind::IntentAttack);
			break;
		case EnemyActionType::ChargeStart:
			texture = GetCombatIconResources().Get(CombatIconKind::IntentChargeStart);
			break;
		case EnemyActionType::ChargeAttack:
			texture = GetCombatIconResources().Get(CombatIconKind::IntentChargeAttack);
			break;
		default:
			break;
		}
		if (texture != nullptr)
		{
			DrawCombatIcon(drawList, texture, minimum, maximum);
		}
		else if (action != EnemyActionType::NormalAttack && action != EnemyActionType::ChargeStart &&
			action != EnemyActionType::ChargeAttack)
		{
			const char* label = PresentationUtf8(u8"…");
			const ImVec2 textSize = ImGui::CalcTextSize(label);
			drawList->AddText(ImVec2(minimum.x + (kCombatIconSize - textSize.x) * 0.5f,
				minimum.y + (kCombatIconSize - textSize.y) * 0.5f),
				IM_COL32(145, 155, 170, 255), label);
		}
	}

	void DrawStatusIcon(ImDrawList* drawList, const ImVec2& minimum, StatusEffectType type, int magnitude)
	{
		const ImVec2 maximum(minimum.x + kCombatIconSize, minimum.y + kCombatIconSize);
		const CombatIconKind icon =
			IsAttackStatusEffect(type) ? CombatIconKind::BuffAttack : CombatIconKind::BuffDefense;
		DrawCombatIcon(drawList, GetCombatIconResources().Get(icon), minimum, maximum);
		char value[16]{};
		const int signedMagnitude = IsPositiveStatusEffect(type) ? magnitude : -magnitude;
		sprintf_s(value, "%+d", signedMagnitude);
		const float fontSize = ImGui::GetFontSize() * 0.72f;
		const ImVec2 valueSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, value);
		DrawOutlinedValue(drawList, ImVec2(maximum.x - valueSize.x, maximum.y - valueSize.y), value);
	}

	struct ResourceBadge
	{
		CombatIconKind icon = CombatIconKind::ResourceHeavy;
		const char* title = "";
		const char* description = "";
		int value = 0;
		ImU32 color = IM_COL32_WHITE;
	};

	constexpr float kResourceBadgeWidth = kCombatIconSize;
	constexpr float kResourceBadgeHeight = kCombatIconSize;

	void DrawResourceBadge(ImDrawList* drawList, const ImVec2& minimum, const ResourceBadge& badge)
	{
		const ImVec2 maximum(minimum.x + kResourceBadgeWidth, minimum.y + kResourceBadgeHeight);
		DrawCombatIcon(drawList, GetCombatIconResources().Get(badge.icon), minimum, maximum);
		char text[32]{};
		sprintf_s(text, "%d", badge.value);
		const float fontSize = ImGui::GetFontSize() * 0.72f;
		const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
		DrawOutlinedValue(drawList, ImVec2(maximum.x - textSize.x, maximum.y - textSize.y), text,
			IM_COL32(255, 255, 255, 255));
	}

	bool IsMouseInside(const ImVec2& minimum, const ImVec2& maximum)
	{
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		return mouse.x >= minimum.x && mouse.x < maximum.x &&
			mouse.y >= minimum.y && mouse.y < maximum.y;
	}

	bool DrawPileButton(
		const char* id,
		const char* label,
		int count,
		ImU32 accent,
		const ImVec2& size)
	{
		const ImVec2 position = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(id, size);
		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 background = hovered
			? IM_COL32(26, 37, 51, 246)
			: IM_COL32(12, 20, 30, 232);
		drawList->AddRectFilled(
			position,
			ImVec2(position.x + size.x, position.y + size.y),
			background,
			10.0f);
		drawList->AddRect(
			position,
			ImVec2(position.x + size.x, position.y + size.y),
			hovered ? accent : IM_COL32(83, 99, 120, 220),
			10.0f,
			0,
			hovered ? 2.0f : 1.0f);

		for (int layer = 2; layer >= 0; --layer)
		{
			const float offset = static_cast<float>(layer) * 3.0f;
			drawList->AddRectFilled(
				ImVec2(position.x + 10.0f + offset, position.y + 12.0f - offset),
				ImVec2(position.x + 43.0f + offset, position.y + 61.0f - offset),
				layer == 0 ? IM_COL32(25, 34, 46, 255) : IM_COL32(48, 59, 73, 255),
				4.0f);
			drawList->AddRect(
				ImVec2(position.x + 10.0f + offset, position.y + 12.0f - offset),
				ImVec2(position.x + 43.0f + offset, position.y + 61.0f - offset),
				layer == 0 ? accent : IM_COL32(96, 108, 126, 220),
				4.0f);
		}

		drawList->AddText(
			ImVec2(position.x + 52.0f, position.y + 13.0f),
			IM_COL32(210, 220, 233, 255),
			label);
		char countText[24]{};
		sprintf_s(countText, PresentationUtf8(u8"%d枚"), count);
		drawList->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize() * 1.15f,
			ImVec2(position.x + 52.0f, position.y + 36.0f),
			IM_COL32(255, 255, 255, 255),
			countText);
		return ImGui::IsItemClicked(ImGuiMouseButton_Left);
	}

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

	float ReadNumber(const json& value, const char* key, float fallback = 0.0f)
	{
		if (!value.is_object() || !value.contains(key) ||
			!value[key].is_number())
		{
			return fallback;
		}
		return value[key].get<float>();
	}

	std::string ReadString(
		const json& value,
		const char* key,
		const std::string& fallback = std::string())
	{
		if (!value.is_object() || !value.contains(key) ||
			!value[key].is_string())
		{
			return fallback;
		}
		return value[key].get<std::string>();
	}
}

void GamePresentation::Initialize(Game& game)
{
	// Upload every combat HUD icon once after the renderer is available. Draw
	// calls only reuse the cached shader-resource views.
	GetCombatIconResources().Load();
	std::ifstream tutorialFile(kTutorialProgressPath);
	if (tutorialFile)
	{
		try
		{
			json progress;
			tutorialFile >> progress;
			m_TutorialCompleted = progress.value("completed", false);
		}
		catch (const json::exception&)
		{
			m_TutorialCompleted = false;
		}
	}
	m_PreviousBattleState = static_cast<int>(game.GetBattleState());
	LoadDashboard();
}

void GamePresentation::ClearHitReactions()
{
	for (HitReaction& reaction : m_HitReactions)
	{
		if (reaction.active && reaction.targetKey != 0)
		{
			EnemyBall* target = reinterpret_cast<EnemyBall*>(reaction.targetKey);
			if (GameRuntime::World().Contains(target))
				target->SetPresentationHitFlash(0.0f);
		}
		reaction = {};
	}
	m_CombatReactionDiagnostics.activeHitFlashes = 0;
}

void GamePresentation::BeginHitReaction(
	std::uintptr_t targetKey,
	int damage,
	bool defeated)
{
	++m_CombatReactionDiagnostics.hitFlashRequests;
	if (damage <= 0 && !defeated)
	{
		++m_CombatReactionDiagnostics.zeroDamageFlashesSuppressed;
		return;
	}
	Game* game = Game::GetInstance();
	EnemyBall* target = reinterpret_cast<EnemyBall*>(targetKey);
	if (game == nullptr || !game->IsEnemyHitFlashEnabled() ||
		targetKey == 0 || !GameRuntime::World().Contains(target))
	{
		++m_CombatReactionDiagnostics.hitFlashesSkipped;
		return;
	}

	CombatPresentationRules::HitFlash flash =
		CombatPresentationRules::FlashForDamage((std::max)(1, damage));
	if (defeated)
	{
		flash.duration = (std::max)(flash.duration, 0.18f);
		flash.intensity = 1.0f;
	}

	HitReaction* slot = nullptr;
	for (HitReaction& reaction : m_HitReactions)
	{
		if (reaction.active && reaction.targetKey == targetKey)
		{
			slot = &reaction;
			break;
		}
		if (slot == nullptr && !reaction.active) slot = &reaction;
	}
	if (slot == nullptr)
	{
		slot = &*std::min_element(
			m_HitReactions.begin(),
			m_HitReactions.end(),
			[](const HitReaction& left, const HitReaction& right)
			{
				return left.lifetime < right.lifetime;
			});
		EnemyBall* replaced = reinterpret_cast<EnemyBall*>(slot->targetKey);
		if (slot->targetKey != 0 && GameRuntime::World().Contains(replaced))
			replaced->SetPresentationHitFlash(0.0f);
	}

	*slot = HitReaction{
		targetKey,
		flash.duration,
		flash.duration,
		flash.intensity,
		true,
	};
	target->SetPresentationHitFlash(flash.intensity);
	++m_CombatReactionDiagnostics.hitFlashesTriggered;
	const int active = static_cast<int>(std::count_if(
		m_HitReactions.begin(),
		m_HitReactions.end(),
		[](const HitReaction& reaction) { return reaction.active; }));
	m_CombatReactionDiagnostics.activeHitFlashes = active;
	m_CombatReactionDiagnostics.maximumActiveHitFlashes = (std::max)(
		m_CombatReactionDiagnostics.maximumActiveHitFlashes,
		active);
}

void GamePresentation::UpdateHitReactions(Game& game, float deltaSeconds)
{
	const bool enabled = game.IsEnemyHitFlashEnabled() && IsBattleScene(game);
	int active = 0;
	for (HitReaction& reaction : m_HitReactions)
	{
		if (!reaction.active) continue;
		EnemyBall* target = reinterpret_cast<EnemyBall*>(reaction.targetKey);
		if (!enabled || reaction.targetKey == 0 ||
			!GameRuntime::World().Contains(target))
		{
			if (reaction.targetKey != 0 &&
				GameRuntime::World().Contains(target))
				target->SetPresentationHitFlash(0.0f);
			reaction = {};
			continue;
		}
		reaction.lifetime -= deltaSeconds;
		if (reaction.lifetime <= 0.0f)
		{
			target->SetPresentationHitFlash(0.0f);
			reaction = {};
			continue;
		}
		const float life = std::clamp(
			reaction.lifetime / reaction.totalLifetime,
			0.0f,
			1.0f);
		target->SetPresentationHitFlash(
			reaction.intensity * life * life);
		++active;
	}
	m_CombatReactionDiagnostics.activeHitFlashes = active;
}

void GamePresentation::UpdateEnemyHpVisuals(float deltaSeconds)
{
	std::vector<std::uintptr_t> liveKeys;
	for (const EnemyBall* enemy :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed())
		{
			continue;
		}
		const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(enemy);
		liveKeys.push_back(key);
		auto visual = std::find_if(
			m_EnemyHpVisuals.begin(),
			m_EnemyHpVisuals.end(),
			[key](const EnemyHpVisual& value)
			{
				return value.targetKey == key;
			});
		const int hp = (std::max)(0, enemy->GetHP());
		const int maximumHp = (std::max)(1, enemy->GetMaxHP());
		if (visual == m_EnemyHpVisuals.end())
		{
			m_EnemyHpVisuals.push_back({
				key,
				hp,
				maximumHp,
				static_cast<float>(hp),
				0.0f,
				});
			continue;
		}
		if (hp < visual->currentHp)
		{
			visual->trailingHp = (std::max)(
				visual->trailingHp,
				static_cast<float>(visual->currentHp));
			visual->damageHold = 0.28f;
		}
		else if (hp > visual->currentHp)
		{
			visual->trailingHp = static_cast<float>(hp);
		}
		visual->currentHp = hp;
		visual->maximumHp = maximumHp;
		visual->damageHold = (std::max)(
			0.0f,
			visual->damageHold - deltaSeconds);
		if (visual->damageHold <= 0.0f)
		{
			const float approach = (std::max)(
				18.0f,
				static_cast<float>(maximumHp) * 2.5f) * deltaSeconds;
			visual->trailingHp = (std::max)(
				static_cast<float>(hp),
				visual->trailingHp - approach);
		}
	}
	std::erase_if(
		m_EnemyHpVisuals,
		[&liveKeys](const EnemyHpVisual& visual)
		{
			return std::find(liveKeys.begin(), liveKeys.end(), visual.targetKey) ==
				liveKeys.end();
		});
}

bool GamePresentation::HasTrackedDefeat(std::uintptr_t targetKey) const
{
	if (targetKey == 0) return false;
	return std::find(
		m_DefeatKeys.begin(),
		m_DefeatKeys.begin() + m_TrackedDefeatCount,
		targetKey) != m_DefeatKeys.begin() + m_TrackedDefeatCount;
}

bool GamePresentation::BeginDefeatReaction(
	const Vector3& worldPosition,
	std::uintptr_t targetKey)
{
	++m_CombatReactionDiagnostics.defeatRequests;
	if (HasTrackedDefeat(targetKey))
	{
		++m_CombatReactionDiagnostics.duplicateDefeatsSuppressed;
		return false;
	}
	if (targetKey != 0 &&
		m_TrackedDefeatCount < static_cast<int>(m_DefeatKeys.size()))
	{
		m_DefeatKeys[m_TrackedDefeatCount++] = targetKey;
	}
	++m_CurrentShotDefeatCount;
	++m_CombatReactionDiagnostics.uniqueDefeats;

	Game* game = Game::GetInstance();
	if (game != nullptr && game->IsDefeatVfxEnabled())
	{
		CollisionVfxSystem::SpawnRequest request;
		request.worldPosition = worldPosition;
		request.collisionAxis = Vector3::UnitX;
		request.strength = CollisionPresentationRules::Strength::Strong;
		request.chainTier = CollisionPresentationRules::ChainTier(
			m_HeavyPhysicsChainCount);
		request.priority = 4;
		request.density = game->GetDecorativeRingAmount();
		request.frame = m_PresentationFrame;
		request.defeated = true;
		m_CollisionVfx.Spawn(request);
	}

	if (game != nullptr && game->IsMultiDefeatEffectsEnabled() &&
		m_CurrentShotDefeatCount >= 2)
	{
		// Every additional defeat restarts the same readable pop/hold/fade cycle.
		m_MultiDefeatDisplayTotalLifetime = 2.17f;
		m_MultiDefeatDisplayLifetime = m_MultiDefeatDisplayTotalLifetime;
		++m_CombatReactionDiagnostics.multiDefeatDisplays;
	}
	return true;
}

void GamePresentation::Update(Game& game, float deltaSeconds)
{
	const float presentationDelta = std::clamp(deltaSeconds, 0.0f, 0.10f);
	m_PresentationTimeSeconds += presentationDelta;
	if (Input::GetKeyTrigger(VK_F1))
	{
		if (m_TutorialActive)
		{
			m_TutorialActive = false;
		}
		else
		{
			StartTutorial(true);
		}
	}
	if (Input::GetKeyTrigger(VK_F2))
	{
		m_DashboardOpen = !m_DashboardOpen;
		if (m_DashboardOpen)
		{
			LoadDashboard();
		}
	}

	for (FloatingFeedback& feedback : m_Feedback)
	{
		feedback.lifetime -= presentationDelta;
	}
	UpdateHitReactions(game, presentationDelta);
	UpdateEnemyHpVisuals(presentationDelta);
	m_CollisionVfx.Update(
		presentationDelta,
		(game.IsCollisionImpactEffectsEnabled() || game.IsDefeatVfxEnabled()) &&
		IsBattleScene(game));
	std::erase_if(
		m_Feedback,
		[](const FloatingFeedback& feedback)
		{
			return feedback.lifetime <= 0.0f;
		});
	for (ChainRangeFeedback& feedback : m_ChainRanges)
	{
		feedback.lifetime -= presentationDelta;
	}
	std::erase_if(
		m_ChainRanges,
		[](const ChainRangeFeedback& feedback)
		{
			return feedback.lifetime <= 0.0f;
		});
	m_ImpactFlashLifetime = (std::max)(
		0.0f,
		m_ImpactFlashLifetime - presentationDelta);
	m_DamageFlashLifetime = (std::max)(
		0.0f,
		m_DamageFlashLifetime - presentationDelta);
	m_HitSummaryLifetime = (std::max)(
		0.0f,
		m_HitSummaryLifetime - presentationDelta);
	m_HeavyChainDisplayLifetime = (std::max)(
		0.0f,
		m_HeavyChainDisplayLifetime - presentationDelta);
	m_MultiDefeatDisplayLifetime = (std::max)(
		0.0f,
		m_MultiDefeatDisplayLifetime - presentationDelta);
	m_ImpactFlashCooldown = (std::max)(
		0.0f,
		m_ImpactFlashCooldown - presentationDelta);
	m_CameraShakeCooldown = (std::max)(
		0.0f,
		m_CameraShakeCooldown - presentationDelta);
	if (game.IsCameraShakeEnabled() && m_CameraShakeLifetime > 0.0f)
	{
		m_CameraShakeLifetime = (std::max)(
			0.0f,
			m_CameraShakeLifetime - presentationDelta);
		m_CameraShakePhase += 1.65f;
		const float fade = std::clamp(
			m_CameraShakeLifetime / 0.24f,
			0.0f,
			1.0f);
		Camera::GetInstance().SetTarget(Vector3(
			std::sin(m_CameraShakePhase) * m_CameraShakeStrength * fade,
			0.0f,
			std::cos(m_CameraShakePhase * 1.31f) *
			m_CameraShakeStrength * fade));
	}
	else
	{
		m_CameraShakeLifetime = 0.0f;
		Camera::GetInstance().SetTarget(Vector3::Zero);
	}

	const int currentState = static_cast<int>(game.GetBattleState());
	if (m_PreviousBattleState == static_cast<int>(BattleState::BallsMoving) &&
		currentState != static_cast<int>(BattleState::BallsMoving))
	{
		ClearHitReactions();
	}
	if (m_TutorialActive && IsBattleScene(game))
	{
		if (m_TutorialStep == TutorialStep::ChooseAndAim &&
			game.GetBattleState() == BattleState::AimingPower)
		{
			m_TutorialStep = TutorialStep::SetPower;
		}
		else if (m_TutorialStep == TutorialStep::WatchShot &&
			m_PreviousBattleState == static_cast<int>(BattleState::BallsMoving) &&
			currentState != static_cast<int>(BattleState::BallsMoving))
		{
			m_TutorialStep = TutorialStep::ReadResult;
		}
	}
	m_PreviousBattleState = currentState;
	++m_PresentationFrame;
}

void GamePresentation::Draw(Game& game)
{
	DrawFeedback(game);
	if (GameUi::showDebugger && IsBattleScene(game))
	{
		m_CollisionVfx.DrawDebugTuning();
	}
	if (m_DashboardOpen)
	{
		DrawDashboard();
	}

	if (ImGui::BeginMainMenuBar())
	{
		ImGui::BeginDisabled(!game.CanOpenPauseMenu());
		if (ImGui::Button(game.IsPaused() ? "再開" : "ポーズ / 設定")) game.RequestPauseToggle();
		if (ImGui::Button("セーブ")) game.RequestManualSave();
		ImGui::EndDisabled();
		if (ImGui::Button("ガイド")) { if (m_TutorialActive) m_TutorialActive = false; else StartTutorial(true); }
		if (ImGui::Button("分析")) { m_DashboardOpen = !m_DashboardOpen; if (m_DashboardOpen) LoadDashboard(); }
		ImGui::Checkbox("デバッグ", &GameUi::showDebugger);
		if (ImGui::Button("UIを画面内へ戻す")) GameUi::ResetWindows();
		if (ImGui::Button("全画面切替")) game.RequestFullscreenToggle();
		if (ImGui::Button("終了")) PostMessage(Application::GetWindow(), WM_CLOSE, 0, 0);
		ImGui::EndMainMenuBar();
	}
	if (IsBattleScene(game))
	{
		DrawPierceTraces(game);
		DrawEnemyStatusEffects(game);
		DrawBattleHud(game);
		if (m_TutorialActive)
		{
			DrawTutorial(game);
		}
		return;
	}
	GameUi::PrepareWindow("play_hints", ImVec2(805, 35), ImVec2(450, 170));
	if (ImGui::Begin("操作と攻略", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (IsBattleScene(game))
		{
			const auto players =
				GameRuntime::World().GetComponents<PlayerBall>();
			const int currentHp = players.empty() ? game.GetPlayerCurrentHp() : players[0]->GetHP();
			ImGui::Text("HP %d / %d   シールド %d   所持金 %d",
				currentHp, game.GetPlayerMaxHp(), game.GetPlayerShield(), game.GetPlayerMoney());
		}
		ImGui::TextUnformatted("盤面を左ドラッグして離す：発射 / 右クリック：取消");
		ImGui::TextUnformatted("ホイール：拡大縮小 / タイトルバーをドラッグ：UI移動");
		if (IsBattleScene(game))
		{
			for (const EnemyBall* enemy :
				GameRuntime::World().GetComponents<EnemyBall>())
			{
				if (enemy == nullptr || enemy->IsDefeated()) continue;
				if (enemy->IsArmorBoss())
				{
					const auto& boss = enemy->GetBossState();
					ImGui::Separator();
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.6f, 1.0f), "最終ボス  HP %d / %d", enemy->GetHP(), enemy->GetMaxHP());
					ImGui::Text("Armor %d / %d", boss.armor, BossCombatRules::MaxArmor);
					if (boss.IsBroken())
					{
						ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "BREAK！ 通常ダメージ100%% / 残り%dショット", boss.shotsRemaining);
						if (boss.startedThisShot && game.GetBattleState() == BattleState::BallsMoving)
							ImGui::TextUnformatted("このショットの残り ＋ 次の2ショットが有効");
					}
					else ImGui::TextUnformatted("通常ダメージ25%（切り上げ・最低1） / ポケット無効");
					ImGui::TextUnformatted("黄色の球をボスへ押し込もう：Armor -1 / HP -4");
					ImGui::TextUnformatted("黄色の球は命中・落下後、ショット終了時に再配置");
					for (auto* neutral :
						GameRuntime::World().GetComponents<BreakBall>())
						ImGui::Text("ブレイク球%d：%s", neutral->GetIndex() + 1,
							neutral->GetGameObject()->IsActive() ? "使用可能" : "再配置待ち");
				}
				if (enemy->GetFrontalDamageMultiplier() < 1.0f)
				{
					ImGui::Separator();
					ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "中ボス：ガード球（水色）  HP %d / %d", enemy->GetHP(), enemy->GetMaxHP());
					ImGui::Text("テーブル手前側からの衝突ダメージを%d%%軽減（最低1）。", static_cast<int>(std::lround((1.0f - enemy->GetFrontalDamageMultiplier()) * 100.0f)));
					ImGui::TextUnformatted("側面・奥側から狙おう。ガードの向きは固定。");
				}
				if (enemy->GetPocketDamageRatio() > 0.0f)
				{
					ImGui::Separator();
					ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "中ボス：ポケット球（緑）  HP %d / %d", enemy->GetHP(), enemy->GetMaxHP());
					ImGui::Text("ポケットに落とすと最大HPの%d%%ダメージ。", static_cast<int>(std::lround(enemy->GetPocketDamageRatio() * 100.0f)));
					ImGui::TextUnformatted("重い球で押し出そう。生存時は待機列に入り、後で復帰する。");
				}
			}
		}
	}
	ImGui::End();
	if (m_TutorialActive)
	{
		DrawTutorial(game);
	}
}

void GamePresentation::DrawEnemyStatusEffects(Game& game)
{
	const BattleState state = game.GetBattleState();
	const bool showActionPlan =
		state == BattleState::AimingDirection ||
		state == BattleState::AimingPower ||
		state == BattleState::BallsMoving;
	const bool showTimedStatusEffects =
		state == BattleState::AimingDirection ||
		state == BattleState::AimingPower;
	const EnemyHpDisplaySettings& hpDisplay =
		game.GetEnemyHpDisplaySettings();
	const bool showOverheadHp =
		hpDisplay.mode == EnemyHpDisplayMode::OverheadBar;
	const bool showCircularHp =
		hpDisplay.mode == EnemyHpDisplayMode::CircularGauge;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoInputs;

	// 背景・枠線で使用している余白と同じ値にする
	constexpr float kEnemyFramePadding = 4.0f;

	// 枠同士の最低間隔
	constexpr float kEnemyFrameGap = 2.0f;

	// このフレームで配置済みのウィンドウ領域
	std::vector<ImVec4> occupiedEnemyFrames;
	// 全敵の画面上の当たり判定領域
	std::vector<std::pair<std::uintptr_t, ImVec4>> enemyBodyRegions;

	// ウィンドウの外枠領域を計算
	const auto makeEnemyFrameRect =
		[&](const ImVec2& pos, float width, float height) -> ImVec4
		{
			return ImVec4(
				pos.x - kEnemyFramePadding,
				pos.y - kEnemyFramePadding,
				pos.x + width + kEnemyFramePadding,
				pos.y + height + kEnemyFramePadding
			);
		};

	// ホバー中は本来の上下位置に戻す。ただし画面外だけはみ出さないようにする。
	const auto clampEnemyHoverPosition =
		[&](const ImVec2& base, float width, float height) -> ImVec2
		{
			const float left = viewport->Pos.x + kEnemyFramePadding + 2.0f;
			const float top = viewport->Pos.y + kEnemyFramePadding + 2.0f;
			const float right = viewport->Pos.x + viewport->Size.x -
				kEnemyFramePadding - width - 2.0f;
			const float bottom = viewport->Pos.y + viewport->Size.y -
				kEnemyFramePadding - height - 2.0f;
			return ImVec2(
				std::clamp(base.x, left, (std::max)(left, right)),
				std::clamp(base.y, top, (std::max)(top, bottom))
			);
		};

	// ============================================================
// Enemy UI: vertical-first, horizontal-fallback placement
// ============================================================
	constexpr float kEnemyVerticalLimit = 56.0f;
	constexpr float kEnemyHorizontalLimit = 30.0f;
	constexpr float kEnemyVerticalStep = 4.0f;
	constexpr float kEnemyHorizontalStep = 6.0f;

	struct EnemyUiLink
	{
		ImVec2 start;
		ImVec2 end;
	};

	struct EnemyUiPlacement
	{
		ImVec2 position;
		EnemyUiLink link{};
		bool moved = false;
		bool hidden = false;
	};

	std::vector<EnemyUiLink> occupiedEnemyLinks;

	// 敵ボールとウィンドウを結ぶ線の両端を計算
	const auto makeEnemyUiLink =
		[&](const ImVec2& pos, float width, float height,
			bool upper, const ImVec2& ball, float radius) -> EnemyUiLink
		{
			const ImVec4 frame = makeEnemyFrameRect(pos, width, height);
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
		};

	// 点と線分の最短距離（二乗）
	const auto pointSegmentDistanceSquared =
		[](const ImVec2& point,
			const ImVec2& a,
			const ImVec2& b) -> float
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
		};

	// ============================================================
	// ウィンドウ配置の決定
	// ============================================================
	const auto resolveEnemyInfoPosition =
		[&](const ImVec2& base,
			float width,
			float height,
			bool upper,
			std::uintptr_t owner,
			const ImVec2& ball,
			float radius) -> EnemyUiPlacement
		{
			EnemyUiPlacement result{};
			result.position = base;

			float fallbackScore = FLT_MAX;
			ImVec2 fallbackPosition = base;
			bool found = false;

			// 指定位置にウィンドウを置けるか評価
			const auto examine =
				[&](const ImVec2& pos, bool& valid, float& score)
				{
					const ImVec4 frame =
						makeEnemyFrameRect(pos, width, height);

					valid = true;
					score = 0.0f;

					// 画面外判定
					if (frame.x < viewport->Pos.x + 2.0f ||
						frame.y < viewport->Pos.y + 2.0f ||
						frame.z > viewport->Pos.x + viewport->Size.x - 2.0f ||
						frame.w > viewport->Pos.y + viewport->Size.y - 2.0f)
					{
						valid = false;
						score += 100000.0f;
					}

					// 1. 他の上部・下部ウィンドウとの重なり
					for (const ImVec4& used : occupiedEnemyFrames)
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
					for (const EnemyUiLink& previous : occupiedEnemyLinks)
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
					for (const auto& [key, body] : enemyBodyRegions)
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
						const EnemyUiLink link = makeEnemyUiLink(
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
							for (const auto& [key, body] : enemyBodyRegions)
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
							for (const ImVec4& used : occupiedEnemyFrames)
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
							for (const EnemyUiLink& previous : occupiedEnemyLinks)
							{
								if (pointSegmentDistanceSquared(
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
				result.link = makeEnemyUiLink(
					result.position, width, height,
					upper, ball, radius
				);
			}

			// ホバー状態では配置予約を増やさない。通常配置だけを占有領域へ記録する。
			if (!result.hidden)
			{
				occupiedEnemyFrames.push_back(
					makeEnemyFrameRect(
						result.position, width, height
					)
				);

				if (result.moved)
				{
					occupiedEnemyLinks.push_back(result.link);
				}
			}

			return result;
		};

	// ============================================================
	// 点線描画：ホバー中は色を強調
	// ============================================================
	const auto drawEnemyInfoDottedLine =
		[&](const EnemyUiLink& link, bool highlight)
		{
			const float dx = link.end.x - link.start.x;
			const float dy = link.end.y - link.start.y;
			const float length = std::sqrt(dx * dx + dy * dy);

			if (length < 0.01f)
				return;

			ImDrawList* layer = highlight
				? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
				: ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

			const ImU32 color = highlight
				? IM_COL32(255, 238, 164, 245)
				: IM_COL32(190, 215, 230, 185);

			for (float distance = 0.0f;
				distance <= length;
				distance += 5.0f)
			{
				const ImVec2 dot(
					link.start.x + dx * distance / length,
					link.start.y + dy * distance / length
				);

				layer->AddCircleFilled(
					dot,
					highlight ? 1.5f : 1.1f,
					color,
					6
				);
			}
		};

	// 重なり回避に失敗して隠れた情報がある敵に「i」を表示する。
	const auto drawHiddenEnemyInfoMarker =
		[&](const ImVec2& ballCenter, float ballRadius)
		{
			constexpr float markerRadius = 6.0f;
			const ImVec2 markerCenter(
				ballCenter.x + ballRadius * 0.78f,
				ballCenter.y + ballRadius * 0.78f
			);
			if (markerCenter.x - markerRadius < viewport->Pos.x ||
				markerCenter.y - markerRadius < viewport->Pos.y ||
				markerCenter.x + markerRadius > viewport->Pos.x + viewport->Size.x ||
				markerCenter.y + markerRadius > viewport->Pos.y + viewport->Size.y)
			{
				return;
			}

			ImDrawList* layer = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
			layer->AddCircleFilled(markerCenter, markerRadius, IM_COL32(15, 25, 31, 240), 16);
			layer->AddCircle(markerCenter, markerRadius, IM_COL32(180, 224, 239, 235), 16, 1.2f);

			const char* label = "i";
			ImFont* font = ImGui::GetFont();
			const float fontSize = 11.0f;
			const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
			layer->AddText(
				font, fontSize,
				ImVec2(markerCenter.x - textSize.x * 0.5f,
					markerCenter.y - textSize.y * 0.5f),
				IM_COL32(235, 248, 255, 255), label);

			const float mx = ImGui::GetIO().MousePos.x - markerCenter.x;
			const float my = ImGui::GetIO().MousePos.y - markerCenter.y;
			if (mx * mx + my * my <= markerRadius * markerRadius)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(PresentationUtf8(u8"この敵には非表示の情報があります。"));
				ImGui::TextUnformatted(PresentationUtf8(u8"敵ボールにマウスを合わせて確認できます。"));
				ImGui::EndTooltip();
			}
		};

	std::vector<ImVec4> placedEnemyPanels;
	std::vector<std::uintptr_t> liveEnemyInfoKeys;
	std::vector<ImVec4> protectedEnemyUiRegions;
	const ImVec2 workPosition = viewport->WorkPos;
	const ImVec2 workSize = viewport->WorkSize;
	protectedEnemyUiRegions.emplace_back(
		workPosition.x + 8.0f,
		workPosition.y + 8.0f,
		workPosition.x + 306.0f,
		workPosition.y + 136.0f);
	protectedEnemyUiRegions.emplace_back(
		workPosition.x + workSize.x - 210.0f,
		workPosition.y + 8.0f,
		workPosition.x + workSize.x - 8.0f,
		workPosition.y + 82.0f);
	// Ball cards occupy the bottom center, while draw/discard piles use the
	// corners. Treat the complete strip as protected so enemy intent never
	// becomes part of the card silhouette.
	protectedEnemyUiRegions.emplace_back(
		workPosition.x + 8.0f,
		workPosition.y + workSize.y - 184.0f,
		workPosition.x + workSize.x - 8.0f,
		workPosition.y + workSize.y - 4.0f);
	if (m_HitSummaryLifetime > 0.0f)
	{
		protectedEnemyUiRegions.emplace_back(
			viewport->Pos.x + viewport->Size.x - 408.0f,
			viewport->Pos.y + 256.0f,
			viewport->Pos.x + viewport->Size.x - 10.0f,
			viewport->Pos.y + 368.0f);
	}

	int visibleEnemyCount = 0;
	for (const EnemyBall* candidate :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (candidate != nullptr && !candidate->IsDefeated() &&
			!candidate->IsPocketed())
		{
			++visibleEnemyCount;
			ImVec2 bodyCenter;
			ImVec2 bodyTop;
			if (TryProjectToScreen(candidate->GetPosition(), bodyCenter) &&
				TryProjectToScreen(
					candidate->GetPosition() +
					Vector3(0.0f, candidate->GetVisualRadius(), 0.0f),
					bodyTop))
			{
				const float bodyRadius = (std::max)(
					14.0f,
					std::abs(bodyCenter.y - bodyTop.y));
				enemyBodyRegions.emplace_back(
					reinterpret_cast<std::uintptr_t>(candidate),
					ImVec4(
						bodyCenter.x - bodyRadius - 5.0f,
						bodyCenter.y - bodyRadius - 5.0f,
						bodyCenter.x + bodyRadius + 5.0f,
						bodyCenter.y + bodyRadius + 5.0f));
			}
		}
	}

	// 敵ボールへのマウスオーバー判定
	std::uintptr_t hoveredEnemyKey = 0;
	float nearestHoverScore = FLT_MAX;

	const ImVec2 mousePos = ImGui::GetIO().MousePos;

	for (const auto& [key, body] : enemyBodyRegions)
	{
		const float cx = (body.x + body.z) * 0.5f;
		const float cy = (body.y + body.w) * 0.5f;

		// enemyBodyRegionsは5px拡張されているため補正
		const float radius = (std::max)(
			1.0f,
			(body.z - body.x) * 0.5f - 5.0f
			);

		const float dx = mousePos.x - cx;
		const float dy = mousePos.y - cy;

		const float score =
			(dx * dx + dy * dy) / (radius * radius);

		if (score <= 1.0f && score < nearestHoverScore)
		{
			nearestHoverScore = score;
			hoveredEnemyKey = key;
		}
	}

	// Pass 0: 通常配置の計算・描画。ホバー対象も配置だけは予約しておく。
	// Pass 1: 対象の上下UIを基準位置へ戻し、最後に最前面へ重ね描画する。
	for (int uiPass = 0; uiPass < 2; ++uiPass)
	{
		const bool hoverOverlayPass = uiPass == 1;
		if (hoverOverlayPass && hoveredEnemyKey == 0)
			break;
		int enemyIndex = 0;
		for (const EnemyBall* enemy :
			GameRuntime::World().GetComponents<EnemyBall>())
		{
			const int currentIndex = enemyIndex++;
			if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed()) continue;
			if (hoverOverlayPass &&
				reinterpret_cast<std::uintptr_t>(enemy) != hoveredEnemyKey)
				continue;
			const auto& effects = enemy->GetStatusEffects();
			const int effectCount = showTimedStatusEffects
				? effects.GetActiveCount()
				: 0;
			const int anchorStacks = enemy->GetAnchorStacks();
			const EnemyBoardAbilityType boardAbility =
				enemy->GetBoardAbilityType();
			const bool protectedByResonator =
				enemy->IsProtectedByResonator();
			const int boardStatusCount =
				(boardAbility != EnemyBoardAbilityType::None ? 1 : 0) +
				(protectedByResonator ? 1 : 0);
			const EnemyActionType action = enemy->GetPlannedAction();
			const bool actionIsAttack =
				action == EnemyActionType::NormalAttack ||
				action == EnemyActionType::ChargeAttack;
			int expectedDamage = 0;
			if (showActionPlan && actionIsAttack)
			{
				const GameObject* owner = enemy->GetGameObject();
				const EnemyAttackComponent* attack = owner != nullptr
					? owner->GetComponent<EnemyAttackComponent>()
					: nullptr;
				expectedDamage = attack != nullptr
					? PlayerBall::CalculateIncomingDamage(
						attack->CalculateDamage(
							enemy->GetPlannedDamageMultiplier()))
					: 0;
			}
			const std::string intentText = actionIsAttack ? "-" + std::to_string(expectedDamage) : std::string{};
			const int shownEffectCount = visibleEnemyCount >= 10 ? (std::min)(effectCount, 1)
				: visibleEnemyCount >= 8 ? (std::min)(effectCount, 2)
				: effectCount;
			const int suppressedEffectCount = effectCount - shownEffectCount;
			const int itemCount = shownEffectCount + boardStatusCount + (anchorStacks > 0 ? 1 : 0);

			ImVec2 center;
			ImVec2 top;
			if (!TryProjectToScreen(enemy->GetPosition(), center) ||
				!TryProjectToScreen(enemy->GetPosition() + Vector3(0.0f, enemy->GetVisualRadius(), 0.0f), top))
			{
				continue;
			}
			const float projectedRadius = (std::max)(14.0f, std::abs(center.y - top.y));
			if (!hoverOverlayPass && GameUi::showDebugger && enemy->GetBoardAbilityRadius() > 0.0f)
			{
				ImVec2 rangeEdge;
				if (TryProjectToScreen(enemy->GetPosition() + Vector3(enemy->GetBoardAbilityRadius(), 0.0f, 0.0f),
					rangeEdge))
				{
					const float screenRadius = std::abs(rangeEdge.x - center.x);
					ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())
						->AddCircle(center, screenRadius,
							boardAbility == EnemyBoardAbilityType::ProtectionResonator
							? IM_COL32(70, 220, 245, 105)
							: IM_COL32(255, 92, 155, 105),
							48, 1.5f);
				}
			}
			const std::uintptr_t targetKey =
				reinterpret_cast<std::uintptr_t>(enemy);

			const bool isEnemyHovered =
				targetKey == hoveredEnemyKey;
			// 他の敵をホバーしている間は、重なった通常アイコンのツールチップを抑止する。
			const bool allowEnemyIconTooltip =
				hoveredEnemyKey == 0 || isEnemyHovered;

			if (!hoverOverlayPass)
				liveEnemyInfoKeys.push_back(targetKey);

			// ホバー中の敵を最後のパスでリング強調
			if (hoverOverlayPass && isEnemyHovered)
			{
				ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->AddCircle(
					center,
					projectedRadius + 3.0f,
					IM_COL32(255, 238, 164, 220),
					32,
					1.6f
				);
			}
			const auto hpVisual =
				std::find_if(m_EnemyHpVisuals.begin(), m_EnemyHpVisuals.end(),
					[targetKey](const EnemyHpVisual& value) { return value.targetKey == targetKey; });
			const int currentHp = hpVisual != m_EnemyHpVisuals.end() ? hpVisual->currentHp : (std::max)(0, enemy->GetHP());
			const int maximumHp =
				hpVisual != m_EnemyHpVisuals.end() ? hpVisual->maximumHp : (std::max)(1, enemy->GetMaxHP());
			const float trailingHp =
				hpVisual != m_EnemyHpVisuals.end() ? hpVisual->trailingHp : static_cast<float>(currentHp);
			const float damageHold = hpVisual != m_EnemyHpVisuals.end() ? hpVisual->damageHold : 0.0f;
			const float hpRatio = std::clamp(static_cast<float>(currentHp) / static_cast<float>(maximumHp), 0.0f, 1.0f);
			const float trailingRatio = std::clamp(trailingHp / static_cast<float>(maximumHp), 0.0f, 1.0f);
			if (!hoverOverlayPass && showCircularHp && currentHp > 0)
			{
				DrawEnemyHpRing(hpDisplay, center, projectedRadius, hpRatio, trailingRatio, damageHold);
			}

			// 上部の行動予告：通常は回避位置、ホバー中のみ敵の真上へ戻す。
			bool upperInfoHidden = false;
			if (showActionPlan)
			{
				const ImVec2 actionBasePosition(
					center.x - kCombatIconSize * 0.5f,
					center.y - projectedRadius - kCombatIconSize - 7.0f
				);

				EnemyUiPlacement actionPlacement{};
				if (!hoverOverlayPass)
				{
					actionPlacement = resolveEnemyInfoPosition(
						actionBasePosition, kCombatIconSize, kCombatIconSize,
						true, targetKey, center, projectedRadius);
					upperInfoHidden = actionPlacement.hidden;
				}
				else
				{
					// 配置処理へ再登録せず、ホバー対象だけ元の真上に重ねる。
					actionPlacement.position = clampEnemyHoverPosition(
						actionBasePosition, kCombatIconSize, kCombatIconSize);
					actionPlacement.moved =
						std::abs(actionPlacement.position.x - actionBasePosition.x) +
						std::abs(actionPlacement.position.y - actionBasePosition.y) > 0.01f;
					if (actionPlacement.moved)
						actionPlacement.link = makeEnemyUiLink(actionPlacement.position,
							kCombatIconSize, kCombatIconSize, true, center, projectedRadius);
				}

				const ImVec2 actionPosition = actionPlacement.position;
				const bool drawActionWindow = hoverOverlayPass
					? isEnemyHovered
					: (!isEnemyHovered && !actionPlacement.hidden);

				if (drawActionWindow && actionPlacement.moved)
					drawEnemyInfoDottedLine(actionPlacement.link, hoverOverlayPass);

				if (drawActionWindow)
				{
					char actionWindowId[64]{};
					sprintf_s(actionWindowId, "##enemy_action_intent_%d", currentIndex);
					ImGui::SetNextWindowViewport(viewport->ID);
					ImGui::SetNextWindowPos(actionPosition);
					ImGui::SetNextWindowSize(ImVec2(kCombatIconSize, kCombatIconSize));

					// 行動予告アイコンの背景・枠線
					{
						constexpr float framePadding = 4.0f;
						constexpr float frameRounding = 5.0f;

						const ImVec2 frameMin(
							actionPosition.x - framePadding,
							actionPosition.y - framePadding
						);

						const ImVec2 frameMax(
							actionPosition.x + kCombatIconSize + framePadding,
							actionPosition.y + kCombatIconSize + framePadding
						);

						ImDrawList* frameDrawList = isEnemyHovered
							? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
							: ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

						// 半透明の暗い背景
						frameDrawList->AddRectFilled(
							frameMin,
							frameMax,
							IM_COL32(12, 20, 27, isEnemyHovered ? 245 : 109),
							frameRounding
						);

						// 灰色の細い枠線
						frameDrawList->AddRect(
							frameMin,
							frameMax,
							isEnemyHovered
							? IM_COL32(255, 225, 120, 255)
							: IM_COL32(105, 128, 147, 220),
							frameRounding,
							0,
							isEnemyHovered ? 2.4f : 1.2f
						);
					}

					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
					if (ImGui::Begin(actionWindowId, nullptr, flags))
					{
						const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
						const ImVec2 iconMaximum(iconMinimum.x + kCombatIconSize, iconMinimum.y + kCombatIconSize);
						ImGui::Dummy(ImVec2(kCombatIconSize, kCombatIconSize));
						ImDrawList* actionDrawList = isEnemyHovered
							? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
							: ImGui::GetWindowDrawList();
						DrawEnemyActionBadge(actionDrawList, iconMinimum, action);
						if (actionIsAttack)
						{
							const float damageFontSize = ImGui::GetFontSize() * 0.72f;
							const ImVec2 damageSize =
								ImGui::GetFont()->CalcTextSizeA(damageFontSize, FLT_MAX, 0.0f, intentText.c_str());
							DrawOutlinedValue(actionDrawList,
								ImVec2(iconMaximum.x - damageSize.x, iconMaximum.y - damageSize.y),
								intentText.c_str(), IM_COL32(255, 245, 218, 255));
						}
						if (allowEnemyIconTooltip && IsMouseInside(iconMinimum, iconMaximum))
						{
							ImGui::BeginTooltip();
							ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(EnemyActionColor(action)), "%s",
								EnemyActionDisplayName(action));
							ImGui::Text("%s: %d", PresentationUtf8(u8"行動コスト"), enemy->GetPlannedActionCost());
							ImGui::Text("Charge: %s", enemy->IsCharging() ? "Charging" : "Normal");
							if (actionIsAttack)
							{
								ImGui::Text(PresentationUtf8(u8"予測ダメージ: %d（シールド適用前）"), expectedDamage);
							}
							else if (action == EnemyActionType::ChargeStart)
							{
								ImGui::TextUnformatted(PresentationUtf8(u8"このターンは攻撃せずChargeを開始"));
							}
							else
							{
								ImGui::TextUnformatted(PresentationUtf8(u8"このターンは行動しません。"));
							}
							ImGui::EndTooltip();
						}
					}
					ImGui::End();
					ImGui::PopStyleVar();
				}
			}
			if (!showOverheadHp && itemCount <= 0)
			{
				if (!hoverOverlayPass && !isEnemyHovered && upperInfoHidden)
					drawHiddenEnemyInfoMarker(center, projectedRadius);
				continue;
			}
			const float rowWidth = (shownEffectCount + boardStatusCount) * kCombatIconSize +
				(anchorStacks > 0 ? kResourceBadgeWidth : 0.0f) +
				(std::max)(0, itemCount - 1) * kCombatIconSpacing;
			const float panelWidth = showOverheadHp ? (std::max)(104.0f, rowWidth) : (std::max)(1.0f, rowWidth);
			const float panelHeight =
				showOverheadHp ? (itemCount > 0 ? 53.0f : 18.0f) : (itemCount > 0 ? kCombatIconSize : 0.0f);
			// 敵の真下を基準位置とする
			const ImVec2 panelBasePosition(
				center.x - panelWidth * 0.5f,
				center.y + projectedRadius + 6.0f
			);

			EnemyUiPlacement panelPlacement{};
			if (!hoverOverlayPass)
			{
				panelPlacement = resolveEnemyInfoPosition(
					panelBasePosition, panelWidth, panelHeight,
					false, targetKey, center, projectedRadius);
			}
			else
			{
				// ホバー表示では通常配置を変えず、真下の基準位置に戻す。
				panelPlacement.position = clampEnemyHoverPosition(
					panelBasePosition, panelWidth, panelHeight);
				panelPlacement.moved =
					std::abs(panelPlacement.position.x - panelBasePosition.x) +
					std::abs(panelPlacement.position.y - panelBasePosition.y) > 0.01f;
				if (panelPlacement.moved)
					panelPlacement.link = makeEnemyUiLink(panelPlacement.position,
						panelWidth, panelHeight, false, center, projectedRadius);
			}

			// 省略されたウィンドウがある敵だけ、通常描画時に小さな i を付ける。
			if (!hoverOverlayPass && !isEnemyHovered &&
				(upperInfoHidden || panelPlacement.hidden))
				drawHiddenEnemyInfoMarker(center, projectedRadius);

			const bool drawLowerWindow = hoverOverlayPass
				? isEnemyHovered
				: (!isEnemyHovered && !panelPlacement.hidden);
			if (!drawLowerWindow)
				continue;

			const ImVec2 panelPosition = panelPlacement.position;

			if (panelPlacement.moved)
				drawEnemyInfoDottedLine(panelPlacement.link, hoverOverlayPass);
			// パネル全体が画面外の場合のみ描画をスキップする。
			// 画面内では位置をずらさない。
			if (panelPosition.x + panelWidth < viewport->Pos.x ||
				panelPosition.x > viewport->Pos.x + viewport->Size.x ||
				panelPosition.y + panelHeight < viewport->Pos.y ||
				panelPosition.y > viewport->Pos.y + viewport->Size.y)
			{
				continue;
			}

			char windowId[64]{};
			sprintf_s(windowId, "##enemy_status_effects_%d", currentIndex);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::SetNextWindowPos(panelPosition);
			ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));

			// バフ・デバフ一覧の背景・枠線
			{
				constexpr float framePadding = 4.0f;
				constexpr float frameRounding = 5.0f;

				const ImVec2 frameMin(
					panelPosition.x - framePadding,
					panelPosition.y - framePadding
				);

				const ImVec2 frameMax(
					panelPosition.x + panelWidth + framePadding,
					panelPosition.y + panelHeight + framePadding
				);

				ImDrawList* frameDrawList = isEnemyHovered
					? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
					: ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

				// 半透明の暗い背景
				frameDrawList->AddRectFilled(
					frameMin,
					frameMax,
					IM_COL32(12, 20, 27, isEnemyHovered ? 245 : 109),
					frameRounding
				);

				// 灰色の細い枠線
				frameDrawList->AddRect(
					frameMin,
					frameMax,
					isEnemyHovered
					? IM_COL32(255, 225, 120, 255)
					: IM_COL32(105, 128, 147, 220),
					frameRounding,
					0,
					isEnemyHovered ? 2.4f : 1.2f
				);
			}

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
			if (ImGui::Begin(windowId, nullptr, flags))
			{
				ImDrawList* drawList = isEnemyHovered
					? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
					: ImGui::GetWindowDrawList();
				if (showOverheadHp)
				{
					const ImVec2 hpMinimum(
						panelPosition.x,
						panelPosition.y);
					const ImVec2 hpMaximum(
						panelPosition.x + panelWidth,
						panelPosition.y + 16.0f);
					drawList->AddRectFilled(
						hpMinimum,
						hpMaximum,
						IM_COL32(8, 12, 18, 235),
						4.0f);
					drawList->AddRectFilled(
						hpMinimum,
						ImVec2(
							hpMinimum.x + panelWidth * trailingRatio,
							hpMaximum.y),
						IM_COL32(255, 154, 54, 225),
						4.0f);
					const ImU32 hpColor = EnemyHpColor(hpRatio, 1.0f);
					drawList->AddRectFilled(
						hpMinimum,
						ImVec2(
							hpMinimum.x + panelWidth * hpRatio,
							hpMaximum.y),
						hpColor,
						4.0f);
					drawList->AddRect(
						hpMinimum,
						hpMaximum,
						IM_COL32(232, 240, 250, 230),
						4.0f,
						0,
						1.2f);
					char hpText[48]{};
					sprintf_s(hpText, "HP %d / %d", currentHp, maximumHp);
					const ImVec2 hpTextSize = ImGui::CalcTextSize(hpText);
					drawList->AddText(
						ImVec2(
							hpMinimum.x + (panelWidth - hpTextSize.x) * 0.5f,
							hpMinimum.y + (16.0f - hpTextSize.y) * 0.5f),
						IM_COL32(255, 255, 255, 255),
						hpText);
					ImGui::Dummy(ImVec2(panelWidth, 16.0f));
				}
				if (itemCount > 0)
				{
					ImGui::SetCursorScreenPos(ImVec2(
						panelPosition.x + (panelWidth - rowWidth) * 0.5f,
						panelPosition.y + (showOverheadHp ? 22.0f : 0.0f)));
				}
				int drawn = 0;
				const auto drawBoardBadge =
					[&drawn, drawList, allowEnemyIconTooltip](CombatIconKind icon, ImU32 color, const char* title, const std::string& detail)
					{
						if (drawn++ > 0)
							ImGui::SameLine();
						const ImVec2 minimum = ImGui::GetCursorScreenPos();
						const ImVec2 maximum(minimum.x + kCombatIconSize, minimum.y + kCombatIconSize);
						ImGui::Dummy(ImVec2(kCombatIconSize, kCombatIconSize));
						DrawCombatIcon(drawList, GetCombatIconResources().Get(icon), minimum, maximum);
						if (allowEnemyIconTooltip && IsMouseInside(minimum, maximum))
						{
							ImGui::BeginTooltip();
							ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", title);
							ImGui::TextUnformatted(detail.c_str());
							ImGui::EndTooltip();
						}
					};
				if (boardAbility == EnemyBoardAbilityType::SwarmAmplifier)
				{
					drawBoardBadge(CombatIconKind::EnemySwarm, IM_COL32(255, 86, 155, 255), PresentationUtf8(u8"群体強化"),
						std::string(PresentationUtf8(u8"周囲の生存中の味方 ")) +
						std::to_string(enemy->GetNearbyLivingAllyCount()) +
						PresentationUtf8(u8"体 / 強化段階 ") + std::to_string(enemy->GetSwarmStage() + 1) +
						PresentationUtf8(u8" / 現在攻撃力 ") + std::to_string(enemy->GetAttack()));
				}
				else if (boardAbility == EnemyBoardAbilityType::ImpactBrittle)
				{
					drawBoardBadge(CombatIconKind::EnemyBrittle,
						enemy->IsBrittle() ? IM_COL32(120, 220, 255, 255) : IM_COL32(255, 158, 55, 255),
						PresentationUtf8(enemy->IsBrittle() ? u8"衝撃脆化中" : u8"衝撃脆化球"),
						enemy->IsBrittle() ? PresentationUtf8(u8"次の攻撃1回は攻撃力が低下します。")
						: PresentationUtf8(u8"強く壁へ衝突させると脆化します。"));
				}
				else if (boardAbility == EnemyBoardAbilityType::ProtectionResonator)
				{
					drawBoardBadge(CombatIconKind::EnemyResonance, IM_COL32(64, 218, 238, 255),
						PresentationUtf8(u8"防護共鳴球"),
						PresentationUtf8(u8"範囲内の他の味方が受けるダメージを1軽減します。"));
				}
				if (protectedByResonator)
				{
					drawBoardBadge(CombatIconKind::EnemyResonance, IM_COL32(92, 232, 248, 255),
						PresentationUtf8(u8"防護共鳴中"),
						PresentationUtf8(u8"受けるダメージが1軽減されます。効果は重複しません。"));
				}
				int drawnStatusEffects = 0;
				for (const StatusEffectType effectType : AllStatusEffectTypes)
				{
					if (!showTimedStatusEffects)
						break;
					const int magnitude = effects.GetMagnitude(effectType);
					if (magnitude <= 0)
						continue;
					if (drawnStatusEffects >= shownEffectCount)
						break;
					if (drawn++ > 0)
						ImGui::SameLine();
					++drawnStatusEffects;
					const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
					const ImVec2 iconMaximum(iconMinimum.x + kCombatIconSize, iconMinimum.y + kCombatIconSize);
					ImGui::Dummy(ImVec2(kCombatIconSize, kCombatIconSize));
					DrawStatusIcon(drawList, iconMinimum, effectType, magnitude);
					if (drawnStatusEffects == shownEffectCount && suppressedEffectCount > 0)
					{
						char hiddenCount[12]{};
						sprintf_s(hiddenCount, "+%d", suppressedEffectCount);
						const float hiddenFontSize = ImGui::GetFontSize() * 0.68f;
						const ImVec2 hiddenSize =
							ImGui::GetFont()->CalcTextSizeA(hiddenFontSize, FLT_MAX, 0.0f, hiddenCount);
						const ImVec2 hiddenMinimum(iconMaximum.x - hiddenSize.x, iconMaximum.y - hiddenSize.y);
						DrawOutlinedValue(drawList, hiddenMinimum, hiddenCount, IM_COL32(240, 244, 250, 255), 0.68f);
					}
					const ImVec2 mouse = ImGui::GetIO().MousePos;
					const bool hovered = mouse.x >= iconMinimum.x && mouse.x < iconMaximum.x && mouse.y >= iconMinimum.y &&
						mouse.y < iconMaximum.y;
					if (hovered && allowEnemyIconTooltip)
					{
						const bool attack = IsAttackStatusEffect(effectType);
						const int signedMagnitude = IsPositiveStatusEffect(effectType) ? magnitude : -magnitude;
						const int baseValue = attack ? enemy->GetStatus().attack : enemy->GetStatus().defense;
						const int currentValue = attack ? enemy->GetAttack() : enemy->GetDefense();
						ImGui::BeginTooltip();
						ImGui::TextColored(attack ? ImVec4(1.0f, 0.35f, 0.31f, 1.0f) : ImVec4(0.32f, 0.62f, 1.0f, 1.0f),
							"%s", StatusEffectName(effectType));
						ImGui::Text("現在の効果：%s %+d", attack ? "攻撃力" : "防御力", signedMagnitude);
						ImGui::Text("基礎値 %d  →  現在値 %d", baseValue, currentValue);
						ImGui::EndTooltip();
					}
				}
				if (anchorStacks > 0)
				{
					if (drawn++ > 0)
						ImGui::SameLine();
					const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
					const ImVec2 iconMaximum(iconMinimum.x + kResourceBadgeWidth, iconMinimum.y + kResourceBadgeHeight);
					ImGui::Dummy(ImVec2(kResourceBadgeWidth, kResourceBadgeHeight));
					DrawResourceBadge(drawList, iconMinimum,
						{ CombatIconKind::ResourceEnemyAnchor, PresentationUtf8(u8"敵の錨スタック"),
						 PresentationUtf8(u8"この敵が個別に保持する資源です。"), anchorStacks,
						 IM_COL32(75, 225, 183, 255) });
					if (allowEnemyIconTooltip && IsMouseInside(iconMinimum, iconMaximum))
					{
						ImGui::BeginTooltip();
						ImGui::TextColored(ImVec4(0.29f, 0.88f, 0.72f, 1.0f), "%s", PresentationUtf8(u8"敵の錨スタック"));
						ImGui::Text(PresentationUtf8(u8"現在値: %d"), anchorStacks);
						ImGui::TextUnformatted(PresentationUtf8(u8"この敵が個別に保持する資源です。"));
						ImGui::EndTooltip();
					}
				}
			}
			ImGui::End();
			ImGui::PopStyleVar(2);
		}
	}
	m_EnemyInfoLayouts.erase(
		std::remove_if(
			m_EnemyInfoLayouts.begin(),
			m_EnemyInfoLayouts.end(),
			[&liveEnemyInfoKeys](const EnemyInfoLayoutVisual& value)
			{
				return std::find(
					liveEnemyInfoKeys.begin(),
					liveEnemyInfoKeys.end(),
					value.targetKey) == liveEnemyInfoKeys.end();
			}),
		m_EnemyInfoLayouts.end());

	const auto players = GameRuntime::World().GetComponents<PlayerBall>();
	const PlayerBall* player = players.empty() ? nullptr : players.front();
	if (player == nullptr || player->IsDefeated()) return;
	ImVec2 center;
	ImVec2 top;
	if (!TryProjectToScreen(player->GetPosition(), center) ||
		!TryProjectToScreen(player->GetPosition() + Vector3(
			0.0f, player->GetBall()->GetRadius(), 0.0f), top))
		return;
	const float projectedRadius = (std::max)(14.0f, std::abs(center.y - top.y));
	if (player->IsPowerDragging())
	{
		ImDrawList* gaugeDrawList = ImGui::GetBackgroundDrawList(
			ImGui::GetMainViewport());
		const float powerRatio = player->GetShotPowerRatio();
		const float gaugeRadius = projectedRadius + 8.0f;
		constexpr float gaugeStart = -3.92699082f;
		constexpr float gaugeSweep = 4.71238898f;
		DrawArc(
			gaugeDrawList,
			center,
			gaugeRadius,
			gaugeStart,
			gaugeStart + gaugeSweep,
			IM_COL32(4, 10, 18, 205),
			3.6f);
		const ImU32 powerColor = powerRatio >= 0.72f
			? IM_COL32(255, 119, 58, 245)
			: powerRatio >= 0.38f
			? IM_COL32(255, 215, 74, 245)
			: IM_COL32(76, 218, 255, 245);
		DrawArc(
			gaugeDrawList,
			center,
			gaugeRadius,
			gaugeStart,
			gaugeStart + gaugeSweep * powerRatio,
			powerColor,
			2.4f);
	}
	std::vector<ResourceBadge> resources;
	const int heavyStacks = game.GetHeavyCollisionCount();
	if (heavyStacks > 0)
		resources.push_back({ CombatIconKind::ResourceHeavy, PresentationUtf8(u8"重量スタック"),
							 PresentationUtf8(u8"重量軸の能力発動に使う蓄積値です。"), heavyStacks,
							 IM_COL32(255, 146, 61, 255) });
	const int pierceStacks = game.GetPierceTraceState().stackCount;
	const int traceCount = static_cast<int>(game.GetPierceTraceState().traces.size());
	if (pierceStacks > 0 || traceCount > 0)
		resources.push_back({ CombatIconKind::ResourcePierce, PresentationUtf8(u8"貫通"),
							 PresentationUtf8(u8"左が貫通痕数、右が貫通スタック数です。"), pierceStacks,
							 IM_COL32(184, 92, 255, 255) });
	const int cushionStacks = CushionChargeRules::TotalStacks(game.GetCushionCharges());
	if (cushionStacks > 0)
		resources.push_back({ CombatIconKind::ResourceReflect, PresentationUtf8(u8"反射スタック"),
							 PresentationUtf8(u8"壁反射で獲得し、反射軸の能力発動に使う蓄積値です。"), cushionStacks,
							 IM_COL32(255, 221, 58, 255) });
	if (game.GetPlayerAnchorStacks() > 0)
		resources.push_back({ CombatIconKind::ResourceAnchor, PresentationUtf8(u8"プレイヤー錨スタック"),
							 PresentationUtf8(u8"錨軸の変換・防御能力に使う共有資源です。"),
							 game.GetPlayerAnchorStacks(), IM_COL32(75, 225, 183, 255) });
	if (resources.empty())
		return;

	constexpr float pierceSideWidth = 12.0f;
	constexpr float pierceGroupWidth = kCombatIconSize + pierceSideWidth * 2.0f;
	float rowWidth = (resources.size() - 1) * kCombatIconSpacing;
	for (const ResourceBadge& resource : resources)
	{
		rowWidth += resource.icon == CombatIconKind::ResourcePierce ? pierceGroupWidth : kResourceBadgeWidth;
	}
	const ImVec2 rowPosition(center.x - rowWidth * 0.5f, center.y + projectedRadius + 7.0f);
	if (rowPosition.x + rowWidth < viewport->Pos.x || rowPosition.x > viewport->Pos.x + viewport->Size.x ||
		rowPosition.y + kResourceBadgeHeight < viewport->Pos.y || rowPosition.y > viewport->Pos.y + viewport->Size.y)
		return;

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(rowPosition);
	ImGui::SetNextWindowSize(ImVec2(rowWidth, kResourceBadgeHeight));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kCombatIconSpacing, 0.0f));
	if (ImGui::Begin("##player_synergy_resources", nullptr, flags))
	{
		for (std::size_t index = 0; index < resources.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			const ResourceBadge& resource = resources[index];
			const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
			if (resource.icon == CombatIconKind::ResourcePierce)
			{
				const ImVec2 groupMaximum(iconMinimum.x + pierceGroupWidth, iconMinimum.y + kResourceBadgeHeight);
				const ImVec2 imageMinimum(iconMinimum.x + pierceSideWidth, iconMinimum.y);
				const ImVec2 imageMaximum(imageMinimum.x + kCombatIconSize, imageMinimum.y + kCombatIconSize);
				ImGui::Dummy(ImVec2(pierceGroupWidth, kResourceBadgeHeight));
				ImDrawList* resourceDrawList = ImGui::GetWindowDrawList();
				DrawCombatIcon(resourceDrawList, GetCombatIconResources().Get(CombatIconKind::ResourcePierce),
					imageMinimum, imageMaximum);
				char traceText[12]{};
				char stackText[12]{};
				sprintf_s(traceText, "%d", traceCount);
				sprintf_s(stackText, "%d", pierceStacks);
				const float valueFontSize = ImGui::GetFontSize() * 0.67f;
				const ImVec2 traceSize = ImGui::GetFont()->CalcTextSizeA(valueFontSize, FLT_MAX, 0.0f, traceText);
				const ImVec2 stackSize = ImGui::GetFont()->CalcTextSizeA(valueFontSize, FLT_MAX, 0.0f, stackText);
				DrawOutlinedValue(resourceDrawList,
					ImVec2(iconMinimum.x + (pierceSideWidth - traceSize.x) * 0.5f,
						iconMinimum.y + (kCombatIconSize - traceSize.y) * 0.5f),
					traceText, IM_COL32(230, 204, 255, 255), 0.67f);
				DrawOutlinedValue(resourceDrawList,
					ImVec2(imageMaximum.x + (pierceSideWidth - stackSize.x) * 0.5f,
						iconMinimum.y + (kCombatIconSize - stackSize.y) * 0.5f),
					stackText, IM_COL32(230, 204, 255, 255), 0.67f);

				const ImVec2 mouse = ImGui::GetIO().MousePos;
				if (mouse.x >= iconMinimum.x && mouse.x < imageMinimum.x && mouse.y >= iconMinimum.y &&
					mouse.y < groupMaximum.y)
				{
					ImGui::BeginTooltip();
					ImGui::TextColored(ImVec4(0.78f, 0.46f, 1.0f, 1.0f), PresentationUtf8(u8"貫通痕: %d"), traceCount);
					ImGui::TextUnformatted(PresentationUtf8(u8"フィールド上に残っている貫通痕の数です。"));
					ImGui::EndTooltip();
				}
				else if (mouse.x >= imageMinimum.x && mouse.x < imageMaximum.x && mouse.y >= imageMinimum.y &&
					mouse.y < imageMaximum.y)
				{
					ImGui::BeginTooltip();
					ImGui::TextColored(ImVec4(0.78f, 0.46f, 1.0f, 1.0f), "%s", resource.title);
					ImGui::TextUnformatted(resource.description);
					ImGui::EndTooltip();
				}
				else if (mouse.x >= imageMaximum.x && mouse.x < groupMaximum.x && mouse.y >= iconMinimum.y &&
					mouse.y < groupMaximum.y)
				{
					ImGui::BeginTooltip();
					ImGui::TextColored(ImVec4(0.78f, 0.46f, 1.0f, 1.0f), PresentationUtf8(u8"貫通スタック: %d"),
						pierceStacks);
					ImGui::TextUnformatted(PresentationUtf8(u8"次の貫通能力に使う蓄積値です。"));
					ImGui::EndTooltip();
				}
				continue;
			}
			const ImVec2 iconMaximum(iconMinimum.x + kResourceBadgeWidth, iconMinimum.y + kResourceBadgeHeight);
			ImGui::Dummy(ImVec2(kResourceBadgeWidth, kResourceBadgeHeight));
			DrawResourceBadge(ImGui::GetWindowDrawList(), iconMinimum, resource);
			if (IsMouseInside(iconMinimum, iconMaximum))
			{
				ImGui::BeginTooltip();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(resource.color), "%s: %d", resource.title,
					resource.value);
				ImGui::TextUnformatted(resource.description);
				ImGui::EndTooltip();
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void GamePresentation::DrawPierceTraces(Game& game)
{
	if (game.IsPaused() || game.GetLastBattleResult() != BattleResult::None) return;
	const auto& traces = game.GetPierceTraceState().traces;
	if (traces.empty()) return;
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImDrawList* foreground = ImGui::GetForegroundDrawList(viewport);
	const ImU32 purple = IM_COL32(205, 91, 255, 255);
	for (const auto& trace : traces)
	{
		ImVec2 start;
		ImVec2 end;
		ImVec2 directionBase;
		const Vector3 height(0.0f, 0.55f, 0.0f);
		if (!TryProjectToScreen(trace.start + height, start) ||
			!TryProjectToScreen(trace.end + height, end) ||
			!TryProjectToScreen(trace.end - trace.direction * 2.0f + height, directionBase))
			continue;
		foreground->AddLine(start, end, IM_COL32(18, 6, 28, 235), 9.0f);
		foreground->AddLine(start, end, purple, 5.0f);
		ImVec2 direction(end.x - directionBase.x, end.y - directionBase.y);
		const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
		if (length > 0.001f)
		{
			direction.x /= length;
			direction.y /= length;
			const ImVec2 perpendicular(-direction.y, direction.x);
			const ImVec2 base(end.x - direction.x * 15.0f, end.y - direction.y * 15.0f);
			foreground->AddTriangleFilled(
				end,
				ImVec2(base.x + perpendicular.x * 7.0f, base.y + perpendicular.y * 7.0f),
				ImVec2(base.x - perpendicular.x * 7.0f, base.y - perpendicular.y * 7.0f),
				purple);
		}
		char label[64]{};
		sprintf_s(label, PresentationUtf8(u8"貫通痕 #%llu"),
			static_cast<unsigned long long>(trace.id));
		const ImVec2 midpoint((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f);
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		const ImVec2 textAt(midpoint.x - textSize.x * 0.5f, midpoint.y - textSize.y - 9.0f);
		foreground->AddText(ImVec2(textAt.x + 2.0f, textAt.y + 2.0f), IM_COL32(0, 0, 0, 235), label);
		foreground->AddText(textAt, purple, label);
	}
}

void GamePresentation::DrawBattleHud(Game& game)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 workPosition = viewport->WorkPos;
	const ImVec2 workSize = viewport->WorkSize;
	const ImGuiWindowFlags fixedFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	const auto players = GameRuntime::World().GetComponents<PlayerBall>();
	const int currentHp = players.empty() || players[0] == nullptr
		? game.GetPlayerCurrentHp()
		: players[0]->GetHP();
	const int maxHp = (std::max)(1, game.GetPlayerMaxHp());
	const float hpRatio = std::clamp(
		static_cast<float>(currentHp) / static_cast<float>(maxHp),
		0.0f,
		1.0f);

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(workPosition.x + 16.0f, workPosition.y + 16.0f));
	ImGui::SetNextWindowSize(ImVec2(278.0f, 112.0f));
	ImGui::SetNextWindowBgAlpha(0.91f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(82, 98, 119, 225));
	if (ImGui::Begin("##battle_status_hud", nullptr, fixedFlags))
	{
		ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "HP  %d / %d", currentHp, maxHp);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.92f, 0.25f, 0.22f, 1.0f));
		ImGui::ProgressBar(hpRatio, ImVec2(-1.0f, 8.0f), "");
		ImGui::PopStyleColor();
		ImGui::TextDisabled("シールド %d    所持金 %d", game.GetPlayerShield(), game.GetPlayerMoney());
		char relicButton[64]{};
		sprintf_s(
			relicButton,
			PresentationUtf8(u8"レリック一覧  %d##relic_list"),
			game.GetOwnedRelicCount());
		if (ImGui::Button(relicButton, ImVec2(-1.0f, 25.0f)))
		{
			m_RelicListOpen = !m_RelicListOpen;
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		workPosition.x + workSize.x - 202.0f,
		workPosition.y + 16.0f));
	ImGui::SetNextWindowSize(ImVec2(186.0f, 51.0f));
	ImGui::SetNextWindowBgAlpha(0.91f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	if (ImGui::Begin("##deck_button_hud", nullptr, fixedFlags))
	{
		char deckButton[64]{};
		sprintf_s(
			deckButton,
			PresentationUtf8(u8"デッキ確認  %d枚##deck_list"),
			game.GetDeckBallCount());
		if (ImGui::Button(deckButton, ImVec2(-1.0f, 30.0f)))
		{
			m_DeckListView = DeckListView::All;
			m_DeckListOpen = !m_DeckListOpen;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();

	const ImVec2 pileSize(112.0f, 76.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		workPosition.x + 16.0f,
		workPosition.y + workSize.y - pileSize.y - 12.0f));
	ImGui::SetNextWindowSize(pileSize);
	if (ImGui::Begin("##draw_pile_hud", nullptr, fixedFlags | ImGuiWindowFlags_NoBackground))
	{
		if (DrawPileButton(
			"draw_pile_button",
			"残り札",
			game.GetPlayerDeckCount(),
			IM_COL32(88, 169, 255, 255),
			pileSize))
		{
			m_DeckListView = DeckListView::DrawPile;
			m_DeckListOpen = true;
		}
	}
	ImGui::End();

	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		workPosition.x + workSize.x - pileSize.x - 16.0f,
		workPosition.y + workSize.y - pileSize.y - 12.0f));
	ImGui::SetNextWindowSize(pileSize);
	if (ImGui::Begin("##discard_pile_hud", nullptr, fixedFlags | ImGuiWindowFlags_NoBackground))
	{
		if (DrawPileButton(
			"discard_pile_button",
			"捨て札",
			game.GetPlayerDiscardCount(),
			IM_COL32(205, 118, 255, 255),
			pileSize))
		{
			m_DeckListView = DeckListView::DiscardPile;
			m_DeckListOpen = true;
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();

	if (m_DeckListOpen)
	{
		DrawDeckList(game);
	}
	if (m_RelicListOpen)
	{
		DrawRelicList(game);
	}
}

void GamePresentation::DrawDeckList(Game& game)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float overlayHeight = (std::min)(440.0f, viewport->WorkSize.y - 112.0f);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		viewport->WorkPos.x + viewport->WorkSize.x - 388.0f,
		viewport->WorkPos.y + 76.0f));
	ImGui::SetNextWindowSize(ImVec2(372.0f, overlayHeight));
	ImGui::SetNextWindowBgAlpha(0.96f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(88, 113, 143, 255));
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;
	if (ImGui::Begin("##deck_list_overlay", nullptr, flags))
	{
		ImGui::TextUnformatted("デッキ確認");
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 26.0f);
		if (ImGui::SmallButton("X##close_deck"))
		{
			m_DeckListOpen = false;
		}
		ImGui::Separator();

		if (ImGui::Selectable("すべて", m_DeckListView == DeckListView::All, 0, ImVec2(88.0f, 24.0f)))
			m_DeckListView = DeckListView::All;
		ImGui::SameLine();
		if (ImGui::Selectable("残り札", m_DeckListView == DeckListView::DrawPile, 0, ImVec2(88.0f, 24.0f)))
			m_DeckListView = DeckListView::DrawPile;
		ImGui::SameLine();
		if (ImGui::Selectable("捨て札", m_DeckListView == DeckListView::DiscardPile, 0, ImVec2(88.0f, 24.0f)))
			m_DeckListView = DeckListView::DiscardPile;

		int count = 0;
		auto ballAt = [&](int index) -> const PlayerBallData*
			{
				switch (m_DeckListView)
				{
				case DeckListView::DrawPile:
					return game.GetDrawPileBall(index);
				case DeckListView::DiscardPile:
					return game.GetDiscardPileBall(index);
				default:
					return game.GetDeckBall(index);
				}
			};
		switch (m_DeckListView)
		{
		case DeckListView::DrawPile: count = game.GetPlayerDeckCount(); break;
		case DeckListView::DiscardPile: count = game.GetPlayerDiscardCount(); break;
		default: count = game.GetDeckBallCount(); break;
		}
		ImGui::TextDisabled("%d枚", count);
		ImGui::BeginChild("deck_list_rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		if (count == 0)
		{
			ImGui::TextDisabled("ここにはまだカードがありません。");
		}
		for (int index = 0; index < count; ++index)
		{
			const PlayerBallData* ball = ballAt(index);
			if (ball == nullptr) continue;
			ImGui::PushID(index);
			const auto color = PlayerBallText::GetColor(*ball);
			ImGui::ColorButton(
				"ball_color",
				ImVec4(color[0], color[1], color[2], 1.0f),
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
				ImVec2(18.0f, 18.0f));
			ImGui::SameLine();
			ImGui::Text("%s  Lv.%d", PlayerBallText::GetName(ball->definitionId), ball->upgradeLevel);
			ImGui::TextDisabled("%s", PlayerBallText::GetTrait(*ball));
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", PlayerBallText::GetDescription(ball->definitionId));
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void GamePresentation::DrawRelicList(Game& game)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float overlayHeight = (std::min)(360.0f, viewport->WorkSize.y - 164.0f);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(ImVec2(
		viewport->WorkPos.x + 16.0f,
		viewport->WorkPos.y + 138.0f));
	ImGui::SetNextWindowSize(ImVec2(362.0f, overlayHeight));
	ImGui::SetNextWindowBgAlpha(0.96f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(112, 190, 166, 255));
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;
	if (ImGui::Begin("##relic_list_overlay", nullptr, flags))
	{
		ImGui::Text("レリック一覧  %d", game.GetOwnedRelicCount());
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 26.0f);
		if (ImGui::SmallButton("X##close_relic"))
		{
			m_RelicListOpen = false;
		}
		ImGui::Separator();
		ImGui::BeginChild("relic_list_rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		bool found = false;
		for (int index = 0; index < game.GetRelicCount(); ++index)
		{
			const RelicDefinition* relic = game.GetRelic(index);
			if (relic == nullptr || !game.HasRelic(relic->type)) continue;
			found = true;
			ImGui::PushID(index);
			ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.80f, 1.0f), "%s", relic->name);
			ImGui::TextWrapped("%s", relic->description);
			ImGui::Separator();
			ImGui::PopID();
		}
		if (!found)
		{
			ImGui::TextDisabled("レリックはまだありません。");
		}
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void GamePresentation::OnBattleStarted(Game&)
{
	ClearHitReactions();
	m_CollisionVfx.Reset();
	m_CombatReactionDiagnostics = {};
	m_DefeatKeys.fill(0);
	m_TrackedDefeatCount = 0;
	m_CurrentShotDefeatCount = 0;
	m_CurrentShotChargeInterruptions = 0;
	m_CurrentShotResourceGained.fill(0);
	m_CurrentShotResourceConsumed.fill(0);
	m_CurrentShotFinisherMask = 0;
	m_HitSummaryLifetime = 0.0f;
	m_EnemyHpVisuals.clear();
	m_MultiDefeatDisplayLifetime = 0.0f;
	m_CurrentShotBallCategory = static_cast<int>(BallCategory::Standard);
	m_HeavyPhysicsChainCount = 0;
	m_HeavyChainDisplayLifetime = 0.0f;
	m_HeavyChainMilestone = false;
	m_ImpactFlashCooldown = 0.0f;
	m_CameraShakeCooldown = 0.0f;
	m_DeckListOpen = false;
	m_RelicListOpen = false;
	if (!m_TutorialCompleted)
	{
		StartTutorial(false);
	}
}

void GamePresentation::OnShotFired(int ballCategory)
{
	ClearHitReactions();
	m_CurrentShotBallCategory = ballCategory;
	m_HeavyPhysicsChainCount = 0;
	m_HeavyChainDisplayLifetime = 0.0f;
	m_HeavyChainMilestone = false;
	m_CollisionVfx.Reset();
	m_CombatReactionDiagnostics = {};
	m_DefeatKeys.fill(0);
	m_TrackedDefeatCount = 0;
	m_CurrentShotDefeatCount = 0;
	m_MultiDefeatDisplayLifetime = 0.0f;
	m_ImpactFlashCooldown = 0.0f;
	m_CameraShakeCooldown = 0.0f;
	m_CurrentShotHitCount = 0;
	m_CurrentShotDamage = 0;
	m_CurrentShotChargeInterruptions = 0;
	m_CurrentShotResourceGained.fill(0);
	m_CurrentShotResourceConsumed.fill(0);
	m_CurrentShotFinisherMask = 0;
	m_HitSummaryLifetime = 0.0f;
	if (m_TutorialActive &&
		(m_TutorialStep == TutorialStep::ChooseAndAim ||
			m_TutorialStep == TutorialStep::SetPower))
	{
		m_TutorialStep = TutorialStep::WatchShot;
	}
}

void GamePresentation::OnCombatFeedback(
	const Vector3& worldPosition,
	int damage,
	bool defeated,
	bool enemyEnemyCollision,
	std::uintptr_t targetKey,
	const std::string& targetEnemyId)
{
	(void)targetEnemyId;
	BeginHitReaction(targetKey, damage, defeated);
	if (defeated && !BeginDefeatReaction(worldPosition, targetKey))
	{
		return;
	}
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = defeated ? 1.35f : 0.82f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.fontScale = defeated
		? m_CurrentShotDefeatCount >= 3 ? 1.38f
		: m_CurrentShotDefeatCount == 2 ? 1.28f : 1.20f
		: 0.86f;
	const bool enhancedImpact =
		Game::GetInstance()->IsCollisionImpactEffectsEnabled();
	feedback.drawRing = !enhancedImpact;
	feedback.enemyEnemyCollision = enemyEnemyCollision;
	feedback.createdFrame = m_PresentationFrame;
	feedback.createdTimeSeconds = m_PresentationTimeSeconds;
	feedback.targetKey = targetKey;
	feedback.color = defeated
		? IM_COL32(255, 205, 70, 255)
		: enemyEnemyCollision
		? IM_COL32(110, 220, 255, 255)
		: IM_COL32(255, 118, 80, 255);
	if (defeated)
	{
		feedback.text = m_CurrentShotDefeatCount >= 3
			? "連続撃破！  -" + std::to_string(damage)
			: "撃破！  -" + std::to_string(damage);
	}
	else if (damage > 0)
	{
		feedback.text = "-" + std::to_string(damage) + " HP";
	}
	else
	{
		feedback.text = "防御";
	}
	bool merged = false;
	if (!defeated && damage > 0)
	{
		for (auto iterator = m_Feedback.rbegin();
			iterator != m_Feedback.rend();
			++iterator)
		{
			FloatingFeedback& existing = *iterator;
			const float ageSeconds = (std::max)(
				0.0f,
				m_PresentationTimeSeconds - existing.createdTimeSeconds);
			const Vector3 separation =
				existing.worldPosition - worldPosition;
			int existingDamage = 0;
			if (!existing.playerHudMessage &&
				existing.enemyEnemyCollision == enemyEnemyCollision &&
				CombatPresentationRules::CanMergeDamage(
					existing.targetKey,
					targetKey,
					ageSeconds,
					separation.LengthSquared(),
					defeated) &&
				sscanf_s(existing.text.c_str(), "-%d HP", &existingDamage) == 1)
			{
				existing.text = "-" +
					std::to_string(existingDamage + damage) + " HP";
				existing.worldPosition = worldPosition;
				existing.createdFrame = m_PresentationFrame;
				existing.createdTimeSeconds = m_PresentationTimeSeconds;
				existing.totalLifetime = (std::max)(
					existing.totalLifetime,
					feedback.totalLifetime);
				existing.lifetime = existing.totalLifetime;
				existing.fontScale = (std::max)(existing.fontScale, 1.08f);
				merged = true;
				break;
			}
		}
	}
	if (!merged)
	{
		int visibleDamageNumbers = 0;
		for (const FloatingFeedback& existing : m_Feedback)
		{
			int visibleDamage = 0;
			if (sscanf_s(
				existing.text.c_str(),
				"-%d HP",
				&visibleDamage) == 1)
			{
				++visibleDamageNumbers;
			}
		}
		if (defeated || damage <= 0 ||
			visibleDamageNumbers <
			CombatPresentationRules::MaximumFloatingDamageNumbers)
		{
			m_Feedback.push_back(std::move(feedback));
		}
	}
	if (!enhancedImpact || defeated)
	{
		m_ImpactFlashLifetime = (std::max)(m_ImpactFlashLifetime, 0.11f);
	}
	m_CurrentShotHitCount++;
	m_CurrentShotDamage += (std::max)(0, damage);
	if (Game::GetInstance()->IsCameraShakeEnabled() &&
		(!enhancedImpact || defeated))
	{
		m_CameraShakeLifetime = (std::max)(
			m_CameraShakeLifetime,
			defeated ? 0.24f : 0.12f);
		m_CameraShakeStrength = (std::max)(
			m_CameraShakeStrength,
			defeated ? 1.25f : 0.55f);
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(defeated ? 7 : 3, defeated ? 0.78f : 0.36f);
	}
}

void GamePresentation::OnCollisionImpact(const CollisionImpactEvent& event)
{
	const CollisionPresentationRules::Strength strength =
		CollisionPresentationRules::Classify(event.relativeApproachSpeed);
	const bool secondary =
		event.collisionKind == CombatCollisionKind::EnemyEnemy;
	const bool heavyShot = event.shotBallCategory ==
		static_cast<int>(BallCategory::Heavy);

	const float feedbackScale = strength == CollisionPresentationRules::Strength::Strong
		? 1.10f
		: strength == CollisionPresentationRules::Strength::Medium
		? 0.98f
		: 0.86f;
	for (FloatingFeedback& feedback : m_Feedback)
	{
		if (feedback.createdFrame != m_PresentationFrame ||
			feedback.enemyEnemyCollision != secondary)
		{
			continue;
		}
		const Vector3 firstDelta =
			feedback.worldPosition - event.firstTargetPosition;
		const Vector3 secondDelta =
			feedback.worldPosition - event.secondTargetPosition;
		if (firstDelta.LengthSquared() <= 0.25f ||
			(secondary && secondDelta.LengthSquared() <= 0.25f))
		{
			feedback.fontScale = (std::max)(feedback.fontScale, feedbackScale);
		}
	}

	bool milestone = false;
	int chainTier = 0;
	if (secondary && heavyShot)
	{
		++m_HeavyPhysicsChainCount;
		chainTier = CollisionPresentationRules::ChainTier(
			m_HeavyPhysicsChainCount);
		milestone = CollisionPresentationRules::IsChainMilestone(
			m_HeavyPhysicsChainCount);
		if (milestone)
		{
			m_HeavyChainDisplayTotalLifetime = chainTier >= 3 ? 1.55f : 1.25f;
			m_HeavyChainDisplayLifetime = m_HeavyChainDisplayTotalLifetime;
			m_HeavyChainMilestone = true;
		}
		else
		{
			m_HeavyChainDisplayTotalLifetime = (std::max)(
				m_HeavyChainDisplayTotalLifetime,
				0.95f);
			m_HeavyChainDisplayLifetime = (std::max)(
				m_HeavyChainDisplayLifetime,
				0.95f);
		}
	}

	Game* game = Game::GetInstance();
	const bool showMilestone =
		milestone && game->IsHeavyChainEffectsEnabled();
	if (showMilestone)
	{
		if (game->IsScreenFlashEnabled() && m_ImpactFlashCooldown <= 0.0f)
		{
			m_ImpactFlashLifetime = (std::max)(m_ImpactFlashLifetime, 0.11f);
			m_ImpactFlashCooldown = 0.16f;
		}
		if (game->IsCameraShakeEnabled() && m_CameraShakeCooldown <= 0.0f)
		{
			const float milestoneStrength = m_HeavyPhysicsChainCount >= 15
				? 1.35f
				: m_HeavyPhysicsChainCount >= 10
				? 1.05f
				: m_HeavyPhysicsChainCount >= 6 ? 0.82f : 0.62f;
			m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.18f);
			m_CameraShakeStrength = (std::max)(
				m_CameraShakeStrength,
				milestoneStrength);
			m_CameraShakeCooldown = 0.12f;
		}
	}

	const bool defeatAlreadyPresented = event.defeated &&
		(HasTrackedDefeat(event.firstTargetKey) ||
			HasTrackedDefeat(event.secondTargetKey));
	if (defeatAlreadyPresented || !game->IsCollisionImpactEffectsEnabled())
	{
		return;
	}

	CollisionVfxSystem::SpawnRequest vfx;
	vfx.worldPosition = event.worldPosition;
	vfx.collisionAxis = event.firstTargetPosition - event.secondTargetPosition;
	vfx.strength = strength;
	vfx.chainTier = chainTier;
	vfx.priority = event.defeated || showMilestone
		? 3
		: strength == CollisionPresentationRules::Strength::Strong
		? 2
		: strength == CollisionPresentationRules::Strength::Medium ? 1 : 0;
	vfx.density = game->GetDecorativeRingAmount();
	vfx.frame = m_PresentationFrame;
	vfx.secondaryCollision = secondary;
	vfx.milestone = showMilestone;
	vfx.defeated = event.defeated;
	m_CollisionVfx.Spawn(vfx);

	if (strength == CollisionPresentationRules::Strength::Strong &&
		game->IsScreenFlashEnabled() && m_ImpactFlashCooldown <= 0.0f)
	{
		m_ImpactFlashLifetime = (std::max)(m_ImpactFlashLifetime, 0.08f);
		m_ImpactFlashCooldown = 0.12f;
	}
	if (strength != CollisionPresentationRules::Strength::Weak &&
		game->IsCameraShakeEnabled() && m_CameraShakeCooldown <= 0.0f)
	{
		const float requestedStrength =
			strength == CollisionPresentationRules::Strength::Strong
			? (secondary ? 0.72f : 0.88f)
			: (secondary ? 0.30f : 0.42f);
		m_CameraShakeLifetime = (std::max)(
			m_CameraShakeLifetime,
			strength == CollisionPresentationRules::Strength::Strong
			? 0.16f
			: 0.10f);
		m_CameraShakeStrength = std::clamp(
			(std::max)(m_CameraShakeStrength, requestedStrength),
			0.0f,
			1.5f);
		m_CameraShakeCooldown = 0.08f;
	}
}

void GamePresentation::OnChainImpact(
	const Vector3& worldPosition,
	float radius,
	int hitCount)
{
	if (radius <= 0.0f)
	{
		return;
	}
	ChainRangeFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.radius = radius;
	feedback.hitCount = (std::max)(0, hitCount);
	feedback.totalLifetime = 1.05f;
	feedback.lifetime = feedback.totalLifetime;
	m_ChainRanges.push_back(feedback);
}

void GamePresentation::OnPocketFeedback(
	const Vector3& worldPosition,
	bool playerPocket,
	bool finisher,
	int damage,
	std::uintptr_t targetKey,
	const std::string& targetEnemyId)
{
	(void)targetEnemyId;
	if (!playerPocket && finisher)
	{
		BeginHitReaction(targetKey, damage, true);
		BeginDefeatReaction(worldPosition, targetKey);
	}
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = 1.5f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	if (playerPocket)
	{
		feedback.text = "スクラッチ  -" + std::to_string(damage) + " HP";
		feedback.color = IM_COL32(255, 88, 96, 255);
		m_DamageFlashLifetime = 0.28f;
	}
	else if (finisher)
	{
		feedback.text = "ポケット撃破！";
		feedback.color = IM_COL32(255, 215, 64, 255);
	}
	else
	{
		feedback.text = "ポケット制圧";
		feedback.color = IM_COL32(92, 224, 255, 255);
	}
	m_Feedback.push_back(std::move(feedback));
	if (Game::GetInstance()->IsCameraShakeEnabled())
	{
		m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.24f);
		m_CameraShakeStrength = finisher ? 1.45f : 0.85f;
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(finisher ? 9 : 5, finisher ? 0.9f : 0.55f);
	}
}

void GamePresentation::OnPlayerDamage(
	int damage,
	const std::string& source)
{
	if (damage <= 0)
	{
		return;
	}
	m_DamageFlashLifetime = (std::max)(m_DamageFlashLifetime, 0.22f);
	if (source != "pocket")
	{
		FloatingFeedback feedback;
		feedback.worldPosition = Vector3::Zero;
		feedback.text = "プレイヤー  -" + std::to_string(damage) + " HP";
		feedback.playerHudMessage = true;
		feedback.totalLifetime = 1.1f;
		feedback.lifetime = feedback.totalLifetime;
		feedback.color = IM_COL32(255, 86, 92, 255);
		m_Feedback.push_back(std::move(feedback));
	}
	if (Game::GetInstance()->IsCameraShakeEnabled())
	{
		m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.20f);
		m_CameraShakeStrength = 1.0f;
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(5, 0.62f);
	}
}

void GamePresentation::OnChargeInterrupted(
	const Vector3& worldPosition,
	ChargeInterruptReason reason)
{
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = 1.45f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	feedback.color = IM_COL32(205, 92, 255, 255);
	switch (reason)
	{
	case ChargeInterruptReason::Movement:
		feedback.text = "CHARGE BREAK  重量";
		break;
	case ChargeInterruptReason::PierceDamage:
		feedback.text = "CHARGE BREAK  貫通";
		break;
	case ChargeInterruptReason::Pocketed:
		feedback.text = "CHARGE BREAK  ポケット";
		break;
	case ChargeInterruptReason::Defeated:
		feedback.text = "CHARGE BREAK  撃破";
		break;
	default:
		return;
	}
	m_Feedback.push_back(std::move(feedback));
}

void GamePresentation::OnEnemyAbilityStateChanged(
	const EnemyAbilityStateChangedEvent& event)
{
	FloatingFeedback feedback;
	feedback.worldPosition = event.worldPosition;
	feedback.totalLifetime = 1.35f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	feedback.color = IM_COL32(92, 222, 246, 255);
	if (event.abilityId == "swarm_amplifier")
	{
		feedback.text = event.currentAttack < event.previousAttack
			? "SWARM DOWN  ATK -" +
			std::to_string(event.previousAttack - event.currentAttack)
			: "SWARM UP  ATK +" +
			std::to_string(event.currentAttack - event.previousAttack);
		feedback.color = IM_COL32(255, 92, 158, 255);
	}
	else if (event.abilityId == "impact_brittle")
	{
		feedback.text = event.currentStage > 0
			? "WALL BREAK  ATK " + std::to_string(event.currentAttack)
			: "BRITTLE END";
		CollisionVfxSystem::SpawnRequest request;
		request.worldPosition = event.worldPosition;
		request.strength = CollisionPresentationRules::Strength::Strong;
		request.priority = 2;
		request.density = 8;
		request.frame = m_PresentationFrame;
		request.milestone = true;
		request.enhanced = true;
		m_CollisionVfx.Spawn(request);
	}
	else if (event.abilityId == "protection_resonance")
	{
		feedback.text = event.currentStage > 0
			? "PROTECTED"
			: "PROTECTION LOST";
	}
	else
	{
		return;
	}
	m_Feedback.push_back(std::move(feedback));
}

void GamePresentation::OnAxisResourceChanged(
	const AxisResourceChangedEvent& event)
{
	const int delta = event.valueAfter - event.valueBefore;
	if (delta == 0 && !event.finisher)
	{
		return;
	}
	const int axis = static_cast<int>(event.axis);
	const int owner = static_cast<int>(event.owner);
	auto updateText = [](FloatingFeedback& feedback)
		{
			const auto resourceAxis = static_cast<AxisResourceKind>(
				feedback.resourceAxis);
			std::string label = AxisResourceLabel(resourceAxis);
			if (resourceAxis == AxisResourceKind::Anchor &&
				feedback.resourceOwner == static_cast<int>(AxisResourceOwner::Enemy))
			{
				label = PresentationUtf8(u8"敵Anchor");
			}
			const int change = feedback.resourceAfter - feedback.resourceBefore;
			feedback.text = label;
			if (change != 0)
			{
				feedback.text += change > 0 ? " +" : " ";
				feedback.text += std::to_string(change);
			}
			if (feedback.finisher)
			{
				feedback.text += "  FINISHER";
			}
			else
			{
				feedback.text += "  [";
				feedback.text += AxisResourceReasonLabel(
					static_cast<AxisResourceChangeReason>(feedback.resourceReason));
				feedback.text += "]";
			}
		};

	for (auto iterator = m_Feedback.rbegin();
		iterator != m_Feedback.rend();
		++iterator)
	{
		FloatingFeedback& existing = *iterator;
		if (existing.createdFrame != m_PresentationFrame ||
			existing.resourceAxis != axis ||
			existing.resourceOwner != owner ||
			existing.targetKey != event.targetKey)
		{
			continue;
		}
		existing.resourceAfter = event.valueAfter;
		existing.resourceReason = static_cast<int>(event.reason);
		existing.finisher = existing.finisher || event.finisher;
		existing.worldPosition = event.worldPosition;
		existing.totalLifetime = (std::max)(existing.totalLifetime, 1.20f);
		existing.lifetime = existing.totalLifetime;
		updateText(existing);
		return;
	}

	FloatingFeedback feedback;
	feedback.worldPosition = event.worldPosition;
	feedback.totalLifetime = event.finisher ? 1.55f : 1.10f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.fontScale = event.finisher ? 1.12f : 0.88f;
	feedback.color = AxisResourceColor(event.axis);
	feedback.createdFrame = m_PresentationFrame;
	feedback.createdTimeSeconds = m_PresentationTimeSeconds;
	feedback.targetKey = event.targetKey;
	feedback.resourceAxis = axis;
	feedback.resourceOwner = owner;
	feedback.resourceBefore = event.valueBefore;
	feedback.resourceAfter = event.valueAfter;
	feedback.resourceReason = static_cast<int>(event.reason);
	feedback.finisher = event.finisher;
	feedback.playerHudMessage = event.targetKey == 0 &&
		event.worldPosition.LengthSquared() <= 0.0001f;
	updateText(feedback);
	m_Feedback.push_back(std::move(feedback));
}

void GamePresentation::OnAxisAbilityResolved(
	const AxisAbilityResolvedEvent& event)
{
	Game* game = Game::GetInstance();
	if (game == nullptr || !game->IsCollisionImpactEffectsEnabled()) return;

	CollisionVfxSystem::Style style = CollisionVfxSystem::Style::Pierce;
	if (event.axis == AxisResourceKind::Reflection)
		style = CollisionVfxSystem::Style::Reflection;
	else if (event.axis == AxisResourceKind::Anchor)
		style = CollisionVfxSystem::Style::Anchor;

	const bool isTravel =
		event.action == AxisAbilityAction::PierceMinionTravel;
	const bool isDirected = isTravel ||
		event.action == AxisAbilityAction::PierceTraceCreated ||
		event.action == AxisAbilityAction::PierceMinionLaunched ||
		event.action == AxisAbilityAction::ReflectionWallBounce ||
		event.action == AxisAbilityAction::ReflectionPinballCollision ||
		event.action == AxisAbilityAction::AnchorRecovered;
	if (isDirected)
	{
		CollisionVfxSystem::TrailRequest trail;
		trail.startPosition = event.sourcePosition;
		trail.endPosition = event.targetPosition;
		trail.style = style;
		trail.lifetime = isTravel ? 0.20f : event.finisher ? 0.62f : 0.34f;
		trail.width = isTravel ? 2.3f : event.finisher ? 5.0f : 3.2f;
		trail.priority = event.finisher ? 4 : event.enhanced ? 3 : 2;
		trail.frame = m_PresentationFrame;
		trail.sourceKey = event.sourceKey;
		trail.enhanced = event.enhanced;
		trail.finisher = event.finisher;
		m_CollisionVfx.SpawnTrail(trail);
	}

	if (!isTravel)
	{
		CollisionVfxSystem::SpawnRequest burst;
		burst.worldPosition = event.targetPosition;
		burst.collisionAxis = event.targetPosition - event.sourcePosition;
		burst.strength = event.finisher
			? CollisionPresentationRules::Strength::Strong
			: event.enhanced
			? CollisionPresentationRules::Strength::Medium
			: CollisionPresentationRules::Strength::Weak;
		burst.chainTier = std::clamp(event.tier, 0, 5);
		burst.priority = event.finisher ? 4 : event.enhanced ? 3 : 2;
		burst.density = game->GetDecorativeRingAmount();
		burst.frame = m_PresentationFrame;
		burst.milestone = event.tier >= 3;
		burst.enhanced = event.enhanced;
		burst.finisher = event.finisher;
		burst.radius = event.radius;
		burst.style = style;
		m_CollisionVfx.Spawn(burst);
	}

	const char* label = nullptr;
	switch (event.action)
	{
	case AxisAbilityAction::PierceTraceCreated:
		label = PresentationUtf8(u8"貫通痕 生成");
		break;
	case AxisAbilityAction::PierceMinionLaunched:
		label = event.finisher && event.enhanced
			? "PIERCE OVERCHARGE"
			: event.enhanced
			? PresentationUtf8(u8"強化ミニオン 発進")
			: PresentationUtf8(u8"ミニオン 発進");
		break;
	case AxisAbilityAction::PierceFinisherHit:
		label = "PIERCE FINISHER";
		break;
	case AxisAbilityAction::ReflectionWallBounce:
		label = event.enhanced
			? PresentationUtf8(u8"強化反射")
			: PresentationUtf8(u8"反射");
		break;
	case AxisAbilityAction::ReflectionEmpoweredHit:
		label = PresentationUtf8(u8"反射強化 HIT");
		break;
	case AxisAbilityAction::ReflectionPinballActivated:
		label = "RICOCHET FINISHER";
		break;
	case AxisAbilityAction::AnchorApplied:
		label = PresentationUtf8(u8"Anchor 蓄積");
		break;
	case AxisAbilityAction::AnchorRecovered:
		label = PresentationUtf8(u8"Anchor 回収 5:1");
		break;
	case AxisAbilityAction::AnchorShieldGranted:
		label = "SHIELD GAIN";
		break;
	case AxisAbilityAction::AnchorShieldAbsorbed:
		label = "SHIELD BLOCK";
		break;
	case AxisAbilityAction::AnchorFinisherArea:
		label = "ANCHOR FINISHER";
		break;
	case AxisAbilityAction::AnchorPlayerEmpowered:
		label = "PLAYER ANCHOR UP";
		break;
	default:
		break;
	}
	if (label == nullptr) return;

	FloatingFeedback feedback;
	feedback.worldPosition = event.targetPosition;
	feedback.text = label;
	if (event.amount > 0 &&
		event.action != AxisAbilityAction::PierceMinionLaunched)
	{
		feedback.text += " +" + std::to_string(event.amount);
	}
	feedback.totalLifetime = event.finisher ? 1.65f : 0.92f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.fontScale = event.finisher ? 1.16f : 0.84f;
	feedback.color = AxisResourceColor(event.axis);
	feedback.drawRing = event.finisher;
	feedback.createdFrame = m_PresentationFrame;
	feedback.createdTimeSeconds = m_PresentationTimeSeconds;
	feedback.targetKey = event.targetKey;
	feedback.finisher = event.finisher;
	m_Feedback.push_back(std::move(feedback));
}

void GamePresentation::OnShotResolved(const ShotResolvedEvent& event)
{
	m_CurrentShotHitCount = event.hitCount;
	m_CurrentShotDamage = event.totalDamage;
	m_CurrentShotDefeatCount = event.defeatCount;
	m_CurrentShotChargeInterruptions = event.chargeInterruptions;
	m_CurrentShotResourceGained = event.resourceGained;
	m_CurrentShotResourceConsumed = event.resourceConsumed;
	m_CurrentShotFinisherMask = event.finisherMask;
	m_HitSummaryLifetime = 2.15f;
}

void GamePresentation::StartTutorial(bool replay)
{
	m_TutorialReplay = replay;
	m_TutorialActive = true;
	m_TutorialStep = TutorialStep::Welcome;
}

void GamePresentation::CompleteTutorial()
{
	m_TutorialStep = TutorialStep::Complete;
	m_TutorialActive = false;
	if (!m_TutorialReplay)
	{
		m_TutorialCompleted = true;
		SaveTutorialProgress();
	}
}

void GamePresentation::SaveTutorialProgress() const
{
	std::error_code error;
	std::filesystem::create_directories("runtime", error);
	std::ofstream destination(kTutorialProgressPath, std::ios::trunc);
	if (!destination)
	{
		return;
	}
	destination << json{
		{ "schema_version", 1 },
		{ "completed", m_TutorialCompleted },
	}.dump(2) << '\n';
}

void GamePresentation::DrawTutorial(Game& game)
{
	GameUi::PrepareWindow("tutorial", ImVec2(305, 400), ImVec2(670, 270));
	ImGui::SetNextWindowBgAlpha(0.94f);
	// Keep the guide above the battle HUD and selection cards while it is open.
	ImGui::SetNextWindowFocus();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
	if (!ImGui::Begin("プレイガイド", nullptr, flags))
	{
		ImGui::End();
		return;
	}

	const int stepNumber = static_cast<int>(m_TutorialStep) + 1;
	ImGui::TextColored(
		ImVec4(0.35f, 0.88f, 1.0f, 1.0f),
		"チュートリアル  %d / 6",
		stepNumber);
	ImGui::SameLine();
	ImGui::ProgressBar(
		static_cast<float>(stepNumber) / 6.0f,
		ImVec2(220.0f, 0.0f),
		"");
	ImGui::Separator();

	switch (m_TutorialStep)
	{
	case TutorialStep::Welcome:
		ImGui::TextUnformatted("ボールを弾いて敵を倒す、ビリヤード型ローグライトです。");
		ImGui::TextUnformatted("各ボールにはカテゴリと「生成・利用・決着」の役割があります。");
		ImGui::TextUnformatted("まずは1ショットを操作して、基本の流れを確認します。");
		if (ImGui::Button("ガイドを開始", ImVec2(170.0f, 0.0f)))
		{
			m_TutorialStep = TutorialStep::ChooseAndAim;
		}
		break;
	case TutorialStep::ChooseAndAim:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "1. ボールを選び、狙う");
		ImGui::TextUnformatted("カードにはカテゴリ・役割・攻撃力・特性が表示されます。");
		ImGui::TextUnformatted("未選択カードをクリックすると選択します。");
		ImGui::TextUnformatted("選択中のカードを再クリックすると詳細を開きます。");
		ImGui::TextUnformatted("マウスを動かして軌道予測線を敵へ合わせ、左ボタンを押してください。");
		break;
	case TutorialStep::SetPower:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "2. パワーを決める");
		ImGui::TextUnformatted("左ボタンを押したまま手前へドラッグするとパワーが上がります。");
		ImGui::TextUnformatted("離すとショット。右クリックで狙い直せます。");
		break;
	case TutorialStep::WatchShot:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "3. 衝突結果を見る");
		ImGui::TextUnformatted("赤い数値は与えたダメージ、「撃破！」は敵を倒した印です。");
		ImGui::TextUnformatted("カテゴリ固有資源は、対応カテゴリの生成球だけが蓄積できます。");
		ImGui::TextUnformatted("対象：重量カウント / 貫通痕 / クッションスタック / 錨スタック");
		ImGui::TextUnformatted("貫通痕とクッションは全カテゴリで利用でき、カテゴリ一致時だけ強い効果になります。");
		break;
	case TutorialStep::ReadResult:
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.3f, 1.0f), "4. ターンの流れ");
		ImGui::TextUnformatted("ボールが止まると敵が反撃し、次の使用球を選びます。");
		ImGui::TextUnformatted("生成球で資源を作り、利用球・決着球へつないで敵を倒しましょう。");
		ImGui::TextUnformatted("敵を全滅させると報酬を選び、次のルートへ進めます。");
		if (ImGui::Button("理解した", ImVec2(150.0f, 0.0f)))
		{
			m_TutorialStep = TutorialStep::Complete;
		}
		break;
	case TutorialStep::Complete:
		ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "基本操作は完了です！");
		ImGui::TextUnformatted("F1でガイドを再表示、F2でプレイログの分析画面を開けます。");
		if (ImGui::Button("ゲームへ戻る", ImVec2(170.0f, 0.0f)))
		{
			CompleteTutorial();
		}
		break;
	}

	ImGui::Separator();
	if (ImGui::SmallButton("スキップ"))
	{
		CompleteTutorial();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("F1: 表示切替");
	if (!IsBattleScene(game))
	{
		ImGui::SameLine();
		ImGui::TextDisabled("（戦闘開始後に操作できます）");
	}
	ImGui::End();
}

void GamePresentation::DrawFeedback(Game& game)
{
	ImDrawList* foreground = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
	ImDrawList* effects = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 topLeft = viewport->Pos;
	const ImVec2 bottomRight(
		viewport->Pos.x + viewport->Size.x,
		viewport->Pos.y + viewport->Size.y);
	if (game.IsScreenFlashEnabled() && m_ImpactFlashLifetime > 0.0f)
	{
		const float alpha = m_ImpactFlashLifetime / 0.11f;
		foreground->AddRectFilled(
			topLeft,
			bottomRight,
			IM_COL32(255, 238, 190, static_cast<int>(36.0f * alpha)));
	}
	if (game.IsScreenFlashEnabled() && m_DamageFlashLifetime > 0.0f)
	{
		const float alpha = std::clamp(m_DamageFlashLifetime / 0.28f, 0.0f, 1.0f);
		const ImU32 red = IM_COL32(255, 45, 55, static_cast<int>(80.0f * alpha));
		const float border = 24.0f + 24.0f * alpha;
		foreground->AddRectFilled(topLeft, ImVec2(bottomRight.x, topLeft.y + border), red);
		foreground->AddRectFilled(ImVec2(topLeft.x, bottomRight.y - border), bottomRight, red);
		foreground->AddRectFilled(topLeft, ImVec2(topLeft.x + border, bottomRight.y), red);
		foreground->AddRectFilled(ImVec2(bottomRight.x - border, topLeft.y), bottomRight, red);
	}

	for (const ChainRangeFeedback& feedback : m_ChainRanges)
	{
		constexpr int segmentCount = 64;
		std::vector<ImVec2> points;
		points.reserve(segmentCount);
		for (int index = 0; index < segmentCount; ++index)
		{
			const float angle = DirectX::XM_2PI *
				static_cast<float>(index) / static_cast<float>(segmentCount);
			const Vector3 worldPoint = feedback.worldPosition + Vector3(
				std::cos(angle) * feedback.radius,
				0.18f,
				std::sin(angle) * feedback.radius);
			ImVec2 screenPoint;
			if (TryProjectToScreen(worldPoint, screenPoint))
			{
				points.push_back(screenPoint);
			}
		}
		if (points.size() != segmentCount)
		{
			continue;
		}
		const float progress = 1.0f - feedback.lifetime / feedback.totalLifetime;
		const float fade = std::clamp(feedback.lifetime / 0.28f, 0.0f, 1.0f);
		const float pulse = 0.65f +
			0.35f * std::sin(progress * DirectX::XM_PI * 3.0f);
		const ImU32 purple = IM_COL32(198, 86, 255, 255);
		foreground->AddConvexPolyFilled(
			points.data(),
			static_cast<int>(points.size()),
			WithAlpha(purple, fade * 0.11f));
		foreground->AddPolyline(
			points.data(),
			static_cast<int>(points.size()),
			WithAlpha(purple, fade * (0.65f + pulse * 0.25f)),
			ImDrawFlags_Closed,
			3.0f + pulse * 2.0f);

		ImVec2 center;
		if (TryProjectToScreen(
			feedback.worldPosition + Vector3(0.0f, 1.0f, 0.0f),
			center))
		{
			std::ostringstream label;
			label << "連鎖範囲 " << std::fixed << std::setprecision(0)
				<< feedback.radius << "  /  " << feedback.hitCount << "体に連鎖";
			const std::string text = label.str();
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());
			const ImVec2 at(
				center.x - size.x * 0.5f,
				center.y - size.y - 18.0f);
			foreground->AddText(
				ImVec2(at.x + 2.0f, at.y + 2.0f),
				IM_COL32(0, 0, 0, static_cast<int>(220.0f * fade)),
				text.c_str());
			foreground->AddText(at, WithAlpha(purple, fade), text.c_str());
		}
	}

	m_CollisionVfx.Draw(
		effects,
		&TryProjectToScreen,
		game.IsCollisionImpactEffectsEnabled() || game.IsDefeatVfxEnabled());

	if (m_HeavyChainDisplayLifetime > 0.0f &&
		m_HeavyPhysicsChainCount > 0 &&
		game.IsHeavyChainEffectsEnabled())
	{
		const int tier = CollisionPresentationRules::ChainTier(
			m_HeavyPhysicsChainCount);
		const ImU32 colors[5] = {
			IM_COL32(205, 238, 255, 255),
			IM_COL32(255, 224, 92, 255),
			IM_COL32(255, 154, 58, 255),
			IM_COL32(255, 178, 38, 255),
			IM_COL32(255, 238, 150, 255),
		};
		const float progress = 1.0f -
			m_HeavyChainDisplayLifetime / m_HeavyChainDisplayTotalLifetime;
		const float intro = std::clamp(progress / 0.22f, 0.0f, 1.0f);
		const float eased = 1.0f - std::pow(1.0f - intro, 3.0f);
		const float overshoot = std::sin(intro * DirectX::XM_PI) *
			(tier >= 3 ? 0.18f : 0.08f);
		const float baseScale = tier >= 3
			? 2.05f
			: tier == 2 ? 1.72f : tier == 1 ? 1.46f : 1.12f;
		const float scale = baseScale * (0.82f + eased * 0.18f + overshoot);
		const float alpha = std::clamp(
			m_HeavyChainDisplayLifetime / 0.24f,
			0.0f,
			1.0f);
		const std::string chainText =
			"CHAIN " + std::to_string(m_HeavyPhysicsChainCount);
		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize() * scale;
		const ImVec2 size = font->CalcTextSizeA(
			fontSize,
			FLT_MAX,
			0.0f,
			chainText.c_str());
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float centerX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.405f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float centerY = viewport->Pos.y +
			(avoidDebugWindow ? 108.0f : 88.0f);
		const float bannerWidth = tier >= 3 ? 360.0f : tier == 2 ? 320.0f : 276.0f;
		const float bannerHeight = tier >= 3 ? 62.0f : 56.0f;
		const ImVec2 bannerMinimum(
			centerX - bannerWidth * 0.5f,
			centerY - bannerHeight * 0.5f);
		const ImVec2 bannerMaximum(
			centerX + bannerWidth * 0.5f,
			centerY + bannerHeight * 0.5f);

		if (tier >= 2)
		{
			foreground->PushClipRect(
				ImVec2(bannerMinimum.x - 42.0f, bannerMinimum.y - 34.0f),
				ImVec2(bannerMaximum.x + 42.0f, bannerMaximum.y + 34.0f),
				true);
			const int rayCount = tier >= 3 ? 12 : 8;
			for (int index = 0; index < rayCount; ++index)
			{
				const float angle = DirectX::XM_2PI *
					static_cast<float>(index) / static_cast<float>(rayCount);
				const float inner = tier >= 3 ? 45.0f : 34.0f;
				const float outer = inner + 58.0f +
					26.0f * std::abs(std::sin(angle * 2.3f));
				foreground->AddLine(
					ImVec2(centerX + std::cos(angle) * inner,
						centerY + std::sin(angle) * inner * 0.45f),
					ImVec2(centerX + std::cos(angle) * outer,
						centerY + std::sin(angle) * outer * 0.45f),
					WithAlpha(colors[tier], alpha * (tier >= 3 ? 0.22f : 0.15f)),
					tier >= 3 ? 1.8f : 1.2f);
			}
			foreground->PopClipRect();
			foreground->AddRectFilled(
				bannerMinimum,
				bannerMaximum,
				IM_COL32(24, 10, 5, static_cast<int>(176.0f * alpha)),
				18.0f);
			foreground->AddRect(
				bannerMinimum,
				bannerMaximum,
				WithAlpha(colors[tier], alpha * 0.88f),
				18.0f,
				0,
				tier >= 3 ? 3.2f : 2.0f);
			foreground->AddLine(
				ImVec2(bannerMinimum.x + 26.0f, bannerMaximum.y + 7.0f),
				ImVec2(centerX - 28.0f, bannerMaximum.y + 7.0f),
				WithAlpha(colors[tier], alpha * 0.82f),
				2.0f);
			foreground->AddLine(
				ImVec2(centerX + 28.0f, bannerMaximum.y + 7.0f),
				ImVec2(bannerMaximum.x - 26.0f, bannerMaximum.y + 7.0f),
				WithAlpha(colors[tier], alpha * 0.82f),
				2.0f);
			const float ornament = tier >= 3 ? 14.0f : 9.0f;
			foreground->AddTriangleFilled(
				ImVec2(bannerMinimum.x - ornament, centerY),
				ImVec2(bannerMinimum.x + 5.0f, centerY - ornament),
				ImVec2(bannerMinimum.x + 5.0f, centerY + ornament),
				WithAlpha(colors[tier], alpha));
			foreground->AddTriangleFilled(
				ImVec2(bannerMaximum.x + ornament, centerY),
				ImVec2(bannerMaximum.x - 5.0f, centerY - ornament),
				ImVec2(bannerMaximum.x - 5.0f, centerY + ornament),
				WithAlpha(colors[tier], alpha));
		}

		const ImVec2 at(centerX - size.x * 0.5f, centerY - size.y * 0.58f);
		const ImU32 outline = IM_COL32(92, 18, 4, static_cast<int>(245.0f * alpha));
		for (const ImVec2 offset : {
			ImVec2(-3.0f, 0.0f), ImVec2(3.0f, 0.0f),
				ImVec2(0.0f, -3.0f), ImVec2(0.0f, 3.0f) })
		{
			foreground->AddText(
				font,
				fontSize,
				ImVec2(at.x + offset.x, at.y + offset.y),
				outline,
				chainText.c_str());
		}
		foreground->AddText(
			font,
			fontSize,
			at,
			WithAlpha(colors[tier], alpha),
			chainText.c_str());
		foreground->AddText(
			font,
			fontSize,
			ImVec2(at.x, at.y - 1.5f),
			IM_COL32(255, 250, 220, static_cast<int>(105.0f * alpha)),
			chainText.c_str());
		const char* meaning = PresentationUtf8(
			u8"採用された敵同士の接触回数");
		const ImVec2 meaningSize = ImGui::CalcTextSize(meaning);
		foreground->AddText(
			ImVec2(
				centerX - meaningSize.x * 0.5f,
				centerY + bannerHeight * 0.5f + 9.0f),
			IM_COL32(0, 0, 0, static_cast<int>(220.0f * alpha)),
			meaning);
		foreground->AddText(
			ImVec2(
				centerX - meaningSize.x * 0.5f,
				centerY + bannerHeight * 0.5f + 7.0f),
			IM_COL32(238, 242, 248, static_cast<int>(190.0f * alpha)),
			meaning);
	}

	if (m_MultiDefeatDisplayLifetime > 0.0f &&
		m_CurrentShotDefeatCount >= 2 &&
		game.IsMultiDefeatEffectsEnabled())
	{
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float centerX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.70f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float centerY = viewport->Pos.y +
			(avoidDebugWindow ? 250.0f :
				(m_HeavyChainDisplayLifetime > 0.0f ? 154.0f : 94.0f));
		const float elapsed = (std::max)(
			0.0f,
			m_MultiDefeatDisplayTotalLifetime - m_MultiDefeatDisplayLifetime);
		constexpr float popDuration = 0.12f;
		constexpr float settleDuration = 0.25f;
		constexpr float holdDuration = 0.80f;
		constexpr float fadeDuration = 1.00f;
		float popScale = 1.0f;
		if (elapsed < popDuration)
		{
			popScale = 1.0f + 0.35f * (elapsed / popDuration);
		}
		else if (elapsed < popDuration + settleDuration)
		{
			const float settle =
				(elapsed - popDuration) / settleDuration;
			popScale = 1.35f - 0.35f * settle;
		}
		const float fadeStart = popDuration + settleDuration + holdDuration;
		const float alpha = elapsed <= fadeStart
			? 1.0f
			: std::clamp(1.0f - (elapsed - fadeStart) / fadeDuration, 0.0f, 1.0f);
		const int tier = CombatPresentationRules::MultiDefeatTier(
			m_CurrentShotDefeatCount);
		const ImU32 accent = tier >= 2
			? IM_COL32(255, 132, 58, static_cast<int>(255.0f * alpha))
			: IM_COL32(255, 220, 92, static_cast<int>(255.0f * alpha));
		const std::string text = m_CurrentShotDefeatCount == 2
			? "DOUBLE BREAK"
			: "MULTI BREAK  x" + std::to_string(m_CurrentShotDefeatCount);
		ImFont* multiFont = ImGui::GetFont();
		const float multiFontSize = ImGui::GetFontSize() *
			(tier >= 2 ? 1.30f : 1.18f) * popScale;
		const ImVec2 textSize = multiFont->CalcTextSizeA(
			multiFontSize,
			FLT_MAX,
			0.0f,
			text.c_str());
		const float width = (std::max)(252.0f, textSize.x + 82.0f);
		const float halfHeight = 23.0f * popScale;
		const ImVec2 minimum(centerX - width * 0.5f, centerY - halfHeight);
		const ImVec2 maximum(centerX + width * 0.5f, centerY + halfHeight);
		foreground->AddRectFilled(
			minimum,
			maximum,
			IM_COL32(12, 6, 5, static_cast<int>(238.0f * alpha)),
			13.0f);
		foreground->AddRect(minimum, maximum, accent, 13.0f, 0, 2.5f);
		foreground->AddLine(
			ImVec2(minimum.x - 34.0f, centerY),
			ImVec2(minimum.x - 4.0f, centerY),
			accent,
			3.0f);
		foreground->AddLine(
			ImVec2(maximum.x + 4.0f, centerY),
			ImVec2(maximum.x + 34.0f, centerY),
			accent,
			3.0f);
		const ImVec2 textAt(
			centerX - textSize.x * 0.5f,
			centerY - textSize.y * 0.5f);
		for (const ImVec2 offset : {
			ImVec2(-2.0f, 0.0f), ImVec2(2.0f, 0.0f),
				ImVec2(0.0f, -2.0f), ImVec2(0.0f, 2.0f) })
		{
			foreground->AddText(
				multiFont,
				multiFontSize,
				ImVec2(textAt.x + offset.x, textAt.y + offset.y),
				IM_COL32(0, 0, 0, static_cast<int>(250.0f * alpha)),
				text.c_str());
		}
		foreground->AddText(
			multiFont,
			multiFontSize,
			textAt,
			WithAlpha(
				IM_COL32(255, 238, 188, 255),
				alpha),
			text.c_str());
	}

	std::vector<std::size_t> feedbackOrder;
	feedbackOrder.reserve(m_Feedback.size());
	for (std::size_t index = 0; index < m_Feedback.size(); ++index)
		feedbackOrder.push_back(index);
	std::stable_sort(
		feedbackOrder.begin(),
		feedbackOrder.end(),
		[&](std::size_t first, std::size_t second)
		{
			const auto priority = [](const FloatingFeedback& feedback)
				{
					if (feedback.text.find("撃破") != std::string::npos) return 10000;
					int damage = 0;
					return sscanf_s(feedback.text.c_str(), "-%d HP", &damage) == 1
						? 100 + damage
						: 0;
				};
			return priority(m_Feedback[first]) > priority(m_Feedback[second]);
		});
	std::vector<ImVec4> placedFeedbackRectangles;
	placedFeedbackRectangles.reserve(m_Feedback.size() + 16);
	if (m_HeavyChainDisplayLifetime > 0.0f &&
		CollisionPresentationRules::ChainTier(m_HeavyPhysicsChainCount) >= 2)
	{
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float protectedCenterX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.405f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float protectedCenterY = viewport->Pos.y +
			(avoidDebugWindow ? 108.0f : 88.0f);
		placedFeedbackRectangles.emplace_back(
			protectedCenterX - 205.0f,
			protectedCenterY - 40.0f,
			protectedCenterX + 205.0f,
			protectedCenterY + 52.0f);
	}
	if (m_MultiDefeatDisplayLifetime > 0.0f &&
		m_CurrentShotDefeatCount >= 2)
	{
		const bool avoidDebugWindow = game.IsDebugMode() || GameUi::showDebugger;
		const float centerX = avoidDebugWindow
			? viewport->Pos.x + viewport->Size.x * 0.70f
			: viewport->Pos.x + viewport->Size.x * 0.5f;
		const float centerY = viewport->Pos.y +
			(avoidDebugWindow ? 250.0f :
				(m_HeavyChainDisplayLifetime > 0.0f ? 154.0f : 94.0f));
		placedFeedbackRectangles.emplace_back(
			centerX - 178.0f,
			centerY - 30.0f,
			centerX + 178.0f,
			centerY + 30.0f);
	}
	if (m_HitSummaryLifetime > 0.0f)
	{
		placedFeedbackRectangles.emplace_back(
			viewport->Pos.x + viewport->Size.x - 400.0f,
			viewport->Pos.y + 264.0f,
			viewport->Pos.x + viewport->Size.x - 18.0f,
			viewport->Pos.y + 360.0f);
	}
	for (const EnemyBall* enemy :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed()) continue;
		ImVec2 center;
		ImVec2 top;
		if (!TryProjectToScreen(enemy->GetPosition(), center) ||
			!TryProjectToScreen(
				enemy->GetPosition() + Vector3(0.0f, enemy->GetVisualRadius(), 0.0f),
				top))
		{
			continue;
		}
		const float radius = (std::max)(14.0f, std::abs(center.y - top.y));
		placedFeedbackRectangles.emplace_back(
			center.x - 108.0f,
			center.y - radius - 76.0f,
			center.x + 108.0f,
			center.y + radius + 108.0f);
	}
	const std::array<ImVec2, 24> feedbackOffsets = {
		ImVec2(0.0f, 0.0f),
		ImVec2(-34.0f, -26.0f), ImVec2(34.0f, -26.0f),
		ImVec2(-62.0f, -10.0f), ImVec2(62.0f, -10.0f),
		ImVec2(-42.0f, 28.0f), ImVec2(42.0f, 28.0f),
		ImVec2(0.0f, 46.0f),
		ImVec2(-78.0f, 34.0f), ImVec2(78.0f, 34.0f),
		ImVec2(-96.0f, 0.0f), ImVec2(96.0f, 0.0f),
		ImVec2(-72.0f, 68.0f), ImVec2(72.0f, 68.0f),
		ImVec2(0.0f, 76.0f),
		ImVec2(-108.0f, 58.0f), ImVec2(108.0f, 58.0f),
		ImVec2(-120.0f, 18.0f), ImVec2(120.0f, 18.0f),
		ImVec2(-45.0f, 98.0f), ImVec2(45.0f, 98.0f),
		ImVec2(0.0f, 112.0f),
		ImVec2(-105.0f, 96.0f), ImVec2(105.0f, 96.0f),
	};
	for (const std::size_t feedbackIndex : feedbackOrder)
	{
		const FloatingFeedback& feedback = m_Feedback[feedbackIndex];
		ImVec2 position;
		ImVec2 worldAnchor;
		const bool playerHudMessage = feedback.playerHudMessage;
		if (playerHudMessage)
		{
			position = ImVec2(
				viewport->Pos.x + viewport->Size.x * 0.5f,
				viewport->Pos.y +
				(m_HeavyChainDisplayLifetime > 0.0f ? 278.0f : 105.0f));
			worldAnchor = position;
		}
		else if (!TryProjectToScreen(feedback.worldPosition, worldAnchor))
		{
			continue;
		}
		else
		{
			position = worldAnchor;
		}

		const float progress = 1.0f -
			feedback.lifetime / feedback.totalLifetime;
		position.y -= 20.0f + progress * 58.0f;
		const float fadeIn = std::clamp(progress * 8.0f, 0.0f, 1.0f);
		const float fadeOut = std::clamp(
			feedback.lifetime / 0.28f,
			0.0f,
			1.0f);
		const float alpha = fadeIn * fadeOut;
		ImFont* font = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize() * feedback.fontScale;
		const ImVec2 textSize = font->CalcTextSizeA(
			fontSize,
			FLT_MAX,
			0.0f,
			feedback.text.c_str());
		ImVec2 textPosition(
			position.x - textSize.x * 0.5f,
			position.y - textSize.y * 0.5f);
		if (!playerHudMessage)
		{
			float bestScore = FLT_MAX;
			ImVec2 bestPosition = textPosition;
			ImVec4 bestRectangle{};
			for (const ImVec2& offset : feedbackOffsets)
			{
				ImVec2 candidate(
					textPosition.x + offset.x,
					textPosition.y + offset.y);
				candidate.x = std::clamp(
					candidate.x,
					viewport->Pos.x + 12.0f,
					viewport->Pos.x + viewport->Size.x - textSize.x - 12.0f);
				candidate.y = std::clamp(
					candidate.y,
					viewport->Pos.y + 28.0f,
					viewport->Pos.y + viewport->Size.y - textSize.y - 18.0f);
				const ImVec4 candidateRectangle(
					candidate.x - 5.0f,
					candidate.y - 3.0f,
					candidate.x + textSize.x + 5.0f,
					candidate.y + textSize.y + 3.0f);
				float overlapArea = 0.0f;
				for (const ImVec4& placed : placedFeedbackRectangles)
				{
					const float width = (std::max)(
						0.0f,
						(std::min)(candidateRectangle.z, placed.z) -
						(std::max)(candidateRectangle.x, placed.x));
					const float height = (std::max)(
						0.0f,
						(std::min)(candidateRectangle.w, placed.w) -
						(std::max)(candidateRectangle.y, placed.y));
					overlapArea += width * height;
				}
				const float distancePenalty =
					(offset.x * offset.x + offset.y * offset.y) * 0.012f;
				const float score = overlapArea * 12.0f + distancePenalty;
				if (score < bestScore)
				{
					bestScore = score;
					bestPosition = candidate;
					bestRectangle = candidateRectangle;
					if (overlapArea <= 0.0f && distancePenalty <= 0.0f) break;
				}
			}
			textPosition = bestPosition;
			placedFeedbackRectangles.push_back(bestRectangle);
		}
		const bool defeated =
			feedback.text.find("撃破") != std::string::npos;
		if (defeated)
		{
			foreground->AddRectFilled(
				ImVec2(textPosition.x - 7.0f, textPosition.y - 4.0f),
				ImVec2(textPosition.x + textSize.x + 7.0f,
					textPosition.y + textSize.y + 4.0f),
				IM_COL32(38, 18, 2, static_cast<int>(156.0f * alpha)),
				7.0f);
		}
		else if (!playerHudMessage)
		{
			foreground->AddRectFilled(
				ImVec2(textPosition.x - 5.0f, textPosition.y - 2.0f),
				ImVec2(textPosition.x + textSize.x + 5.0f,
					textPosition.y + textSize.y + 2.0f),
				IM_COL32(6, 12, 18, static_cast<int>(92.0f * alpha)),
				5.0f);
		}
		for (const ImVec2 outlineOffset : {
			ImVec2(-1.8f, 0.0f), ImVec2(1.8f, 0.0f),
				ImVec2(0.0f, -1.8f), ImVec2(0.0f, 1.8f) })
		{
			foreground->AddText(
				font,
				fontSize,
				ImVec2(
					textPosition.x + outlineOffset.x,
					textPosition.y + outlineOffset.y),
				IM_COL32(0, 0, 0, static_cast<int>(225.0f * alpha)),
				feedback.text.c_str());
		}
		foreground->AddText(
			font,
			fontSize,
			textPosition,
			WithAlpha(feedback.color, alpha),
			feedback.text.c_str());
		if (feedback.drawRing)
		{
			const float radius = 16.0f + progress * 48.0f;
			foreground->AddCircle(
				worldAnchor,
				radius,
				WithAlpha(feedback.color, alpha * 0.7f),
				32,
				3.0f);
		}
	}

	if (GameUi::showDebugger && IsBattleScene(game))
	{
		const CollisionVfxDiagnostics& vfx =
			m_CollisionVfx.GetDiagnostics();
		char diagnostics[256]{};
		sprintf_s(
			diagnostics,
			"VFX impact=%d chain=%d sparks=%d/%d waves=%d/%d burst=%d/%d trail=%d/%d hitflash=%d/%d defeat=%d dup=%d gen=%d skip=%d merge=%d prim=%d/%d",
			vfx.impactEvents,
			m_HeavyPhysicsChainCount,
			vfx.activeSparks,
			vfx.maximumActiveSparks,
			vfx.activeWaves,
			vfx.maximumActiveWaves,
			vfx.activeBursts,
			vfx.maximumActiveBursts,
			vfx.activeTrails,
			vfx.maximumActiveTrails,
			m_CombatReactionDiagnostics.activeHitFlashes,
			m_CombatReactionDiagnostics.maximumActiveHitFlashes,
			m_CombatReactionDiagnostics.uniqueDefeats,
			m_CombatReactionDiagnostics.duplicateDefeatsSuppressed,
			vfx.sparksGenerated + vfx.burstsGenerated + vfx.trailsGenerated,
			vfx.sparksSkipped + vfx.wavesSkipped + vfx.burstsSkipped +
			vfx.trailsSkipped,
			vfx.wavesMerged,
			vfx.primitiveSubmissionsLastFrame,
			vfx.maximumPrimitiveSubmissions);
		foreground->AddText(
			ImVec2(viewport->Pos.x + 18.0f, viewport->Pos.y + viewport->Size.y - 28.0f),
			IM_COL32(210, 235, 255, 230),
			diagnostics);
		const FrameTimingSnapshot& timing = game.GetFrameTimingSnapshot();
		char timingText[256]{};
		sprintf_s(
			timingText,
			"FRAME %s n=%d avg=%.2f p95=%.2f p99=%.2f max=%.2f ms  update=%.2f draw=%.2f present=%.2f",
			timing.active ? "MEASURING" : timing.completed ? "SHOT" : "IDLE",
			timing.sampleCount,
			timing.averageFrameMilliseconds,
			timing.p95FrameMilliseconds,
			timing.p99FrameMilliseconds,
			timing.maximumFrameMilliseconds,
			timing.averageUpdateMilliseconds,
			timing.averageDrawMilliseconds,
			timing.averagePresentMilliseconds);
		foreground->AddText(
			ImVec2(viewport->Pos.x + 18.0f, viewport->Pos.y + viewport->Size.y - 50.0f),
			IM_COL32(255, 232, 150, 235),
			timingText);
	}

	if (m_HitSummaryLifetime > 0.0f)
	{
		std::vector<std::string> lines;
		std::ostringstream primary;
		primary << "SHOT RESULT   DMG " << m_CurrentShotDamage
			<< "   HIT " << m_CurrentShotHitCount;
		lines.push_back(primary.str());
		if (m_CurrentShotDefeatCount > 0 ||
			m_CurrentShotChargeInterruptions > 0)
		{
			std::ostringstream result;
			if (m_CurrentShotDefeatCount > 0)
				result << "BREAK " << m_CurrentShotDefeatCount;
			if (m_CurrentShotChargeInterruptions > 0)
			{
				if (m_CurrentShotDefeatCount > 0) result << "   ";
				result << "CHARGE BREAK " << m_CurrentShotChargeInterruptions;
			}
			lines.push_back(result.str());
		}
		std::ostringstream resources;
		int resourceItems = 0;
		for (std::size_t index = 0;
			index < m_CurrentShotResourceGained.size();
			++index)
		{
			const int gained = m_CurrentShotResourceGained[index];
			const int consumed = m_CurrentShotResourceConsumed[index];
			if (gained <= 0 && consumed <= 0) continue;
			if (resourceItems > 0) resources << "   ";
			resources << AxisResourceLabel(
				static_cast<AxisResourceKind>(index));
			if (gained > 0) resources << " +" << gained;
			if (consumed > 0) resources << " -" << consumed;
			++resourceItems;
			if (resourceItems >= 2) break;
		}
		if (m_CurrentShotFinisherMask != 0)
		{
			if (resourceItems > 0) resources << "   ";
			resources << "FINISHER";
		}
		if (resourceItems > 0 || m_CurrentShotFinisherMask != 0)
			lines.push_back(resources.str());
		if (lines.size() == 1 && m_CurrentShotHitCount == 0)
			lines.push_back("NO HIT");

		float width = 0.0f;
		for (const std::string& line : lines)
			width = (std::max)(width, ImGui::CalcTextSize(line.c_str()).x);
		width += 34.0f;
		const float height = 18.0f +
			static_cast<float>(lines.size()) * 22.0f;
		// Keep the result below the built-in top-right HUD and the debug-battle
		// controls.  Dense battles also tend to occupy the centre-left, so this
		// preserves a stable, readable result area without covering enemy state.
		constexpr float summaryTop = 270.0f;
		const ImVec2 maximum(
			viewport->Pos.x + viewport->Size.x - 26.0f,
			viewport->Pos.y + summaryTop + height);
		const ImVec2 minimum(
			maximum.x - width,
			viewport->Pos.y + summaryTop);
		const float alpha = std::clamp(
			m_HitSummaryLifetime / 0.28f,
			0.0f,
			1.0f);
		foreground->AddRectFilled(
			minimum,
			maximum,
			IM_COL32(8, 13, 22, static_cast<int>(226.0f * alpha)),
			9.0f);
		foreground->AddRect(
			minimum,
			maximum,
			IM_COL32(255, 207, 72, static_cast<int>(235.0f * alpha)),
			9.0f,
			0,
			1.6f);
		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			foreground->AddText(
				ImVec2(minimum.x + 17.0f, minimum.y + 10.0f + index * 22.0f),
				index == 0
				? IM_COL32(255, 224, 100, static_cast<int>(255.0f * alpha))
				: IM_COL32(224, 235, 246, static_cast<int>(240.0f * alpha)),
				lines[index].c_str());
		}
	}
}

void GamePresentation::DrawDashboard()
{
	GameUi::PrepareWindow("dashboard", ImVec2(120, 35), ImVec2(1040, 650));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (!ImGui::Begin("バランス分析ダッシュボード", &m_DashboardOpen, flags))
	{
		ImGui::End();
		return;
	}

	if (ImGui::Button("レポートを再読込"))
	{
		LoadDashboard();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("F2で開閉 / %s", kBalanceReportPath);
	ImGui::Separator();
	if (!m_Dashboard.loaded)
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
			"%s",
			m_Dashboard.error.c_str());
		ImGui::TextWrapped(
			"tools\\analyze_balance_logs.py を実行してレポートを生成してください。");
		ImGui::End();
		return;
	}

	ImGui::Text("現行バランス: %s", m_Dashboard.currentBalanceVersion.c_str());
	ImGui::SameLine();
	if (m_Dashboard.currentVersionHasData)
	{
		ImGui::TextColored(
			ImVec4(0.35f, 0.9f, 0.52f, 1.0f),
			"計測済み");
	}
	else
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
			"未計測");
	}
	if (!m_Dashboard.versions.empty())
	{
		m_Dashboard.selectedVersionIndex = std::clamp(
			m_Dashboard.selectedVersionIndex,
			0,
			static_cast<int>(m_Dashboard.versions.size()) - 1);
		const char* selectedLabel = m_Dashboard.versions[
			m_Dashboard.selectedVersionIndex].version.c_str();
			ImGui::SetNextItemWidth(230.0f);
			if (ImGui::BeginCombo("表示バージョン", selectedLabel))
			{
				for (std::size_t index = 0;
					index < m_Dashboard.versions.size();
					++index)
				{
					const DashboardVersionData& item = m_Dashboard.versions[index];
					std::string label = item.version;
					if (item.current) label += " (現行)";
					if (!item.hasData) label += " - 未計測";
					const bool selected = static_cast<int>(index) ==
						m_Dashboard.selectedVersionIndex;
					if (ImGui::Selectable(label.c_str(), selected))
					{
						m_Dashboard.selectedVersionIndex = static_cast<int>(index);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			const char* comparisonLabel = "比較なし";
			if (m_Dashboard.comparisonVersionIndex >= 0 &&
				m_Dashboard.comparisonVersionIndex <
				static_cast<int>(m_Dashboard.versions.size()))
			{
				comparisonLabel = m_Dashboard.versions[
					m_Dashboard.comparisonVersionIndex].version.c_str();
			}
			ImGui::SetNextItemWidth(230.0f);
			if (ImGui::BeginCombo("比較対象", comparisonLabel))
			{
				if (ImGui::Selectable(
					"比較なし",
					m_Dashboard.comparisonVersionIndex < 0))
				{
					m_Dashboard.comparisonVersionIndex = -1;
				}
				for (std::size_t index = 0;
					index < m_Dashboard.versions.size();
					++index)
				{
					if (static_cast<int>(index) ==
						m_Dashboard.selectedVersionIndex)
					{
						continue;
					}
					const DashboardVersionData& item = m_Dashboard.versions[index];
					std::string label = item.version;
					if (!item.hasData) label += " - 未計測";
					if (ImGui::Selectable(
						label.c_str(),
						static_cast<int>(index) ==
						m_Dashboard.comparisonVersionIndex))
					{
						m_Dashboard.comparisonVersionIndex = static_cast<int>(index);
					}
				}
				ImGui::EndCombo();
			}

			const DashboardVersionData& selectedVersion =
				m_Dashboard.versions[m_Dashboard.selectedVersionIndex];
			const DashboardVersionData* comparisonVersion = nullptr;
			if (m_Dashboard.comparisonVersionIndex >= 0 &&
				m_Dashboard.comparisonVersionIndex <
				static_cast<int>(m_Dashboard.versions.size()))
			{
				comparisonVersion = &m_Dashboard.versions[
					m_Dashboard.comparisonVersionIndex];
			}
			ImGui::TextDisabled(
				"最終計測: %s / ラン: %d / 完了ラン: %d / ショット: %d",
				selectedVersion.lastMeasuredAt.empty()
				? "未計測"
				: selectedVersion.lastMeasuredAt.c_str(),
				selectedVersion.runCount,
				selectedVersion.terminalRunCount,
				selectedVersion.totalShotCount);
			if (!selectedVersion.conditionSummary.empty())
			{
				ImGui::TextWrapped(
					"計測条件: %s",
					selectedVersion.conditionSummary.c_str());
			}
			if (!selectedVersion.hasData)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
					"このバージョンのログはありません。過去値や仮値は表示していません。");
			}
			if (selectedVersion.smallSampleWarning && selectedVersion.hasData)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.75f, 0.28f, 1.0f),
					"注意: 30ラン未満のため傾向判断には追加計測が必要です。");
			}
			if (selectedVersion.mixedConfigurationWarning)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
					"同一版に複数設定があり、最新設定コホートだけを表示しています。");
			}
			if (selectedVersion.heterogeneousConditionsWarning)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
					"計測条件が複数混在しています。版内の数値も記述統計として扱ってください。");
			}
			if (comparisonVersion != nullptr &&
				selectedVersion.hasData && comparisonVersion->hasData &&
				selectedVersion.conditionSignature !=
				comparisonVersion->conditionSignature)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
					"比較条件が一致しません。シード・操作主体・ビルド・敵構成差を含むため、改善/悪化を断定できません。");
			}

			const auto drawMetric = [&comparisonVersion](
				const char* label,
				float value,
				bool available,
				float otherValue,
				bool otherAvailable,
				bool percentage)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
					ImGui::TableNextColumn();
					if (available)
						ImGui::Text(percentage ? "%.1f%%" : "%.2f",
							percentage ? value * 100.0f : value);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (comparisonVersion == nullptr) ImGui::TextDisabled("-");
					else if (otherAvailable)
						ImGui::Text(percentage ? "%.1f%%" : "%.2f",
							percentage ? otherValue * 100.0f : otherValue);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (comparisonVersion != nullptr && available && otherAvailable)
						ImGui::Text(percentage ? "%+.1fpt" : "%+.2f",
							percentage
							? (value - otherValue) * 100.0f
							: value - otherValue);
					else ImGui::TextDisabled("-");
				};
			if (ImGui::BeginTable(
				"version_metric_table",
				4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("指標");
				ImGui::TableSetupColumn(selectedVersion.version.c_str());
				ImGui::TableSetupColumn(
					comparisonVersion != nullptr
					? comparisonVersion->version.c_str()
					: "比較対象");
				ImGui::TableSetupColumn("差分");
				ImGui::TableHeadersRow();
				const DashboardVersionData empty{};
				const DashboardVersionData& other = comparisonVersion != nullptr
					? *comparisonVersion
					: empty;
				drawMetric("クリア率", selectedVersion.clearRate,
					selectedVersion.hasClearRate, other.clearRate,
					other.hasClearRate, true);
				drawMetric("平均ダメージ/ショット", selectedVersion.averageDamage,
					selectedVersion.hasAverageDamage, other.averageDamage,
					other.hasAverageDamage, false);
				drawMetric("平均ショット数/完了ラン", selectedVersion.averageShots,
					selectedVersion.hasAverageShots, other.averageShots,
					other.hasAverageShots, false);
				drawMetric("平均戦闘ターン/ステージ",
					selectedVersion.averageBattleTurns,
					selectedVersion.hasAverageBattleTurns,
					other.averageBattleTurns,
					other.hasAverageBattleTurns, false);
				drawMetric("反射後攻撃成功率",
					selectedVersion.reflectionSuccessRate,
					selectedVersion.hasReflectionSuccessRate,
					other.reflectionSuccessRate,
					other.hasReflectionSuccessRate, true);
				ImGui::EndTable();
			}
			const char* axisLabels[] = { "重量", "貫通", "反射", "Anchor" };
			if (ImGui::BeginTable(
				"axis_metric_table",
				4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("軸");
				ImGui::TableSetupColumn("獲得");
				ImGui::TableSetupColumn("消費");
				ImGui::TableSetupColumn("フィニッシャー");
				ImGui::TableHeadersRow();
				for (int axis = 0; axis < 4; ++axis)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(axisLabels[axis]);
					ImGui::TableNextColumn();
					if (selectedVersion.hasAxisGained[axis])
						ImGui::Text("%d", selectedVersion.axisGained[axis]);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (selectedVersion.hasAxisConsumed[axis])
						ImGui::Text("%d", selectedVersion.axisConsumed[axis]);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (selectedVersion.hasAxisFinishers[axis])
						ImGui::Text("%d", selectedVersion.axisFinishers[axis]);
					else ImGui::TextDisabled("未計測");
				}
				ImGui::EndTable();
			}
			if (!selectedVersion.historyReason.empty())
			{
				ImGui::TextWrapped(
					"変更履歴 [%s]: %s",
					selectedVersion.historyTarget.c_str(),
					selectedVersion.historyReason.c_str());
			}
			ImGui::SeparatorText("生成時選択コホートの詳細");
	}
	if (m_Dashboard.analyzedRuns <= 0)
	{
		ImGui::TextDisabled(
			"選択コホートの計測ログがないため、詳細指標は未計測です。");
		ImGui::End();
		return;
	}

	if (ImGui::BeginTable("summary_cards", 4, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextColumn();
		ImGui::TextDisabled("総合スコア");
		ImGui::Text("%.1f / 100", m_Dashboard.balanceScore);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("判定");
		ImGui::TextColored(
			JudgementColor(m_Dashboard.judgement),
			"%s",
			JudgementLabel(m_Dashboard.judgement));
		ImGui::TableNextColumn();
		ImGui::TextDisabled("分析ログ");
		ImGui::Text("%dラン", m_Dashboard.analyzedRuns);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("完了ラン母数");
		ImGui::Text("%d件", m_Dashboard.balanceSamples);
		ImGui::EndTable();
	}
	ImGui::TextDisabled("生成: %s", m_Dashboard.generatedAt.c_str());
	if (!m_Dashboard.warning.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.28f, 1.0f));
		ImGui::TextWrapped("注意: %s", m_Dashboard.warning.c_str());
		ImGui::PopStyleColor();
	}

	if (ImGui::BeginTabBar("dashboard_tabs"))
	{
		if (ImGui::BeginTabItem("ラン進行"))
		{
			ImGui::Spacing();
			for (const DashboardMetric& metric : m_Dashboard.metrics)
			{
				ImGui::PushID(metric.id.c_str());
				const float displayValue = metric.percentage
					? metric.value * 100.0f
					: metric.value;
				const float displayMin = metric.percentage
					? metric.targetMin * 100.0f
					: metric.targetMin;
				const float displayMax = metric.percentage
					? metric.targetMax * 100.0f
					: metric.targetMax;
				ImGui::TextColored(
					metric.inRange
					? ImVec4(0.35f, 0.9f, 0.52f, 1.0f)
					: ImVec4(1.0f, 0.48f, 0.32f, 1.0f),
					"%s",
					metric.inRange ? "OK" : "要確認");
				ImGui::SameLine(86.0f);
				if (metric.percentage)
				{
					ImGui::Text(
						"%-24s %6.1f%%   目標 %.1f?%.1f%%",
						metric.label.c_str(),
						displayValue,
						displayMin,
						displayMax);
				}
				else
				{
					ImGui::Text(
						"%-24s %6.1f   目標 %.1f?%.1f",
						metric.label.c_str(),
						displayValue,
						displayMin,
						displayMax);
				}
				const float scaleMax = (std::max)(
					metric.targetMax * 1.35f,
					metric.value * 1.1f);
				const float fraction = scaleMax > 0.0f
					? std::clamp(metric.value / scaleMax, 0.0f, 1.0f)
					: 0.0f;
				ImGui::PushStyleColor(
					ImGuiCol_PlotHistogram,
					metric.inRange
					? ImVec4(0.25f, 0.78f, 0.45f, 1.0f)
					: ImVec4(0.92f, 0.35f, 0.25f, 1.0f));
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 12.0f), "");
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("ステージ別"))
		{
			const ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable;
			if (ImGui::BeginTable(
				"stage_table",
				7,
				tableFlags,
				ImVec2(0.0f, 390.0f)))
			{
				ImGui::TableSetupColumn("ステージ");
				ImGui::TableSetupColumn("種別");
				ImGui::TableSetupColumn("母数");
				ImGui::TableSetupColumn("勝率");
				ImGui::TableSetupColumn("中央値打数");
				ImGui::TableSetupColumn("残HP");
				ImGui::TableSetupColumn("スコア / 判定");
				ImGui::TableHeadersRow();
				for (const StageSummary& stage : m_Dashboard.stages)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.id.c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.type.c_str());
					ImGui::TableNextColumn(); ImGui::Text("%d", stage.sampleCount);
					ImGui::TableNextColumn(); ImGui::Text("%.1f%%", stage.clearRate * 100.0f);
					ImGui::TableNextColumn(); ImGui::Text("%.1f", stage.medianShots);
					ImGui::TableNextColumn(); ImGui::Text("%.1f%%", stage.remainingHpRate * 100.0f);
					ImGui::TableNextColumn();
					ImGui::TextColored(
						JudgementColor(stage.judgement),
						"%.1f  %s",
						stage.balanceScore,
						JudgementLabel(stage.judgement));
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("ビルド・経済・ポケット"))
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("使用ボール構成");
			const float shares[2] = {
				m_Dashboard.standardShotShare,
				m_Dashboard.heavyShotShare,
			};
			ImGui::Text("スタンダード球  %.1f%%", shares[0] * 100.0f);
			ImGui::ProgressBar(shares[0], ImVec2(-1.0f, 16.0f), "");
			ImGui::Text("ヘビー球        %.1f%%", shares[1] * 100.0f);
			ImGui::ProgressBar(shares[1], ImVec2(-1.0f, 16.0f), "");
			ImGui::Separator();
			ImGui::Text(
				"戦術的ポケット使用ラン率  %.1f%%",
				m_Dashboard.tacticalPocketRate * 100.0f);
			ImGui::Text(
				"1ラン平均獲得マネー        %.1f",
				m_Dashboard.averageMoneyPerRun);
			ImGui::TextWrapped(
				"これらは因果関係ではなく記述統計です。使用率が高いボールが強いとは限らないため、プレイヤープロフィールと設定コホートを揃えて比較してください。");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void GamePresentation::LoadDashboard()
{
	m_Dashboard = DashboardData{};
	std::ifstream source(kBalanceReportPath);
	if (!source)
	{
		m_Dashboard.error =
			"分析レポートが見つかりません: " +
			std::string(kBalanceReportPath);
		return;
	}

	try
	{
		json report;
		source >> report;
		m_Dashboard.generatedAt = report.value("generated_at", "unknown");
		m_Dashboard.analyzedRuns = report.value("analyzed_file_count", 0);
		m_Dashboard.currentBalanceVersion = report.value(
			"current_balance_version",
			std::string("Unknown"));
		m_Dashboard.selectedBalanceVersion = report.value(
			"selected_balance_version",
			std::string("Legacy/Unknown"));
		m_Dashboard.latestMeasuredBalanceVersion = report.value(
			"latest_measured_balance_version",
			std::string());
		m_Dashboard.currentVersionHasData = report.value(
			"current_version_has_data",
			false);
		if (report.contains("version_catalog") &&
			report["version_catalog"].is_array())
		{
			for (const json& item : report["version_catalog"])
			{
				if (!item.is_object())
				{
					continue;
				}
				DashboardVersionData version;
				version.version = ReadString(item, "version");
				version.lastMeasuredAt = ReadString(item, "last_measured_at");
				version.configurationFingerprint = ReadString(
					item,
					"configuration_fingerprint");
				version.hasData = item.value("has_data", false);
				version.current = item.value("is_current", false);
				version.smallSampleWarning = item.value(
					"small_sample_warning",
					false);
				version.mixedConfigurationWarning = item.value(
					"mixed_configuration_warning",
					false);
				version.heterogeneousConditionsWarning = item.value(
					"heterogeneous_conditions_warning",
					false);
				version.runCount = item.value("run_count", 0);
				const json& conditions = item.value(
					"conditions",
					json::object());
				if (conditions.is_object())
				{
					version.conditionSignature = ReadString(
						conditions,
						"condition_signature");
					const auto summarizeCounts = [](const json& counts)
						{
							std::ostringstream stream;
							if (!counts.is_object()) return stream.str();
							int displayed = 0;
							for (auto iterator = counts.begin();
								iterator != counts.end() && displayed < 3;
								++iterator, ++displayed)
							{
								if (displayed > 0) stream << ", ";
								stream << iterator.key() << ':';
								if (iterator.value().is_number_integer())
									stream << iterator.value().get<int>();
								else stream << '?';
							}
							if (counts.size() > 3) stream << ", ...";
							return stream.str();
						};
					const json& controllers = conditions.value(
						"controller_types", json::object());
					const json& profiles = conditions.value(
						"controller_profiles", json::object());
					const json& builds = conditions.value(
						"build_profiles", json::object());
					const json& compositions = conditions.value(
						"stage_compositions", json::object());
					std::ostringstream conditionStream;
					conditionStream
						<< "操作[" << summarizeCounts(controllers) << "] / "
						<< "Profile[" << summarizeCounts(profiles) << "] / "
						<< "Build[" << summarizeCounts(builds) << "] / "
						<< "構成パターン " << compositions.size() << " / "
						<< "Seed記録 " << conditions.value(
							"runs_with_recorded_seeds", 0)
						<< '/' << version.runCount;
					version.conditionSummary = conditionStream.str();
				}
				const json& history = item.value(
					"history",
					json::object());
				if (history.is_object())
				{
					version.historyReason = ReadString(history, "reason");
					version.historyTarget = ReadString(history, "target");
				}
				const json& observed = item.value(
					"metrics",
					json::object());
				const auto readOptional = [](
					const json& object,
					const char* key,
					float& value,
					bool& available)
					{
						available = object.is_object() &&
							object.contains(key) &&
							object[key].is_number();
						if (available)
						{
							value = object[key].get<float>();
						}
					};
				if (observed.is_object())
				{
					version.terminalRunCount = observed.value(
						"terminal_run_count",
						0);
					version.totalShotCount = observed.value(
						"total_shot_count",
						0);
					readOptional(observed, "clear_rate",
						version.clearRate, version.hasClearRate);
					readOptional(observed, "average_damage_per_shot",
						version.averageDamage, version.hasAverageDamage);
					readOptional(observed, "average_shots_per_terminal_run",
						version.averageShots, version.hasAverageShots);
					readOptional(observed, "average_battle_turns_per_stage",
						version.averageBattleTurns,
						version.hasAverageBattleTurns);
					readOptional(observed, "reflection_success_rate",
						version.reflectionSuccessRate,
						version.hasReflectionSuccessRate);
					const json& axes = observed.value("axes", json::object());
					const char* axisNames[] = {
						"heavy", "pierce", "reflection", "anchor" };
					for (int axisIndex = 0; axisIndex < 4; ++axisIndex)
					{
						if (!axes.contains(axisNames[axisIndex]) ||
							!axes[axisNames[axisIndex]].is_object())
						{
							continue;
						}
						const json& axis = axes[axisNames[axisIndex]];
						const auto readAxisValue = [&axis](
							const char* key,
							int& value,
							bool& available)
							{
								available = axis.contains(key) && axis[key].is_number_integer();
								if (available) value = axis[key].get<int>();
							};
						readAxisValue("stacks_gained",
							version.axisGained[axisIndex],
							version.hasAxisGained[axisIndex]);
						readAxisValue("stacks_consumed",
							version.axisConsumed[axisIndex],
							version.hasAxisConsumed[axisIndex]);
						readAxisValue("finisher_activations",
							version.axisFinishers[axisIndex],
							version.hasAxisFinishers[axisIndex]);
					}
				}
				m_Dashboard.versions.push_back(std::move(version));
			}
		}
		for (std::size_t index = 0;
			index < m_Dashboard.versions.size();
			++index)
		{
			if (m_Dashboard.versions[index].version ==
				m_Dashboard.selectedBalanceVersion)
			{
				m_Dashboard.selectedVersionIndex = static_cast<int>(index);
				break;
			}
		}
		const json& runSelection = report.value(
			"run_selection",
			json::object());
		m_Dashboard.warning = runSelection.value("warning", "");

		const json& run = report.value("run_balance", json::object());
		m_Dashboard.balanceSamples = run.value("balance_sample_count", 0);
		m_Dashboard.balanceScore = ReadNumber(run, "balance_score");
		m_Dashboard.judgement = ReadString(run, "judgement");
		const json& metrics = run.value("metrics", json::object());
		const json& statuses = run.value("metric_status", json::object());
		const auto addMetric = [&](
			const char* id,
			const char* label,
			bool percentage)
			{
				DashboardMetric metric;
				metric.id = id;
				metric.label = label;
				metric.value = ReadNumber(metrics, id);
				if (statuses.contains(id) && statuses[id].is_object())
				{
					metric.targetMin = ReadNumber(statuses[id], "target_min");
					metric.targetMax = ReadNumber(statuses[id], "target_max", 1.0f);
					metric.inRange = statuses[id].value("in_range", false);
				}
				metric.percentage = percentage;
				m_Dashboard.metrics.push_back(std::move(metric));
			};
		addMetric("first_boss_reach_rate", "第1ボス到達率", true);
		addMetric("first_boss_clear_rate", "第1ボス撃破率", true);
		addMetric("second_boss_reach_rate", "第2ボス到達率", true);
		addMetric("median_reached_progress", "到達進行度中央値", false);
		addMetric("terminal_run_rate", "正常終了ラン率", true);

		const json& system = report.value("system_metrics", json::object());
		const json& build = system.value("build", json::object());
		const json& usage = build.value("ball_usage", json::object());
		const json& shares = usage.value(
			"shot_share_by_ball_id",
			json::object());
		m_Dashboard.standardShotShare = ReadNumber(
			shares,
			"player_standard");
		m_Dashboard.heavyShotShare = ReadNumber(
			shares,
			"player_heavy");
		const json& pockets = system.value("pockets", json::object());
		m_Dashboard.tacticalPocketRate = ReadNumber(
			pockets,
			"tactical_pocket_run_rate");
		const json& economy = system.value("economy", json::object());
		m_Dashboard.averageMoneyPerRun = ReadNumber(
			economy,
			"average_money_earned_per_run");

		if (report.contains("stages") && report["stages"].is_array())
		{
			for (const json& item : report["stages"])
			{
				StageSummary stage;
				stage.id = ReadString(item, "stage_id");
				stage.type = ReadString(item, "stage_type");
				stage.sampleCount = item.value("sample_count", 0);
				const json& stageMetrics = item.value(
					"metrics",
					json::object());
				stage.clearRate = ReadNumber(stageMetrics, "clear_rate");
				stage.medianShots = ReadNumber(stageMetrics, "median_shots");
				stage.remainingHpRate = ReadNumber(
					stageMetrics,
					"average_remaining_hp_ratio");
				stage.balanceScore = ReadNumber(item, "balance_score");
				stage.judgement = ReadString(item, "judgement");
				m_Dashboard.stages.push_back(std::move(stage));
			}
		}
		m_Dashboard.loaded = true;
	}
	catch (const json::exception& exception)
	{
		m_Dashboard.error =
			"分析レポートを読み込めません: " +
			std::string(exception.what());
	}
}

bool GamePresentation::IsBattleScene(const Game& game) const
{
	return dynamic_cast<const BattleScene*>(game.GetCurrentScene()) != nullptr;
}

// Game keeps only the timing decision; presentation owns the UI details.
void Game::DrawPauseUI()
{
	if (m_GamePresentation == nullptr)
	{
		return;
	}

	PausePresentationModel model{};
	model.settings = m_SettingsRuntime->Current();
	model.debugMode = IsDebugMode();
	model.confirmReturnToTitle =
		m_PauseState->IsReturnToTitleConfirmationActive();
	model.battleScene = GetCurrentSceneType() == SceneType::Battle;
	model.clearRewardActive = IsClearRewardActive();
	const PausePresentationIntent intent = m_GamePresentation->DrawPause(model);

	if (intent.settingsChanged)
	{
		if (m_SettingsRuntime->Replace(intent.settings))
		{
			Input::SetVibration(0, 0.0f);
		}
	}
	if (intent.applyDisplay)
	{
		m_PauseState->RequestDisplayApply();
		m_SettingsRuntime->SaveIfDirty();
	}
	if (intent.resume)
	{
		m_SettingsRuntime->SaveIfDirty();
		m_PauseState->ResumeAndDismissConfirmation();
	}
	if (intent.requestDebugExit)
	{
		m_DebugController->RequestExit();
	}
	if (intent.requestReturnConfirmation)
	{
		m_PauseState->RequestReturnToTitleConfirmation();
	}
	if (intent.saveAndReturnToTitle)
	{
		SaveAndReturnToTitle();
	}
	if (intent.cancelReturnToTitle)
	{
		m_PauseState->CancelReturnToTitleConfirmation();
	}
}

void Game::DrawBallSelectionUI()
{
	if (m_GamePresentation == nullptr)
	{
		return;
	}

	BallSelectionPresentationModel model{};
	model.selectedOfferIndex = GetSelectedBallOfferIndex();
	model.selectedHoldIndex = GetHeldBallOfferIndex();
	const int offerCount = GetBallOfferCount();
	model.offers.reserve(static_cast<std::size_t>(offerCount));
	model.effectiveAttacks.reserve(static_cast<std::size_t>(offerCount));
	for (int index = 0; index < offerCount; ++index)
	{
		const PlayerBallData* ball = GetBallOffer(index);
		model.offers.push_back(ball);
		model.effectiveAttacks.push_back(GetEffectivePlayerBallAttack(ball));
	}

	const BallSelectionPresentationIntent intent =
		m_GamePresentation->DrawBallSelection(model);
	m_BallSelectionState->ApplyPresentationSelection(
		intent.selectedOfferIndex,
		intent.selectedHoldIndex,
		offerCount);
	if (intent.selectionChanged)
	{
		ApplySelectedBallPreview();
	}
}

void Game::DrawClearRewardUI()
{
	if (m_GamePresentation == nullptr)
	{
		return;
	}

	ClearRewardPresentationModel model{};
	model.stageRewardMoney = m_RunController->GetCurrentStageRewardMoney();
	model.playerMoney = m_RunController->Status().money;
	model.playerCurrentHp = m_RunController->Status().currentHp;
	model.playerMaxHp = m_RunController->Status().maxHp;
	model.midBossRelicSelectionActive =
		m_RewardState->IsMidBossRelicSelectionActive();
	model.clearRewardChosen = m_RewardState->IsChosen();
	model.selectedRelicOfferIndex = m_RewardState->SelectedRelicOfferIndex();
	model.selectedRewardIndex = m_RewardState->SelectedRewardIndex();
	model.selectedRewardBallIndex = m_RewardState->SelectedRewardBallIndex();
	model.extraMoneyAmount = kClearRewardExtraMoney;
	model.rewardMessage = m_RewardState->Message();
	for (int index = 0; index < GetMidBossRelicOfferCount(); ++index)
	{
		model.relicOffers.push_back(GetMidBossRelicOffer(index));
	}
	for (int index = 0; index < GetClearRewardBallOfferCount(); ++index)
	{
		model.newBallOffers.push_back(GetClearRewardBallOffer(index));
	}
	const int targetCount = GetDeckBallCount();
	model.upgradeTargets.reserve(static_cast<std::size_t>(targetCount));
	for (int index = 0; index < targetCount; ++index)
	{
		model.upgradeTargets.push_back({
			GetDeckBall(index),
			GetClearRewardUpgradeCost(index),
			});
	}

	const ClearRewardPresentationIntent intent =
		m_GamePresentation->DrawClearReward(model);
	m_RewardState->ApplyPresentationSelection(
		intent.selectedRelicOfferIndex,
		intent.selectedRewardIndex,
		intent.selectedRewardBallIndex);
	if (intent.confirm)
	{
		m_RewardState->RequestConfirmation();
	}
}
