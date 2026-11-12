# PES 2014 (PS2) — share pack

This folder contains **everything that can legally be shared** so someone else can download it,
reproduce the build, modify the runtime and help fix the remaining bug.

**Start here:** [`docs/README.md`](docs/README.md) (index in PT/EN/ES) ·
[`docs/TECHNICAL-NOTES.md`](docs/TECHNICAL-NOTES.md) (code level) ·
[`SETUP.md`](SETUP.md) (how to rebuild from scratch).

---

## What is inside

```
share/
├── README.md            ← you are here
├── CHECK.ps1            ← environment check (run this FIRST)
├── SETUP.md             ← step-by-step reproduction
├── LEGAL.md             ← licences and what is NOT included
├── docs/                ← full documentation (PT / EN / ES + technical + screenshots)
├── tools/               ← the overlay pipeline scripts and build helpers
├── patches/             ← applyable git patch against upstream PS2Recomp (verified)
├── config/
│   ├── pes2014.toml     ← main-ELF recompiler config
│   └── overlays/*.toml  ← per-overlay recompiler configs (entry points included)
└── source/              ← the runtime files we changed (drop-in over upstream)
```

Total: ~1.3 MB.

### Step 0 — run the check

```powershell
powershell -ExecutionPolicy Bypass -File .\CHECK.ps1
```

It is read-only and verifies:

- Windows x64, CMake ≥ 3.25, Python 3, Git, Visual Studio (MSVC x64)
- Ghidra (optional — only to regenerate `functions.csv`)
- your game layout (`disc/SLES_556.76`, `SYSTEM.CNF`, `OVER.AFS`, `IOP/`)
- the ELF copy at the work-folder root
- the PS2Recomp checkout and whether it is at the expected commit (`e563698`)
- free disk space (the build needs ~4–6 GB)

Exit code `0` = ready. Any `FAIL` line tells you exactly what is missing. Use `-WorkDir` and
`-RepoDir` if your paths are not next to this folder.

### The fastest way to start

```bash
git clone https://github.com/ran-j/PS2Recomp.git
cd PS2Recomp
git checkout e563698
git apply /path/to/share/patches/pes2014-runtime.patch
```

The patch was verified with `git apply --check` on a clean clone of `e563698`
(+1676 / −174 lines, 31 files). Then follow `SETUP.md` from §3.

## What is NOT inside (and why)

| Missing | Why |
|---|---|
| `disc/`, the ISO, `SLES_556.76`, `*.AFS` | copyrighted game data |
| BIOS | copyrighted, comes from your own console |
| `PES2014-Recompiled.exe` | 672 MB, and it is derived from the game |
| `functions.csv`, `code-*/`, `output/` | generated from the game binary |
| `*.gs`, `*.gs.zst`, `boot-vram-*.bin` | captured from the game |

See [`LEGAL.md`](LEGAL.md).

## What you need to own

- A **legal copy of PES 2014 (PS2, PAL)** — the share pack assumes `SLES_556.76`.
- A PS2 **BIOS dump** from your own console (only if you want to compare with PCSX2).
- PCSX2 is optional and only used to capture reference GS dumps.

## How to use this pack

1. Read [`SETUP.md`](SETUP.md) and follow it in order.
2. The files in `source/` are **complete replacement files** for an upstream PS2Recomp
   checkout — copy them over the matching paths. `docs/patches/README.md` explains each change.
3. The configs in `config/` are the exact ones used for this build.
4. If you only want the bug report, read `docs/`.

## Licence / Licença

This repository contains code derived from **PS2Recomp**, so the code parts are licensed under
the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE). The documentation and the
screenshots may additionally be reused under CC BY 4.0 with attribution.

The **game data is not distributed**. `LEGAL.md` explains exactly what is and is not included.

O repositório contém código derivado do **PS2Recomp**, então a parte de código é **GPLv3** —
veja [`LICENSE`](LICENSE). A documentação e as capturas podem ser reutilizadas sob CC BY 4.0
com atribuição. **Os dados do jogo não são distribuídos**; detalhes em `LEGAL.md`.

---

## Estado / Status

The game boots; menus, title screen and the 3D stadium render in full colour. The **match
scene is still black** (degenerate, colourless geometry). The open problem, the hypotheses and
the exact instrumentation to reproduce it are in `docs/pt-BR.md` §9 (or `docs/en.md` §9).

O jogo inicia; menus, tela de título e o estádio 3D renderizam em cores. A **cena da partida
ainda sai preta** (geometria degenerada e sem cor). O problema aberto, as hipóteses e a
instrumentação exata para reproduzir estão em `docs/pt-BR.md` §9.
