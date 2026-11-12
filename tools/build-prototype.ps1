$ErrorActionPreference = 'Stop'
$local = Split-Path -Parent $PSScriptRoot
$output = Join-Path $PSScriptRoot 'ranges/output'
$build = Join-Path $local 'ps2-build'
$source = Join-Path $local 'PS2Recomp'
if (-not (Test-Path -LiteralPath (Join-Path $output 'register_functions.cpp'))) {
    throw 'Run PS2Recomp successfully before building the runner.'
}
@"
from pathlib import Path

output = Path(r"$output")
batches = output.parent / "batches"
batches.mkdir(exist_ok=True)
sources = sorted(path.name for path in output.glob("*.cpp")
                 if path.name != "register_functions.cpp")
for path in batches.glob("pes_batch_*.cpp"):
    path.unlink()
batch_size = 96
for start in range(0, len(sources), batch_size):
    includes = (f'#include "../output/{name}"\n'
                for name in sources[start:start + batch_size])
    path = batches / f"pes_batch_{start // batch_size:04}.cpp"
    path.write_text("".join(includes), encoding="utf-8")
print(f"Prepared {len(sources)} generated translation units for the build.")
"@ | python -
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake -S $source -B $build "-DPES2014_GENERATED_DIR=$output" -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $build --config Release --target ps2EntryRunner --parallel 4
exit $LASTEXITCODE
