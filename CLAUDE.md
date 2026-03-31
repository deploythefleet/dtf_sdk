# CLAUDE.md

## Overview

ESP-IDF C SDK for Deploy the Fleet (DTF). Provides OTA updates and observability (logs + metrics) for ESP32 devices. Built as an ESP-IDF component.

## Project structure

- `include/` — Public headers (API surface)
- `src/` — Implementation
- `certs/` — Root CA certificate for DTF API
- `examples/get_started/` — Example ESP-IDF project
- `utils/` — Python utilities (log parser, profiler)
- `docs/observability/` — Architecture and interface contracts for observability features

## Conventions

- Formatting: Google C style. All C code must conform to these rules:
  - IndentWidth: 2 spaces (no tabs)
  - ColumnLimit: 120
  - Braces: same line as control statements and functions, `else` on new line after closing brace
  - Single space before opening brace
  - No space between function name and parenthesis
  - Pointer alignment: right (`char* p`, not `char *p`)
- Language: C (not C++)
- Build system: ESP-IDF component (CMakeLists.txt + Kconfig)
- Platform-agnostic core: zero `esp_*.h` includes in core logic. All platform-specific code behind PAL (platform abstraction layer) interfaces with weak-linked ESP32 defaults.
- Public API prefix: `dtf_` for all functions, `DTF_` for macros and config
- Header guard style: `#ifndef DTF_<MODULE>_H`
- Menuconfig: all feature toggles via Kconfig (e.g., `CONFIG_DTF_OBSERVABILITY`)
- Branch naming: `claude/[type]-[description]` (feat, fix, refactor)
- Commits: Conventional Commits — `feat(obs): description`, `fix(ota): description`
- Scopes: ota, obs (observability), pal (platform abstraction), core
- PRs target the `next` branch

## Key design principles

- Opinionated defaults, full overridability — end user provides API key and it works; advanced user can swap any PAL component
- Zero binary size impact when observability is disabled via Menuconfig
- All dependencies (storage, transport, clock, timer) behind swappable interfaces
