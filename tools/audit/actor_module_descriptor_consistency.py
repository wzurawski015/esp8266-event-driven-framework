#!/usr/bin/env python3
"""Validate actor catalog and actor module descriptor consistency.

The actor catalog in config/actors.def is the SSOT for actor identity,
execution domain and mailbox kind.  config/modules.def must not silently drift
from that contract, because runtime builders consume module descriptors at a
lower layer.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import ast
import re
import sys
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]

ACTOR_FIELDS = 5
MODULE_FIELDS = 11
MAILBOX_CAPACITY = {
    "EV_MAILBOX_FIFO_8": 8,
    "EV_MAILBOX_FIFO_16": 16,
    "EV_MAILBOX_MAILBOX_1": 1,
    "EV_MAILBOX_LOSSY_RING_8": 8,
    "EV_MAILBOX_COALESCED_FLAG": 1,
}


@dataclass(frozen=True)
class ActorDef:
    actor_id: str
    domain: str
    mailbox_kind: str
    line: int


@dataclass(frozen=True)
class ModuleDef:
    actor_id: str
    module_name: str
    domain: str
    mailbox_capacity: str
    handler_fn: str
    line: int


def strip_c_comments(text: str) -> str:
    """Remove C comments while preserving line numbering and string literals."""
    out: list[str] = []
    i = 0
    state = "normal"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if state == "normal":
            if ch == '"':
                out.append(ch)
                state = "string"
                i += 1
            elif ch == "'":
                out.append(ch)
                state = "char"
                i += 1
            elif ch == "/" and nxt == "/":
                state = "line_comment"
                i += 2
            elif ch == "/" and nxt == "*":
                state = "block_comment"
                i += 2
            else:
                out.append(ch)
                i += 1
        elif state == "string":
            out.append(ch)
            if ch == "\\" and nxt:
                out.append(nxt)
                i += 2
            elif ch == '"':
                state = "normal"
                i += 1
            else:
                i += 1
        elif state == "char":
            out.append(ch)
            if ch == "\\" and nxt:
                out.append(nxt)
                i += 2
            elif ch == "'":
                state = "normal"
                i += 1
            else:
                i += 1
        elif state == "line_comment":
            if ch == "\n":
                out.append(ch)
                state = "normal"
            i += 1
        elif state == "block_comment":
            if ch == "\n":
                out.append(ch)
                i += 1
            elif ch == "*" and nxt == "/":
                state = "normal"
                i += 2
            else:
                i += 1
    return "".join(out)


def find_macro_invocations(text: str, macro_name: str) -> Iterable[tuple[int, str]]:
    """Yield (line, argument_text) for each balanced macro invocation."""
    token = macro_name
    pos = 0
    while True:
        idx = text.find(token, pos)
        if idx < 0:
            return
        before = text[idx - 1] if idx > 0 else ""
        after_idx = idx + len(token)
        after = text[after_idx] if after_idx < len(text) else ""
        if (before == "_" or before.isalnum()) or (after == "_" or after.isalnum()):
            pos = after_idx
            continue

        open_idx = after_idx
        while open_idx < len(text) and text[open_idx].isspace():
            open_idx += 1
        if open_idx >= len(text) or text[open_idx] != "(":
            pos = after_idx
            continue

        line = text.count("\n", 0, idx) + 1
        depth = 0
        state = "normal"
        i = open_idx
        while i < len(text):
            ch = text[i]
            nxt = text[i + 1] if i + 1 < len(text) else ""
            if state == "normal":
                if ch == '"':
                    state = "string"
                elif ch == "'":
                    state = "char"
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        yield line, text[open_idx + 1 : i]
                        pos = i + 1
                        break
                i += 1
            elif state == "string":
                if ch == "\\" and nxt:
                    i += 2
                elif ch == '"':
                    state = "normal"
                    i += 1
                else:
                    i += 1
            elif state == "char":
                if ch == "\\" and nxt:
                    i += 2
                elif ch == "'":
                    state = "normal"
                    i += 1
                else:
                    i += 1
        else:
            raise ValueError(f"unterminated {macro_name} invocation near line {line}")


def split_macro_args(argument_text: str) -> list[str]:
    """Split macro arguments on top-level commas only."""
    args: list[str] = []
    start = 0
    depth = 0
    state = "normal"
    i = 0
    while i < len(argument_text):
        ch = argument_text[i]
        nxt = argument_text[i + 1] if i + 1 < len(argument_text) else ""
        if state == "normal":
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            elif ch in "([{":
                depth += 1
            elif ch in ")]}":
                if depth > 0:
                    depth -= 1
            elif ch == "," and depth == 0:
                args.append(argument_text[start:i].strip())
                start = i + 1
            i += 1
        elif state == "string":
            if ch == "\\" and nxt:
                i += 2
            elif ch == '"':
                state = "normal"
                i += 1
            else:
                i += 1
        elif state == "char":
            if ch == "\\" and nxt:
                i += 2
            elif ch == "'":
                state = "normal"
                i += 1
            else:
                i += 1
    args.append(argument_text[start:].strip())
    return args


def parse_c_string(token: str) -> str | None:
    token = token.strip()
    if not (token.startswith('"') and token.endswith('"')):
        return None
    try:
        value = ast.literal_eval(token)
    except (SyntaxError, ValueError):
        return None
    return value if isinstance(value, str) else None


def parse_uint(token: str) -> int | None:
    match = re.fullmatch(r"([0-9]+)[uUlL]*", token.strip())
    return int(match.group(1)) if match else None


def parse_actors(path: Path) -> tuple[dict[str, ActorDef], list[str]]:
    text = strip_c_comments(path.read_text(encoding="utf-8"))
    actors: dict[str, ActorDef] = {}
    errors: list[str] = []
    for line, body in find_macro_invocations(text, "EV_ACTOR"):
        args = split_macro_args(body)
        if len(args) != ACTOR_FIELDS:
            errors.append(f"actor-module-consistency: {path.name}:{line}: EV_ACTOR expects {ACTOR_FIELDS} fields, got {len(args)}")
            continue
        actor_id = args[0]
        if actor_id in actors:
            errors.append(f"actor-module-consistency: duplicate actor {actor_id} in {path.name}:{line}")
            continue
        actors[actor_id] = ActorDef(actor_id=actor_id, domain=args[1], mailbox_kind=args[2], line=line)
    return actors, errors


def parse_modules(path: Path) -> tuple[dict[str, ModuleDef], list[str]]:
    text = strip_c_comments(path.read_text(encoding="utf-8"))
    modules: dict[str, ModuleDef] = {}
    errors: list[str] = []
    for line, body in find_macro_invocations(text, "EV_ACTOR_MODULE"):
        args = split_macro_args(body)
        if len(args) != MODULE_FIELDS:
            errors.append(f"actor-module-consistency: {path.name}:{line}: EV_ACTOR_MODULE expects {MODULE_FIELDS} fields, got {len(args)}")
            continue
        actor_id = args[0]
        if actor_id in modules:
            errors.append(f"actor-module-consistency: duplicate module for {actor_id} in {path.name}:{line}")
            continue
        modules[actor_id] = ModuleDef(
            actor_id=actor_id,
            module_name=args[1],
            domain=args[6],
            mailbox_capacity=args[7],
            handler_fn=args[10],
            line=line,
        )
    return modules, errors


def self_test_parser() -> None:
    sample = r'''
        // line comment EV_ACTOR(ACT_IGNORED, X, Y, 1U, "bad")
        EV_ACTOR(ACT_SAMPLE, EV_DOMAIN_SLOW_IO, EV_MAILBOX_FIFO_8, 2U,
                 "summary, with comma")
        /* block comment
           EV_ACTOR(ACT_IGNORED2, X, Y, 1U, "bad")
        */
        EV_ACTOR_MODULE(ACT_SAMPLE, "sample,module", EV_CAP_I2C0 | (EV_CAP_OLED),
                        0U, EV_CAP_OLED, 0U, EV_DOMAIN_SLOW_IO, 8U,
                        EV_FAULT_NONE, EV_ROUTE_QOS_COMMAND | EV_ROUTE_QOS_TELEMETRY,
                        ev_framework_actor_handle)
    '''
    stripped = strip_c_comments(sample)
    actor_items = list(find_macro_invocations(stripped, "EV_ACTOR"))
    module_items = list(find_macro_invocations(stripped, "EV_ACTOR_MODULE"))
    assert len(actor_items) == 1
    assert len(module_items) == 1
    actor_args = split_macro_args(actor_items[0][1])
    module_args = split_macro_args(module_items[0][1])
    assert len(actor_args) == ACTOR_FIELDS
    assert actor_args[4] == '"summary, with comma"'
    assert len(module_args) == MODULE_FIELDS
    assert module_args[2] == "EV_CAP_I2C0 | (EV_CAP_OLED)"
    assert parse_c_string(module_args[1]) == "sample,module"


def validate(actors: dict[str, ActorDef], modules: dict[str, ModuleDef]) -> list[str]:
    errors: list[str] = []

    for actor_id in sorted(actors):
        actor = actors[actor_id]
        module = modules.get(actor_id)
        if module is None:
            errors.append(f"actor-module-consistency: {actor_id} missing from modules.def")
            continue

        if actor.domain != module.domain:
            errors.append(
                f"actor-module-consistency: {actor_id} domain mismatch: "
                f"actors.def={actor.domain} modules.def={module.domain}"
            )

        expected_capacity = MAILBOX_CAPACITY.get(actor.mailbox_kind)
        actual_capacity = parse_uint(module.mailbox_capacity)
        if expected_capacity is None:
            errors.append(f"actor-module-consistency: {actor_id} unknown mailbox kind in actors.def: {actor.mailbox_kind}")
        elif actual_capacity != expected_capacity:
            errors.append(
                f"actor-module-consistency: {actor_id} mailbox capacity mismatch: "
                f"actors.def={actor.mailbox_kind}->{expected_capacity} modules.def={module.mailbox_capacity}"
            )

        module_name = parse_c_string(module.module_name)
        if module_name is None or module_name == "":
            errors.append(f"actor-module-consistency: {actor_id} has empty or invalid module_name in modules.def:{module.line}")

        handler = module.handler_fn.strip()
        if handler == "" or handler in {"0", "0U", "NULL"}:
            errors.append(f"actor-module-consistency: {actor_id} has empty handler_fn in modules.def:{module.line}")

    for actor_id in sorted(modules):
        if actor_id not in actors:
            errors.append(f"actor-module-consistency: modules.def references unknown actor {actor_id}")

    return errors


def main() -> int:
    self_test_parser()
    actors, actor_errors = parse_actors(ROOT / "config" / "actors.def")
    modules, module_errors = parse_modules(ROOT / "config" / "modules.def")
    errors = actor_errors + module_errors + validate(actors, modules)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("actor-module descriptor consistency passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
