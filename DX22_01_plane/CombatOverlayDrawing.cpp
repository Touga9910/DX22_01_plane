#include "CombatOverlayDrawing.h"
#include "GamePresentationPrimitives.h"
#include "GameEvent.h"
#include "EnemyData.h"
#include "BattleTypes.h"
#include "StatusEffect.h"
#include "SettingsManager.h"
#include "Renderer.h"
#include "imgui/imgui.h"
#include "stb_image.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <wrl/client.h>

using namespace GamePresentationPrimitives;

namespace CombatOverlayDrawing
{
namespace
{
struct CombatIconTexture
{
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
	int width = 0;
	int height = 0;

	/// <summary>
	/// アイコンのテクスチャビューと画像寸法が描画に使えるか判定する。
	/// </summary>
	bool IsValid() const noexcept
	{
		return view != nullptr && width > 0 && height > 0;
	}
};
class CombatIconResources final
{
public:
	/// <summary>
	/// 戦闘アイコンごとに画像を読み込み、読み込みの試行を一度だけ行う。
	/// </summary>
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

	/// <summary>
	/// 指定された戦闘アイコンの描画用テクスチャが有効なら返す。
	/// </summary>
	const CombatIconTexture* Get(CombatIconKind kind) const noexcept
	{
		const CombatIconTexture& texture = m_Textures[static_cast<std::size_t>(kind)];
		return texture.IsValid() ? &texture : nullptr;
	}

private:
	/// <summary>
	/// 画像ファイルをRGBAで読み込み、Direct3Dで描画するためのテクスチャビューを作成する。
	/// </summary>
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

/// <summary>
/// 戦闘アイコン用リソースを保持する共有インスタンスを返す。
/// </summary>
CombatIconResources& GetCombatIconResources()
{
	static CombatIconResources resources;
	return resources;
}
/// <summary>
/// テクスチャのシェーダーリソースビューをImGuiのテクスチャ参照に変換する。
/// </summary>
ImTextureRef MakeCombatTextureReference(const CombatIconTexture& texture)
{
	return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.view.Get())));
}
}

/// <summary>
/// 戦闘オーバーレイで使うアイコン群の読み込みを要求する。
/// </summary>
void LoadIcons() { GetCombatIconResources().Load(); }

/// <summary>
/// 指定された戦闘アイコンを、画面上の範囲に色補正をかけて描画する。
/// </summary>
void DrawCombatIcon(ImDrawList* drawList, CombatIconKind kind, const ImVec2& minimum,
	const ImVec2& maximum, ImU32 tint)
{
	const CombatIconTexture* texture = GetCombatIconResources().Get(kind);
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

/// <summary>
/// 戦闘中の数値や短い文字列を、縁取り付きで指定位置に表示する。
/// </summary>
void DrawOutlinedValue(ImDrawList* drawList, const ImVec2& position, const char* value,
	ImU32 color, float fontScale)
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

/// <summary>
/// 敵の残りHPの割合に応じて、HP演出の色と透明度を設定する。
/// </summary>
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

/// <summary>
/// 指定の角度範囲に、色と線幅を適用した円弧を描画する。
/// </summary>
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

/// <summary>
/// 現在HPとダメージの残像を、敵球の画面上の位置に合わせてリングで表示する。
/// </summary>
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

/// <summary>
/// 状態効果を画面上に表示する日本語名に変換する。
/// </summary>
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

/// <summary>
/// 敵の次の行動を画面上に表示する日本語名に変換する。
/// </summary>
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

/// <summary>
/// 敵の行動の種類に対応するバッジ色を返す。
/// </summary>
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

/// <summary>
/// 敵の行動に応じたアイコンと表示色で行動予告バッジを描画する。
/// </summary>
void DrawEnemyActionBadge(ImDrawList* drawList, const ImVec2& minimum, EnemyActionType action)
{
	const ImVec2 maximum(minimum.x + kCombatIconSize, minimum.y + kCombatIconSize);
	CombatIconKind icon{};
	bool hasIcon = true;
	switch (action)
	{
	case EnemyActionType::NormalAttack:
		icon = CombatIconKind::IntentAttack;
		break;
	case EnemyActionType::ChargeStart:
		icon = CombatIconKind::IntentChargeStart;
		break;
	case EnemyActionType::ChargeAttack:
		icon = CombatIconKind::IntentChargeAttack;
		break;
	default:
		hasIcon = false;
		break;
	}
	if (hasIcon)
	{
		DrawCombatIcon(drawList, icon, minimum, maximum);
	}
}

/// <summary>
/// 攻撃・防御の状態効果をアイコンで示し、効果量を重ねて描画する。
/// </summary>
void DrawStatusIcon(ImDrawList* drawList, const ImVec2& minimum, StatusEffectType type, int magnitude)
{
	const ImVec2 maximum(minimum.x + kCombatIconSize, minimum.y + kCombatIconSize);
	const CombatIconKind icon =
		IsAttackStatusEffect(type) ? CombatIconKind::BuffAttack : CombatIconKind::BuffDefense;
	DrawCombatIcon(drawList, icon, minimum, maximum);
	char value[16]{};
	const int signedMagnitude = IsPositiveStatusEffect(type) ? magnitude : -magnitude;
	sprintf_s(value, "%+d", signedMagnitude);
	const float fontSize = ImGui::GetFontSize() * 0.72f;
	const ImVec2 valueSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, value);
	DrawOutlinedValue(drawList, ImVec2(maximum.x - valueSize.x, maximum.y - valueSize.y), value);
}

/// <summary>
/// 指定されたリソースのアイコンと数値をバッジとして描画する。
/// </summary>
void DrawResourceBadge(ImDrawList* drawList, const ImVec2& minimum, const ResourceBadge& badge)
{
	const ImVec2 maximum(minimum.x + kResourceBadgeWidth, minimum.y + kResourceBadgeHeight);
	DrawCombatIcon(drawList, badge.icon, minimum, maximum);
	char text[32]{};
	sprintf_s(text, "%d", badge.value);
	const float fontSize = ImGui::GetFontSize() * 0.72f;
	const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
	DrawOutlinedValue(drawList, ImVec2(maximum.x - textSize.x, maximum.y - textSize.y), text,
		IM_COL32(255, 255, 255, 255));
}

/// <summary>
/// マウス座標が指定された画面上の矩形に入っているか判定する。
/// </summary>
bool IsMouseInside(const ImVec2& minimum, const ImVec2& maximum)
{
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	return mouse.x >= minimum.x && mouse.x < maximum.x &&
		mouse.y >= minimum.y && mouse.y < maximum.y;
}
}
