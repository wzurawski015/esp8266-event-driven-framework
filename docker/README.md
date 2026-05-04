# Corrected Dockerfiles for esp8266-event-driven-framework

These files harden the CI Docker build path against two observed failure classes:

1. transient Ubuntu APT mirror/network failures during image build;
2. `./tools/fw release-gate` running `memory-budget` in the docs image without a C compiler.

## Files

- `Dockerfile.host` — host C11 validation image with compiler, make, Python and valgrind.
- `Dockerfile.docs` — documentation image with Doxygen/Graphviz and `build-essential` so the current `release-gate` contract can run `memory-budget` if it is invoked inside the docs image.
- `Dockerfile.sdk` — pinned ESP8266 RTOS SDK v3.4 image with retry-hardened APT, toolchain download, SDK clone/submodules and pip bootstrap.

## Recommended placement

Copy them into the project as:

```text
docker/Dockerfile.host
docker/Dockerfile.docs
docker/Dockerfile.sdk
```

Then validate:

```bash
./tools/fw host-image
./tools/fw docs-image
./tools/fw sdk-check
./tools/fw release-gate
```

A stronger long-term architecture is to update `tools/fw release-gate` so that `make quality-gate` runs in the host image and only `make docgen docs` runs in the docs image. These Dockerfiles are still compatible with the current release-gate behavior.
