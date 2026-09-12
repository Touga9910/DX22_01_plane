#pragma once
#include "BallStatus.h"
#include "json/json.hpp"
#include <cmath>

inline BallStatus ReadBallStatus(const nlohmann::json& value, BallStatus status = {})
{
    if (!value.is_object()) return status;
    status.attack = value.value("attack", status.attack);
    status.defense = value.value("defense", status.defense);
    status.mass = value.value("mass", status.mass);
    status.radius = value.value("radius", status.radius);
    status.restitution = value.value("restitution", status.restitution);
    status.friction = value.value("friction", status.friction);
    status.knockbackTransfer = value.value("knockbackTransfer", status.knockbackTransfer);
    status.pierceMaxUses = value.value("pierceMaxUses", status.pierceMaxUses);
    status.pierceSpeedRetention = value.value("pierceSpeedRetention", status.pierceSpeedRetention);
    status.anchorBrakeMultiplier = value.value("anchorBrakeMultiplier", status.anchorBrakeMultiplier);
    status.anchorStopSpeedSquared = value.value("anchorStopSpeedSquared", status.anchorStopSpeedSquared);
    status.anchorKnockbackImmune = value.value("anchorKnockbackImmune", status.anchorKnockbackImmune);
    status.cushionChargeSpeedMultiplier = value.value(
        "cushionChargeSpeedMultiplier", status.cushionChargeSpeedMultiplier);
	status.stopShieldAmount = value.value("stopShieldAmount", status.stopShieldAmount);
	status.chainImpactRadius = value.value("chainImpactRadius", status.chainImpactRadius);
    if (value.contains("abilities") && value["abilities"].is_object())
    {
        const auto& abilities = value["abilities"];
        status.abilities.split = abilities.value("split", status.abilities.split);
        status.abilities.pierce = abilities.value("pierce", status.abilities.pierce);
        status.abilities.anchor = abilities.value("anchor", status.abilities.anchor);
		status.abilities.refractAfterPierce = abilities.value(
			"refractAfterPierce", status.abilities.refractAfterPierce);
    }
    return status;
}

inline nlohmann::json WriteBallStatus(const BallStatus& status)
{
    return {
        { "attack", status.attack },
        { "defense", status.defense },
        { "mass", status.mass },
        { "radius", status.radius },
        { "restitution", status.restitution },
        { "friction", status.friction },
        { "knockbackTransfer", status.knockbackTransfer },
        { "pierceMaxUses", status.pierceMaxUses },
        { "pierceSpeedRetention", status.pierceSpeedRetention },
        { "anchorBrakeMultiplier", status.anchorBrakeMultiplier },
        { "anchorStopSpeedSquared", status.anchorStopSpeedSquared },
        { "anchorKnockbackImmune", status.anchorKnockbackImmune },
        { "cushionChargeSpeedMultiplier", status.cushionChargeSpeedMultiplier },
		{ "stopShieldAmount", status.stopShieldAmount },
		{ "chainImpactRadius", status.chainImpactRadius },
		{ "abilities", { { "split", status.abilities.split }, { "pierce", status.abilities.pierce }, { "anchor", status.abilities.anchor },
			{ "refractAfterPierce", status.abilities.refractAfterPierce } } },
    };
}

inline bool IsValidBallStatus(const BallStatus& s)
{
    return s.attack >= 0 && s.attack <= 100000 && s.defense >= 0 && s.defense <= 100000 &&
        std::isfinite(s.mass) && s.mass > 0.0f && s.mass <= 1000.0f &&
        std::isfinite(s.radius) && s.radius >= 0.0f && s.radius <= 100.0f &&
        std::isfinite(s.restitution) && s.restitution >= 0.0f && s.restitution <= 1.0f &&
        std::isfinite(s.friction) && s.friction >= 0.0f && s.friction <= 10.0f &&
        std::isfinite(s.knockbackTransfer) && s.knockbackTransfer >= 0.0f && s.knockbackTransfer <= 3.0f &&
        s.pierceMaxUses >= 0 && s.pierceMaxUses <= 16 &&
        std::isfinite(s.pierceSpeedRetention) && s.pierceSpeedRetention >= 0.0f && s.pierceSpeedRetention <= 1.0f &&
        std::isfinite(s.anchorBrakeMultiplier) && s.anchorBrakeMultiplier >= 1.0f && s.anchorBrakeMultiplier <= 5.0f &&
        std::isfinite(s.anchorStopSpeedSquared) && s.anchorStopSpeedSquared >= 0.03f && s.anchorStopSpeedSquared <= 1.0f &&
        std::isfinite(s.cushionChargeSpeedMultiplier) && s.cushionChargeSpeedMultiplier >= 1.0f &&
		s.cushionChargeSpeedMultiplier <= 3.0f &&
		s.stopShieldAmount >= 0 && s.stopShieldAmount <= 100 &&
		std::isfinite(s.chainImpactRadius) && s.chainImpactRadius >= 0.0f &&
		s.chainImpactRadius <= 100.0f;
}
