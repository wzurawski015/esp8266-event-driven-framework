# ATNEL I2C HIL report

| Field | Value |
|---|---|
| Status | FAIL |
| Reason | Base I2C cases passed; `sda-stuck-low-containment` failed with `stuck_status=OK` and `recovery_status=OK`, indicating that the fault-injection GPIO likely did not pull the real SDA line low or the fixture coupling is incomplete. |
| Evidence | `docs/release/hil_evidence/i2c/20260501T155505Z_excerpt.md` |
| Local log path | `logs/hil/i2c/20260502T062720Z.log` |

PASS requires the serial marker `EV_HIL_RESULT PASS failures=0 skipped=0`.
