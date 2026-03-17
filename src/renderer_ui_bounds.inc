Rectangle GetPlayerPanelBounds(void)
{
    const float panelWidth = 250.0f;
    const float panelHeight = 238.0f;
    return (Rectangle){(float)GetScreenWidth() - panelWidth - 24.0f, 24.0f, panelWidth, panelHeight};
}

Rectangle GetTurnPanelBounds(void)
{
    const float panelWidth = 250.0f;
    const float panelHeight = 316.0f;
    return (Rectangle){(float)GetScreenWidth() - panelWidth - 24.0f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - 42.0f, panelWidth, panelHeight};
}

Rectangle GetTradeButtonBounds(void)
{
    const float panelWidth = 250.0f;
    const float panelHeight = 44.0f;
    const Rectangle turnPanel = GetTurnPanelBounds();
    return (Rectangle){turnPanel.x, turnPanel.y + turnPanel.height + 18.0f, panelWidth, panelHeight};
}

Rectangle GetPlayerTradeButtonBounds(void)
{
    const Rectangle waterTrade = GetTradeButtonBounds();
    return (Rectangle){waterTrade.x, waterTrade.y + waterTrade.height + 12.0f, waterTrade.width, waterTrade.height};
}

Rectangle GetSettingsButtonBounds(void)
{
    return (Rectangle){24.0f, 24.0f, 156.0f, 42.0f};
}

Rectangle GetTradeModalBounds(void)
{
    const float panelWidth = 460.0f;
    const float panelHeight = 380.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetSettingsModalBounds(void)
{
    const float panelWidth = 320.0f;
    const float panelHeight = 340.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetSettingsSubmenuBounds(void)
{
    const Rectangle menu = GetSettingsModalBounds();
    const float panelWidth = 320.0f;
    const float panelHeight = 404.0f;
    return (Rectangle){
        menu.x + menu.width + 22.0f,
        menu.y + menu.height * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

Rectangle GetSettingsMultiplayerPanelBounds(void)
{
    const Rectangle menu = GetSettingsModalBounds();
    const float panelWidth = 280.0f;
    const float panelHeight = 172.0f;
    return (Rectangle){
        menu.x - panelWidth - 22.0f,
        menu.y + menu.height * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

Rectangle GetPlayerTradeModalBounds(void)
{
    const float panelWidth = 440.0f;
    const float panelHeight = 430.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetIncomingTradeOfferModalBounds(void)
{
    const float panelWidth = 430.0f;
    const float panelHeight = 372.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetIncomingTradeOfferAcceptButtonBounds(void)
{
    const Rectangle panel = GetIncomingTradeOfferModalBounds();
    return (Rectangle){panel.x + 24.0f, panel.y + panel.height - 58.0f, panel.width * 0.5f - 30.0f, 40.0f};
}

Rectangle GetIncomingTradeOfferDeclineButtonBounds(void)
{
    const Rectangle panel = GetIncomingTradeOfferModalBounds();
    return (Rectangle){panel.x + panel.width * 0.5f + 6.0f, panel.y + panel.height - 58.0f, panel.width * 0.5f - 30.0f, 40.0f};
}

Rectangle GetDiscardModalBounds(void)
{
    const float panelWidth = 390.0f;
    const float panelHeight = 360.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetThiefVictimModalBounds(void)
{
    const float panelWidth = 430.0f;
    const float panelHeight = 190.0f;
    return (Rectangle){(float)GetScreenWidth() * 0.5f - panelWidth * 0.5f, (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f, panelWidth, panelHeight};
}

Rectangle GetBuildPanelBounds(void)
{
    const float closedWidth = 220.0f;
    const float openWidth = 392.0f;
    const float closedHeight = 54.0f;
    const float openHeight = 182.0f;
    const float panelWidth = closedWidth + (openWidth - closedWidth) * uiGetBuildPanelOpenAmount();
    const float panelHeight = closedHeight + (openHeight - closedHeight) * uiGetBuildPanelOpenAmount();
    return (Rectangle){(float)GetScreenWidth() - panelWidth - 24.0f, (float)GetScreenHeight() - panelHeight - 24.0f, panelWidth, panelHeight};
}

Rectangle GetBuildPanelDevelopmentCardBounds(void)
{
    const Rectangle panel = GetBuildPanelBounds();
    return (Rectangle){panel.x + 204.0f, panel.y + 118.0f, 170.0f, 56.0f};
}

Rectangle GetDevelopmentPurchaseOverlayBounds(void)
{
    const Rectangle buildPanel = GetBuildPanelBounds();
    return (Rectangle){buildPanel.x + 10.0f, buildPanel.y + 10.0f, buildPanel.width - 20.0f, buildPanel.height - 20.0f};
}

Rectangle GetDevelopmentPurchaseConfirmButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPurchaseOverlayBounds();
    return (Rectangle){panel.x + 18.0f, panel.y + panel.height - 48.0f, panel.width - 132.0f, 36.0f};
}

Rectangle GetDevelopmentPurchaseCancelButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPurchaseOverlayBounds();
    return (Rectangle){panel.x + panel.width - 100.0f, panel.y + panel.height - 48.0f, 82.0f, 36.0f};
}

Rectangle GetDevelopmentPlayOverlayBounds(void)
{
    const float panelWidth = 476.0f;
    const float panelHeight = 320.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f - 24.0f,
        panelWidth,
        panelHeight};
}

Rectangle GetDevelopmentPlayConfirmButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPlayOverlayBounds();
    return (Rectangle){panel.x + 24.0f, panel.y + panel.height - 54.0f, panel.width - 164.0f, 38.0f};
}

Rectangle GetDevelopmentPlayCancelButtonBounds(void)
{
    const Rectangle panel = GetDevelopmentPlayOverlayBounds();
    return (Rectangle){panel.x + panel.width - 116.0f, panel.y + panel.height - 54.0f, 92.0f, 38.0f};
}

Rectangle GetRollDiceButtonBounds(void)
{
    const Rectangle panel = GetTurnPanelBounds();
    return (Rectangle){panel.x + 16.0f, panel.y + 176.0f, panel.width - 32.0f, 42.0f};
}

Rectangle GetEndTurnButtonBounds(void)
{
    const Rectangle panel = GetTurnPanelBounds();
    return (Rectangle){panel.x + 16.0f, panel.y + 226.0f, panel.width - 32.0f, 42.0f};
}

Rectangle GetVictoryOverlayBounds(void)
{
    const float panelWidth = 500.0f;
    const float panelHeight = 316.0f;
    return (Rectangle){
        (float)GetScreenWidth() * 0.5f - panelWidth * 0.5f,
        (float)GetScreenHeight() * 0.5f - panelHeight * 0.5f,
        panelWidth,
        panelHeight};
}

Rectangle GetVictoryOverlayRestartButtonBounds(void)
{
    const Rectangle panel = GetVictoryOverlayBounds();
    return (Rectangle){panel.x + 34.0f, panel.y + panel.height - 62.0f, 196.0f, 42.0f};
}

Rectangle GetVictoryOverlayMenuButtonBounds(void)
{
    const Rectangle panel = GetVictoryOverlayBounds();
    return (Rectangle){panel.x + panel.width - 230.0f, panel.y + panel.height - 62.0f, 196.0f, 42.0f};
}
