---
name: gitnexus-area-scripts
description: "Skill for the Scripts area of OpenCBP_FO4. 30 symbols across 4 files."
---

# Scripts

30 symbols | 4 files | Cohesion: 96%

## When to Use

- Working with code in `scripts/`
- Understanding how verifyInstall work
- Modifying scripts-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `scripts/bearing-verify.mjs` | checkModuleDelivery, checkPackageGates, checkRuntimeCoversAgent, checkSkillSymlinks, checkZed (+7) |
| `scripts/bearing-ci.mjs` | blastRadius, collectDiff, detectChanges, num, gn (+4) |
| `scripts/bearing-token-benchmark.mjs` | classicalCost, graphCost, tok, cypher, gn (+1) |
| `scripts/bearing-agent.mjs` | currentBranch, git, resolveBaseRef |

## Entry Points

Start here when exploring this area:

- **`verifyInstall`** (Function) — `scripts/bearing-verify.mjs:367`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `verifyInstall` | Function | `scripts/bearing-verify.mjs` | 367 |
| `checkModuleDelivery` | Function | `scripts/bearing-verify.mjs` | 205 |
| `checkPackageGates` | Function | `scripts/bearing-verify.mjs` | 121 |
| `checkRuntimeCoversAgent` | Function | `scripts/bearing-verify.mjs` | 186 |
| `checkSkillSymlinks` | Function | `scripts/bearing-verify.mjs` | 302 |
| `checkZed` | Function | `scripts/bearing-verify.mjs` | 326 |
| `main` | Function | `scripts/bearing-verify.mjs` | 462 |
| `printHuman` | Function | `scripts/bearing-verify.mjs` | 413 |
| `readRuntime` | Function | `scripts/bearing-verify.mjs` | 56 |
| `runtimeSet` | Function | `scripts/bearing-verify.mjs` | 85 |
| `wantsClaude` | Function | `scripts/bearing-verify.mjs` | 102 |
| `wantsZed` | Function | `scripts/bearing-verify.mjs` | 99 |
| `blastRadius` | Function | `scripts/bearing-ci.mjs` | 110 |
| `collectDiff` | Function | `scripts/bearing-ci.mjs` | 78 |
| `detectChanges` | Function | `scripts/bearing-ci.mjs` | 92 |
| `num` | Function | `scripts/bearing-ci.mjs` | 95 |
| `gn` | Function | `scripts/bearing-ci.mjs` | 57 |
| `main` | Function | `scripts/bearing-ci.mjs` | 331 |
| `render` | Function | `scripts/bearing-ci.mjs` | 155 |
| `riskTag` | Function | `scripts/bearing-ci.mjs` | 147 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Main → Git` | intra_community | 3 |
| `Main → Num` | intra_community | 3 |
| `Main → Gn` | intra_community | 3 |
| `VerifyInstall → ReadStealth` | intra_community | 3 |

## How to Explore

1. `context({name: "verifyInstall"})` — see callers and callees
2. `query({search_query: "scripts"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
