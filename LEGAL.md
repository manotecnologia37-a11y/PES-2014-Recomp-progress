# LEGAL — what is licensed, what is not, what you may redistribute

## 1. This share pack

| Part | Licence / status |
|---|---|
| `docs/` (text, screenshots) | © the pack author / repository owner. You may reuse it with attribution. |
| `tools/*.py`, `tools/*.ps1` | written for this work; same licence as the project they serve (GPLv3-compatible). |
| `config/*.toml` | configuration data generated from the user's own game analysis. No game code. |
| `source/*` | **modified PS2Recomp sources** → **GNU GPL v3**, because they derive from PS2Recomp. If you redistribute them, keep GPLv3 and keep the copyright notices. |

PS2Recomp upstream: <https://github.com/ran-j/PS2Recomp> — GPLv3 (see its `LICENSE`).

## 2. What is deliberately NOT included

The pack contains **no game data**. Specifically, it does not include:

- `SLES_556.76`, `SYSTEM.CNF`, `IOP/`, `*.AFS`, `PSS/`
- the ISO image
- a PS2 BIOS dump
- `PES2014-Recompiled.exe` and any generated translation units (`code-*/`, `output/`)
- `functions.csv` and any analysis artefact derived from the game binary
- GS dumps (`*.gs`, `*.gs.zst`) and VRAM snapshots (`*.bin`)
- screenshots of copyrighted content beyond the three illustrative captures in `docs/images/`

Those are derived from **Pro Evolution Soccer 2014**, © Konami Digital Entertainment.
Distributing them is not permitted.

## 3. What you must provide yourself

1. **A legal copy of the game** (PS2 PAL, `SLES_556.76`). Rip it from your own disc.
2. **Your own PS2 BIOS dump**, only if you want to use PCSX2 for reference captures. The BIOS
   must come from your own console.

## 4. Redistributing your build

If you publish a build:

- **Do not** ship the exe together with the game data.
- **Do not** ship the extracted `disc/` tree or the ISO.
- Publishing an executable that contains recompiled game code is legally the same problem as
  publishing a ROM hack or a decompilation binary — many projects do it only as a **patch**
  that the user applies to their own files.
- The safest route is to publish **only the source, the patches and the documentation** (this
  pack), and let each user build against their own copy of the game.

## 5. Emulator reference

PCSX2 is GPLv3. Its GS dump format and its `GSLocalMemory` model were used as **reference** for
behaviour and for validating our output. No PCSX2 code was copied into this pack; the PS2Recomp
runtime is a port/derivative and keeps its own licence.

## 6. Trademarks

"Pro Evolution Soccer", "PES", "Konami" and related marks are trademarks of Konami Digital
Entertainment. This project is not affiliated with or endorsed by Konami.

## 7. AI disclosure

The debugging, instrumentation, runtime patches, overlay recompilation and documentation were
produced by an **AI coding agent** (OpenCode running a large language model) in an interactive
session with the repository owner. The owner ran the game, captured the screenshots and
provided the reference dump. Keep this disclosure when redistributing the docs.

---

*If in doubt: share this folder, never share the `disc/` folder.*
