#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "config" / "hotpath_contract.def"
errors: list[str] = []

HOTPATH_ROW = re.compile(r"^\s*HOTPATH_FILE\(([^)]+)\)\s*$")
ALLOW_ROW = re.compile(r"^\s*ALLOW_INLINE_METADATA_COPY\(([^,]+),\s*([^)]+)\)\s*$")
FORBIDDEN_ALLOC = re.compile(r"\b(malloc|calloc|realloc|free|strdup|pvPortMalloc|vPortFree|heap_caps_malloc|heap_caps_free)\s*\(")
FORBIDDEN_BLOCKING = re.compile(r"\b(portMAX_DELAY|vTaskDelay|xSemaphoreTake|xQueueReceive)\b")
FORBIDDEN_LOG_IO = re.compile(r"\b(printf|fprintf|vprintf|puts|fopen|getline)\s*\(")
FORBIDDEN_SDK_INCLUDE = re.compile(r'#\s*include\s*[<"](?:esp_|freertos/|FreeRTOS|driver/|gpio|i2c|user_interface\.h)')
COPY_RE = re.compile(r"\b(memcpy|memmove)\s*\(")

REQUIRED_HOTPATH_FILES = {
    "core/src/ev_msg.c",
    "core/src/ev_publish.c",
    "core/src/ev_send.c",
    "core/src/ev_mailbox.c",
    "core/src/ev_dispose.c",
    "core/src/ev_lease_pool.c",
    "runtime/src/ev_delivery_service.c",
    "runtime/src/ev_runtime_poll.c",
    "runtime/src/ev_runtime_loop.c",
    "runtime/src/ev_actor_publish_port.c",
    "runtime/src/ev_active_route_table.c",
}

REQUIRED_MSG_HELPERS = [
    "ev_msg_payload_is_inline_contract",
    "ev_msg_payload_is_lease_contract",
    "ev_msg_payload_is_stream_view_contract",
    "ev_msg_payload_is_zero_copy_contract",
    "ev_msg_payload_requires_release",
    "ev_msg_validate_payload_contract",
]


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text


def load_manifest() -> tuple[set[str], dict[str, list[str]]]:
    hotpath_files: set[str] = set()
    copy_allow: dict[str, list[str]] = {}
    if not MANIFEST.exists():
        errors.append("hotpath manifest missing: config/hotpath_contract.def")
        return hotpath_files, copy_allow

    for line_no, raw in enumerate(MANIFEST.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = HOTPATH_ROW.match(line)
        if match is not None:
            hotpath_files.add(match.group(1).strip())
            continue
        match = ALLOW_ROW.match(line)
        if match is not None:
            rel = match.group(1).strip()
            token = match.group(2).strip()
            copy_allow.setdefault(rel, []).append(token)
            continue
        errors.append(f"invalid hotpath manifest row {line_no}: {raw}")

    for rel in sorted(REQUIRED_HOTPATH_FILES - hotpath_files):
        errors.append(f"required hotpath file missing from manifest: {rel}")
    for rel in sorted(hotpath_files):
        if not (ROOT / rel).exists():
            errors.append(f"manifest references missing file: {rel}")
    return hotpath_files, copy_allow


def line_no_for(text: str, pos: int) -> int:
    return text.count("\n", 0, pos) + 1


def source_line(text: str, pos: int) -> str:
    start = text.rfind("\n", 0, pos) + 1
    end = text.find("\n", pos)
    if end < 0:
        end = len(text)
    return text[start:end].strip()


def validate_hotpath_sources(hotpath_files: set[str], copy_allow: dict[str, list[str]]) -> None:
    for rel in sorted(hotpath_files):
        path = ROOT / rel
        if not path.exists():
            continue
        code = strip_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for pattern, label in [
            (FORBIDDEN_ALLOC, "heap allocation/deallocation"),
            (FORBIDDEN_BLOCKING, "blocking primitive"),
            (FORBIDDEN_LOG_IO, "stdio/log I/O"),
            (FORBIDDEN_SDK_INCLUDE, "SDK include"),
        ]:
            for match in pattern.finditer(code):
                errors.append(f"forbidden {label} in hotpath {rel}:{line_no_for(code, match.start())}")
        for match in COPY_RE.finditer(code):
            line = source_line(code, match.start())
            if not any(token in line for token in copy_allow.get(rel, [])):
                errors.append(f"unbudgeted payload copy in hotpath {rel}:{line_no_for(code, match.start())}: {line}")


def validate_payload_contract_api() -> None:
    msg_h = ROOT / "core" / "include" / "ev" / "msg.h"
    msg_c = ROOT / "core" / "src" / "ev_msg.c"
    for path in [msg_h, msg_c]:
        if not path.exists():
            errors.append(f"required message contract file missing: {path.relative_to(ROOT).as_posix()}")
            continue
        text = strip_comments(path.read_text(encoding="utf-8", errors="ignore"))
        for helper in REQUIRED_MSG_HELPERS:
            if helper not in text:
                errors.append(f"payload contract helper missing from {path.relative_to(ROOT).as_posix()}: {helper}")
    if msg_c.exists():
        code = strip_comments(msg_c.read_text(encoding="utf-8", errors="ignore"))
        if "EV_PAYLOAD_STREAM_VIEW" not in code or "retain_fn != NULL" not in code or "release_fn != NULL" not in code:
            errors.append("stream-view borrowed payload contract is not enforced in ev_msg.c")
        if "EV_PAYLOAD_LEASE" not in code or "retain_fn == NULL" not in code or "release_fn == NULL" not in code:
            errors.append("lease payload contract must require retain and release callbacks")


def validate_make_and_docs() -> None:
    makefile_path = ROOT / "Makefile"
    if not makefile_path.exists():
        errors.append("Makefile missing")
    else:
        makefile = makefile_path.read_text(encoding="utf-8", errors="ignore")
        if re.search(r"^hotpath-zero-alloc-gate\s*:", makefile, flags=re.MULTILINE) is None:
            errors.append("Makefile target missing: hotpath-zero-alloc-gate")
        if "tools/audit/hotpath_zero_alloc_contract.py" not in makefile:
            errors.append("hotpath-zero-alloc-gate must run tools/audit/hotpath_zero_alloc_contract.py")
        if "test_zero_copy_payload_contract" not in makefile:
            errors.append("zero-copy payload contract host test is not registered in Makefile")
    for rel in [
        "docs/architecture/hotpath-zero-allocation-contract.md",
        "docs/architecture/zero_copy_payload_contract.md",
        "docs/release/hotpath_zero_allocation_report.md",
    ]:
        if not (ROOT / rel).exists():
            errors.append(f"required hotpath documentation missing: {rel}")


def validate_delivery_hotpath_shape() -> None:
    delivery = ROOT / "runtime" / "src" / "ev_delivery_service.c"
    if not delivery.exists():
        errors.append("delivery service missing")
        return
    code = strip_comments(delivery.read_text(encoding="utf-8", errors="ignore"))
    if re.search(r"for\s*\([^\)]*active_routes\.count", code) is not None:
        errors.append("active publish hotpath must not scan active_routes.count directly")
    if "ev_active_route_table_span_for_event" not in code:
        errors.append("active publish hotpath must use per-event active route spans")


def self_test() -> None:
    commented = "/* malloc(1); memcpy(dst, src, n); */\n// free(p)\n"
    if FORBIDDEN_ALLOC.search(strip_comments(commented)) is not None:
        errors.append("hotpath audit self-test failed: comments not ignored for heap calls")
    if FORBIDDEN_ALLOC.search(strip_comments("void f(void){ malloc(1); }")) is None:
        errors.append("hotpath audit self-test failed: malloc not detected")


def main() -> int:
    self_test()
    hotpath_files, copy_allow = load_manifest()
    validate_hotpath_sources(hotpath_files, copy_allow)
    validate_payload_contract_api()
    validate_make_and_docs()
    validate_delivery_hotpath_shape()
    if errors:
        for error in errors:
            print(error)
        return 1
    print("hotpath zero-allocation contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
