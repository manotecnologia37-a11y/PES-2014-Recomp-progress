# Technical Notes — PES 2014 (PS2) native recompilation

Code-level companion to `README.md` / `pt-BR.md` / `en.md` / `es.md`.
Everything below was applied to the **PS2Recomp** runtime in this project.

> Sumário em português no fim do arquivo (§13).

---

## 1. Environment

| Item | Value |
|---|---|
| Host | Windows 11 x86-64 |
| Compiler | MSVC 19.4x (Visual Studio 2022, x64) |
| Build system | CMake ≥ 3.25, MSBuild generator |
| Language std | C++20 |
| Reference emulator | PCSX2 2.x (for GS dumps only) |
| Game | `SLES_556.76` PAL, extracted `disc/` tree |

Build targets:

- `ps2EntryRunner` → `PES2014-Recompiled.exe`
- `ps2_recomp` → `ps2_recomp.exe` (code generator)
- `ps2_analyzer` → `ps2_analyzer.exe` (ELF analysis)

---

## 2. The VIF1 streaming fix (root cause of the black scene)

### Symptom

```
[PES VIF trunc] op=6c imm=eaa1 num=70 vl=0 vn=3 cl=1 wl=1
                tops=0 vuAddr=2a1 bpv=16 vectors=70 bytes=1120 avail=796
```

`avail` is not a multiple of 16 and is smaller than `bytes`: the UNPACK payload was split
across a **DMA tag boundary**. The old code required `pos + totalBytes <= sizeBytes`; when it
failed, the entire UNPACK was skipped and `pos` jumped past the end of the buffer.

### Why it matters

The VU1 geometry program reads its vertex records from VU data memory:

```
2B30: lq.xyzw vf07, 0(vi04)   ; position
2B38: lq.xyzw vf05, 2(vi04)   ; colour  <-- was always zero
2B40: lq.xy   vf20, 3(vi04)   ; UV
...
2B58: maxx.xyzw vf05, vf05, vf00x
2B80: mini.xyzw vf05, vf05, vf01     ; vf01 = (128,128,128,255)
2BA0: ftoi0.xyzw vf05, vf05
2BC0: sq.xyzw vf05, 514(vi12)        ; RGBAQ
```

If the UNPACK that fills those records is dropped, the record stays zero and **the whole
scene renders black** (with `ABE` on, `As = 0`).

### Fix

`ps2xRuntime/include/runtime/ps2_memory.h`:

```cpp
// Stream tail for VIF1: a VIFcode may straddle DMA tags.
std::vector<uint8_t> m_vif1Stream;
uint32_t m_vif1StreamPos = 0u;
```

`ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp`:

```cpp
void PS2Memory::processVIF1Data(const uint8_t *inData, uint32_t inSizeBytes)
{
    if (inSizeBytes == 0u)
        return;

    // VIF is a byte stream: a single VIFcode (notably UNPACK) can straddle DMA
    // tags. Keep the unconsumed tail and resume parsing there.
    if (m_vif1StreamPos != 0u)
    {
        if (m_vif1StreamPos >= m_vif1Stream.size())
        {
            m_vif1Stream.clear();
            m_vif1StreamPos = 0u;
        }
        else if (m_vif1StreamPos >= 0x10000u)   // compact only every 64 KiB
        {
            m_vif1Stream.erase(m_vif1Stream.begin(),
                m_vif1Stream.begin() + static_cast<std::ptrdiff_t>(m_vif1StreamPos));
            m_vif1StreamPos = 0u;
        }
    }
    m_vif1Stream.insert(m_vif1Stream.end(), inData, inData + inSizeBytes);

    const uint8_t *const data = m_vif1Stream.data();
    const uint32_t sizeBytes = static_cast<uint32_t>(m_vif1Stream.size());

    uint32_t pos = m_vif1StreamPos;
    ...
        const uint32_t cmdStart = pos;      // remember where the VIFcode starts
        uint32_t cmd;
        memcpy(&cmd, data + pos, 4);
        pos += 4;
    ...
            if (m_vu1Data && totalBytes > 0 && pos + totalBytes > sizeBytes)
            {
                pos = cmdStart;             // retry with the next DMA tag
                break;
            }
    ...
    m_vif1StreamPos = pos;
    if (m_vif1StreamPos >= m_vif1Stream.size())
    {
        m_vif1Stream.clear();
        m_vif1StreamPos = 0u;
    }
}
```

The same `pos = cmdStart; break;` treatment is applied to:

- `VIF_DIRECT` / `VIF_DIRECTHL` (do **not** consume a partial block; it would desync the GIF)
- `VIF_MPG`
- `VIF_STMASK` / `VIF_STROW` / `VIF_STCOL`

### Measured effect

| Metric | Before | After |
|---|---|---|
| `XGKICK aborted` | 130 048 | **128** |
| Menus/stadium | black | full colour |
| `PES VIF trunc` retries | dropped | resumed successfully |

---

## 3. The GS CSR `FINISH` fix (root cause of the transition hang)

`sceGsSyncPath`-style code in the game:

```
0x1018EC: ld   r3, 0(0x12001000)   ; GS CSR
0x1018F8: andi r3, r3, 0x2         ; bit 1 = FINISH
0x1018FC: bne  r3, r0, 0x101938
0x101904..: (timeout counter r16 vs 0x01000000) -> +-16.7M iterations
```

Our GS backend is **immediate/synchronous**, so `FINISH` is always true in reality, but the
value read from the CSR never carried bit 1 → the EE spun until `draws` froze.

`ps2xRuntime/src/lib/ps2_memory.cpp`:

```cpp
// The CPU GS backend renders synchronously: by the time the EE observes the
// CSR the queued GIF/GS work has already completed.
constexpr uint64_t kGsCsrFinishBit = 1ull << 1u;

inline uint64_t readCsrValue(const std::atomic<uint64_t> &csr)
{
    return csr.load(std::memory_order_relaxed) | kGsCsrFinishBit;
}
```

Applied at every CSR read site (`read32`, `read64`, and the generic register reader):

```cpp
if (regOff == kGsCsrRegOffset)
    return readCsrValue(gs_regs.csr);
```

Writes still honor write-1-to-clear (`writeCsrHalf` / `writeCsrFull`).

Related detail: the **FIELD bit (bit 13)** is already toggled per vblank in
`EeScheduler::processEvent` (`VBlankStart`):

```cpp
if ((m_vsyncTick & 1u) != 0u)
    m_runtime.memory().gs().csr.fetch_or(0x2000ull);
else
    m_runtime.memory().gs().csr.fetch_and(~0x2000ull);
```

---

## 4. VU1 `JR`/`JALR` must read the current VI (upstream PR #418)

`ps2xRuntime/src/lib/vu/ps2_vu1_lower.cpp`

Old behaviour: `JR`/`JALR` used a backup copy of `VI`, taken before the delay-slot write.
That made the entry code jump to `0x2F98` instead of `0x3D30`, skipping `iaddiu vi12, vi00, 0x1f0`.

```cpp
// JR / JALR: the target is the *current* VI value.
const uint32_t target = m_state.vi[is];
```

The saved-VI backup is only used by conditional branches, where the delay slot may clobber
the compared register.

---

## 5. GIF arbiter must drain in FIFO order

`ps2xRuntime/src/lib/gs/ps2_gif_arbiter.cpp`

The old comparator treated the path id as a total order, which is UB for the sort and could
reorder `Path1` before `Path3`. The GS requires submission order to be preserved.

```cpp
void GifArbiter::drain()
{
    while (!m_queue.empty())
    {
        auto item = std::move(m_queue.front());
        m_queue.pop_front();
        submit(item);          // strict FIFO, no reordering
    }
}
```

---

## 6. XGKICK must not kill the VU

`ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp`

- An invalid/oversized packet cancels **only that transfer**, not the VU program.
- Packet size is capped at the 16 KiB VU data memory (mirrors PCSX2 `GetGSPacketSize`).
- Rounding mode is restored around `submitGifPacket` (upstream PR #422) because the rasteriser
  runs on the host FP path while the VU runs with `FE_TOWARDZERO`:

```cpp
const int savedRound = std::fegetround();
if (m_hostRoundingMode != -1)
    std::fesetround(m_hostRoundingMode);
m_activeMemory->submitGifPacket(GifPathId::Path1, m_xgkick.packet.data(), m_xgkick.totalBytes);
if (m_hostRoundingMode != -1)
    std::fesetround(savedRound);
```

---

## 7. MSCNT resumes at the MSCAL-latched address

`MSCNT` restarts execution at the address latched by the last `MSCAL`, not at `0`. The game
uses the pair `MSCAL pc=0x10 -> itop 0x1E0` and `pc=0x140 -> itop 0x3E0`.

---

## 8. STMASK consumes one qword

VIF `STMASK` transfers **one qword** (16 bytes); only the first word is the mask. Consuming 4
bytes desynchronises the following commands by 12 bytes.

---

## 9. Overlay dispatch model

Overlays are extracted from `OVER.AFS` as ELFs (`export-overlays.py`) and recompiled one by
one. Several share base `0x93A880`, so the runtime must know **which one is resident**:

```cpp
PS2Runtime::RecompiledFunction resolvePesOverlay(const uint8_t *rdram, uint32_t pc)
{
    if (pc >= 0x93a880u && pc < 0x971e80u)             // enter.ovl range
    {
        constexpr std::array<uint32_t, 4> fp =
            {0x27bdfee0u, 0xffbf0090u, 0x7fbe0080u, 0x7fb70070u};
        if (std::memcmp(rdram + 0x93a880u, fp.data(), sizeof(fp)) == 0)
            return findEntry(pes_overlay_29_entries, pc);
    }
    ...
}
```

The fingerprint is the **first 16 bytes of the resident overlay**, at `base + 0x80`.

### Fingerprints of the overlays that share `0x93A880`

```
29 enter.ovl          e0 fe bd 27 90 00 bf ff 80 00 be 7f 70 00 b7 7f
30 title.ovl          84 01 83 8c 03 00 02 24 45 00 62 14 00 00 00 00
31 bootset.ovl        d0 ff bd 27 44 da 82 8f 20 00 bf ff 10 00 b1 7f
32 result.ovl         80 fe bd 27 97 00 03 3c 90 00 bf ff 80 00 be 7f
33 ending.ovl         ff 00 83 30 16 00 07 24 9b 00 67 50 c0 da 83 93
34 training_free.ovl  f0 ff bd 27 00 00 bf ff 08 7a 09 0c 00 00 00 00
35 mc_conv.ovl        94 00 02 3c 00 f4 42 90 08 00 e0 03 2b 10 02 00
36 fantasista2.ovl    d0 ff bd 27 20 00 bf ff 10 00 b1 7f 00 00 b0 7f
```

### Real extents (from the ELF program headers)

| # | Overlay | Start | End |
|---|---|---|---|
| 29 | enter | 0x93A880 | 0x97AE80 |
| 30 | title | 0x93A880 | 0x947A80 |
| 31 | bootset | 0x93A880 | 0x940480 |
| 32 | result | 0x93A880 | 0x99C900 |
| 33 | ending | 0x93A880 | 0x94EA80 |
| 34 | training_free | 0x93A880 | 0x972E80 |
| 35 | mc_conv | 0x93A880 | 0x93F500 |
| 36 | fantasista2 | 0x93A880 | 0x98FD80 |

> The range check in the generated dispatcher uses `base + 0x40 + code_size` from
> `overlays.json`, which matches the real extent. Older hand-written checks used the max
> analysed entry point and were **too small**.

### Cross-overlay reference scanning (pipeline fix)

`ranges/recompile-overlays.py` originally did:

```python
hints = external_references((main, image), start, start + len(body), body)
```

That misses a direct call from a *different* overlay, e.g. `select.ovl` (0x99D080) calling
`training_free.ovl` at `0x959A70`. Now:

```python
# Overlays call into each other directly, so harvest references from every
# overlay image, not just the main ELF.
inputs = [main] + sorted(root.glob('*.ovl.elf'))
hints = external_references(inputs, start, start + len(body), body)
```

Effect for overlay 34: **569 → 4,025 entry points**, and `0x959A70` appeared in
`register_functions.cpp`:

```
g_ps2RecompiledFunctionTable[31868] = sub_009596B0_0x9596b0; // 0x959a70
```

---

## 10. Diagnostics added (log tags and what they mean)

All gated by environment variables (see the language guides §8).

| Log tag | Meaning |
|---|---|
| `[PES VIF trunc]` | an UNPACK did not fit in the current buffer (now retried) |
| `[PES VIF low]` | UNPACK landing at low VU data addresses (vertex records) |
| `[PES VU input]` | VI values + memory read by the VU at XGKICK |
| `[PES SQ rgba]` | `SQ` writes into the RGBAQ slots (`imm` 514/517/520/523) |
| `[PES XGKICK issue]` / `[PES XGKICK stats]` | issued/completed/aborted counters |
| `[PES GS draw]` | per-draw summary (fbp, prim, scissor, texture, alpha, **rgb min/max**) |
| `[PES GS dump]` | full vertex dump for a single frame (`PES2014_GS_FRAME`) |
| `[PES GS transfer]` | image transfers (direction, source, destination, pixels) |
| `[PES scene]` | game state machine (`scene`, `game`, `step`, flags) |
| `[PES sema state]` | semaphore table at boot-trace frames |
| `[PES iopfile]` | host files opened/read by the IOP |
| `[PES overlay]` | overlay recognised by fingerprint |

### Game state machine addresses

| Global | Address | Meaning |
|---|---|---|
| scene index | `0x0043BFE4` | current scene (0 = main) |
| scene fn table | `0x003AAE90 + scene*4` | per-scene update function |
| update | `0x0042A51C` | current update callback |
| pending | `0x0042A084` | pending transition |
| transition | `0x005D208C` | transition target |
| game state | `0x00552150` | `game=` in the log |
| game step | `0x00552158` | `step=` in the log |
| flags | `0x00438924` | global flags |
| main loop | `FUN_001210F0` | calls the above |

Observed progression: `game=0 → 1 → 2 → 3 → 4 → 5 → 10 (match) → 9 (training_free)`.

---

## 11. Known pitfalls

1. **CMake globs the runner sources.** After adding/removing `pes_overlay_*.cpp` you must
   re-run `cmake -S ... -B ...` or you get `LNK2001: unresolved external`.
2. **Overlay base collisions.** Never infer the overlay from the address range alone — always
   check the 16-byte fingerprint. `0x966E20` exists in both `enter.ovl` and
   `training_free.ovl`.
3. **`stage-overlays.py` rewrites `pes_overlay_dispatch.cpp`.** Any local edit to that file is
   lost; make changes in the script or re-apply after staging.
4. **`register_functions.cpp` is the source of truth** for the dispatch table. If an address
   is not there, `findEntry` returns `nullptr` and the EE reports `missing-target`.
5. **Diagnostics prerequisites.** `PES2014_BOOT_DIAGNOSTICS` must be set for most traces and
   for the periodic dumps.

---

## 12. Open problem — exact instrumentation

Goal: find why match vertices arrive zeroed.

Add these probes (all already implemented):

```powershell
$env:PES2014_BOOT_DIAGNOSTICS='1'
$env:PES2014_TEST_INPUT='1'
$env:PES2014_VU_INPUT='1'        # prints vi4 and mem[vi4..vi4+3] at XGKICK
$env:PES2014_VU_LOWTRACE='1'     # prints UNPACKs that write 0x18..0x28
$env:PES2014_SQ_TRACE='1'        # prints vf05 at the RGBAQ SQ
$env:PES2014_VIF_TRUNC='1'
$env:PES2014_GS_FRAME='9000'
$env:PES2014_GS_FRAME_SPAN='2'
```

Expected data to compare:

- `[PES VIF low] ... vuAddr=0xNN ... src[...]` — what the EE wrote.
- `[PES VU input] vi4 mem[...]` — what the VU reads.
- `[PES SQ rgba] pc=2bc0 vf05 ...` — what is converted to RGBAQ.
- `[PES GS draw] ... rgb=rMin-rMax/...` — what reaches the GS.

If `mem[vi4+2]` (colour) is non-zero but `vf05` is zero at the SQ, the bug is in the VU
path (LQ/branch). If `mem[vi4+2]` is zero, the bug is on the producer side (VIF/EE).

### 12.1 Result of the trace (2026-11-12)

The match vertex colour is zero **because the VU1 skinning program reads the material from the
wrong place**, not because of VU1 interpreter accuracy:

```
0160: lq.xyzw vf01, 0(vi13)     ; vi13 = XITOP = VIF1 ITOP = 0x1E0
...
05b8: sq.xyzw vf01, 2(vi04)     ; write material -> record colour slot
```

- `XITOP vi13` moves the VIF1 **ITOP** (set by the `ITOP` VIFcode) into `vi13`. For the
  skinning program (`MSCAL pc=0x10`) the ITOP is `0x1E0`; for the other program
  (`MSCAL pc=0x140`) it is `0x3E0`.
- The game uploads **one qword of zeros** to `0x1E0` (`UNPACK imm=0x01E0 num=1`) and the
  neutral tint `(128,128,128,255)` + view constants to `0x1FD` (`imm=0x01FD num=3`).
- So `lq vf01, 0(vi13)` loads `0x1E0` = `0.0`, and the record colour becomes `0`.

Measured with `PES2014_MAT_TRACE=1` / `PES2014_SKIN_TRACE=1`:

```
[PES mat LQ] pc=160 vf1=0(vi13=1e0) val=0.000,0.000,0.000,0.000
[PES skin SQ] pc=5b8 vf01 imm=2 ... val=0.000,0.000,0.000,0.000 vf01=0,0,0,0
```

Positions and normals are correct (skinning works): `SQ vf12 → -2211,-2173,47` and
`SQ vf13 → -0.499,-0.865,0.015`. Only the material (vf01) is zero.

**Conclusion:** the bug is on the producer side — the material the game intends to load from
`ITOP` (`0x1E0`) is zero. Either the upload that should carry the per-model colour (the
reference RGBAQ is `128,124,128,128`) is being dropped/zeroed, or the material lives at
`ITOP+0x1D` (`0x1FD`) and the game actually reads it through a different pointer. This is a
VIF1/upload issue, not a VU1-accuracy issue.

---

## 13. Resumo técnico em português

- **VIF1 é um stream**: `processVIF1Data` agora mantém um buffer persistente e, quando um
  UNPACK não cabe no bloco DMA atual, volta ao início do comando e retoma na chamada seguinte.
  Era isso que zerava as cores dos vértices e deixava a cena preta.
- **CSR do GS**: leituras fazem OR do bit `FINISH` porque o backend GS é síncrono; sem isso o
  jogo travava no laço de espera em `0x1018EC` na transição para a partida.
- **Overlays**: 8 overlays compartilham a base `0x93A880`; a identificação é feita por
  fingerprint de 16 bytes em `base + 0x80`. O `recompile-overlays.py` passou a varrer todos os
  `.ovl.elf` para achar referências cruzadas (569 → 4.025 entry points).
- **Resultado**: `missing-target` = 0, `XGKICK aborted` 130.048 → 128.
- **Pendente**: a geometria do partido chega degenerada e sem cor
  (`x=0..2048`, `y=2048..2048`, `rgb=0`); a tela fica preta porque `fbp=128` nunca recebe o
  blit. O próximo passo é comparar, no mesmo instante, o UNPACK que escreve o registro de
  vértice com o que a VU lê em `lq.xyzw vf05, 2(vi04)`.
