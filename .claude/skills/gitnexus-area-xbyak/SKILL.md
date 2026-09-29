---
name: gitnexus-area-xbyak
description: "Skill for the Xbyak area of OpenCBP_FO4. 214 symbols across 4 files."
---

# Xbyak

214 symbols | 4 files | Cohesion: 75%

## When to Use

- Working with code in `f4se/`
- Understanding how bsf, bsr, btc work
- Modifying xbyak-related functionality

## Key Files

| File | Symbols |
|------|---------|
| `f4se/xbyak/xbyak.h` | operator==, cmpxchg, crc32, imul, imul (+130) |
| `f4se/xbyak/xbyak_mnemonic.h` | bsf, bsr, btc, btr, bts (+64) |
| `f4se/xbyak/xbyak_util.h` | init, StackFrame, close, getOrderTbl, getRegIdx (+4) |
| `f4se/f4se/Hooks_Scaleform.cpp` | ScaleformInitHook_Code |

## Entry Points

Start here when exploring this area:

- **`bsf`** (Function) — `f4se/xbyak/xbyak_mnemonic.h:481`
- **`bsr`** (Function) — `f4se/xbyak/xbyak_mnemonic.h:482`
- **`btc`** (Function) — `f4se/xbyak/xbyak_mnemonic.h:450`
- **`btr`** (Function) — `f4se/xbyak/xbyak_mnemonic.h:448`
- **`bts`** (Function) — `f4se/xbyak/xbyak_mnemonic.h:446`

## Key Symbols

| Symbol | Type | File | Line |
|--------|------|------|------|
| `Operand` | Class | `f4se/xbyak/xbyak.h` | 323 |
| `Reg` | Class | `f4se/xbyak/xbyak.h` | 427 |
| `bsf` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 481 |
| `bsr` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 482 |
| `btc` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 450 |
| `btr` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 448 |
| `bts` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 446 |
| `bt` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 444 |
| `cmovae` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 208 |
| `cmova` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 244 |
| `cmovbe` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 232 |
| `cmovb` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 192 |
| `cmovc` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 196 |
| `cmove` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 216 |
| `cmovge` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 284 |
| `cmovg` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 300 |
| `cmovle` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 288 |
| `cmovl` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 272 |
| `cmovnae` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 200 |
| `cmovna` | Function | `f4se/xbyak/xbyak_mnemonic.h` | 236 |

## Execution Flows

| Flow | Type | Steps |
|------|------|-------|
| `Pinsrw → Is` | cross_community | 6 |
| `Pinsrw → Verify` | cross_community | 5 |
| `Pinsrw → UpdateRegField` | cross_community | 4 |
| `Pinsrw → GetIdx` | cross_community | 4 |
| `Pinsrw → IsBit` | cross_community | 4 |
| `CodeArray → AlignedMalloc` | cross_community | 3 |
| `CodeArray → AlignedFree` | cross_community | 3 |
| `OpMovxx → IsExt8bit` | cross_community | 3 |
| `RegExp → GetBit` | intra_community | 3 |
| `Cpu → __xgetbv` | intra_community | 3 |

## How to Explore

1. `context({name: "bsf"})` — see callers and callees
2. `query({search_query: "xbyak"})` — find related execution flows
3. Read key files listed above for implementation details
4. `explain({target: "<file or symbol>"})` — persisted taint findings (source→sink data flows), when indexed with `--pdg`
