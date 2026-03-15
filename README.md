# SoC

Raylib-based Settlers of Catan prototype with procedural board rendering, playable match flow, hotseat support, and single-player AI opponents.

## Current Status

This is a playable prototype, not a complete rules-perfect Catan clone.

Implemented already:

- Main menu with:
  - `Start Game` popup
  - `Start vs AI` popup
  - theme toggle
  - player color selection
  - AI difficulty selection
- Two game modes:
  - local hotseat
  - one human vs three AI opponents
- Randomized setup start player and randomized snake-order setup based on that start player
- Randomized board generation:
  - terrain layout
  - dice numbers
  - development card deck order
- Full placement rules for:
  - roads
  - settlements
  - cities
- Setup flow:
  - settlement then road placement
  - reverse second setup round
  - starting resource gain from the second settlement
- Core turn flow:
  - roll dice
  - distribute resources
  - end turn
  - win at 10 victory points
- Robber / thief flow:
  - rolling a `7`
  - discard half when required
  - move thief
  - steal a random resource
  - privacy handoff UI for shared-screen discard/steal steps
- Maritime trading, including port-based trade rates
- Player-to-player trading UI
- Development cards:
  - buying from a shuffled deck
  - hidden victory point cards
  - timing lock on newly bought cards
  - one non-VP development card per turn
  - Knight
  - Road Building
  - Year of Plenty
  - Monopoly
- Award cards:
  - Largest Army
  - Longest Road
- AI opponents:
  - `Easy`, `Medium`, and `Hard`
  - build / robber / discard / development-card behavior
  - AI trade acceptance / rejection for player offers
  - configurable AI speed in settings
- Win / loss handling:
  - game-over screen
  - restart from overlay
  - back-to-menu from overlay
- UI / feedback work:
  - animated board intro
  - UI fade-in
  - button hover / press animation
  - development-card hand and draw animation
  - top-bar opponent VP display
  - public change notifications
  - centered status messages
  - tile highlights for production and thief position
- Rule validation suite for placement, setup rewards, production, development-card timing, Largest Army, and Longest Road

## Important Prototype Differences

These are intentional shortcuts or current prototype settings:

- Players currently start with `10` of each resource by default
- The board setup is randomized freely; it does not try to reproduce the official balanced spiral layout
- The app is tuned for shared-screen play and prototype iteration, not hidden-information multiplayer polish

## Still Missing Or Incomplete

- Full hidden-hand hotseat support is still incomplete
  - some handoff screens exist
  - true private local multiplayer flow is not complete
- Human-to-human player trading is still simplified
  - there is no offer / accept / decline negotiation screen between two human players
  - legal trades resolve immediately
- Bank resource supply is not tracked or enforced
  - bank shortages are ignored for production, trades, and card effects
- No save / load system
- No networked multiplayer
- AI is heuristic, not exhaustive
  - it is playable, but still needs tuning and balancing
- The `Start Game` popup now shows a color selector, but hotseat still remains a shared all-human mode rather than a true single-local-player perspective mode

## Build

The `Makefile` is set up for Windows with Raylib installed at:

- `C:/raylib/w64devkit/x86_64-w64-mingw32/include`
- `C:/raylib/w64devkit/x86_64-w64-mingw32/lib`

Build the game:

```bash
make
```

This produces:

```bash
settlers.exe
```

## Validation

Run the headless rules suite:

```bash
make rules-test
.\rules_test.exe
```

The current suite covers:

- setup settlement distance rule
- road connectivity
- road blocking by opponent settlements
- second-setup starting resources
- dice payout and thief blocking
- development-card timing
- Largest Army threshold / transfer
- Longest Road threshold / tie retention / transfer / blocking
- city upgrade validation

At the time of the latest update, the suite passes:

```text
11/11 rule tests passed
```
