# bsp

Board support policy and per-board pin maps.
Framework code must not hardcode board-level GPIO assignments outside this boundary.

Stage 2A1 begins by freezing the BSP boundary and adding a first non-production
ESP8266 board placeholder under `bsp/esp8266_generic_dev/`.


Stage 2A3 adds the first concrete board profile under `bsp/atnel_air_esp_motherboard/` for the ATNEL AIR ESP module mounted on ATB WIFI ESP Motherboard.

All concrete board profiles must use the unified `EV_BSP_PIN(...)` / `EV_BSP_PIN_ANALOG(...)`
DSL so that targets can consume one board-scoped source of truth without per-board macro dialects.


All production targets must consume board-scoped constants through a sibling `board_profile.h`
that expands the unified DSL into target-facing `EV_BOARD_*` definitions instead of duplicating GPIO
or device address macros inside target-local `app_main.c` files.

- `bsp/wemos_esp_wroom_02_18650/` - Wemos ESP-WROOM-02 with integrated 18650 battery holder.


## Runtime SSoT rule

Production targets must derive `ev_demo_app_board_profile_t` from their
`board_profile.h`. Device addresses, hardware-present masks, and supervisor
required/optional masks belong to the BSP, not to `app/` defaults.

A target with no declared runtime hardware may pass a no-hardware profile and
still run the portable app in degraded mode; a target that declares RTC,
MCP23008, OLED, or DS18B20 must provide the matching port contracts.

## Private board secrets policy

Tracked BSP files must not contain private WiFi passwords, SSIDs, broker URIs,
or deployment credentials.  Board profiles provide empty defaults and may opt in
to a developer-local file named `board_secrets.local.h`.

For the ATNEL board, copy:

```text
bsp/atnel_air_esp_motherboard/board_secrets.example.h
```

to:

```text
bsp/atnel_air_esp_motherboard/board_secrets.local.h
```

and build with `EV_BOARD_INCLUDE_LOCAL_SECRETS` defined.  The local file is
ignored by git.  Equivalent compile-time `-D` overrides may also be used, for
example for CI or a lab-specific build wrapper.  `EV_BOARD_HAS_NET` defaults to
`0U` unless local secrets or build flags explicitly enable the network
capability.

For ESP8266 SDK target builds, the ATNEL target component automatically defines `EV_BOARD_INCLUDE_LOCAL_SECRETS` when the ignored file `bsp/atnel_air_esp_motherboard/board_secrets.local.h` exists. This keeps CI and default builds secret-free while making local WiFi/HIL builds reproducible without editing tracked headers. Without that local file, `EV_BOARD_HAS_NET` remains at its safe default of `0U`.
