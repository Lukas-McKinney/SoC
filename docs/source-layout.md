# Source Layout

This repo keeps the executable split into a small number of public `.c` entrypoints and a few focused implementation includes. The `.inc` files are intentional: they let large subsystems keep file-local helpers and `static` state without adding more internal headers or linker-visible symbols than necessary.

## App Flow

- `src/soc.c`
  - Owns the main loop, screen switching, and high-level match lifecycle.
- `src/soc_launch.inc`
  - Owns launch option parsing, persisted settings bootstrap, and match configuration helpers used by `soc.c`.

## Rendering

- `src/renderer.c`
  - Owns map input handling plus low-level board/render primitives shared across the renderer.
- `src/renderer_input_state.inc`
  - Holds renderer-owned interaction state and the trade/build normalization helpers that support `HandleMapInput()`.
- `src/renderer_board.c`
  - Owns shared board drawing helpers such as text/font helpers and map presentation pieces.
- `src/renderer_tiles.c`
  - Owns tile-specific rendering details.
- `src/renderer_ui.c`
  - Owns the public HUD/modal drawing entrypoints.
- `src/renderer_ui_common.inc`
  - Shared UI copy/layout utilities such as wrapped text, victory copy, and playtime/netplay labels.
- `src/renderer_ui_bounds.inc`
  - Screen-space bounds calculators for panels, overlays, and buttons.
- `src/renderer_ui_helpers.inc`
  - Private HUD helpers for dice, development cards, award cards, and current-player/private-info resolution.

## Game Systems

- `src/game_logic.c`
  - Core rule execution and turn progression.
- `src/game_action.c`
  - Thin action helpers and action application plumbing.
- `src/board_rules.c`
  - Placement validation rules reused by runtime code and the headless test suite.
- `src/match_session.c`
  - Match authority, submission, and synchronization glue for local and private multiplayer.
- `src/netplay.c`
  - Transport and connection-state handling for private multiplayer.
- `src/ui_state.c`
  - Time-based UI state, notifications, and transient overlays.
- `src/ai_controller.c`
  - AI turn search, evaluation, and action selection.

## Supporting Data / Persistence

- `src/localization.c`
  - User-facing string lookup and display labels.
- `src/settings_store.c`
  - Persisted settings and match-history storage.
- `src/map.c`
  - Board generation and base map helpers.
- `src/map_snapshot.c`
  - Small snapshot helpers for copying/comparing map state.

## Tests

- `tests/rule_validation.c`
  - Headless regression coverage for placement rules, production, development-card timing, and award-card logic.
