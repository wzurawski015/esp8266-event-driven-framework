#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
build = ROOT / "build" / "memory_budget"
build.mkdir(parents=True, exist_ok=True)
probe = build / "memory_budget_probe.c"

probe_c = r'''
#include <stdio.h>
#include "ev/runtime_graph.h"
#include "ev/msg.h"
#include "ev/mailbox.h"
#include "ev/actor_runtime.h"
#include "ev/timer_service.h"
#include "ev/trace_ring.h"
#include "ev/fault_bus.h"
#include "ev/metrics_registry.h"
#include "ev/network_outbox.h"

int main(void)
{
    printf("ev_runtime_graph_t %zu\n", sizeof(ev_runtime_graph_t));
    printf("ev_msg_t %zu\n", sizeof(ev_msg_t));
    printf("ev_mailbox_t %zu\n", sizeof(ev_mailbox_t));
    printf("ev_actor_runtime_t %zu\n", sizeof(ev_actor_runtime_t));
    printf("ev_timer_service_t %zu\n", sizeof(ev_timer_service_t));
    printf("ev_trace_ring_t %zu\n", sizeof(ev_trace_ring_t));
    printf("ev_fault_registry_t %zu\n", sizeof(ev_fault_registry_t));
    printf("ev_metric_registry_t %zu\n", sizeof(ev_metric_registry_t));
    printf("ev_network_outbox_t %zu\n", sizeof(ev_network_outbox_t));
    printf("mailbox_storage %zu\n", sizeof(((ev_runtime_graph_t *)0)->mailbox_storage));
    printf("adapter_static_buffers %u\n", 0U);
    return 0;
}
'''
probe.write_text(probe_c, encoding="utf-8")

cmd = [
    "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Icore/include",
    "-Icore/generated/include", "-Iruntime/include", "-Imodules/include",
    "-Idrivers/include", "-Iports/include", "-Iapps/demo/include", "-Iconfig",
    str(probe), "-o", str(build / "memory_budget_probe")
]
res = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
if res.returncode != 0:
    print(res.stdout)
    print(res.stderr)
    raise SystemExit(res.returncode)

out = subprocess.check_output([str(build / "memory_budget_probe")], text=True)
sizes = {}
for line in out.splitlines():
    key, value = line.rsplit(" ", 1)
    sizes[key] = int(value)

if sizes["ev_runtime_graph_t"] > 131072:
    print("ev_runtime_graph_t exceeds host static budget")
    raise SystemExit(1)

report = ROOT / "docs" / "release" / "memory_budget_report.md"
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(
    "# Memory budget report\n\n"
    + "\n".join(f"- {name}: {size} bytes" for name, size in sizes.items())
    + "\n\nBudget gate: pass for host static-size probe. ESP8266 linker-map validation requires the SDK/toolchain.\n",
    encoding="utf-8",
)
print("memory budget passed")
