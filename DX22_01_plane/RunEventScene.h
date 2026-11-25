#pragma once

#include "MenuSelection.h"
#include "Scene.h"

class RunEventScene final : public Scene
{
public:
    void Update() override;
    void DrawUI() override;

private:
    MenuSelection m_Choices;
    MenuSelection m_Targets;
    MenuSelection m_Result;
    MenuSelection m_Confirmation;
    MenuSelection m_Deck;
    int m_ActiveChoice = -1;
    int m_ConfirmChoice = -1;
    int m_ConfirmTarget = -1;
    bool m_ResultVisible = false;
    bool m_ResultInputReady = false;
    bool m_ConfirmationInputReady = false;
    bool m_ShowDeck = false;
    bool m_DeckInputReady = false;
    bool m_ScrollToFocus = false;
    bool m_SelectionRejected = false;

    void SelectChoice(int choiceIndex, int targetIndex, bool requiresConfirmation);
};
