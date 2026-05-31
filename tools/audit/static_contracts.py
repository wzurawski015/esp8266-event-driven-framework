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



def check_sdk_evidence_contracts() -> None:
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8", errors="ignore")
    required_targets = ["sdk-build-evidence", "sdk-map-stack-evidence", "sdk-evidence-gate"]
    for target in required_targets:
        if f"{target}:" not in makefile:
            errors.append(f"static-contracts: Makefile missing {target}")
    tool = ROOT / "tools" / "release" / "capture_sdk_evidence.py"
    if not tool.is_file():
        errors.append("static-contracts: missing tools/release/capture_sdk_evidence.py")
    else:
        text = tool.read_text(encoding="utf-8", errors="ignore")
        for token in ["ENVIRONMENT_BLOCKED", "--gate", "evidence.json", "EV_MEM_"]:
            if token not in text:
                errors.append(f"static-contracts: SDK evidence tool missing {token}")


def check_hil_i2c_evidence_contracts() -> None:
    parser = ROOT / "tools" / "hil" / "parse_atnel_i2c_hil_log.py"
    if not parser.is_file():
        errors.append("static-contracts: missing ATNEL I2C HIL parser")
    else:
        text = parser.read_text(encoding="utf-8", errors="ignore")
        for token in ["sda-stuck-low-containment", "FIXTURE_NOT_COUPLED", "EV_HIL_RESULT PASS", "--self-test"]:
            if token not in text:
                errors.append(f"static-contracts: ATNEL I2C parser missing {token}")
    hil_c = ROOT / "adapters" / "esp8266_rtos_sdk" / "targets" / "atnel_air_esp_motherboard_i2c_hil" / "main" / "ev_i2c_hil.c"
    if hil_c.is_file():
        text = hil_c.read_text(encoding="utf-8", errors="ignore")
        for token in ["EV_HIL_I2C_CASE_BEGIN", "EV_HIL_I2C_SDA_FORCE_LOW", "EV_HIL_I2C_RECOVERY_RESULT", "EV_HIL_I2C_CASE_RESULT"]:
            if token not in text:
                errors.append(f"static-contracts: ATNEL I2C HIL firmware missing {token}")
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8", errors="ignore")
    for target in ["hil-atnel-i2c-evidence", "hil-atnel-i2c-gate"]:
        if f"{target}:" not in makefile:
            errors.append(f"static-contracts: Makefile missing {target}")

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
GRAPH_ACCESS_ALLOWLIST = {"tools/audit/static_contracts.py"}


def is_graph_audit_path(rel: str) -> bool:
    return any(rel == root or rel.startswith(f"{root}/") for root in GRAPH_ACCESS_AUDIT_ROOTS)


def graph_access_is_allowlisted(rel: str) -> bool:
    return rel.startswith("runtime/src/") or rel in GRAPH_ACCESS_ALLOWLIST



RUNTIME_GRAPH_PUBLIC_HEADER_FORBIDDEN = [
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
    "scheduler",
    "active_routes_bound",
    "board_capabilities",
    "runtime_capabilities",
]
RUNTIME_GRAPH_INTERNAL_INCLUDE_RE = re.compile(r'#\s*include\s*[<"]ev_runtime_graph_internal\.h[>"]')


def validate_runtime_graph_public_header_opaque() -> None:
    public_header = ROOT / "runtime" / "include" / "ev" / "runtime_graph.h"
    if not public_header.exists():
        errors.append("runtime graph public header missing: runtime/include/ev/runtime_graph.h")
        return
    code = strip_comments(public_header.read_text(encoding="utf-8", errors="ignore"))
    for field in RUNTIME_GRAPH_PUBLIC_HEADER_FORBIDDEN:
        if re.search(rf"\b{re.escape(field)}\b", code) is not None:
            errors.append(f"runtime graph public header exposes internal field name: {field}")
    if "EV_RUNTIME_GRAPH_OPAQUE_STORAGE_BYTES" not in code:
        errors.append("runtime graph public header must expose bounded opaque storage size")
    if "ev_runtime_graph_opaque_storage_t" not in code:
        errors.append("runtime graph public header must use an opaque storage wrapper")


def validate_runtime_graph_internal_header_boundary() -> None:
    for p in iter_repo_files(ROOT):
        if p.suffix not in {".c", ".h"}:
            continue
        rel = p.relative_to(ROOT).as_posix()
        if rel.startswith("runtime/src/"):
            continue
        code = strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
        if RUNTIME_GRAPH_INTERNAL_INCLUDE_RE.search(code) is not None:
            errors.append(f"runtime graph internal header included outside runtime/src: {rel}")

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




DEVICE_ACTOR_SOURCES = {
    "ev_rtc_actor.c",
    "ev_ds18b20_actor.c",
    "ev_mcp23008_actor.c",
    "ev_oled_actor.c",
    "ev_panel_actor.c",
}
FRAMEWORK_ACTOR_SOURCES = {
    "ev_network_actor.c",
    "ev_command_actor.c",
    "ev_power_actor.c",
    "ev_watchdog_actor.c",
    "ev_supervisor_actor.c",
}
DEVICE_ACTOR_HEADERS = {
    "rtc_actor.h",
    "ds18b20_actor.h",
    "mcp23008_actor.h",
    "oled_actor.h",
    "panel_actor.h",
}
FRAMEWORK_ACTOR_HEADERS = {
    "network_actor.h",
    "command_actor.h",
    "power_actor.h",
    "watchdog_actor.h",
    "supervisor_actor.h",
}
CONCRETE_ACTOR_HEADERS = DEVICE_ACTOR_HEADERS | FRAMEWORK_ACTOR_HEADERS
CORE_ACTOR_KERNEL_SOURCE_ALLOWLIST = {"ev_actor_catalog.c", "ev_actor_runtime.c"}
CORE_ACTOR_KERNEL_HEADER_ALLOWLIST = {"actor_catalog.h", "actor_id.h", "actor_runtime.h"}
RELATIVE_ACTORS_INCLUDE_RE = re.compile(r'#\s*include\s*[<"][^>"]*\.\./[^>"]*actors/')
CONCRETE_ACTOR_INCLUDE_RE = re.compile(r'#\s*include\s*[<"]ev/(%s)[>"]' % "|".join(re.escape(h) for h in sorted(CONCRETE_ACTOR_HEADERS)))


def validate_actor_layering_boundary() -> None:
    for name in sorted(DEVICE_ACTOR_SOURCES):
        if (ROOT / "core" / "src" / name).exists():
            errors.append(f"concrete device actor implementation remains in core/src: {name}")
        if not (ROOT / "actors" / "device" / name).exists():
            errors.append(f"device actor implementation missing from actors/device: {name}")
    for name in sorted(FRAMEWORK_ACTOR_SOURCES):
        if (ROOT / "core" / "src" / name).exists():
            errors.append(f"concrete framework actor implementation remains in core/src: {name}")
        if not (ROOT / "actors" / "framework" / name).exists():
            errors.append(f"framework actor implementation missing from actors/framework: {name}")
    for name in sorted(DEVICE_ACTOR_HEADERS):
        if (ROOT / "core" / "include" / "ev" / name).exists():
            errors.append(f"concrete device actor header remains in core/include/ev: {name}")
        if not (ROOT / "actors" / "device" / "include" / "ev" / name).exists():
            errors.append(f"device actor header missing from actors/device/include/ev: {name}")
    for name in sorted(FRAMEWORK_ACTOR_HEADERS):
        if (ROOT / "core" / "include" / "ev" / name).exists():
            errors.append(f"concrete framework actor header remains in core/include/ev: {name}")
        if not (ROOT / "actors" / "framework" / "include" / "ev" / name).exists():
            errors.append(f"framework actor header missing from actors/framework/include/ev: {name}")

    core_src_actor_files = {p.name for p in (ROOT / "core" / "src").glob("*actor*.c")}
    for name in sorted(core_src_actor_files - CORE_ACTOR_KERNEL_SOURCE_ALLOWLIST):
        errors.append(f"non-kernel actor source in core/src: {name}")
    core_header_actor_files = {p.name for p in (ROOT / "core" / "include" / "ev").glob("*actor*.h")}
    for name in sorted(core_header_actor_files - CORE_ACTOR_KERNEL_HEADER_ALLOWLIST):
        errors.append(f"non-kernel actor header in core/include/ev: {name}")

    for p in iter_repo_files(ROOT):
        if p.suffix not in {".c", ".h"}:
            continue
        rel = p.relative_to(ROOT).as_posix()
        code = strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
        if RELATIVE_ACTORS_INCLUDE_RE.search(code) is not None:
            errors.append(f"relative include into actors layer is forbidden: {rel}")
        if rel.startswith("core/") and CONCRETE_ACTOR_INCLUDE_RE.search(code) is not None:
            errors.append(f"core must not include concrete device/framework actor headers: {rel}")
        if rel.startswith(("actors/device/", "actors/framework/")) and SDK_INCLUDE.search(code) is not None:
            errors.append(f"SDK include leak in actor layer {rel}")
        if rel.startswith(("actors/device/", "actors/framework/")) and FORBIDDEN_BLOCK.search(code):
            errors.append(f"forbidden blocking primitive in actor layer {rel}")
        if rel.startswith(("actors/device/", "actors/framework/")) and FORBIDDEN_HEAP.search(code):
            errors.append(f"forbidden heap call in actor layer {rel}")

def static_contract_self_test() -> None:
    for symbol in ["pvPortMalloc", "vPortFree", "heap_caps_malloc", "heap_caps_free"]:
        sample = f"void *p = {symbol}(16);" if symbol.endswith("malloc") or symbol == "pvPortMalloc" else f"{symbol}(p);"
        if FORBIDDEN_HEAP.search(strip_comments(sample)) is None:
            errors.append(f"static-contract self-test failed: {symbol} was not detected")
    comment_only = "/* pvPortMalloc(16); */\n// vPortFree(p);\n/* heap_caps_malloc(16, 0); */"
    if FORBIDDEN_HEAP.search(strip_comments(comment_only)) is not None:
        errors.append("static-contract self-test failed: heap API in comments was not ignored")



def validate_route_qos_contract() -> None:
    required = ["runtime/include/ev/qos_contract.h", "runtime/src/ev_qos_contract.c", "tools/audit/qos_contract_check.py", "tests/host/test_qos_contract_table.c", "tests/host/test_qos_route_module_compatibility.c", "docs/architecture/qos_delivery_contract.md", "docs/release/qos_end_to_end_enforcement_report.md"]
    for rel in required:
        if not (ROOT / rel).exists():
            errors.append(f"QoS contract artifact missing: {rel}")
    delivery_header = ROOT / "runtime" / "include" / "ev" / "delivery_service.h"
    if delivery_header.exists():
        header_text = delivery_header.read_text(encoding="utf-8", errors="ignore")
        for field in ["rejected_routes", "qos_conflict_routes"]:
            if field not in header_text:
                errors.append(f"delivery report missing QoS visibility field: {field}")
    delivery_path = ROOT / "runtime" / "src" / "ev_delivery_service.c"
    if delivery_path.exists():
        code = strip_comments(delivery_path.read_text(encoding="utf-8", errors="ignore"))
        if re.search(r"EV_ROUTE_QOS_CRITICAL\s*\|\|[^;{}]*" r"EV_ROUTE_QOS_WAKEUP_CRITICAL\s*\|\|[^;{}]*" r"EV_ROUTE_QOS_COMMAND", code) is not None:
            errors.append("delivery service reintroduced hand-coded strict QoS disjunction")
        if "ev_qos_failure_is_drop_allowed" not in code:
            errors.append("delivery service must use central QoS failure behavior")
    graph_path = ROOT / "runtime" / "src" / "ev_runtime_graph.c"
    if graph_path.exists() and "ev_qos_validate_route_against_module" not in strip_comments(graph_path.read_text(encoding="utf-8", errors="ignore")):
        errors.append("runtime builder must validate route/module QoS before hot path")
    makefile_path = ROOT / "Makefile"
    if makefile_path.exists():
        makefile = makefile_path.read_text(encoding="utf-8", errors="ignore")
        if "qos-contracts" not in makefile or "tools/audit/qos_contract_check.py" not in makefile:
            errors.append("QoS contract checker must be registered in Makefile")
        for test_name in ["test_qos_contract_table", "test_qos_route_module_compatibility"]:
            if test_name not in makefile:
                errors.append(f"QoS host test not registered in Makefile: {test_name}")

def validate_trace_timestamp_contract() -> None:
    delivery_path = ROOT / "runtime" / "src" / "ev_delivery_service.c"
    if not delivery_path.exists():
        errors.append("delivery service missing: runtime/src/ev_delivery_service.c")
        return
    code = strip_comments(delivery_path.read_text(encoding="utf-8", errors="ignore"))
    if re.search(r"\brec\s*\.\s*timestamp_us\s*=\s*0U\s*;", code) is not None:
        errors.append("delivery trace timestamp must be read from the monotonic clock port, with zero only as fallback")
    if "ev_delivery_trace_timestamp_us" not in code:
        errors.append("delivery trace timestamp helper missing")
    if "mono_now_us" not in code:
        errors.append("delivery trace must call the monotonic clock port")

def extract_make_target_body(makefile: str, target: str) -> str:
    match = re.search(rf"^{re.escape(target)}\s*:[^\n]*\n(?P<body>(?:\t.*\n|\s*\n)*)", makefile, flags=re.MULTILINE)
    return match.group("body") if match else ""


def validate_host_safety_gate_contract() -> None:
    makefile_path = ROOT / "Makefile"
    if not makefile_path.exists():
        errors.append("Makefile missing for host safety gate contract")
        return
    makefile = makefile_path.read_text(encoding="utf-8", errors="ignore")
    for target in ["host-strict-test", "host-sanitize-cc-check", "host-sanitize-test", "host-tsan-cc-check", "host-tsan-test", "clang-tidy-gate", "safety-gate"]:
        if re.search(rf"^{re.escape(target)}\s*:", makefile, flags=re.MULTILINE) is None:
            errors.append(f"host safety target missing: {target}")
    strict_flags = re.search(r"^HOST_STRICT_CFLAGS\s*\?=\s*(.*)$", makefile, flags=re.MULTILINE)
    if strict_flags is None:
        errors.append("HOST_STRICT_CFLAGS missing")
    else:
        for flag in ["-std=c17", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]:
            if flag not in strict_flags.group(1):
                errors.append(f"HOST_STRICT_CFLAGS missing {flag}")
    sanitize_flags = re.search(r"^HOST_SANITIZE_CFLAGS\s*\?=\s*(.*)$", makefile, flags=re.MULTILINE)
    if sanitize_flags is None:
        errors.append("HOST_SANITIZE_CFLAGS missing")
    else:
        for flag in ["-std=c17", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]:
            if flag not in sanitize_flags.group(1):
                errors.append(f"HOST_SANITIZE_CFLAGS missing {flag}")
    sanitize_ld = re.search(r"^HOST_SANITIZE_LDFLAGS\s*\?=\s*(.*)$", makefile, flags=re.MULTILINE)
    if sanitize_ld is None or "-fsanitize=address,undefined" not in sanitize_ld.group(1):
        errors.append("HOST_SANITIZE_LDFLAGS must include -fsanitize=address,undefined")
    strict_body = extract_make_target_body(makefile, "host-strict-test")
    if "host-test" not in strict_body or "HOST_STRICT_CFLAGS" not in strict_body:
        errors.append("host-strict-test must build and run host-test with HOST_STRICT_CFLAGS")
    sanitize_body = extract_make_target_body(makefile, "host-sanitize-test")
    if "host-test" not in sanitize_body or "HOST_SANITIZE_CFLAGS" not in sanitize_body:
        errors.append("host-sanitize-test must build and run host-test with HOST_SANITIZE_CFLAGS")
    if "sdk-" in sanitize_body:
        errors.append("host-sanitize-test must remain host-only and must not invoke SDK targets")
    if re.search(r"host-sanitize-test\s*:[^\n]*host-sanitize-cc-check", makefile) is None:
        errors.append("host-sanitize-test must depend on host-sanitize-cc-check")
    tsan_body = extract_make_target_body(makefile, "host-tsan-test")
    if "sdk-" in tsan_body:
        errors.append("host-tsan-test must remain host-only and must not invoke SDK targets")
    if "ENVIRONMENT_BLOCKED" not in extract_make_target_body(makefile, "host-sanitize-cc-check"):
        errors.append("host-sanitize compiler check must report ENVIRONMENT_BLOCKED when unsupported")
    if "ENVIRONMENT_BLOCKED" not in extract_make_target_body(makefile, "host-tsan-cc-check"):
        errors.append("host-tsan compiler check must report ENVIRONMENT_BLOCKED when unsupported")
    if "ENVIRONMENT_BLOCKED" not in extract_make_target_body(makefile, "clang-tidy-gate"):
        errors.append("clang-tidy-gate must report ENVIRONMENT_BLOCKED when clang-tidy is unavailable")
    safety = re.search(r"^safety-gate\s*:(.*)$", makefile, flags=re.MULTILINE)
    if safety is None:
        errors.append("safety-gate target missing")
    else:
        for dep in ["host-strict-test", "host-sanitize-test"]:
            if dep not in safety.group(1):
                errors.append(f"safety-gate missing dependency: {dep}")
    if not (ROOT / "docs" / "release" / "host_safety_gate_report.md").exists():
        errors.append("host safety gate report missing: docs/release/host_safety_gate_report.md")


def validate_hotpath_zero_alloc_contract_registration() -> None:
    required = [
        "config/hotpath_contract.def",
        "tools/audit/hotpath_zero_alloc_contract.py",
        "docs/architecture/hotpath-zero-allocation-contract.md",
        "docs/architecture/zero_copy_payload_contract.md",
        "docs/release/hotpath_zero_allocation_report.md",
        "tests/host/test_zero_copy_payload_contract.c",
    ]
    for rel in required:
        if not (ROOT / rel).exists():
            errors.append(f"hotpath zero-allocation contract artifact missing: {rel}")

    makefile_path = ROOT / "Makefile"
    if not makefile_path.exists():
        errors.append("Makefile missing for hotpath zero-allocation contract")
        return
    makefile = makefile_path.read_text(encoding="utf-8", errors="ignore")
    if re.search(r"^hotpath-zero-alloc-gate\s*:", makefile, flags=re.MULTILINE) is None:
        errors.append("hotpath-zero-alloc-gate target missing")
    if "tools/audit/hotpath_zero_alloc_contract.py" not in makefile:
        errors.append("hotpath-zero-alloc-gate must run tools/audit/hotpath_zero_alloc_contract.py")
    if "test_zero_copy_payload_contract" not in makefile:
        errors.append("zero-copy payload contract test must be registered in Makefile")

    manifest = ROOT / "config" / "hotpath_contract.def"
    if manifest.exists():
        manifest_text = manifest.read_text(encoding="utf-8", errors="ignore")
        for rel in [
            "core/src/ev_msg.c",
            "core/src/ev_mailbox.c",
            "core/src/ev_lease_pool.c",
            "runtime/src/ev_delivery_service.c",
            "runtime/src/ev_runtime_poll.c",
        ]:
            if f"HOTPATH_FILE({rel})" not in manifest_text:
                errors.append(f"hotpath manifest does not cover required file: {rel}")


def validate_power_state_machine_contract() -> None:
    header = ROOT / "runtime" / "include" / "ev" / "power_state_machine.h"
    source = ROOT / "runtime" / "src" / "ev_power_state_machine.c"
    actor = ROOT / "actors" / "framework" / "ev_power_actor.c"
    test = ROOT / "tests" / "host" / "test_power_state_machine.c"
    for path in [header, source, actor, test, ROOT / "docs" / "architecture" / "deep_sleep_state_protocol.md", ROOT / "docs" / "release" / "deep_sleep_state_protocol_report.md"]:
        if not path.exists():
            errors.append(f"deep sleep protocol artifact missing: {path.relative_to(ROOT).as_posix()}")
            return
    header_code = strip_comments(header.read_text(encoding="utf-8", errors="ignore"))
    for state in ["EV_POWER_STATE_ACTIVE", "EV_POWER_STATE_SLEEP_REQUESTED", "EV_POWER_STATE_DRAINING_RUNTIME", "EV_POWER_STATE_LOG_FLUSHING", "EV_POWER_STATE_PORTS_PREPARE_SLEEP", "EV_POWER_STATE_RTC_STATE_SAVED", "EV_POWER_STATE_ENTERING_DEEP_SLEEP", "EV_POWER_STATE_WAKE_BOOT", "EV_POWER_STATE_REJECTED", "EV_POWER_STATE_FAILED"]:
        if state not in header_code:
            errors.append(f"deep sleep state enum missing: {state}")
    source_code = strip_comments(source.read_text(encoding="utf-8", errors="ignore"))
    if FORBIDDEN_HEAP.search(source_code):
        errors.append("power state machine must not use heap allocation")
    if SDK_INCLUDE.search(source_code):
        errors.append("power state machine must not include ESP8266 SDK")
    if FORBIDDEN_BLOCK.search(source_code):
        errors.append("power state machine must not use blocking primitives")
    actor_code = strip_comments(actor.read_text(encoding="utf-8", errors="ignore"))
    if "ev_power_state_machine_step" not in actor_code:
        errors.append("power actor must use the formal power state machine")
    if "EV_POWER_ACTION_RTC_STATE_SAVED" not in actor_code:
        errors.append("power actor must traverse RTC_STATE_SAVED marker before deep sleep")
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8", errors="ignore") if (ROOT / "Makefile").exists() else ""
    if "test_power_state_machine" not in makefile:
        errors.append("power state machine host test is not registered in Makefile")
    if "ev_power_state_machine.c" not in makefile:
        errors.append("power state machine source is not registered in Makefile")


def validate_perf_budget_contract() -> None:
    required = ["config/perf_budgets.json", "tools/bench_report.py", "docs/perf/perf_regression_budget_policy.md", "docs/release/perf_regression_budget_report.md", "docs/specs/performance_baseline.md"]
    for rel in required:
        if not (ROOT / rel).exists():
            errors.append(f"perf budget artifact missing: {rel}")
    makefile_path = ROOT / "Makefile"
    if not makefile_path.exists():
        errors.append("Makefile missing for perf budget contract")
        return
    makefile = makefile_path.read_text(encoding="utf-8", errors="ignore")
    for target in ["perf-report", "perf-budget-gate", "perf-gate"]:
        if re.search(rf"^{re.escape(target)}\s*:", makefile, flags=re.MULTILINE) is None:
            errors.append(f"perf target missing: {target}")
    m = re.search(r"^perf-gate\s*:(.*)$", makefile, flags=re.MULTILINE)
    if m is None or "perf-budget-gate" not in m.group(1):
        errors.append("perf-gate must depend on perf-budget-gate")
    if "perf-gate passed (report-only baseline)" in makefile:
        errors.append("perf-gate must not be report-only baseline")
    budget_body = extract_make_target_body(makefile, "perf-budget-gate")
    if "--strict" not in budget_body or "tools/bench_report.py" not in budget_body:
        errors.append("perf-budget-gate must invoke bench_report.py --strict")
    report_body = extract_make_target_body(makefile, "perf-report")
    if "--report-only" not in report_body:
        errors.append("perf-report must be explicit report-only mode")
    bench_report = (ROOT / "tools" / "bench_report.py").read_text(encoding="utf-8", errors="ignore") if (ROOT / "tools" / "bench_report.py").exists() else ""
    for token in ["load_budgets", "hard_max_ns_per_op", "--self-test", "duplicate benchmark", "missing benchmark"]:
        if token not in bench_report:
            errors.append(f"bench_report.py missing budget/parser contract token: {token}")
    budget_text = (ROOT / "config" / "perf_budgets.json").read_text(encoding="utf-8", errors="ignore") if (ROOT / "config" / "perf_budgets.json").exists() else ""
    for bench in ["static_publish_tick_fanout", "active_publish_tick_fanout", "runtime_poll_empty", "runtime_poll_prefilled_mailbox", "runtime_loop_poll_empty", "runtime_loop_poll_prefilled_mailbox"]:
        if bench not in budget_text:
            errors.append(f"perf budget missing required benchmark: {bench}")


static_contract_self_test()
validate_power_state_machine_contract()
validate_layering_contract_document()
validate_runtime_graph_public_header_opaque()
validate_runtime_graph_internal_header_boundary()
validate_runtime_graph_access_boundary()
validate_actor_layering_boundary()
validate_route_qos_contract()
validate_trace_timestamp_contract()
validate_host_safety_gate_contract()
validate_hotpath_zero_alloc_contract_registration()
validate_perf_budget_contract()

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


for subdir in ["core", "runtime", "actors", "modules", "drivers", "ports", "apps", "tests/host", "tests/property"]:
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
        if subdir in {"core", "runtime", "actors", "modules", "drivers", "apps"} and SDK_INCLUDE.search(code):
            errors.append(f"SDK include leak in portable layer {rel}")
        if subdir in {"core", "runtime", "actors", "modules", "drivers", "apps"} and FORBIDDEN_BLOCK.search(code):
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


# Hard demo composition-root and runtime_graph migration contracts.
demo_dir = ROOT / "apps" / "demo"
demo_h = demo_dir / "include" / "ev" / "demo_app.h"
demo_c = demo_dir / "ev_demo_app.c"
adapter_c = ROOT / "adapters" / "esp8266_rtos_sdk" / "components" / "ev_platform" / "ev_runtime_app.c"
demo_required_split_files = [
    "ev_demo_policy.c",
    "ev_demo_board_wiring.c",
    "ev_demo_presentation.c",
    "include/ev/demo_policy.h",
    "include/ev/demo_board_wiring.h",
    "include/ev/demo_presentation.h",
    "include/ev/demo_internal.h",
]
for rel_demo in demo_required_split_files:
    if not (demo_dir / rel_demo).exists():
        errors.append(f"demo composition-root split file missing: apps/demo/{rel_demo}")
report_path = ROOT / "docs" / "release" / "demo_composition_root_report.md"
if not report_path.exists():
    errors.append("demo composition-root report missing: docs/release/demo_composition_root_report.md")
if demo_dir.exists():
    for p in iter_repo_files(demo_dir):
        if p.suffix not in {".c", ".h"}:
            continue
        rel = p.relative_to(ROOT).as_posix()
        code = strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
        if RUNTIME_GRAPH_INTERNAL_INCLUDE_RE.search(code) is not None:
            errors.append(f"demo must not include runtime graph internals: {rel}")
        if SDK_INCLUDE.search(code) is not None:
            errors.append(f"demo must not include ESP8266 SDK directly: {rel}")
        for token, message in {
            "ev_actor_registry_bind": "demo must not manually bind actor registry",
            "ev_domain_pump_init": "demo must not initialize domain pumps",
            "ev_system_pump_init": "demo must not initialize system pump",
            "ev_system_pump_run": "demo must not run system pump directly",
            "ev_runtime_scheduler_poll_once": "demo must use runtime_loop instead of polling scheduler directly",
            "ev_timer_publish_due": "demo must use runtime_loop/graph timer APIs instead of publishing timers directly",
        }.items():
            if token in code:
                errors.append(f"{message}: {rel}")
if demo_c.exists():
    demo_app_text = demo_c.read_text(encoding="utf-8", errors="ignore")
    demo_app_line_count = len(demo_app_text.splitlines())
    demo_app_code = strip_comments(demo_app_text)
    if demo_app_line_count > 1100:
        errors.append(f"demo composition root too large: apps/demo/ev_demo_app.c has {demo_app_line_count} lines")
    for token, message in {
        "ev_runtime_builder_init": "demo runtime builder wiring belongs in demo_board_wiring.c",
        "ev_runtime_builder_add_instance": "demo runtime instance binding belongs in demo_board_wiring.c",
        "ev_runtime_builder_bind_routes": "demo route binding belongs in demo_board_wiring.c",
        "ev_panel_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_supervisor_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_power_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_watchdog_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_network_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_command_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_mcp23008_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_rtc_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_ds18b20_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
        "ev_oled_actor_init": "concrete actor initialization belongs in demo_board_wiring.c",
    }.items():
        if token in demo_app_code:
            errors.append(message)
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
