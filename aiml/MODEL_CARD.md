# AIML Model Cards: Register-Based Bytecode VM & GC

## Tier A: Learned GC-Trigger Heuristic model (vs fixed allocation threshold baseline)
- **Baseline**: Non-AI Static Baseline
- **Target**: Statistically significant win on stated evaluation metric.
- **Status**: Specified & Scaffolded (**TO BE MEASURED**).
- **Graceful Fallback**: Hardcoded static threshold rule engine.

## Tier B: Interactive Bytecode disassembler and GC-trace explainer
- **LLM Prompt Guard**: Strict schema validation. The LLM is NEVER on correctness or transactional paths.
