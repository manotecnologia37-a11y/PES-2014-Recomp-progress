# Documentation index — PES 2014 (PS2)

The **project landing page** (status, screenshots and the summary in three languages) is the
repository root **[`README.md`](../README.md)**.

| Document | What it covers |
|---|---|
| [`pt-BR.md`](pt-BR.md) | guia completo em português |
| [`en.md`](en.md) | full guide (English) |
| [`es.md`](es.md) | guía completa (español) |
| [`TECHNICAL-NOTES.md`](TECHNICAL-NOTES.md) | code-level notes: patches, addresses, overlay fingerprints, pitfalls |
| [`POSTING-ON-GITHUB.md`](POSTING-ON-GITHUB.md) | ready-to-paste issue/discussion text (PT/EN/ES) |
| [`patches/README.md`](patches/README.md) | changed files + abridged diffs |
| [`images/`](images/) | the screenshots shown in the root README |

The three language guides are **equivalent** — same structure, same numbers, same open problem.
Pick whichever is easier for you.

## Structure of every language guide

1. Current status (what works / what does not)
2. Architecture of the recompilation
3. What was done, in order
4. Runtime bugs fixed
5. Overlays: how they work and why they matter
6. How to build
7. How to run
8. Diagnostic environment variables
9. **What is still broken** (the open problem) + how to reproduce
10. Reference data (PCSX2 GS dump)
11. Project tools and scripts
12. How to help

## Quick links

- Build from scratch → [`../SETUP.md`](../SETUP.md)
- Check your machine → [`../CHECK.ps1`](../CHECK.ps1)
- What is / is not shared → [`../PACK.md`](../PACK.md) and [`../LEGAL.md`](../LEGAL.md)
- Applyable patch → [`../patches/pes2014-runtime.patch`](../patches/pes2014-runtime.patch)
