# New board onboarding

To add a board:

1. Create `bsp/<board>/board_profile.h`.
2. Create `bsp/<board>/pins.def` using `EV_BSP_PIN`, `EV_BSP_PIN_ANALOG`, or `EV_PIN`.
3. Create an ESP8266 target skeleton under `adapters/esp8266_rtos_sdk/targets/<board>/`.
4. Add profile variants under `bsp/<board>/profiles/` when required.
5. Run `make quality-gate`.

A helper exists at:

```sh
tools/new_board.py <board-name>
```
