# config

| File | Purpose |
|---|---|
| `pes2014.toml` | recompiler config for the **main ELF** (`SLES_556.76`). Contains the stub table (SCE kernel functions) and the extra `entry_points` added for this game. |
| `analyzer.toml` | `ps2xAnalyzer` config used to produce `functions.csv` (Ghidra). |
| `overlays/NN.toml` | per-overlay recompiler configs. **These are generated** by `tools/recompile-overlays.py`; shipped as reference because the `entry_points` list documents which functions were discovered. |

## Before running

Every file uses the `<PES2014_DIR>` placeholder. Replace it with the absolute path of your work
folder — the one that contains:

```
disc/            extracted game
output/          generated translation units
SLES_556.76      ELF copy used by the recompiler
ranges/overlays/ generated overlay ELFs
```

Example:

```toml
input = "D:/pes2014/SLES_556.76"
output = "D:/pes2014/output"
ghidra_output = "D:/pes2014/functions.csv"
```

Use forward slashes or escape backslashes (`\\`).

## Overlay entry points

The big arrays in `overlays/*.toml` (e.g. `37.toml` has hundreds of addresses) are the
**entry-point hints** used to discover functions that Ghidra does not promote on its own. They
are produced from:

1. cross-references found in the main ELF **and in every other overlay ELF**,
2. `addiu sp, sp, -N` function prologues,
3. manual hints from `ranges/overlays/additional-entries.json`.

If a call lands on an address that is not in the table, the runtime reports
`[guest-branch:missing-target]`. Add that address to `additional-entries.json` under the
overlay id and re-run `recompile-overlays.py`:

```json
{
  "34": ["0x959a70"]
}
```
