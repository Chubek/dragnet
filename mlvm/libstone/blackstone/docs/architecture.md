# Blackstone Architecture

## Overview

Blackstone is a declarative database of optimization kernels. Its architecture separates
concerns into distinct layers:

```
┌─────────────────────────────────────────────────────────┐
│                    Kernel Database                       │
│         (150 .olisp files, organized by attributes)      │
├─────────────────────────────────────────────────────────┤
│              Optilisp Language Implementation            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐  │
│  │  Reader  │→│ Semantic │→│ Validator│→│ Query  │  │
│  │ (Layer 1)│  │ Analyzer │  │(Layers 2-6)│ │ Engine │  │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘  │
├─────────────────────────────────────────────────────────┤
│              Registries (Capabilities, Analyses, Monoids)│
├─────────────────────────────────────────────────────────┤
│              JSON Schemas (kernel, capability, ...)      │
└─────────────────────────────────────────────────────────┘
```

## Validation layers

### Layer 1: Syntax

- Balanced parentheses
- Well-formed S-expressions
- Valid strings and escapes
- Source locations retained for diagnostics

### Layer 2: Structural

- Required fields present
- Field types correct
- Stage recognized
- One primary optimization objective
- No duplicate singleton sections

### Layer 3: References

- Kernel IDs unique
- Referenced kernels exist
- Required capabilities defined
- Required analyses defined
- Referenced monoids exist

### Layer 4: Semantic contract

- Stage compatibility
- Contradictory preconditions detected
- Invalidated analyses not declared preserved
- Prerequisites not forbidden

### Layer 5: Transformation

- Verb vocabulary recognized
- Stage-appropriate verbs
- Transform arguments present
- Limitation: no executor available (reported, not hidden)

### Layer 6: Database

- Deterministic load (sorted path order)
- Indexes buildable
- Cross-kernel references resolve
- All entries pass configured policy

## Directory organization

Kernels are organized by **attributes**:

```
kernels/
├── hir/                    # High-level IR
│   ├── constant-folding/   #   Optimization category
│   ├── algebraic-simplification/
│   └── ...
├── mir/                    # Mid-level IR
│   ├── loop-optimization/
│   ├── ssa-optimization/
│   └── ...
└── lir/                    # Low-level IR
    ├── instruction-combining/
    ├── target-specific/
    └── ...
```

## Design principles

1. **One kernel, one optimization**
2. **Declarative descriptions**
3. **Explicit applicability**
4. **Explicit IR stage**
5. **Explicit information requirements**
6. **Correctness first**
7. **Machine-readable metadata**
8. **First-class documentation**
9. **Composability without hidden assumptions**
10. **Separation of concerns**
