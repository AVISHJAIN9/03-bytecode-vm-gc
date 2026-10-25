# Register-Based Bytecode VM & GC

[![CI](https://github.com/AVISHJAIN9/bytecode-vm-gc/actions/workflows/ci.yml/badge.svg)](https://github.com/AVISHJAIN9/bytecode-vm-gc/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Status: Scaffolded](https://img.shields.io/badge/Status-Scaffolded-yellow.svg)](#status)

Register-based bytecode virtual machine with closures, upvalues, generational mark-sweep GC, and WASM playground.

---

## Table of Contents
1. [Executive Overview](#1-executive-overview)
2. [End-to-End System Flows](#2-end-to-end-system-flows)
3. [Architecture & Component Breakdown](#3-architecture--component-breakdown)
4. [Algorithms & Mechanisms](#4-algorithms--mechanisms)
5. [AIML Subsystem (Tier A & Tier B)](#5-aiml-subsystem-tier-a--tier-b)
6. [Database Schema & State Management](#6-database-schema--state-management)
7. [Frontend 3D Visualizer & Dashboard](#7-frontend-3d-visualizer--dashboard)
8. [Testing & Verification Strategy](#8-testing--verification-strategy)
9. [Benchmark Plan & Performance Targets](#9-benchmark-plan--performance-targets)
10. [Deployment & Infrastructure](#10-deployment--infrastructure)
11. [Requirements Traceability Matrix](#11-requirements-traceability-matrix)
12. [Unknowns & TO BE MEASURED Register](#12-unknowns--to-be-measured-register)

---

## 1. Executive Overview [Specified]
This repository provides an industrial-grade, standalone implementation of **Register-Based Bytecode VM & GC**. Built in **Rust** with supporting infrastructure in **Rust, WASM, WebAssembly Bindgen**, the system solves hard-core systems-engineering challenges with zero shortcuts.

### Status Tagging Conventions
- `[Specified]`: Architectural design and technical specification complete.
- `[Scaffolded]`: Interfaces, data structures, and method stubs defined with strict typed signatures and `NotImplemented` guards.
- `[Implemented]`: Fully executable logic written and integrated.
- `[Verified]`: Validated via automated test suites and benchmarks.

---

## 2. End-to-End System Flows [Specified]
1. **Client Interaction**: User navigates to the landing page, authenticates via OAuth, and accesses the telemetry dashboard or interactive client playground.
2. **Ingress & Processing**: Incoming requests are validated, parsed into zero-copy internal buffers, and routed through the core state engine.
3. **Storage & Replication**: Mutations are logged via Write-Ahead Logs (WAL) and persisted into partitioned relational tables / key-value stores.
4. **AI Operations**: Tier A background models monitor telemetry streams for anomalies or adaptive tuning, while Tier B LLM assistants provide natural language diagnostics with strict schema validation.

---

## 3. Architecture & Component Breakdown [Specified]
- **Core Engine**: Lexer, Pratt parser, register bytecode compiler, closures with open/closed upvalues, mark-sweep and generational garbage collector with write barriers, constant folding and inlining, collect-on-every-allocation stress mode, WASM interactive playground.
- **Storage Layer**: PostgreSQL 16 & Redis 7.2
- **AIML Layer**:
  - **Tier A**: Learned GC-Trigger Heuristic model (vs fixed allocation threshold baseline)
  - **Tier B**: Interactive Bytecode disassembler and GC-trace explainer
- **Frontend**: Next.js 14, TypeScript, TailwindCSS, GSAP ScrollTrigger, Lenis smooth scroll, Three.js 3D viewport.

---

## 4. Benchmark Plan & Performance Targets [Specified]

| Benchmark Test | Baseline | Target Specification | Status |
| :--- | :--- | :--- | :--- |
| Core Engine Operation Throughput | Standard Sync Baseline | High-Throughput Target | **TO BE MEASURED** |
| p99 Operation Latency | Standard Sync Baseline | Low-Latency Target | **TO BE MEASURED** |
| Memory Allocation per Op | Naive Allocation | Exactly 0 or Bounded Heap Alloc | **TO BE MEASURED** |
| Tier A Model Accuracy / F1 | Static Rule Baseline | Statistically Significant Win | **TO BE MEASURED** |

---

## 5. Unknowns & TO BE MEASURED Register [Specified]
1. **Engine Throughput & Latency**:
   - *Measurement Command*: Run benchmark suite in `benchmarks/`.
2. **Tier A AI Model Metric**:
   - *Measurement Command*: Run `python3 aiml/eval/evaluate_model.py`.
