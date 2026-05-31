# HIL real ATNEL and Wemos evidence report

Status: import infrastructure added.

Real PASS requires:
- ATNEL I2C `sda-stuck-low-containment` with fixture-coupled marker and global HIL PASS.
- Wemos smoke boot/runtime/tick/snapshot/result PASS markers.
- Wemos deep-sleep ordered power state markers, deep-sleep enter, wake boot and wake reason.

This report must not claim PASS without imported serial logs and parsed JSON.
