# SETUP — reproducing the PES 2014 recompilation

Order matters. Everything below is done **on your machine, with your own legally owned
game**.

---

## 0. Prerequisites

| Tool | Version | Why |
|---|---|---|
| Windows | 10/11 x64 | target platform |
| Visual Studio | 2022 (MSVC x64 + Windows SDK) | builds the runtime |
| CMake | ≥ 3.25 | build system |
| Python | 3.11+ | overlay pipeline scripts |
| Git | any | upstream checkout |
| Ghidra | 11.x / 12.x | function analysis (optional if you already have `functions.csv`) |
| 7-Zip or a PS2 ISO tool | any | extract your own ISO |
| PCSX2 | 2.x (optional) | capture reference GS dumps |

> Build and test were done with MSVC and the "Visual Studio 17 2022" CMake generator.

### Run the automated check first

```powershell
powershell -ExecutionPolicy Bypass -File .\CHECK.ps1
```

It verifies everything in the table above, your game layout, the repository state and the free
disk space, and prints the next step. Exit code `0` means you are ready.

If your folders are not next to the share pack:

```powershell
.\CHECK.ps1 -WorkDir D:\pes2014 -RepoDir D:\PS2Recomp
```

---

## 1. Get the upstream recompiler

```powershell
git clone https://github.com/ran-j/PS2Recomp.git
cd PS2Recomp
```

Check out the revision that matches the files in `source/` (ask the pack author for the exact
commit if the build fails; the runtime changes are small and can be re-applied by hand using
`docs/patches/README.md`).

## 2. Apply the runtime fixes

Prefer the patch:

```powershell
git checkout e563698
git apply --check ..\share\patches\pes2014-runtime.patch
git apply ..\share\patches\pes2014-runtime.patch
```

Alternative (if the patch does not apply): copy the files from `source/` over the checkout,
keeping the tree:

```powershell
robocopy "<share>\source" "<repo>\PS2Recomp" /E
```

That covers:

```
ps2xRuntime/include/runtime/ps2_memory.h
ps2xRuntime/include/runtime/gs/gs_frontend.h
ps2xRuntime/src/lib/ps2_memory.cpp
ps2xRuntime/src/lib/ps2_runtime.cpp
ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp
ps2xRuntime/src/lib/ps2_vfs.cpp
ps2xRuntime/src/lib/ps2_iop_host.cpp
ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp
ps2xRuntime/src/lib/vu/ps2_vu1_lower.cpp
ps2xRuntime/src/lib/gs/ps2_gif_arbiter.cpp
ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
```

> `ps2_vfs.cpp`, `ps2_iop_host.cpp`, `EeScheduler.cpp` and parts of `ps2_runtime.cpp` only add
> **opt-in diagnostics** (`PES2014_FILE_TRACE`, `PES2014_SEMA_TRACE`, RGB min/max in
> `[PES GS draw]`). You can drop them if you do not need the traces.

## 3. Extract your own game

You need this layout:

```
<work>\pes2014\
├── SLES_556.76      ← ELF used by the recompiler (copy it out of disc/)
├── disc\
│   ├── SLES_556.76
│   ├── SYSTEM.CNF
│   ├── IOP\             (IOP modules, IOPRP300.IMG, RECOMP\, ...)
│   ├── 0_SOUND.AFS
│   ├── 0_TEXT.AFS
│   ├── OVER.AFS
│   ├── Q_SOUND.AFS
│   ├── Q_TEXT.AFS
│   ├── S_SOUND.AFS
│   ├── S_TEXT.AFS
│   ├── PSS\
│   ├── mc0\
│   └── mc1\
├── output\          ← generated translation units (created by ps2_recomp)
└── ranges\overlays\ ← generated overlay ELFs (created by export-overlays.py)
```

Extract your ISO with 7-Zip (it can open ISO9660) or a dedicated tool. **Do not redistribute
this folder.**

## 4. Point the configs at your paths

The files in `config/` use a `<PES2014_DIR>` placeholder. Replace it with the absolute path of
your work folder (the one containing `disc/`, `output/` and `SLES_556.76`).

Example edit in `config/pes2014.toml`:

```toml
input = "D:/pes2014/SLES_556.76"
output = "D:/pes2014/output"
ghidra_output = "D:/pes2014/functions.csv"
```

Use forward slashes, or escape backslashes (`\\`).

> The overlay configs in `config/overlays/*.toml` are **regenerated** by
> `tools/recompile-overlays.py`; you normally do not edit them by hand. They are shipped as
> reference because they document which entry points were discovered.

## 5. Analyze the main ELF (Ghidra) — only if you do not have `functions.csv`

The main-ELF config reads `ghidra_output` (`functions.csv`). Run the analyzer with your Ghidra
installation:

```powershell
cmake -S <repo>\PS2Recomp -B <build> -DPES2014_GENERATED_DIR=<work>\pes2014\output
cmake --build <build> --config Release --target ps2_analyzer --parallel 4
& "<build>\ps2xAnalyzer\Release\ps2_analyzer.exe" <work>\pes2014\analyzer.toml
```

> Adjust paths to your Ghidra install as the analyzer expects.

## 6. Extract the overlays and recompile them

`export-overlays.py` reads `OVER.AFS` and writes `NN-name.ovl.elf` + `overlays.json`.

```powershell
cd <work>\pes2014\ranges
python <share>\tools\export-overlays.py "<work>\pes2014\disc\OVER.AFS" "<work>\pes2014\ranges\overlays"
```

Build the recompiler, then recompile the overlays you need (ids from `overlays.json`):

```powershell
cmake --build <build> --config Release --target ps2_recomp --parallel 4

python <share>\tools\recompile-overlays.py `
    "<work>\pes2014\ranges\overlays" `
    "<work>\pes2014\SLES_556.76" `
    "<build>\ps2xRecomp\Release\ps2_recomp.exe" `
    16 18 19 20 21 29 30 31 34 37 40 41 137
```

> That list is the set used for this build. `34` (`training_free.ovl`) was added by this work.
> `recompile-overlays.py` already scans **every** overlay for cross-overlay references, so its
> entry-point hints are much better than the original version.

## 7. Stage the overlays into the runner

```powershell
python <share>\tools\stage-overlays.py `
    "<work>\pes2014\ranges\overlays" `
    "<repo>\PS2Recomp\ps2xRuntime\src\runner" `
    16 18 19 20 21 29 30 31 34 37 40 41 137
```

This regenerates `pes_overlay_dispatch.cpp`, the per-overlay `pes_overlay_NN_*.cpp` batches and
the headers.

## 8. Build the runner

```powershell
cmake -S <repo>\PS2Recomp -B <build> `
      "-DPES2014_GENERATED_DIR=<work>\pes2014\output" `
      -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build <build> --config Release --target ps2EntryRunner --parallel 4
```

Output: `<build>\ps2xRuntime\Release\PES2014-Recompiled.exe`.

> **Reconfigure after staging.** CMake globs the runner sources; adding/removing
> `pes_overlay_*.cpp` without re-running `cmake -S ... -B ...` produces `LNK2001` unresolved
> symbols.

## 9. Run

```powershell
$env:PES2014_BOOT_DIAGNOSTICS = '1'
$env:PES2014_TEST_INPUT       = '1'
& "<build>\ps2xRuntime\Release\PES2014-Recompiled.exe" "<work>\pes2014\disc\SLES_556.76"
```

Packaging for distribution (optional): copy the exe plus the DLLs that `dumpbin /dependents`
reports (SDL2, FFmpeg, `vcruntime140*`, `msvcp140`, zlib, brotli, OpenEXR, libwebp, freetype,
harfbuzz) next to it. The VC++ runtime DLLs can be shipped app-local.

## 10. Verify you reproduced the same state

| Check | Expected |
|---|---|
| Window title | `PES 2014` |
| `missing-target` in the log | **0** |
| `[PES XGKICK stats] ... aborted=` | ≈ **128** (not tens of thousands) |
| Menus / stadium | full colour (see `docs/images/`) |
| Match | still black (the open bug) |

If `missing-target` > 0, read `docs/TECHNICAL-NOTES.md` §9 (overlay fingerprints) — the log
line `[PES overlay] 0x93a880 resident=<name>` tells you which overlay was loaded.

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `LNK2001` on `pes_overlay_*` | CMake was not reconfigured after staging |
| `missing-target ... codeRegion=no` | address not in any recompiled table: recompile that overlay, or the address is mid-function (add it to `additional-entries.json`) |
| Game freezes with `draws` frozen | something waits on a semaphore; set `PES2014_SEMA_TRACE=1` and `PES2014_BOOT_DIAGNOSTICS=1` |
| Black screen from the start | check `disc/` layout and that the ELF path argument is correct |
| Overlay recognised as the wrong one | the 16-byte fingerprint at `base + 0x80` does not match; see `docs/TECHNICAL-NOTES.md` §9 |
| `git apply` complains about whitespace | `git config core.autocrlf false` then `git apply --whitespace=nowarn` |
