#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import os
import re

ROOT = Path(__file__).resolve().parents[2]
errors: list[str] = []

IGNORED_DIRS = {".git", "build", "logs", "log", "docker", "docs/generated", "__pycache__"}
IGNORED_DIR_NAMES = {entry for entry in IGNORED_DIRS if "/" not in entry}
IGNORED_PATH_PREFIXES = {entry for entry in IGNORED_DIRS if "/" in entry}

FORBIDDEN_HEAP = re.compile(
    r"\b(malloc|calloc|realloc|free|strdup|pvPortMalloc|vPortFree|heap_caps_malloc|heap_caps_free)\s*\("
)
FORBIDDEN_BLOCK = re.compile(r"\b(portMAX_DELAY|vTaskDelay)\b")
SDK_INCLUDE = re.compile(r'#\s*include\s*[<"](?:esp_|freertos/|FreeRTOS|driver/|gpio|i2c)')
TODO = re.compile(r"\b(TODO|FIXME)\b")

ADAPTER_BOOTSTRAP_CALLS = {
    "xSemaphoreCreateMutex": re.compile(r"\bxSemaphoreCreateMutex\s*\("),
    "xSemaphoreCreateBinary": re.compile(r"\bxSemaphoreCreateBinary\s*\("),
    "xTaskCreateStatic": re.compile(r"\bxTaskCreateStatic\s*\("),
    "xTaskCreate": re.compile(r"\bxTaskCreate\s*\("),
    "esp_mqtt_client_init": re.compile(r"\besp_mqtt_client_init\s*\("),
    "esp_wifi_init": re.compile(r"\besp_wifi_init\s*\("),
    "tcpip_adapter_init": re.compile(r"\btcpip_adapter_init\s*\("),
}
ADAPTER_EXCEPTION_RE = re.compile(r"^\s*EV_ADAPTER_EXCEPTION\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*(.*?)\s*\)\s*$")
ADAPTER_EXCEPTION_CATEGORIES = {"bootstrap", "hil_bootstrap", "static_safe"}

REQUIRED_LAYERING_CONTRACT_SECTIONS = [
    "config/codegen",
    "core kernel",
    "runtime",
    "actor descriptors / module registry",
    "device actors",
    "drivers",
    "ports",
    "apps",
    "adapters",
    "bsp",
    "tests",
    "tools",
    "docs",
]

def validate_layering_contract_document() -> None:
    contract_path = ROOT / "docs" / "architecture" / "layering-contract.md"
    if not contract_path.exists():
        errors.append("layering contract missing: docs/architecture/layering-contract.md")
        return
    contract = contract_path.read_text(encoding="utf-8", errors="ignore")
    for section in REQUIRED_LAYERING_CONTRACT_SECTIONS:
        if re.search(rf"^##\s+{re.escape(section)}\s*$", contract, flags=re.MULTILINE) is None:
            errors.append(f"layering contract missing section: {section}")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text


def is_ignored_path(path: Path) -> bool:
    rel_path = path.relative_to(ROOT)
    rel_parts = rel_path.parts
    rel = rel_path.as_posix()
    if any(part in IGNORED_DIR_NAMES for part in rel_parts):
        return True
    return any(rel == ignored or rel.startswith(f"{ignored}/") for ignored in IGNORED_PATH_PREFIXES)


def iter_repo_files(base: Path = ROOT):
    for dirpath, dirnames, filenames in os.walk(base):
        current = Path(dirpath)
        dirnames[:] = [name for name in dirnames if not is_ignored_path(current / name)]
        for filename in filenames:
            candidate = current / filename
            if not is_ignored_path(candidate):
                yield candidate


GRAPH_INTERNAL_FIELDS = [
    "registry",
    "actor_runtimes",
    "mailboxes",
    "mailbox_storage",
    "actor_contexts",
    "descriptors",
    "instances",
    "instance_bound",
    "lifecycle",
    "actor_enabled",
    "timer_service",
    "ingress_service",
    "quiescence_service",
    "faults",
    "metrics",
    "trace_ring",
    "active_routes",
    "delivery_service",
    "scheduler",
    "active_routes_bound",
    "board_capabilities",
    "runtime_capabilities",
    "ports",
    "board_profile",
]
GRAPH_INTERNAL_FIELD_ALT = "|".join(re.escape(field) for field in GRAPH_INTERNAL_FIELDS)
GRAPH_INTERNAL_ACCESS_RE = re.compile(
    rf"(?:\bgraph\s*(?:->|\.)|(?:\.|->)\s*graph\s*(?:->|\.))\s*({GRAPH_INTERNAL_FIELD_ALT})\b"
)
GRAPH_NULL_CAST_ACCESS_RE = re.compile(
    rf"\(\s*\(\s*ev_runtime_graph_t\s*\*\s*\)\s*0\s*\)\s*->\s*({GRAPH_INTERNAL_FIELD_ALT})\b"
)
GRAPH_ACCESS_AUDIT_ROOTS = ("apps", "adapters", "core", "modules", "drivers", "ports", "tests", "tools", "runtime/include")
GRAPH_ACCESS_ALLOWLIST = {"runtime/include/ev/runtime_graph.h", "tools/audit/static_contracts.py"}


def is_graph_audit_path(rel: str) -> bool:
    return any(rel == root or rel.startswith(f"{root}/") for root in GRAPH_ACCESS_AUDIT_ROOTS)


def graph_access_is_allowlisted(rel: str) -> bool:
    return rel.startswith("runtime/src/") or rel in GRAPH_ACCESS_ALLOWLIST


def validate_runtime_graph_access_boundary() -> None:
    for p in iter_repo_files(ROOT):
        if p.suffix not in {".c", ".h", ".py"}:
            continue
        rel = p.relative_to(ROOT).as_posix()
        if not is_graph_audit_path(rel) or graph_access_is_allowlisted(rel):
            continue
        code = strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
        for pattern in (GRAPH_INTERNAL_ACCESS_RE, GRAPH_NULL_CAST_ACCESS_RE):
            for match in pattern.finditer(code):
                line_no = code.count("\n", 0, match.start()) + 1
                errors.append(
                    f"runtime graph internal field access outside runtime boundary: {rel}:{line_no} uses {match.group(1)}"
                )


def static_contract_self_test() -> None:
    for symbol in ["pvPortMalloc", "vPortFree", "heap_caps_malloc", "heap_caps_free"]:
        sample = f"void *p = {symbol}(16);" if symbol.endswith("malloc") or symbol == "pvPortMalloc" else f"{symbol}(p);"
        if FORBIDDEN_HEAP.search(strip_comments(sample)) is None:
            errors.append(f"static-contract self-test failed: {symbol} was not detected")
    comment_only = "/* pvPortMalloc(16); */\n// vPortFree(p);\n/* heap_caps_malloc(16, 0); */"
    if FORBIDDEN_HEAP.search(strip_comments(comment_only)) is not None:
        errors.append("static-contract self-test failed: heap API in comments was not ignored")



def validate_route_qos_contract() -> None:
    report_path = ROOT / "docs" / "release" / "route_qos_enforcement_report.md"
    if not report_path.exists():
        errors.append("route QoS enforcement report missing: docs/release/route_qos_enforcement_report.md")

    delivery_path = ROOT / "runtime" / "src" / "ev_delivery_service.c"
    if delivery_path.exists():
        code = strip_comments(delivery_path.read_text(encoding="utf-8", errors="ignore"))
        if re.search(
            r"EV_ROUTE_QOS_CRITICAL\s*\|\|[^;{}]*"
            r"EV_ROUTE_QOS_WAKEUP_CRITICAL\s*\|\|[^;{}]*"
            r"EV_ROUTE_QOS_COMMAND",
            code,
        ) is not None:
            errors.append("delivery service reintroduced hand-coded strict QoS disjunction")

static_contract_self_test()
validate_layering_contract_document()
validate_runtime_graph_access_boundary()
validate_route_qos_contract()

for artifact in iter_repo_files(ROOT):
    if is_ignored_path(artifact):
        continue
    if artifact.is_file() and artifact.suffix in {".orig", ".rej"}:
        errors.append(f"patch conflict artifact must not be committed: {artifact.relative_to(ROOT).as_posix()}")


def load_adapter_exception_allowlist() -> dict[tuple[str, str], tuple[str, str]]:
    allowlist_path = ROOT / "tools" / "audit" / "adapter_exception_allowlist.def"
    allowed: dict[tuple[str, str], tuple[str, str]] = {}
    if not allowlist_path.exists():
        errors.append("adapter exception allowlist missing: tools/audit/adapter_exception_allowlist.def")
        return allowed
    for line_no, raw in enumerate(allowlist_path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        match = ADAPTER_EXCEPTION_RE.match(line)
        if match is None:
            errors.append(f"invalid adapter exception allowlist row at {line_no}")
            continue
        rel, symbol, category, rationale = (part.strip() for part in match.groups())
        if symbol not in ADAPTER_BOOTSTRAP_CALLS:
            errors.append(f"adapter exception allowlist row {line_no} uses unknown symbol {symbol}")
            continue
        if category not in ADAPTER_EXCEPTION_CATEGORIES:
            errors.append(f"adapter exception allowlist row {line_no} uses invalid category {category}")
            continue
        if not rationale:
            errors.append(f"adapter exception allowlist row {line_no} has empty rationale")
            continue
        if symbol == "xTaskCreate" and category != "hil_bootstrap":
            errors.append(f"adapter exception allowlist row {line_no}: xTaskCreate must be HIL bootstrap only")
            continue
        if symbol == "xTaskCreateStatic" and category != "static_safe":
            errors.append(f"adapter exception allowlist row {line_no}: xTaskCreateStatic must be static_safe")
            continue
        if symbol in {"xSemaphoreCreateMutex", "xSemaphoreCreateBinary", "esp_mqtt_client_init", "esp_wifi_init", "tcpip_adapter_init"} and category != "bootstrap":
            errors.append(f"adapter exception allowlist row {line_no}: {symbol} must be bootstrap")
            continue
        if not (ROOT / rel).exists():
            errors.append(f"adapter exception allowlist row {line_no} references missing file {rel}")
            continue
        key = (rel, symbol)
        if key in allowed:
            errors.append(f"duplicate adapter exception allowlist row for {rel}:{symbol}")
            continue
        allowed[key] = (category, rationale)
    return allowed


ADAPTER_EXCEPTION_ALLOWLIST = load_adapter_exception_allowlist()
ADAPTER_EXCEPTION_OBSERVED: set[tuple[str, str]] = set()


for subdir in ["core", "runtime", "modules", "drivers", "ports", "apps", "tests/host", "tests/property"]:
    base = ROOT / subdir
    if not base.exists():
        continue
    for p in iter_repo_files(base):
        if is_ignored_path(p):
            continue
        if p.suffix not in {".c", ".h"}:
            continue
        rel = p.relative_to(ROOT).as_posix()
        text = p.read_text(encoding="utf-8", errors="ignore")
        code = strip_comments(text)
        if FORBIDDEN_HEAP.search(code):
            errors.append(f"forbidden heap call in {rel}")
        if subdir in {"core", "runtime", "modules", "drivers", "apps"} and SDK_INCLUDE.search(code):
            errors.append(f"SDK include leak in portable layer {rel}")
        if subdir in {"core", "runtime", "modules", "drivers", "apps"} and FORBIDDEN_BLOCK.search(code):
            errors.append(f"forbidden blocking primitive in {rel}")
        if TODO.search(text):
            errors.append(f"production TODO/FIXME marker in {rel}")

route_lines = [
    line.strip()
    for line in (ROOT / "config" / "routes.def").read_text(encoding="utf-8").splitlines()
    if line.strip().startswith(("EV_ROUTE(", "EV_ROUTE_EX("))
]
gen_h = ROOT / "core" / "generated" / "include" / "ev" / "route_table_generated.h"
if not gen_h.exists():
    errors.append("generated route table header missing")
else:
    match = re.search(r"EV_ROUTE_TABLE_GENERATED_COUNT\s+([0-9]+)U", gen_h.read_text(encoding="utf-8"))
    if match is None or int(match.group(1)) != len(route_lines):
        errors.append("generated route table count does not match config/routes.def")

bsp = ROOT / "bsp"
for board in bsp.iterdir():
    if not board.is_dir():
        continue
    if not (board / "board_profile.h").exists():
        errors.append(f"missing board_profile.h in {board.name}")
    pins = board / "pins.def"
    if pins.exists():
        for line_no, raw in enumerate(pins.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            line = raw.strip()
            if line and not line.startswith("//") and not line.startswith(("EV_PIN(", "EV_BSP_PIN(", "EV_BSP_PIN_ANALOG(")):
                errors.append(f"invalid pins.def schema in {board.name}:{line_no}")

for rel in ["config/faults.def", "config/metrics.def", "config/modules.def", "config/capabilities.def"]:
    if not (ROOT / rel).exists():
        errors.append(f"required SSOT file missing: {rel}")

for p in iter_repo_files(ROOT / "adapters"):
    if p.suffix not in {".c", ".h"}:
        continue
    text = p.read_text(encoding="utf-8", errors="ignore")
    if ("ISR" in text or "isr" in p.name.lower()) and ("IRAM_ATTR" not in text):
        if re.search(r"\b[a-zA-Z0-9_]*isr[a-zA-Z0-9_]*\s*\(", text, flags=re.I):
            errors.append(f"ISR-like adapter lacks IRAM_ATTR marker: {p.relative_to(ROOT).as_posix()}")
    if re.search(r"\b[a-zA-Z0-9_]*isr[a-zA-Z0-9_]*\s*\([^)]*\)\s*\{[^}]*\b(vTaskDelay|malloc|calloc|realloc|free|printf|EV_LOG)", text, flags=re.I | re.S):
        errors.append(f"ISR-like adapter contains forbidden operation: {p.relative_to(ROOT).as_posix()}")

if (ROOT / "app" / "ev_demo_app.c").exists():
    errors.append("legacy app/ev_demo_app.c remains outside apps/demo")

for p in iter_repo_files(ROOT / "adapters"):
    if is_ignored_path(p):
        continue
    if p.suffix not in {".c", ".h"}:
        continue
    rel = p.relative_to(ROOT).as_posix()
    code = strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
    for symbol, pattern in ADAPTER_BOOTSTRAP_CALLS.items():
        if pattern.search(code):
            key = (rel, symbol)
            ADAPTER_EXCEPTION_OBSERVED.add(key)
            if key not in ADAPTER_EXCEPTION_ALLOWLIST:
                errors.append(f"unapproved adapter bootstrap/static primitive {symbol} in {rel}")

for rel, symbol in sorted(set(ADAPTER_EXCEPTION_ALLOWLIST) - ADAPTER_EXCEPTION_OBSERVED):
    errors.append(f"adapter exception allowlist entry is unused or stale: {rel}:{symbol}")


# Hard demo runtime_graph migration contracts.
demo_h = ROOT / "apps" / "demo" / "include" / "ev" / "demo_app.h"
demo_c = ROOT / "apps" / "demo" / "ev_demo_app.c"
adapter_c = ROOT / "adapters" / "esp8266_rtos_sdk" / "components" / "ev_platform" / "ev_runtime_app.c"
if demo_h.exists():
    demo_h_text = strip_comments(demo_h.read_text(encoding="utf-8", errors="ignore"))
    forbidden_demo_header_tokens = {
        "ev_mailbox_t": "demo header must not own per-actor mailboxes",
        "ev_actor_runtime_t": "demo header must not own per-actor runtimes",
        "ev_actor_registry_t": "demo header must not own actor registry",
        "ev_domain_pump_t": "demo header must not own domain pump",
        "ev_system_pump_t": "demo header must not own system pump",
        "next_tick_ms": "demo header must not own legacy tick deadline",
        "next_tick_100ms_ms": "demo header must not own legacy fast tick deadline",
    }
    for token, message in forbidden_demo_header_tokens.items():
        if token in demo_h_text:
            errors.append(message)
if demo_c.exists():
    demo_c_text = strip_comments(demo_c.read_text(encoding="utf-8", errors="ignore"))
    for token, message in {
        "ev_actor_registry_bind": "demo app must not manually bind actor registry",
        "ev_domain_pump_init": "demo app must not initialize domain pumps as composition root",
        "ev_system_pump_init": "demo app must not initialize system pump as composition root",
        "ev_system_pump_run": "demo app poll must not run system pump directly",
        "app->graph.scheduler": "demo app must not access runtime graph scheduler internals",
        "app->graph.timer_service": "demo app must not access runtime graph timer internals",
        ".scheduler.system": "demo app must not access runtime graph system-pump internals",
        ".scheduler.domains": "demo app must not access runtime graph domain-pump internals",
        "ev_demo_app_delivery": "demo app must not use the application delivery callback as actor emission path",
        "ev_runtime_scheduler_poll_once": "demo app poll must use runtime_loop instead of polling scheduler directly",
        "ev_timer_publish_due": "demo app poll must use runtime_loop/graph APIs instead of publishing timers directly",
    }.items():
        if token in demo_c_text:
            errors.append(message)
if adapter_c.exists():
    adapter_text = strip_comments(adapter_c.read_text(encoding="utf-8", errors="ignore"))
    if "next_tick_ms" in adapter_text or "next_tick_100ms_ms" in adapter_text:
        errors.append("ESP8266 runtime adapter must not read legacy demo tick fields")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("static contracts passed")
