# Applyable patch — PES 2014 runtime fixes

`pes2014-runtime.patch` is a real `git diff` against upstream **PS2Recomp**.

| Item | Value |
|---|---|
| Base commit | `e563698` — *GIF: treat FLG=3 like IMAGE everywhere a GIFtag is parsed (#384)* |
| Files changed | 31 (1 new: `ps2xRuntime/include/runtime/guest_overlay.h`) |
| Insertions / deletions | +1676 / −174 |
| Patch size | ~146 KB |
| Verified | `git apply --check` on a clean clone of `e563698` → **OK** |

## Apply

```bash
git clone https://github.com/ran-j/PS2Recomp.git
cd PS2Recomp
git checkout e563698
git apply /path/to/pes2014-runtime.patch
```

If your checkout is newer, try:

```bash
git apply -3 /path/to/pes2014-runtime.patch   # 3-way merge
```

If a hunk fails, `git apply --reject` writes `.rej` files you can merge by hand. The two most
important hunks are documented with full code in `../docs/TECHNICAL-NOTES.md` §2 (VIF stream)
and §3 (GS CSR `FINISH`).

## What the patch contains

| Area | Files | Summary |
|---|---|---|
| VIF1 | `ps2_vif1_interpreter.cpp`, `ps2_memory.h` | stream buffer, rewind on incomplete UNPACK/DIRECT/MPG/STMASK, MPG wrap, traces |
| GS | `ps2_memory.cpp`, `gs_frontend.cpp`, `gs_cpu_backend.cpp`, `ps2_gif_arbiter.cpp` | CSR `FINISH`, RGBAQ/XYZF2 decode, draw counters, FIFO arbiter |
| VU1 | `ps2_vu1_core.cpp`, `ps2_vu1_lower.cpp`, `ps2_vu1.h` | JR/JALR current VI, XGKICK cancel/cap, MSCNT, rounding, SQ trace |
| Kernel | `EeScheduler.cpp`, `ee_scheduler.h` | semaphore trace, VSync/scheduler fixes |
| IOP | `ps2xIOP/...` | CDVD/RPC/emulator tweaks needed by this game |
| Overlays | `guest_overlay.h` (new), `ps2_runtime.cpp` | overlay resolver hook + runtime diagnostics |
| Build | `ps2xRuntime/CMakeLists.txt` | runner wiring for the generated sources |
| Tools/UI | `main.cpp`, `ps2_vfs.cpp`, `ps2_iop_host.cpp`, `SIF.cpp` | traces and small fixes |
| Tests | `ps2xTest/src/*` | adjusted expectations |

> The patch has **no dependency on generated code**. The `pes_overlay_*.cpp` files and
> `pes_overlay_dispatch.cpp` are produced by `tools/stage-overlays.py` and are not part of it.

## Apply in PowerShell

```powershell
git clone https://github.com/ran-j/PS2Recomp.git
git -C .\PS2Recomp checkout e563698
git -C .\PS2Recomp apply --check .\patches\pes2014-runtime.patch
git -C .\PS2Recomp apply .\patches\pes2014-runtime.patch
```

## After applying

Continue with `../SETUP.md` §3 (extract your own game), §5–§7 (recompile overlays, stage,
build).

## Note on line endings

The working tree used CRLF on Windows. If `git apply` complains about whitespace, run:

```bash
git config core.autocrlf false
git apply --whitespace=nowarn pes2014-runtime.patch
```
