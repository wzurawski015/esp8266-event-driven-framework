#!/usr/bin/env python3
"""Generate exact runtime mailbox storage layout from config/actors.def."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
ACTORS_DEF = ROOT / "config" / "actors.def"
OUT_PATH = ROOT / "core" / "generated" / "include" / "ev" / "actor_mailbox_layout_generated.h"

ACTOR_FIELDS = 5
MAILBOX_CAPACITY: dict[str, int] = {
    "EV_MAILBOX_FIFO_8": 8,
    "EV_MAILBOX_FIFO_16": 16,
    "EV_MAILBOX_MAILBOX_1": 1,
    "EV_MAILBOX_LOSSY_RING_8": 8,
    "EV_MAILBOX_COALESCED_FLAG": 1,
}


@dataclass(frozen=True)
class ActorMailboxLayout:
    actor_id: str
    mailbox_kind: str
    offset: int
    capacity: int
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

    if state in {"string", "char", "block_comment"}:
        raise ValueError(f"unterminated C {state} in input")
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


def parse_actor_layouts(path: Path) -> list[ActorMailboxLayout]:
    text = strip_c_comments(path.read_text(encoding="utf-8"))
    layouts: list[ActorMailboxLayout] = []
    seen: set[str] = set()
    offset = 0

    for line, body in find_macro_invocations(text, "EV_ACTOR"):
        args = split_macro_args(body)
        if len(args) != ACTOR_FIELDS:
            raise ValueError(f"mailbox-layoutgen: {path.name}:{line}: EV_ACTOR expects {ACTOR_FIELDS} fields, got {len(args)}")
        actor_id = args[0]
        mailbox_kind = args[2]
        if actor_id in seen:
            raise ValueError(f"mailbox-layoutgen: {path.name}:{line}: duplicate actor {actor_id}")
        seen.add(actor_id)
        try:
            capacity = MAILBOX_CAPACITY[mailbox_kind]
        except KeyError as exc:
            raise ValueError(f"mailbox-layoutgen: {path.name}:{line}: unknown mailbox kind {mailbox_kind}") from exc
        layouts.append(ActorMailboxLayout(actor_id=actor_id,
                                          mailbox_kind=mailbox_kind,
                                          offset=offset,
                                          capacity=capacity,
                                          line=line))
        offset += capacity

    if not layouts:
        raise ValueError(f"mailbox-layoutgen: no EV_ACTOR entries found in {path}")
    return layouts


def render_header(layouts: list[ActorMailboxLayout]) -> str:
    total_capacity = sum(item.capacity for item in layouts)
    out: list[str] = []
    out.append("/* Auto-generated by tools/routegen/mailbox_layoutgen.py.\n")
    out.append(" * Source of truth: config/actors.def.\n")
    out.append(" * Do not edit manually; run make routegen.\n")
    out.append(" */\n")
    out.append("#ifndef EV_ACTOR_MAILBOX_LAYOUT_GENERATED_H\n")
    out.append("#define EV_ACTOR_MAILBOX_LAYOUT_GENERATED_H\n\n")
    out.append("#include <stddef.h>\n\n")
    out.append('#include "ev/actor_id.h"\n')
    out.append('#include "ev/mailbox_kind.h"\n\n')
    out.append(f"#define EV_ACTOR_MAILBOX_LAYOUT_GENERATED_COUNT {len(layouts)}U\n")
    out.append(f"#define EV_RUNTIME_MAILBOX_TOTAL_CAPACITY {total_capacity}U\n\n")
    out.append("typedef struct {\n")
    out.append("    ev_actor_id_t actor_id;\n")
    out.append("    ev_mailbox_kind_t mailbox_kind;\n")
    out.append("    size_t offset;\n")
    out.append("    size_t capacity;\n")
    out.append("} ev_actor_mailbox_layout_entry_t;\n\n")
    out.append("static inline int ev_actor_mailbox_layout_lookup(ev_actor_id_t actor_id, ev_actor_mailbox_layout_entry_t *out_layout)\n")
    out.append("{\n")
    out.append("    if (out_layout == NULL) {\n")
    out.append("        return 0;\n")
    out.append("    }\n\n")
    out.append("    switch (actor_id) {\n")
    for item in layouts:
        out.append(f"    case {item.actor_id}:\n")
        out.append(f"        *out_layout = (ev_actor_mailbox_layout_entry_t){{ {item.actor_id}, {item.mailbox_kind}, {item.offset}U, {item.capacity}U }};\n")
        out.append("        return 1;\n")
    out.append("    default:\n")
    out.append("        return 0;\n")
    out.append("    }\n")
    out.append("}\n\n")
    out.append("#endif /* EV_ACTOR_MAILBOX_LAYOUT_GENERATED_H */\n")
    return "".join(out)


def self_test_parser() -> None:
    sample = r'''
        // EV_ACTOR(ACT_IGNORED, X, EV_MAILBOX_FIFO_8, 1U, "bad")
        EV_ACTOR(ACT_SAMPLE, EV_DOMAIN_FAST_LOOP, EV_MAILBOX_FIFO_8, 4U,
                 "summary, with comma")
        /* block comment
           EV_ACTOR(ACT_IGNORED2, X, EV_MAILBOX_FIFO_16, 1U, "bad")
        */
        EV_ACTOR(ACT_STREAM, EV_DOMAIN_FAST_LOOP, EV_MAILBOX_FIFO_16, 8U,
                 "quoted paren ) and comma, still string")
    '''
    stripped = strip_c_comments(sample)
    invocations = list(find_macro_invocations(stripped, "EV_ACTOR"))
    assert len(invocations) == 2
    first = split_macro_args(invocations[0][1])
    second = split_macro_args(invocations[1][1])
    assert len(first) == ACTOR_FIELDS
    assert first[4] == '"summary, with comma"'
    assert len(second) == ACTOR_FIELDS
    assert second[2] == "EV_MAILBOX_FIFO_16"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when the generated header is stale")
    args = parser.parse_args(argv)

    try:
        self_test_parser()
        layouts = parse_actor_layouts(ACTORS_DEF)
        rendered = render_header(layouts)
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1

    if args.check:
        if not OUT_PATH.exists():
            print(f"mailbox-layoutgen stale: missing {OUT_PATH.relative_to(ROOT)}", file=sys.stderr)
            return 1
        current = OUT_PATH.read_text(encoding="utf-8")
        if current != rendered:
            print(f"mailbox-layoutgen stale: {OUT_PATH.relative_to(ROOT)} is not up to date", file=sys.stderr)
            return 1
        print(f"mailbox-layoutgen-check passed: {len(layouts)} actors, {sum(item.capacity for item in layouts)} slots")
        return 0

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(rendered, encoding="utf-8")
    print(f"generated {len(layouts)} actor mailbox layouts into {OUT_PATH.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
