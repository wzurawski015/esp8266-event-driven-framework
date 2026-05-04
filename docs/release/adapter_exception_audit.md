# Adapter exception audit

This audit records approved bootstrap/static-safe ESP8266 RTOS SDK primitives that are intentionally outside portable runtime hot paths. Any occurrence outside `tools/audit/adapter_exception_allowlist.def` fails `make static-contracts`.

| File | Primitive | Class | Rationale |
|---|---|---|---|
| `adapters/esp8266_rtos_sdk/components/ev_platform/ev_i2c_adapter.c` | `xSemaphoreCreateMutex` | `bootstrap` | Boot-time I2C bus mutex; runtime I2C transaction path remains zero-heap. |
| `adapters/esp8266_rtos_sdk/components/ev_platform/ev_irq_adapter.c` | `xSemaphoreCreateBinary` | `bootstrap` | Boot-time wait semaphore; ISR/ring hot path remains zero-heap. |
| `adapters/esp8266_rtos_sdk/components/ev_platform/ev_net_adapter.c` | `tcpip_adapter_init` | `bootstrap` | Network stack initialization only. |
| `adapters/esp8266_rtos_sdk/components/ev_platform/ev_net_adapter.c` | `esp_wifi_init` | `bootstrap` | WiFi driver initialization only. |
| `adapters/esp8266_rtos_sdk/components/ev_platform/ev_net_adapter.c` | `esp_mqtt_client_init` | `bootstrap` | MQTT client initialization only when MQTT URI is configured. |
| `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_i2c_hil/main/ev_i2c_hil.c` | `xTaskCreateStatic` | `static_safe` | Preferred static IRQ flood HIL task. |
| `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_i2c_hil/main/ev_i2c_hil.c` | `xTaskCreate` | `hil_bootstrap` | HIL-only dynamic fallback if static task API is unavailable. |
| `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_onewire_hil/main/ev_onewire_hil.c` | `xTaskCreateStatic` | `static_safe` | Preferred static IRQ flood HIL task. |
| `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_onewire_hil/main/ev_onewire_hil.c` | `xTaskCreate` | `hil_bootstrap` | HIL-only dynamic fallback if static task API is unavailable. |

The allowlist is strict: invalid categories, duplicate rows and stale rows that no longer correspond to observed code fail `make static-contracts`.
