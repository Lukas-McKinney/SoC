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
  - distribute resources (with bank shortage enforcement)
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

- The board setup is randomized freely; it does not try to reproduce the official balanced spiral layout
- The app is tuned for shared-screen play and prototype iteration, not hidden-information multiplayer polish

## Still Missing Or Incomplete

- Full hidden-hand hotseat support is still incomplete
  - some handoff screens exist
  - true private local multiplayer flow is not complete
- No networked multiplayer
- AI now exhaustively searches bounded deterministic play lines
  - robber sub-choices use fast post-resolution scoring so thief turns stay responsive
  - long-horizon strategy is still guided by state evaluation rather than a full-game solve

## Source Layout

For a quick map of the codebase, see [docs/source-layout.md](docs/source-layout.md).

Notable entrypoints:

- `src/soc.c` drives the app loop
- `src/renderer.c` owns board interaction and render primitives
- `src/renderer_ui.c` owns HUD/modal rendering entrypoints
- `src/game_logic.c` owns core rule execution
- `src/match_session.c` owns authority and multiplayer/session flow

## Build

### Windows

The `Makefile` supports the current Windows setup with Raylib installed at:

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

Build the headless console version (for rule-heavy testing without UI):

```bash
make console
```

This produces:

```bash
soc_console.exe
```

Run it:

```powershell
.\soc_console.exe
```

This console target is currently intended as an internal QA/testing harness.
Friend-ready package scripts ship only the main game binary by default.

Inside the console app, type `help` to list all commands. Core flow:

- setup: `place settlement <tile> <corner>` then `place road <tile> <side>`
- play: `roll`, `buy ...`, `place ...`, `play ...`, `maritime ...`, `trade ...`, `end`
- robber/discard: `discard ...`, `move_thief ...`, `steal ...`

Hidden-information testing helpers in console mode:

- `private on|off` toggles whether non-active hands are redacted in `status`
- `view <player|current>` selects which player hand/details are revealed while private mode is on
- `hands` shows only the currently revealed player in private mode
- `hands all` prints all hands (debug/testing override)

If your Raylib install lives somewhere else, override the paths when invoking `make`:

```bash
make RAYLIB_INCLUDE=... RAYLIB_LIB=...
```

### macOS

Install the Apple command-line toolchain once:

```bash
xcode-select --install
```

Install Raylib and `pkg-config` with Homebrew:

```bash
brew install raylib pkg-config
```

Build the game:

```bash
make
```

This produces:

```bash
./settlers
```

Build the headless console version:

```bash
make console
```

This produces:

```bash
./soc_console
```

The macOS build path uses `pkg-config` to discover Raylib automatically. If your Raylib install is not exposed through `pkg-config`, pass the flags explicitly:

```bash
make RAYLIB_CFLAGS="..." RAYLIB_LIBS="..."
```

## Validation

Run the headless rules suite:

```bash
make rules-test
```

Run the test binary:

- Windows PowerShell: `.\rules_test.exe`
- macOS / Linux shell: `./rules_test`

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
- bank shortage enforcement for roll payout / maritime trade / Year of Plenty
- turn-state legality for buy/trade actions (post-roll and no pending thief/discard)
- robber steal weighting by card counts

At the time of the latest update, the suite passes:

```text
16/16 rule tests passed
```

## Share Build With Friends (Windows + macOS)

You can package a friend-ready zip that does not require raylib to be installed on the target machine.

### Windows package

Build and package:

```bash
make
make package-windows
```

Or run directly:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -Version 0.1.0
```

Output:

- `dist/SoC-<version>-windows-x64.zip`
- `dist/SoC-<version>-windows-x64.sha256.txt`

### macOS package

Build and package (on macOS):

```bash
make
make package-macos
```

Or run directly:

```bash
bash scripts/package_macos.sh 0.1.0
```

Output:

- `dist/SoC-<version>-macos-<arch>.zip`
- `dist/SoC-<version>-macos-<arch>.sha256.txt`

The macOS packager bundles non-system `.dylib` dependencies (such as raylib) and rewrites loader paths so the zip is portable to machines without raylib installed.
The macOS zip now contains a signed `SoC.app` bundle with embedded frameworks to reduce repeated security prompts for individual dylibs.

### What your friend runs

Each zip includes:

- game executable (`settlers.exe` on Windows, `SoC.app` on macOS)
- quick-start script for host
- quick-start script for join
- `QUICKSTART.txt`

Notes:

- Use host LAN IP for remote machine joins, not `127.0.0.1`.
- Default port is `24680`.
- macOS first-run may require removing quarantine:
  - `xattr -dr com.apple.quarantine <folder>`
