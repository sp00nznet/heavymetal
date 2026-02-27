# Contributing to Heavy Metal: FAKK2 Recomp

Welcome, warrior. Julie needs your help.

## How to Contribute

1. **Fork** this repository
2. **Pick an issue** or open one describing what you want to work on
3. **Create a branch** from `main`
4. **Hack** -- follow the code style below
5. **Test** against the original game for behavioral accuracy
6. **Submit a PR** with a clear description

## Code Style

- C17 for engine code, C++20 where genuinely useful
- 4-space indentation, no tabs
- Snake_case for functions: `R_DrawModel()`, `S_StartSound()`
- UPPER_CASE for constants and macros
- Comment non-obvious reverse-engineered behavior with references to original addresses
- Use types from `src/common/fakk_types.h`

## Legal Ground Rules

- **No decompiled code pasted directly** -- clean-room implementation only
- Document your reverse-engineering findings in `docs/`
- Reference SDK source (publicly released by Ritual) for game logic interface understanding
- Test against original for behavioral match, not byte-for-byte binary match
- All contributions must be original work or properly attributed open source

## What Needs Work

Check the issues tab, but broadly:
- Engine subsystem implementation (renderer, sound, networking, filesystem)
- TIKI model system reverse engineering
- Morpheus scripting engine implementation
- Ghost particle system
- Miles Sound System replacement
- Platform abstraction and Win11 compatibility
- Testing infrastructure

## Testing

Compare against the original `fakk2.exe` v1.02. The recomp should:
- Load all original `.pk3` assets
- Play through the full campaign
- Match original gameplay feel and timing
- Support modern resolutions and widescreen
