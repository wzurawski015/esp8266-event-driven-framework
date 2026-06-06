# Release archive discipline

Release archives must be created only after `make release-prearchive-gate` has
passed on the exact `HEAD` that will be archived.  The gate checks sanitized
release evidence, private-repo secret containment, generated contract freshness,
patch hygiene and secret-safe worktree cleanliness.

Do not use a raw `git archive ... HEAD` as the normal release path.  Prefer:

```sh
./tools/fw release-archive
```

or, when running directly on the host:

```sh
make release-prearchive-gate
make release-archive
```

If historical evidence requires redaction, run `make repair-evidence-redaction`,
review the sanitized changes locally, commit them in the private repository, and
only then run the prearchive gate.  Release tooling must not print diff hunks or
line contents; it may print only path/status metadata.


## Top-level duplicate work trees

Release archives must not contain nested source snapshots such as `orig/` or
`repo/`.  Those directories can duplicate private-lab evidence and make audits
ambiguous.  `make release-archive-content-scope-gate` checks this condition
without printing nested file contents.  If the gate fails in a private working
tree, remove the duplicate trees locally with `git rm -r -- orig repo` after
reviewing that they are not the canonical project root, then rerun
`make release-prearchive-gate`.

## Host-quality gates before archiving

`release-prearchive-gate` is also a host-quality gate.  A release archive must not
be produced when route/registry integration, host runtime delivery, strict C17
or sanitizer coverage is red.  The default prearchive path therefore requires:

```text
route-registry-integration-gate
host-test
property-test
host-strict-test
host-sanitize-test
```

This is intentionally stronger than a source-packaging check: adding a route such
as `EV_BOOT_COMPLETED -> ACT_BH1750` must update the full host/demo registry and
its diagnostics before an archive can be created.  Optional hardware presence is
a device policy; it does not make a software actor binding optional when that
actor is present in the route table.

## Archive self-clean verification

`./tools/fw release-archive` verifies the tarball it just created before
reporting success.  The verifier extracts the archive into a temporary directory
and runs the same sanitized-evidence and private-repo secret-containment checks
against the archive contents.  If `serial.redacted.log`,
`serial.normalized.log` or any other public/sanitized artifact requires
redaction after extraction, the archive command fails and removes the invalid
archive.

The verification is intentionally secret-safe.  It reports status, paths,
privacy classes and counters only; it must not print diff hunks, raw log lines or
literal secret values.  `./tools/fw release-archive --dry-run` validates the
prearchive workflow and calculates the future archive name, but it does not claim
that a tarball has been self-clean verified.

For an existing archive produced in a private worktree, operators can run:

```sh
EV_RELEASE_ARCHIVE=esp8266-event-driven-framework_<timestamp>_<sha>.tar.gz \
  make release-archive-self-clean-gate
```

A failure means the archive is not a release-quality artifact.  Repair the
working tree with `make repair-evidence-redaction`, review and commit sanitized
evidence locally, then regenerate the archive with `./tools/fw release-archive`.
