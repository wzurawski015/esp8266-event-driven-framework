#!/usr/bin/env python3
from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

RE_EVENT = re.compile(
    r'^\s*EV_EVENT\(\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*,\s*"([^"]*)"\s*\)\s*$'
)
RE_ACTOR = re.compile(
    r'^\s*EV_ACTOR\(\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*,\s*([0-9]+U?)\s*,\s*"([^"]*)"\s*\)\s*$'
)
RE_ROUTE = re.compile(
    r'^\s*EV_ROUTE\(\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*\)\s*$'
)

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config"
OUT = ROOT / "docs" / "generated"


def read_nonempty_lines(path: Path) -> list[str]:
    lines: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        lines.append(line)
    return lines


def parse_events(path: Path) -> list[dict[str, str]]:
    out = []
    for line in read_nonempty_lines(path):
        match = RE_EVENT.match(line)
        if not match:
            raise ValueError(f"Invalid event definition: {line}")
        name, payload_kind, summary = match.groups()
        out.append(
            {
                "name": name,
                "payload_kind": payload_kind,
                "summary": summary,
            }
        )
    return out


def parse_actors(path: Path) -> list[dict[str, str]]:
    out = []
    for line in read_nonempty_lines(path):
        match = RE_ACTOR.match(line)
        if not match:
            raise ValueError(f"Invalid actor definition: {line}")
        name, domain, mailbox, drain_budget, summary = match.groups()
        out.append(
            {
                "name": name,
                "domain": domain,
                "mailbox": mailbox,
                "drain_budget": drain_budget,
                "summary": summary,
            }
        )
    return out


def parse_routes(path: Path) -> list[dict[str, str]]:
    out = []
    for line in read_nonempty_lines(path):
        match = RE_ROUTE.match(line)
        if not match:
            raise ValueError(f"Invalid route definition: {line}")
        event_name, actor_name = match.groups()
        out.append(
            {
                "event_name": event_name,
                "actor_name": actor_name,
            }
        )
    return out


def write_text(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def generate_events_md(events: list[dict[str, str]]) -> None:
    lines = [
        "# Event Catalog",
        "",
        "| Event | Payload kind | Summary |",
        "|---|---|---|",
    ]
    for event in events:
        lines.append(
            f"| `{event['name']}` | `{event['payload_kind']}` | {event['summary']} |"
        )
    lines.append("")
    write_text(OUT / "events.md", "\n".join(lines))


def generate_actors_md(actors: list[dict[str, str]]) -> None:
    lines = [
        "# Actor Catalog",
        "",
        "| Actor | Execution domain | Mailbox | Drain budget | Summary |",
        "|---|---|---|---:|---|",
    ]
    for actor in actors:
        lines.append(
            f"| `{actor['name']}` | `{actor['domain']}` | `{actor['mailbox']}` | `{actor['drain_budget']}` | {actor['summary']} |"
        )
    lines.append("")
    write_text(OUT / "actors.md", "\n".join(lines))


def generate_routes_md(routes: list[dict[str, str]]) -> None:
    lines = [
        "# Route Catalog",
        "",
        "| Event | Target actor |",
        "|---|---|",
    ]
    for route in routes:
        lines.append(f"| `{route['event_name']}` | `{route['actor_name']}` |")
    lines.append("")
    write_text(OUT / "routes.md", "\n".join(lines))


def generate_routes_dot(routes: list[dict[str, str]]) -> Path:
    lines = [
        "digraph routes {",
        "    rankdir=LR;",
        '    graph [label="Event routing", labelloc=t, fontsize=20];',
        '    node [shape=box, style="rounded"];',
        "",
    ]
    for route in routes:
        lines.append(f'    "{route["event_name"]}" -> "{route["actor_name"]}";')
    lines.append("}")
    dot_path = OUT / "routes.dot"
    write_text(dot_path, "\n".join(lines) + "\n")
    return dot_path


def maybe_render_svg(dot_path: Path) -> None:
    dot_bin = shutil.which("dot")
    if dot_bin is None:
        return
    svg_path = dot_path.with_suffix(".svg")
    subprocess.run(
        [dot_bin, "-Tsvg", str(dot_path), "-o", str(svg_path)],
        check=True,
    )


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    events = parse_events(CONFIG / "events.def")
    actors = parse_actors(CONFIG / "actors.def")
    routes = parse_routes(CONFIG / "routes.def")

    generate_events_md(events)
    generate_actors_md(actors)
    generate_routes_md(routes)
    dot_path = generate_routes_dot(routes)
    maybe_render_svg(dot_path)

    index = "\n".join(
        [
            "# Generated Documentation",
            "",
            "- [Event Catalog](events.md)",
            "- [Actor Catalog](actors.md)",
            "- [Route Catalog](routes.md)",
            "- [Route Graph](routes.dot)",
            "",
        ]
    )
    write_text(OUT / "index.md", index)


if __name__ == "__main__":
    main()
