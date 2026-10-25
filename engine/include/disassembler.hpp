#ifndef BYTECODE_VM_DISASSEMBLER_HPP
#define BYTECODE_VM_DISASSEMBLER_HPP

#include "chunk.hpp"
#include <iostream>
#include <string>

namespace bvm {

/// Disassembles an entire chunk to the provided output stream (Feature 4)
void disassemble_chunk(const Chunk& chunk, const std::string& name, std::ostream& out = std::cout);

/// Disassembles a single instruction at `offset`. Returns the offset of the next instruction.
size_t disassemble_instruction(const Chunk& chunk, size_t offset, std::ostream& out = std::cout);

} // namespace bvm

#endif // BYTECODE_VM_DISASSEMBLER_HPP
