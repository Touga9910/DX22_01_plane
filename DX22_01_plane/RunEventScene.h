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
    int m_ActiveChoice = -1;
    bool m_ScrollToFocus = false;
    bool m_SelectionRejected = false;
};
