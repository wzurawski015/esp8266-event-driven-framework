#!/usr/bin/env python3
"""Verify that a release archive is self-clean for sanitized evidence.

The verifier extracts a tar.gz archive into a temporary directory and runs the
same redaction/privacy checks that guard the working tree.  It reports only
status, paths emitted by existing secret-safe tools and counters; it never prints
archive contents, diff hunks or secret values.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


def _safe_extract(tar: tarfile.TarFile, destination: Path) -> None:
    dest = destination.resolve()
    members = tar.getmembers()
    for member in members:
        target = (dest / member.name).resolve()
        if target != dest and dest not in target.parents:
            raise ValueError(f"unsafe archive member path: {member.name!r}")
        if member.issym() or member.islnk() or member.isdev():
            raise ValueError(f"unsupported archive member type: {member.name!r}")
    for member in members:
        target = (dest / member.name).resolve()
        if member.isdir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        if not member.isfile():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        source = tar.extractfile(member)
        if source is None:
            continue
        with source, target.open("wb") as out:
            while True:
                chunk = source.read(65536)
                if not chunk:
                    break
                out.write(chunk)


def _find_archive_root(extract_dir: Path) -> Path:
    if (extract_dir / "Makefile").is_file() and (extract_dir / "tools").is_dir():
        return extract_dir
    candidates = [path for path in extract_dir.iterdir() if path.is_dir() and (path / "Makefile").is_file()]
    if len(candidates) == 1:
        return candidates[0]
    raise RuntimeError("archive root not found or ambiguous")


def _run_check(args: list[str], *, cwd: Path, repo_mode: str) -> tuple[int, str]:
    env = {**os.environ, "EV_REPO_MODE": repo_mode}
    completed = subprocess.run(
        args,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
        env=env,
    )
    return completed.returncode, completed.stdout


def _emit_prefixed(prefix: str, output: str) -> None:
    for raw in output.splitlines():
        line = raw.strip()
        if line:
            print(f"{prefix} {line}")


def verify_archive(path: Path, *, repo_mode: str = "PRIVATE_REPO") -> int:
    archive = path.resolve()
    if not archive.is_file():
        print(f"EV_RELEASE_ARCHIVE_SELF_CLEAN FAIL archive_missing path={archive.name}")
        return 2

    with tempfile.TemporaryDirectory(prefix="ev_archive_self_clean_") as tmp:
        extract_dir = Path(tmp) / "extract"
        extract_dir.mkdir()
        try:
            with tarfile.open(archive, "r:gz") as tar:
                _safe_extract(tar, extract_dir)
            archive_root = _find_archive_root(extract_dir)
        except Exception as exc:  # noqa: BLE001 - report secret-safe failure kind only.
            print(f"EV_RELEASE_ARCHIVE_SELF_CLEAN FAIL archive={archive.name} extract_error={type(exc).__name__}")
            return 1

        redaction_rc, redaction_out = _run_check(
            [
                sys.executable,
                "tools/release/redact_existing_evidence.py",
                "--root",
                str(archive_root / "docs" / "release"),
                "--mode",
                repo_mode,
            ],
            cwd=archive_root,
            repo_mode=repo_mode,
        )
        policy_rc, policy_out = _run_check(
            [
                sys.executable,
                "tools/audit/private_repo_secrets_policy.py",
                "--root",
                str(archive_root),
                "--mode",
                repo_mode,
            ],
            cwd=archive_root,
            repo_mode=repo_mode,
        )

        if (redaction_rc != 0) or (policy_rc != 0):
            print(
                "EV_RELEASE_ARCHIVE_SELF_CLEAN FAIL "
                f"archive={archive.name} redaction_rc={redaction_rc} policy_rc={policy_rc}"
            )
            _emit_prefixed("ARCHIVE_REDACTION", redaction_out)
            _emit_prefixed("ARCHIVE_SECRET_POLICY", policy_out)
            return 1

        print(f"EV_RELEASE_ARCHIVE_SELF_CLEAN PASS archive={archive.name} mode={repo_mode}")
        return 0


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="ev_archive_self_clean_selftest_") as tmp:
        root = Path(tmp)
        good = root / "good"
        good.mkdir()
        (good / "Makefile").write_text("all:\n\t@true\n", encoding="utf-8")
        (good / "tools" / "release").mkdir(parents=True)
        (good / "tools" / "audit").mkdir(parents=True)
        (good / "docs" / "release").mkdir(parents=True)
        (good / "tools" / "release" / "redact_existing_evidence.py").write_text(
            "#!/usr/bin/env python3\nprint('EV_EXISTING_EVIDENCE_REDACTION DRY_RUN files_changed=0 hash_repairs=0')\n",
            encoding="utf-8",
        )
        (good / "tools" / "audit" / "private_repo_secrets_policy.py").write_text(
            "#!/usr/bin/env python3\nprint('private repo secrets policy passed mode=PRIVATE_REPO')\n",
            encoding="utf-8",
        )
        archive = root / "good.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(good, arcname=".")
        assert verify_archive(archive) == 0

        bad = root / "bad"
        bad.mkdir()
        (bad / "Makefile").write_text("all:\n\t@true\n", encoding="utf-8")
        (bad / "tools" / "release").mkdir(parents=True)
        (bad / "tools" / "audit").mkdir(parents=True)
        (bad / "docs" / "release").mkdir(parents=True)
        (bad / "tools" / "release" / "redact_existing_evidence.py").write_text(
            "#!/usr/bin/env python3\nprint('EV_EXISTING_EVIDENCE_REDACTION DRY_RUN files_changed=1 hash_repairs=0')\nraise SystemExit(1)\n",
            encoding="utf-8",
        )
        (bad / "tools" / "audit" / "private_repo_secrets_policy.py").write_text(
            "#!/usr/bin/env python3\nprint('PRIVATE_SECRET_REFERENCE docs/release/example.redacted.log:1 class=PUBLIC_SANITIZED EV_BOARD_NET_WIFI_SSID=<REDACTED>')\nraise SystemExit(1)\n",
            encoding="utf-8",
        )
        bad_archive = root / "bad.tar.gz"
        with tarfile.open(bad_archive, "w:gz") as tar:
            tar.add(bad, arcname=".")
        assert verify_archive(bad_archive) != 0

        unsafe = root / "unsafe.tar.gz"
        payload = root / "payload.txt"
        payload.write_text("x", encoding="utf-8")
        with tarfile.open(unsafe, "w:gz") as tar:
            tar.add(payload, arcname="../escape")
        assert verify_archive(unsafe) != 0
    print("EV_RELEASE_ARCHIVE_SELF_CLEAN_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify release archive evidence cleanliness without printing secrets.")
    parser.add_argument("archive", nargs="?", type=Path)
    parser.add_argument("--mode", choices=["PRIVATE_REPO", "PUBLIC_RELEASE"], default=os.environ.get("EV_REPO_MODE", "PRIVATE_REPO"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.archive is None:
        print("EV_RELEASE_ARCHIVE_SELF_CLEAN ENVIRONMENT_BLOCKED: archive path not provided")
        return 77
    return verify_archive(args.archive, repo_mode=args.mode)


if __name__ == "__main__":
    raise SystemExit(main())
