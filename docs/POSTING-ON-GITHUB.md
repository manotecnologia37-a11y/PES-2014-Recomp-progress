# How to publish this on GitHub

Practical checklist for turning this folder into a public report so other people can help.

---

## 1. Where to put it

**Option A — inside the PS2Recomp repository (recommended if you have push access):**

```
PS2Recomp/
└── docs/
    └── games/
        └── pes2014/
            ├── README.md
            ├── pt-BR.md
            ├── en.md
            ├── es.md
            ├── TECHNICAL-NOTES.md
            └── patches/
                └── README.md
```

Copy the whole `pes2014/docs/` folder to `docs/games/pes2014/`.

**Option B — your own repository:**

```
pes2014-recomp-progress/
├── README.md          ← the index (this folder's README.md)
├── pt-BR.md
├── en.md
├── es.md
├── TECHNICAL-NOTES.md
└── patches/
    └── README.md
```

Add a `.gitignore` that excludes game data:

```
disc/
*.iso
*.AFS
*.gs
*.gs.zst
*.bin
*.exe
*.dll
build/
```

> **Never commit the game image, the `disc/` tree, the BIOS, or the recompiled `.exe`.**
> They are copyrighted or simply huge. Publish only code, docs, logs and screenshots.

---

## 2. Suggested issue / discussion title

```
PES 2014 (SLES_556.76) — native recompilation: boots, menus and stadium render, match still black
```

Portuguese:

```
PES 2014 (SLES_556.76) — recompilação nativa: inicia, menus e estádio renderizam, partida ainda preta
```

Spanish:

```
PES 2014 (SLES_556.76) — recompilación nativa: arranca, menús y estadio renderizan, partido aún negro
```

---

## 3. Short issue body (copy/paste)

```markdown
## Summary
PES 2014 (PAL, SLES_556.76) statically recompiled with PS2Recomp (~63k functions).
The game boots and renders menus, stadium, crowd and pitch **in full colour**.
The match reaches `game=10` (match state) and loads `game.ovl`, but the scene still
renders black because the submitted geometry is degenerate and colourless.

## What was fixed
1. **VIF1 stream bug** — UNPACK packets that straddle DMA tags were silently dropped,
   which zeroed every vertex colour. Fixed with a persistent stream buffer that rewinds to
   the VIFcode start and resumes on the next DMA tag.
   → `XGKICK aborted` went from **130 048** to **128**.
2. **GS CSR `FINISH` never raised** — the EE spun forever in a `sceGsSyncPath`-style wait
   (`0x1018EC`). The CPU GS backend is synchronous, so CSR reads now OR in bit 1.
3. **Overlay coverage** — recompiled `training_free.ovl` (244 functions) and improved the
   entry-point discovery to scan **all** overlay ELFs for cross-overlay references
   (569 → 4 025 hints). `missing-target` reached **0**.

## Current blocker
During the match the GS receives degenerate draws:

    [PES GS draw] fbp=0 prim=4 iip=1 abe=1 x=0,2048 y=2048,2048
                  textured=1 tex=8224 tpsm=20 clut=15936/0/0/0 alpha=0,0 rgb=0-0/0-0/0-0

`XYOFFSET = 0x7000` (2048 in 12.4), so a position of 0 maps to 2048 — vertices arrive zeroed.
`fbp=128` (the display buffer) never receives the blit, so the screen stays black.

## Already ruled out
- CLUT is correct (validated against a PCSX2 GS dump).
- `vf01 = (128,128,128,255)` is correct.
- The 2D/blit pipeline works (menus render through the same `fbp=0 → 128` blit).
- No `missing-target` anymore.

## Help wanted
1. A **PCSX2 GS dump of the first seconds of a match** (kick-off) to compare packet by packet.
2. Anyone who has seen this specific degenerate-vertex pattern in other PS2 recompilations.
3. Recompiling the remaining overlays (`result`, `ending`, `mc_conv`, `fantasista2`,
   `defaultdataset`, `fantasista_work`, …) with the pipeline described in the docs.

Full details: `docs/pt-BR.md`, `docs/en.md`, `docs/es.md`, `docs/TECHNICAL-NOTES.md`.
```

---

## 4. What to attach

| Attachment | Why | Size |
|---|---|---|
| `docs/images/03-match-menu-stadium.png` | stadium, crowd, pitch, menu layer — proves rendering | ~165 KB |
| `docs/images/02-title-screen.png` | logo + font/text rendering | ~91 KB |
| `docs/images/01-memory-card-dialog.png` | message box / font rendering | ~21 KB |
| `boot-frame-9000.png` (match) | shows the black match | ~12 KB |
| `PES2014-BOOT_DIAGNOSTICS` log excerpt | `[PES scene]`, `[PES GS draw]`, `[PES overlay]` | truncate to the last ~200 lines |
| `TECHNICAL-NOTES.md` | code-level detail | text |

Do **not** attach the full 500 MB+ log from `ranges/ps2_log.txt`.

---

## 5. Useful labels

```
platform/ps2
tool/recompiler
game/pes2014
type/bug
status/help-wanted
component/vif
component/gs
component/overlays
```

---

## 6. If someone offers help

Ask for exactly one of these, in order of value:

1. **GS dump** of a match frame from PCSX2 (`F12`), plus the PCSX2 version.
2. A **memory snapshot** of VU1 data memory at the first match frame (PCSX2 debugger).
3. Confirmation whether `0x966E20` is called in their trace (overlay identity check).
4. A pull request against the docs if anything is inaccurate.

---

## 7. Keeping it updated

When the match starts rendering, update:

- `README.md` → “Status” line
- `pt-BR.md` / `en.md` / `es.md` → §1 (status) and §9 (open problem → solved)
- `TECHNICAL-NOTES.md` → move the fixed item from §12 into §2–§8

Keep the numbers table (before/after) — it is the fastest way for a reader to judge progress.
