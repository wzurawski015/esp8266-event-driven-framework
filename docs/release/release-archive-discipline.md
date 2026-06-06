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
