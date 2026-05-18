Rectangle GetPlayerPanelBounds(void)
{
    const float panelWidth = UiScaled(250.0f);
    const float panelHeight = UiScaled(238.0f);
    const float margin = UiScaled(24.0f);

    return (Rectangle){(float)GetScreenWidth() - panelWidth - margin, margin, panelWidth, panelHeight};
}

Rectangle GetTurnPanelBounds(void)
{
    const float panelWidth = UiScaled(250.0f);
    const float panelHeight = UiScaled(316.0f);
    const float margin = UiScaled(24.0f);

    return (Rectangle){(float)GetScreenWidth() - panelWidth - margin, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - UiScaled(42.0f), panelWidth, panelHeight};
}

Rectangle GetTradeButtonBounds(void)
{
    const float panelWidth = UiScaled(250.0f);
    const float panelHeight = UiScaled(44.0f);
    const Rectangle turnPanel = GetTurnPanelBounds();

    return (Rectangle){turnPanel.x, turnPanel.y + turnPanel.height + UiScaled(18.0f), panelWidth, panelHeight};
}

Rectangle GetPlayerTradeButtonBounds(void)
{
    const Rectangle waterTrade = GetTradeButtonBounds();
    return (Rectangle){waterTrade.x, waterTrade.y + waterTrade.height + UiScaled(12.0f), waterTrade.width, waterTrade.height};
}

Rectangle GetSettingsButtonBounds(void)
{
    return (Rectangle){UiScaled(24.0f), UiScaled(24.0f), UiScaled(156.0f), UiScaled(42.0f)};
}

Rectangle GetTradeModalBounds(void)
{
    const float panelWidth = UiScaled(460.0f);
    const float panelHeight = UiScaled(380.0f);
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetSettingsModalBounds(void)
{
    const float panelWidth = UiScaled(320.0f);
    const float panelHeight = UiScaled(340.0f);
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetSettingsSubmenuBounds(void)
{
    const Rectangle menu = GetSettingsModalBounds();
    const float panelWidth = UiScaled(320.0f);
    const float panelHeight = UiScaled(404.0f);
    return (Rectangle){
        menu.x + menu.width + UiScaled(22.0f),
        menu.y + menu.height * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

Rectangle GetSettingsMultiplayerPanelBounds(void)
{
    const Rectangle menu = GetSettingsModalBounds();
    const float panelWidth = UiScaled(280.0f);
    const float panelHeight = UiScaled(172.0f);
    return (Rectangle){
        menu.x - panelWidth - UiScaled(22.0f),
        menu.y + menu.height * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

Rectangle GetPlayerTradeModalBounds(void)
{
    const float panelWidth = UiScaled(440.0f);
    const float panelHeight = UiScaled(430.0f);
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetIncomingTradeOfferModalBounds(void)
{
    const float panelWidth = UiScaled(430.0f);
    const float panelHeight = UiScaled(372.0f);
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetIncomingTradeOfferAcceptButtonBounds(void)
{
    const Rectangle panel = GetIncomingTradeOfferModalBounds();
    return (Rectangle){panel.x + UiScaled(24.0f), panel.y + panel.height - UiScaled(58.0f), panel.width * 0.5f - UiScaled(30.0f), UiScaled(40.0f)};
}

Rectangle GetIncomingTradeOfferDeclineButtonBounds(void)
{
    const Rectangle panel = GetIncomingTradeOfferModalBounds();
    return (Rectangle){panel.x + panel.width * 0.5f + UiScaled(6.0f), panel.y + panel.height - UiScaled(58.0f), panel.width * 0.5f - UiScaled(30.0f), UiScaled(40.0f)};
}

Rectangle GetDiscardModalBounds(void)
{
    const float panelWidth = UiScaled(390.0f);
    const float panelHeight = UiScaled(360.0f);
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetThiefVictimModalBounds(void)
{
    const float panelWidth = UiScaled(430.0f);
    const float panelHeight = UiScaled(190.0f);
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetBuildPanelBounds(void)
{
    const float closedWidth = UiScaled(220.0f);
    const float openWidth = UiScaled(392.0f);
    const float closedHeight = UiScaled(54.0f);
    const float openHeight = UiScaled(182.0f);
    const float panelWidth = closedWidth + (openWidth - closedWidth) * uiGetBuildPanelOpenAmount();
    const float panelHeight = closedHeight + (openHeight - closedHeight) * uiGetBuildPanelOpenAmount();
    const float margin = UiScaled(24.0f);
    return (Rectangle){(float)GetScreenWidth() - panelWidth - margin, (float)GetScreenHeight() - panelHeight - margin, panelWidth, panelHeight};
}

Rectangle GetBuildPanelDevelopmentCardBounds(void)
{
    const Rectangle panel = GetBuildPanelBounds();
    return (Rectangle){panel.x + UiScaled(196.0f), panel.y + UiScaled(112.0f), UiScaled(175.0f), UiScaled(60.0f)};
}

Rectangle GetDevelopmentPurchaseOverlayBounds(void)
{
    const Rectangle buildPanel = GetBuildPanelBounds();
    const float inset = UiScaled(10.0f);
    return (Rectangle){buildPanel.x + inset, buildPanel.y + inset, buildPanel.width - inset * 2.0f, buildPanel.height - inset * 2.0f};
}

Rectangle GetDevelopmentPurchaseConfirmButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPurchaseOverlayBounds();
    return (Rectangle){panel.x + UiScaled(18.0f), panel.y + panel.height - UiScaled(48.0f), panel.width - UiScaled(132.0f), UiScaled(36.0f)};
}

Rectangle GetDevelopmentPurchaseCancelButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPurchaseOverlayBounds();
    return (Rectangle){panel.x + panel.width - UiScaled(100.0f), panel.y + panel.height - UiScaled(48.0f), UiScaled(82.0f), UiScaled(36.0f)};
}

Rectangle GetDevelopmentPlayOverlayBounds(void)
{
    const float panelWidth = UiScaled(476.0f);
    const float panelHeight = UiScaled(320.0f);
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - UiScaled(24.0f),
        panelWidth,
        panelHeight};
}

Rectangle GetDevelopmentPlayConfirmButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPlayOverlayBounds();
    return (Rectangle){panel.x + UiScaled(24.0f), panel.y + panel.height - UiScaled(54.0f), panel.width - UiScaled(164.0f), UiScaled(38.0f)};
}

Rectangle GetDevelopmentPlayCancelButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPlayOverlayBounds();
    return (Rectangle){panel.x + panel.width - UiScaled(116.0f), panel.y + panel.height - UiScaled(54.0f), UiScaled(92.0f), UiScaled(38.0f)};
}

Rectangle GetRollDiceButtonBounds(void)
{
    const Rectangle panel = GetTurnPanelBounds();
    return (Rectangle){panel.x + UiScaled(16.0f), panel.y + UiScaled(176.0f), panel.width - UiScaled(32.0f), UiScaled(42.0f)};
}

Rectangle GetEndTurnButtonBounds(void)
{
    const Rectangle panel = GetTurnPanelBounds();
    return (Rectangle){panel.x + UiScaled(16.0f), panel.y + UiScaled(226.0f), panel.width - UiScaled(32.0f), UiScaled(42.0f)};
}

Rectangle GetVictoryOverlayBounds(void)
{
    const float panelWidth = UiScaled(500.0f);
    const float panelHeight = UiScaled(316.0f);
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

Rectangle GetVictoryOverlayRestartButtonBounds(void)
{
    const Rectangle panel = GetVictoryOverlayBounds();
    return (Rectangle){panel.x + UiScaled(34.0f), panel.y + panel.height - UiScaled(62.0f), UiScaled(196.0f), UiScaled(42.0f)};
}

Rectangle GetVictoryOverlayMenuButtonBounds(void)
{
    const Rectangle panel = GetVictoryOverlayBounds();
    return (Rectangle){panel.x + panel.width - UiScaled(230.0f), panel.y + panel.height - UiScaled(62.0f), UiScaled(196.0f), UiScaled(42.0f)};
}
