#pragma once

#include <cstddef>

struct ImDrawList;
struct ImVec2;
struct EnemyHpDisplaySettings;
enum class EnemyActionType;
enum class StatusEffectType;

namespace CombatOverlayDrawing
{
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

struct ResourceBadge
{
	CombatIconKind icon = CombatIconKind::ResourceHeavy;
	const char* title = "";
	const char* description = "";
	int value = 0;
	unsigned int color = 0xffffffffu;
};

    constexpr float kCombatIconSize = 20.8f;
    constexpr float kCombatIconSpacing = 2.4f;
    constexpr float kResourceBadgeWidth = kCombatIconSize;
    constexpr float kResourceBadgeHeight = kCombatIconSize;

    void LoadIcons();
    void DrawCombatIcon(ImDrawList* drawList, CombatIconKind kind,
        const ImVec2& minimum, const ImVec2& maximum, unsigned int tint = 0xffffffffu);
    void DrawOutlinedValue(ImDrawList* drawList, const ImVec2& position, const char* value,
        unsigned int color = 0xffffffffu, float fontScale = 0.76f);
    unsigned int EnemyHpColor(float hpRatio, float opacity);
    void DrawArc(ImDrawList* drawList, const ImVec2& center, float radius,
        float startAngle, float endAngle, unsigned int color, float thickness);
    void DrawEnemyHpRing(const EnemyHpDisplaySettings& settings,
        const ImVec2& projectedCenter, float projectedRadius, float hpRatio,
        float trailingRatio, float damageHold);
    const char* StatusEffectName(StatusEffectType type);
    const char* EnemyActionDisplayName(EnemyActionType action);
    unsigned int EnemyActionColor(EnemyActionType action);
    void DrawEnemyActionBadge(ImDrawList* drawList, const ImVec2& minimum, EnemyActionType action);
    void DrawStatusIcon(ImDrawList* drawList, const ImVec2& minimum, StatusEffectType type, int magnitude);
    void DrawResourceBadge(ImDrawList* drawList, const ImVec2& minimum, const ResourceBadge& badge);
    bool IsMouseInside(const ImVec2& minimum, const ImVec2& maximum);
}
