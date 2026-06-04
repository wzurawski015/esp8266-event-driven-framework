#!/usr/bin/env python3
"""Classify operator/terminal footer lines in captured serial transcripts.

The operator footer is not firmware output.  In particular, Ctrl+C is usually
reported by a terminal wrapper as exit code 130 (128 + SIGINT).  This module
lets evidence tools record that condition without treating it as firmware
failure or as proof of smoke PASS by itself.
"""
from __future__ import annotations

import re
from typing import Any

CTRL_C_RE = re.compile(r"^\s*\^C\s*$")
EXIT_BANNER_RE = re.compile(r"^\s*---\s*exit\s*---\s*$", re.IGNORECASE)
PROCESS_EXIT_RE = re.compile(r"\[process exited with code\s+(?P<code>[0-9]+)(?:\s*\([^)]*\))?\]", re.IGNORECASE)
CLOSE_TERMINAL_RE = re.compile(r"You can now close this terminal|press Enter to restart", re.IGNORECASE)
MONITOR_STOP_RE = re.compile(r"EV_MONITOR_(?:WRAPPER_)?STOP\b.*(?:operator_sigint|SIGINT|exit_code=130)", re.IGNORECASE)
SHELL_PROMPT_RE = re.compile(r"^[^\s@]+@[^:]+:[^$#]*[$#]\s*(?:cd\s+.*)?$")


def is_footer_line(line: str) -> bool:
    """Return true if a line is terminal/operator metadata, not firmware UART."""
    stripped = line.rstrip("\r\n")
    return bool(
        CTRL_C_RE.search(stripped)
        or EXIT_BANNER_RE.search(stripped)
        or PROCESS_EXIT_RE.search(stripped)
        or CLOSE_TERMINAL_RE.search(stripped)
        or MONITOR_STOP_RE.search(stripped)
    )


def classify_footer_lines(lines: list[str]) -> dict[str, Any]:
    """Classify a trailing operator footer in an already split transcript.

    The returned line numbers are 1-based and inclusive.  A shell prompt line
    immediately after a detected footer is included in the footer range, but a
    prompt alone does not start a footer.
    """
    footer_indices = [idx for idx, line in enumerate(lines) if is_footer_line(line)]
    if not footer_indices:
        return {
            "operator_interrupt_seen": False,
            "operator_exit_code": None,
            "operator_exit_signal": None,
            "operator_exit_classification": None,
            "footer_start_index": None,
            "footer_end_index": None,
        }

    start = footer_indices[0]
    end = footer_indices[-1] + 1
    while end < len(lines) and SHELL_PROMPT_RE.search(lines[end].rstrip("\r\n")):
        end += 1

    footer_text = "\n".join(lines[start:end])
    exit_codes = [int(m.group("code"), 10) for m in PROCESS_EXIT_RE.finditer(footer_text)]
    exit_code = exit_codes[-1] if exit_codes else (130 if CTRL_C_RE.search(footer_text) else None)
    operator_interrupt = bool(CTRL_C_RE.search(footer_text) or exit_code == 130 or MONITOR_STOP_RE.search(footer_text))
    if exit_code == 130 or operator_interrupt:
        signal = "SIGINT"
        classification = "CONTROLLED_MONITOR_STOP"
    elif exit_code == 0:
        signal = None
        classification = "MONITOR_EXIT_OK"
    else:
        signal = None
        classification = "MONITOR_EXIT_NONZERO"

    return {
        "operator_interrupt_seen": operator_interrupt,
        "operator_exit_code": exit_code,
        "operator_exit_signal": signal,
        "operator_exit_classification": classification,
        "footer_start_index": start,
        "footer_end_index": end,
    }


def strip_footer(text: str) -> tuple[str, dict[str, Any], str]:
    """Return text without trailing footer, footer classification and footer text."""
    lines = text.splitlines()
    info = classify_footer_lines(lines)
    start = info.get("footer_start_index")
    end = info.get("footer_end_index")
    if start is None or end is None:
        return text, info, ""
    serial_lines = lines[: int(start)]
    footer_lines = lines[int(start) : int(end)]
    return "\n".join(serial_lines).rstrip() + ("\n" if serial_lines else ""), info, "\n".join(footer_lines).rstrip() + "\n"


def self_test() -> int:
    serial = "EV_WEMOS_SMOKE_TICK seq=1\nEV_WEMOS_SMOKE_SNAPSHOT seq=1\n"
    footer = "^C\n--- exit ---\n[process exited with code 130 (0x00000082)]\nYou can now close this terminal with Ctrl+D, or press Enter to restart.\n"
    stripped, info, footer_text = strip_footer(serial + footer)
    assert stripped == serial
    assert footer_text.startswith("^C")
    assert info["operator_interrupt_seen"] is True
    assert info["operator_exit_code"] == 130
    assert info["operator_exit_signal"] == "SIGINT"
    assert info["operator_exit_classification"] == "CONTROLLED_MONITOR_STOP"
    no_footer, no_info, no_footer_text = strip_footer(serial)
    assert no_footer == serial
    assert no_info["operator_interrupt_seen"] is False
    assert no_footer_text == ""
    zero = classify_footer_lines(["EV_MONITOR_STOP reason=complete exit_code=0"])
    assert zero["operator_interrupt_seen"] is False
    return 0


if __name__ == "__main__":
    raise SystemExit(self_test())
