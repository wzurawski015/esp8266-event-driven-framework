# Wemos ESP-WROOM-02 18650 smoke report

| Field | Value |
|---|---|
| Status | NOT_RUN |
| Reason | Wemos board was not attached or smoke was not requested after monitor hardening |
| Mode |  |
| Log path | `` |

Preferred PASS requires `EV_WEMOS_SMOKE_BOOT` and `EV_WEMOS_SMOKE_RUNTIME_READY` serial markers. A fallback PASS is allowed only when the monitor observes at least three increasing `diag actor: tick=` values and at least three increasing `app actor: snapshot seq=` values without reset/failure markers.
