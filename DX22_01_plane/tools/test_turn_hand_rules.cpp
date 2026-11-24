#include "../PlayerDeck.h"
#include "../GameBallSelectionState.h"
#include "../BattleFlowRules.h"
#include "../ShotCombatRules.h"
#include "../BallStatusJson.h"
#include "../PlayerBallText.h"
#include "../PlayerBallSaveData.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>

/// <summary>
/// ターン中の勝利と衝突判定、ガード強化、デッキ上限と球選択を検証する。
/// </summary>
int main()
{
    // A cleared empty world differs from an empty world during stage loading.
    assert(!BattleFlowRules::IsLegacyVictory(false, 0));
    assert(!BattleFlowRules::IsLegacyVictory(true, 1));
    assert(BattleFlowRules::IsLegacyVictory(true, 0));
    assert(!BattleFlowRules::IsPhaseVictory(true, 1, 2, 0));
    assert(BattleFlowRules::IsPhaseVictory(true, 2, 2, 0));
    assert(ShotCombatRules::EnemyCollisionDamage(3, 0) == 3);
    assert(ShotCombatRules::EnemyCollisionDamage(0, 0) == 0);
    assert(ShotCombatRules::EnemyCollisionDamage(3, 2) == 5);

    // Real definitions and JSON I/O preserve zero impact and both shield effects.
    nlohmann::json data;
    std::ifstream("assets/data/player_status.json") >> data;
    bool guardFound = false;
    for (const auto& def : data.at("balls"))
    {
        if (def.at("id") != "player_guard") continue;
        guardFound = true;
        auto status = ReadPlayerBallStatus(def.at("status"),
            ReadPlayerBallStatus(data.at("status")));
        assert(status.attack == 1 && status.impactAttack == 0 && status.shotShield == 3);
        assert(status.enemyCollisionShield == 0);
        status = ReadPlayerBallStatus(def.at("upgrades").at(0), status);
        assert(status.attack == 1 && status.impactAttack == 1 && status.shotShield == 4);
        status = ReadPlayerBallStatus(def.at("upgrades").at(1), status);
        assert(status.attack == 1 && status.impactAttack == 1 && status.shotShield == 5);
        assert(status.enemyCollisionShield == 1);
        assert(IsValidPlayerBallStatus(status));
        assert(WritePlayerBallStatus(ReadPlayerBallStatus(WritePlayerBallStatus(status))) ==
            WritePlayerBallStatus(status));
        auto invalid = status;
        invalid.impactAttack = -1;
        assert(!IsValidPlayerBallStatus(invalid));
        PlayerBallData guard;
        guard.definitionId = "player_guard"; guard.status = status;
        assert(std::string(PlayerBallText::GetName(guard.definitionId)) != "player_guard");
        assert(!PlayerBallText::GetShortDescription(guard).empty());
    }
    assert(guardFound);
    const auto loaded = PlayerBallDataLoader::Load("assets/data/player_status.json", {}, {});
    assert(loaded.ballDefinitions.size() == 18);
    for (auto definition : loaded.ballDefinitions)
    {
        if (definition.definitionId != "player_guard")
        {
            assert(definition.status.impactAttack == 1);
            for (const auto& step : definition.upgradeTable) assert(step.impactAttack == 1);
            continue;
        }
        assert(definition.status.impactAttack == 0 && definition.status.shotShield == 3);
        assert(definition.upgradeTable[0].impactAttack == 1 && definition.upgradeTable[0].shotShield == 4);
        definition.instanceId = 42;
        definition.upgradeLevel = 2;
        definition.status = definition.upgradeTable[1];
        auto saved = PlayerBallSaveData::BallToJson(definition);
        auto roundtrip = PlayerBallSaveData::BallFromJson(saved);
        assert(PlayerBallSaveData::BallToJson(roundtrip) == saved);
        saved["upgrade_model_version"] = 6;
        saved["status"].erase("impactAttack");
        auto migrated = PlayerBallSaveData::BallFromJson(saved);
        assert(migrated.instanceId == 42 && migrated.upgradeLevel == 2);
        assert(migrated.status.impactAttack == 1 && migrated.status.shotShield == 5);
        assert(migrated.status.enemyCollisionShield == 1);
        saved["upgrade_model_version"] = 7;
        bool rejected = false;
        try { (void)PlayerBallSaveData::BallFromJson(saved); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    }
    const auto starterCards = PlayerBallDataLoader::LoadDeck(
        "assets/data/player_deck.json", loaded.ballDefinitions);
    assert(starterCards.size() == 13);

    nlohmann::json starter;
    std::ifstream("assets/data/player_deck.json") >> starter;
    assert(starter.at("deck").size() == 13);
    int standardCount = 0, heavyCount = 0, guardCount = 0;
    for (const auto& id : starter.at("deck"))
    { standardCount += id == "player_standard"; heavyCount += id == "player_heavy";
      guardCount += id == "player_guard"; }
    assert(standardCount == 6 && heavyCount == 1 && guardCount == 6);

    for (const int handSize : {5, 7})
    {
        PlayerDeck deck;
        deck.SetDefaultDeck(starterCards); deck.Seed(123); deck.ResetToDefault();
        for (int turn = 0; turn < 30; ++turn)
        {
            assert(deck.PrepareOffer(handSize));
            assert(deck.GetOfferCount() == handSize);
            auto before = deck.CaptureSnapshot();
            assert(!deck.SelectOffer(-1, -1));
            assert(deck.GetOfferCount() == handSize);
            // Even a legacy positive hold request cannot retain an unplayed card.
            assert(deck.SelectOffer(handSize - 1, 0));
            assert(!deck.HasHeldBall() && deck.GetOfferCount() == 0);
            assert(deck.GetDiscardPileCount() ==
                static_cast<int>(before.discardPile.size()) + handSize - 1);
            deck.MarkCurrentUsed(); assert(deck.DiscardCurrentIfUsed());
            assert(deck.GetRewardTargetCount() == 13);
            std::set<std::uint64_t> ids;
            for (int i = 0; i < deck.GetRewardTargetCount(); ++i)
                assert(ids.insert(deck.GetRewardTarget(i)->instanceId).second);
            assert(ids.size() == 13);
        }
        // A held card from an older save moves to discard without being lost.
        auto old = deck.CaptureSnapshot();
        old.heldBall = old.discardPile.back(); old.discardPile.pop_back();
        assert(deck.RestoreSnapshot(old));
        assert(!deck.HasHeldBall() && deck.GetRewardTargetCount() == 13);
    }
    GameBallSelectionState selection;
    selection.Begin(7, 3);
    assert(selection.HeldOfferIndex() == -1);
    assert(selection.SelectOffer(6, 7));
    assert(selection.ConfirmSelection(7));
    selection.Update(0.25f); assert(selection.IsShotReady());
    assert(selection.CancelShotPreparation());
    selection.Update(0.25f); assert(selection.IsSelectionInteractive());
    assert(selection.SelectedOfferIndex() == 6);
    std::cout << "Turn/hand rules: victory, impact, guard upgrades, 5/7-card ownership, legacy save and selection passed\n";
}
