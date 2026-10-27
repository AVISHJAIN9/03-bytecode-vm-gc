# Systems Engineering Glossary: Bytecode VM & Tracing Garbage Collector

### Bytecode
Instruction set architecture executed by a software interpreter rather than physical hardware.

### Tri-Color Marking
Garbage collection abstraction categorizing memory objects as white (unvisited), grey (visited, children pending), or black (retained).

### Stop-the-World
Phase in garbage collection where program execution is suspended to ensure heap consistency during root scanning.

### Stack Frame
Contiguous memory activation record representing a single function call, including local variables and operand stack.
