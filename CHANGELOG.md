# Changelog

This changelog summarizes user-visible changes from `v0.1.5` to the current `HEAD`.

## Unreleased (since v0.1.6)

### Changed

- Simplified the main menu by removing the redundant `Start vs AI` button, leaving a single `Start Game` entry that opens the unified local match seat picker.

## v0.1.6 (since v0.1.5)

### Added

- Added relay-based internet multiplayer with room codes, so matches can be hosted through a standalone relay server instead of LAN-only direct connections.
- Added WebSocket and secure relay transport support, including Render-friendly `wss://` deployment guidance and command-line relay launch options.
- Added a standalone `soc_relay` server target, Docker build support, and a WebSocket test client script for relay validation.
- Added a dedicated `trade-test` suite plus extra rules coverage for settlement placement edge cases, closed seats, thief blocking, development card timing, and rejoin recovery.

### Changed

- Reworked the multiplayer menu so both host and join flows support relay addresses and room codes, normalize `ws://` and `wss://` inputs, and default relay connections to port `443`.
- Updated lobby seat handling to support `Human`, `AI`, and `Closed` states, allowing proper two-player setups and correct setup and turn-order skipping for disabled seats.
- Improved AI decision quality with stronger settlement and road evaluation, smarter trade selectivity, better road placement fallback behavior, and broader rules-aware planning.
- Moved long AI play-phase planning off the main thread so the UI stays responsive while harder AI searches are running.
- Updated build and packaging flow with relay-specific make targets, cleanup rules, pthread linkage, and relay server packaging support.

### Fixed

- Fixed trade-decline handling so declining a pending trade no longer grants resources to the sender, and added automatic resync requests when decline state hashes do not match.
- Fixed multiplayer rejoin flow so authoritative snapshots restore local discard control, stale started-state is cleared correctly, and UI state resets cleanly after match reinitialization.
- Fixed German localization for development card names and descriptions.
- Fixed build panel hitboxes by exposing explicit bounds for the panel header and build cards.
- Fixed AI interaction issues around dice reveal and road placement flow, reducing cases where the game appeared blocked while AI turns were resolving.
- Fixed multiplayer host validation for public Render relay addresses to catch common misconfiguration earlier.
