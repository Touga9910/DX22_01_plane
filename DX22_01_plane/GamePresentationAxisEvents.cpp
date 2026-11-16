#include "GamePresentation.h"
#include "GamePresentationPrimitives.h"
#include "Game.h"
#include "GameEvent.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <utility>

using namespace GamePresentationPrimitives;

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
