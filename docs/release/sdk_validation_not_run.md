# SDK/HIL validation status

SDK toolchain build matrix, SDK linker-map memory, ATNEL HIL and Wemos physical
smoke are not claimed as PASS unless their dedicated reports contain real build
or serial-log evidence.

This patch adds the release tooling and produces NOT_RUN reports where hardware
or SDK execution was not performed.
