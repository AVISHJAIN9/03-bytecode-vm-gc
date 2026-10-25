# 08 Security And Failure Modes: Register-Based Bytecode VM & GC

## 1. Specification Overview [Specified]
Register-based bytecode virtual machine with closures, upvalues, generational mark-sweep GC, and WASM playground.

## 2. Core Mechanisms [Specified]
Lexer, Pratt parser, register bytecode compiler, closures with open/closed upvalues, mark-sweep and generational garbage collector with write barriers, constant folding and inlining, collect-on-every-allocation stress mode, WASM interactive playground.

## 3. Unknowns & Benchmarks
All performance numbers, throughput rates, and latency figures are marked **TO BE MEASURED** until run on standard hardware.
