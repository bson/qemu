# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

QEMU is a generic and open source machine emulator and virtualizer. It emulates CPUs via dynamic
translation (TCG) and can integrate with hardware virtualization (KVM, Xen, Hvf, WHPX, NVMM, MSHV)
for near-native performance. It also provides userspace API virtualization (`linux-user`,
`bsd-user`) to run binaries built for one CPU architecture's ABI on a host with a different
architecture. The codebase is primarily C, with a growing amount of Rust (`rust/`) and Python
(build/test tooling, `python/`).

## Build

Out-of-tree builds are required/expected:

```sh
mkdir build && cd build
../configure
make -j$(nproc)
```

Useful `configure` options: `--target-list=x86_64-softmmu,aarch64-softmmu` to limit which targets
get built (much faster than the default "all targets"), `--enable-debug` for a debug build,
`--disable-werror` while iterating. Run `../configure --help` for the full list.

The build is Meson/Ninja-based under the hood; `make` in the build dir wraps ninja. You can invoke
ninja/meson directly from the build dir too (e.g. `ninja qemu-system-x86_64`,
`./pyvenv/bin/meson test <suite>`).

Rust-specific dev tasks (from the build dir): `make clippy`, `make rustfmt`, `make rustdoc`,
`make check-rust`. See `docs/devel/rust.rst`.

## Testing

Run `make check-help` from the build dir for the full list of test targets. Build QEMU before
running tests — some tests fail with obscure errors if the binaries aren't there yet.

- `make check` — the default aggregate: QAPI schema tests, unit tests, qtests, and a subset of
  block iotests.
- `make check-unit` — plain C unit tests (glib-based) linked directly against QEMU object files.
  Add new ones under `tests/unit/`, wired into the `tests` dict in `tests/unit/meson.build`.
- `make check-qtest` — device-model tests using the qtest protocol to drive/inspect QEMU externally.
- `make check-functional` / `make check-functional-<arch>` — higher-level Python tests that boot
  real VM images (`tests/functional/`, using the `qemu_test` helper package). Run a single file/
  class/test directly instead of through meson:
  `QEMU_TEST_QEMU_BINARY=qemu-system-x86_64 ./build/run tests/functional/x86_64/test_foo.py [ClassName[.test_name]]`
  (add `-k <substr>` to filter). Set `QEMU_TEST_KEEP_SCRATCH=1` to keep scratch files for debugging.
- `make check-block` / `make check-block-<driver>` — block-layer I/O tests (a thin wrapper over
  `tests/qemu-iotests`); accepts `SPEED=slow|thorough` to widen coverage. The iotests can also be
  run directly: `cd tests/qemu-iotests && ./check -qcow2` (see `./check -h`).
  `make check-tcg` — smoke tests of the TCG target + guest code, per architecture.
- `make check-qapi-schema` — QAPI parser tests against fixtures in `tests/qapi-schema/`
  (`${name}.json`/`.out`/`.err`/`.exit`); regenerate expected output with
  `QEMU_TEST_REGENERATE=1 make check-qapi-schema`.
- `TIMEOUT_MULTIPLIER=N make check` raises all test timeouts by N (or disables them at `0`) for
  slow hosts/sanitizer builds.

Full details: `docs/devel/testing/main.rst` and the other files under `docs/devel/testing/`.

## Style and patch hygiene

- Run `scripts/checkpatch.pl <patch>` (or on a commit range) before submitting; it's not
  infallible, especially around macros, so use judgment too.
- 4-space indents, no tabs (except Makefiles); 80-column soft limit, don't force-wrap past
  readability; no trailing whitespace.
- Naming: variables and functions `lower_case_with_underscores`; struct/enum/function-pointer
  typedefs `CamelCase`; scalar typedefs `lower_case_with_underscores_ending_in_t`. Wrapped libc/
  GLib functions get a `qemu_` prefix (e.g. `qemu_strtol`); other cross-codebase utilities usually
  don't (`pstrcpy`). Subsystem-specific public functions typically share a subsystem prefix.
  Common variable names: `cs` for `CPUState *`, `env` for the concrete `CPUArchState *`, `dev` for
  `DeviceState *`.
- Full style guide: `docs/devel/style.rst`. Patch/submission process (Signed-off-by required,
  mailing-list workflow, no direct-push except via CI merge trains):
  `docs/devel/submitting-a-patch.rst`. `.b4-config` is set up for the `b4`/git-send-email
  patch-series workflow to `qemu-devel@nongnu.org`.

## Architecture

QEMU's top-level layout maps roughly to these layers; see `docs/devel/codebase.rst` for the full
per-directory tour (also linked from `MAINTAINERS`, which is the authoritative subsystem→file map
and lists reviewers/maintainers per area).

- **`target/<arch>/`** — per-guest-architecture CPU definitions: register state, instruction
  decode/translation to TCG IR, exception/interrupt handling, and (where applicable) accelerator-
  specific glue (KVM ioctls, hvf, etc). One subdirectory per emulated architecture.
- **`tcg/`** — the Tiny Code Generator: translates each target's IR into host machine code. One
  subdirectory per *host* architecture (the backend), plus the target-independent optimizer/core.
- **`accel/`** — accelerator-agnostic glue code for TCG, KVM, Hvf, WHPX, Xen, NVMM, MSHV; defines
  the interfaces that `target/` implementations plug into.
- **`hw/`** — device and machine/board models, organized by device class/bus/arch (e.g.
  `hw/net`, `hw/scsi`, `hw/arm`). This is almost all "system emulation" mode code.
- **`system/`** — core system-emulation runtime: CPU execution loop glue, MMU/memory dispatch,
  vl.c-style startup, boot support.
- **`qom/`** — the QEMU Object Model: a C-based type system with single inheritance, multiple
  interface inheritance, and property introspection, used pervasively by `hw/` device models and
  more. See `docs/devel/qom.rst`.
- **`qapi/`** — the QAPI schema and generator: JSON-ish schema files describing the QMP/QGA wire
  protocol, from which C marshaling code, docs, and introspection data are generated. See
  `docs/devel/qapi-code-gen.rst`.
- **`monitor/`** — HMP (human) and QMP (machine, QAPI-driven) monitor command implementations.
- **`block/`** — block layer: image formats (qcow2, raw, ...), protocols (nbd, iscsi, ...), and
  the generic I/O path; `blockdev.c`/`blockjob.c` at the top level wire this into device models.
- **`migration/`** — live migration / snapshot machinery (state save/restore, transports).
- **`io/`**, **`chardev/`**, **`net/`**, **`audio/`**, **`ui/`** — host-side I/O channel
  abstraction, character devices, host networking backends, host audio backends, and
  display/input frontends (SDL, GTK, VNC, etc), respectively.
- **`linux-user/`**, **`bsd-user/`**, **`common-user/`** — userspace-emulation mode: guest
  syscall translation and ABI shims, one arch subdirectory each under `linux-user/`.
- **`include/`** — all public headers, mirroring the source layout (e.g. `include/hw/...`
  matches `hw/...`).
- **`rust/`** — Rust integration; currently focused on writing `SysBusDevice`-derived devices in
  safe Rust, built and linked in automatically via Meson/`rustc` (Cargo is only for ancillary dev
  tasks like clippy/rustfmt, not for building QEMU itself). See `docs/devel/rust.rst`.
- **`tests/`** — see Testing above; subfolders per test category (`unit`, `qtest`, `functional`,
  `qemu-iotests`, `tcg`, `qapi-schema`, `fp`, `decode`, `docker` (CI container defs), etc).
- **`python/`**, **`scripts/`** — Python build/test tooling (incl. `QEMUMachine` used by
  functional tests) and misc developer/maintainer scripts (`checkpatch.pl`,
  `get_maintainer.pl`, tracing tools, etc).
- **`contrib/`** — community-contributed devices, plugins, and tools not part of the core build.
- **`docs/`** — Sphinx documentation source; `docs/devel/` is specifically the developer's guide
  referenced throughout this file.
