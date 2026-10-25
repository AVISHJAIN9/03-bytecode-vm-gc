#include "vm.hpp"
#include "disassembler.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace bvm {

VirtualMachine::VirtualMachine() {
    stack_.reserve(STACK_MAX);
    frames_.reserve(FRAMES_MAX);
    reset();
}

void VirtualMachine::reset_execution() {
    stack_.clear();
    frames_.clear();
    open_upvalues_ = nullptr;
    try_handlers_.clear();
    error_message_.clear();
}

void VirtualMachine::reset() {
    reset_execution();
    globals_.clear();
    capabilities_.reset();
    quota_.reset();
    handle_manager_.clear();
    safepoint_.reset_hit_count();
}

void VirtualMachine::reset_telemetry() noexcept {
    opcode_counts_.fill(0);
    for (auto& row : opcode_transitions_) {
        row.fill(0);
    }
    last_opcode_ = static_cast<uint8_t>(Opcode::OP_NOP);
    total_instructions_ = 0;
}

uint64_t VirtualMachine::opcode_count(Opcode op) const noexcept {
    size_t idx = static_cast<size_t>(op);
    if (idx < opcode_counts_.size()) {
        return opcode_counts_[idx];
    }
    return 0;
}

std::vector<OpcodeTransition> VirtualMachine::top_transitions(size_t k) const {
    std::vector<OpcodeTransition> list;
    for (size_t from = 0; from < static_cast<size_t>(Opcode::OP_COUNT); ++from) {
        for (size_t to = 0; to < static_cast<size_t>(Opcode::OP_COUNT); ++to) {
            uint64_t c = opcode_transitions_[from][to];
            if (c > 0) {
                list.push_back(OpcodeTransition{
                    static_cast<Opcode>(from),
                    static_cast<Opcode>(to),
                    c
                });
            }
        }
    }
    std::sort(list.begin(), list.end(), [](const OpcodeTransition& a, const OpcodeTransition& b) {
        return a.count > b.count;
    });
    if (list.size() > k) {
        list.resize(k);
    }
    return list;
}

void VirtualMachine::dump_telemetry(std::ostream& out) const {
    out << "\n=======================================================\n";
    out << "           VM DISPATCH PROFILING & TELEMETRY           \n";
    out << "=======================================================\n";
    out << "Total Instructions Executed: " << total_instructions_ << "\n\n";

    out << std::left << std::setw(20) << "Opcode"
        << std::right << std::setw(15) << "Exec Count"
        << std::setw(15) << "Frequency (%)" << "\n";
    out << "-------------------------------------------------------\n";

    for (size_t i = 0; i < static_cast<size_t>(Opcode::OP_COUNT); ++i) {
        uint64_t cnt = opcode_counts_[i];
        if (cnt > 0) {
            auto op = static_cast<Opcode>(i);
            const auto& info = get_opcode_info(op);
            double pct = total_instructions_ > 0 ? (100.0 * cnt / total_instructions_) : 0.0;
            out << std::left << std::setw(20) << info.name
                << std::right << std::setw(15) << cnt
                << std::fixed << std::setprecision(2) << std::setw(14) << pct << "%\n";
        }
    }

    out << "\nTop Opcode Transition Pairs:\n";
    out << "-------------------------------------------------------\n";
    auto top = top_transitions(8);
    for (size_t i = 0; i < top.size(); ++i) {
        const auto& t = top[i];
        out << "  #" << (i + 1) << " "
            << std::left << std::setw(16) << get_opcode_info(t.from).name
            << " -> "
            << std::left << std::setw(16) << get_opcode_info(t.to).name
            << " : " << std::right << std::setw(10) << t.count << " times\n";
    }
    out << "=======================================================\n\n";
}

void VirtualMachine::runtime_error(const std::string& msg) {
    error_message_ = msg;
}

void VirtualMachine::push(Value val) {
    if (stack_.size() >= STACK_MAX) {
        throw std::overflow_error("Virtual machine stack overflow: exceeded limit of " + std::to_string(STACK_MAX) + " values");
    }
    stack_.push_back(std::move(val));
}

Value VirtualMachine::pop() {
    if (stack_.empty()) {
        throw std::underflow_error("Virtual machine stack underflow: pop attempted on empty stack");
    }
    Value val = std::move(stack_.back());
    stack_.pop_back();
    return val;
}

const Value& VirtualMachine::peek(size_t distance) const {
    if (distance >= stack_.size()) {
        throw std::out_of_range("Peek distance out of bounds: stack size " + std::to_string(stack_.size()));
    }
    return stack_[stack_.size() - 1 - distance];
}

const Value& VirtualMachine::last_stack_value() const {
    if (stack_.empty()) {
        throw std::runtime_error("No values on stack");
    }
    return stack_.back();
}

std::shared_ptr<ObjUpvalue> VirtualMachine::capture_upvalue(Value* local) {
    std::shared_ptr<ObjUpvalue> prev = nullptr;
    std::shared_ptr<ObjUpvalue> curr = open_upvalues_;

    // Walk open upvalues list sorted by pointer address descending
    while (curr != nullptr && curr->location > local) {
        prev = curr;
        curr = curr->next;
    }

    // Reuse existing open upvalue if one already references this stack slot
    if (curr != nullptr && curr->location == local) {
        return curr;
    }

    auto created = std::make_shared<ObjUpvalue>(local);
    created->next = curr;
    if (prev == nullptr) {
        open_upvalues_ = created;
    } else {
        prev->next = created;
    }
    return created;
}

void VirtualMachine::close_upvalues(Value* last_slot) {
    while (open_upvalues_ != nullptr && open_upvalues_->location >= last_slot) {
        auto upvalue = open_upvalues_;
        upvalue->close();
        if (upvalue->closed.is_object() && upvalue->closed.as_object() != nullptr) {
            inc_gc_.notify_field_write(upvalue->closed.as_object(), upvalue->closed);
        }
        open_upvalues_ = upvalue->next;
        upvalue->next = nullptr;
    }
}

InterpretResult VirtualMachine::interpret(const Chunk& chunk, bool verify_first) {
    auto fn = std::make_shared<ObjFunction>("<script>", 0);
    fn->chunk = chunk;
    return interpret(fn, verify_first);
}

InterpretResult VirtualMachine::interpret(std::shared_ptr<ObjFunction> function, bool verify_first) {
    if (verify_first) {
        auto ver_res = BytecodeVerifier::verify(function->chunk);
        if (!ver_res.is_valid) {
            error_message_ = ver_res.message;
            return InterpretResult::COMPILE_ERROR;
        }
    }
    auto closure = std::make_shared<ObjClosure>(function);
    return interpret(closure);
}

InterpretResult VirtualMachine::interpret(std::shared_ptr<ObjClosure> closure) {
    reset_execution();
    quota_.start_execution_timer();

    // Push callee closure at stack slot 0
    push(Value(closure));

    // Allocate root call frame
    frames_.emplace_back(closure, 0, 0);

    try {
        return run();
    } catch (const ResourceLimitException& e) {
        runtime_error(e.what());
        return InterpretResult::RUNTIME_ERROR;
    } catch (const SecurityException& e) {
        runtime_error(e.what());
        return InterpretResult::RUNTIME_ERROR;
    }
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-label-as-value"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wgnu-label-as-value"
#endif

InterpretResult VirtualMachine::run() {
    if (frames_.empty()) {
        return InterpretResult::OK;
    }

    CallFrame* frame = &frames_.back();
    const uint8_t* code = frame->closure->function->chunk.code.data();
    size_t code_size = frame->closure->function->chunk.code.size();
    ConstantPool* constants = &frame->closure->function->chunk.constants;

    #define RECORD_OPCODE(op_byte) do { \
        opcode_counts_[op_byte]++; \
        opcode_transitions_[last_opcode_][op_byte]++; \
        last_opcode_ = op_byte; \
        total_instructions_++; \
    } while (0)

    #define READ_BYTE() (code[frame->ip++])
    #define READ_U16() (frame->ip += 2, static_cast<uint16_t>(code[frame->ip - 2] | (code[frame->ip - 1] << 8)))
    #define READ_I16() static_cast<int16_t>(READ_U16())

    #define UPDATE_FRAME_CONTEXT() do { \
        frame = &frames_.back(); \
        code = frame->closure->function->chunk.code.data(); \
        code_size = frame->closure->function->chunk.code.size(); \
        constants = &frame->closure->function->chunk.constants; \
    } while (0)

#if defined(__GNUC__) || defined(__clang__)
    // Direct Threaded Dispatch via Computed GOTO (Feature 11)
    static const void* const dispatch_table[] = {
        &&do_OP_NOP,
        &&do_OP_CONST,
        &&do_OP_NIL,
        &&do_OP_TRUE,
        &&do_OP_FALSE,
        &&do_OP_POP,
        &&do_OP_DUP,
        &&do_OP_SWAP,
        &&do_OP_ADD,
        &&do_OP_SUB,
        &&do_OP_MUL,
        &&do_OP_DIV,
        &&do_OP_MOD,
        &&do_OP_NEG,
        &&do_OP_BIT_AND,
        &&do_OP_BIT_OR,
        &&do_OP_BIT_XOR,
        &&do_OP_BIT_NOT,
        &&do_OP_EQ,
        &&do_OP_NEQ,
        &&do_OP_LT,
        &&do_OP_LTE,
        &&do_OP_GT,
        &&do_OP_GTE,
        &&do_OP_NOT,
        &&do_OP_JUMP,
        &&do_OP_JUMP_IF_TRUE,
        &&do_OP_JUMP_IF_FALSE,
        &&do_OP_HALT,
        &&do_OP_GET_LOCAL,
        &&do_OP_SET_LOCAL,
        &&do_OP_CALL,
        &&do_OP_RETURN,
        &&do_OP_CLOSURE,
        &&do_OP_GET_UPVALUE,
        &&do_OP_SET_UPVALUE,
        &&do_OP_CLOSE_UPVALUE,
        &&do_OP_PUSH_TRY,
        &&do_OP_POP_TRY,
        &&do_OP_THROW,
        &&do_OP_GET_GLOBAL,
        &&do_OP_SET_GLOBAL,
        &&do_OP_CALL_NATIVE,
        &&do_OP_GET_PROPERTY,
        &&do_OP_SET_PROPERTY,
        &&do_OP_GET_ELEMENT,
        &&do_OP_SET_ELEMENT
    };

    #define DISPATCH() do { \
        if (frame->ip >= code_size) goto do_OP_HALT; \
        if (quota_.gas_metering_enabled) { \
            quota_.consume_gas(1); \
        } \
        uint8_t op_byte = code[frame->ip++]; \
        RECORD_OPCODE(op_byte); \
        goto *dispatch_table[op_byte]; \
    } while (0)

    DISPATCH();
#else
    // Fallback standard switch dispatch loop
    #define DISPATCH() break
    while (true) {
        if (frame->ip >= code_size) return InterpretResult::OK;
        if (quota_.gas_metering_enabled) {
            quota_.consume_gas(1);
        }
        uint8_t op_byte = code[frame->ip++];
        RECORD_OPCODE(op_byte);
        switch (static_cast<Opcode>(op_byte)) {
#endif

    do_OP_NOP: {
        DISPATCH();
    }

    do_OP_CONST: {
        uint16_t idx = READ_U16();
        push(constants->get(idx));
        DISPATCH();
    }

    do_OP_NIL: {
        push(Value());
        DISPATCH();
    }

    do_OP_TRUE: {
        push(Value(true));
        DISPATCH();
    }

    do_OP_FALSE: {
        push(Value(false));
        DISPATCH();
    }

    do_OP_POP: {
        pop();
        DISPATCH();
    }

    do_OP_DUP: {
        push(peek(0));
        DISPATCH();
    }

    do_OP_SWAP: {
        {
            Value a = pop();
            Value b = pop();
            push(std::move(a));
            push(std::move(b));
        }
        DISPATCH();
    }

    do_OP_ADD: {
        {
            Value b = pop();
            Value a = pop();
            if (a.is_string() || b.is_string()) {
                push(Value(a.to_string() + b.to_string()));
            } else if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() + b.as_int()));
            } else if (a.is_number() && b.is_number()) {
                push(Value(a.as_number() + b.as_number()));
            } else {
                runtime_error("Operands for '+' must be numbers or strings");
                return InterpretResult::RUNTIME_ERROR;
            }
        }
        DISPATCH();
    }

    do_OP_SUB: {
        {
            Value b = pop();
            Value a = pop();
            if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() - b.as_int()));
            } else if (a.is_number() && b.is_number()) {
                push(Value(a.as_number() - b.as_number()));
            } else {
                runtime_error("Operands for '-' must be numbers");
                return InterpretResult::RUNTIME_ERROR;
            }
        }
        DISPATCH();
    }

    do_OP_MUL: {
        {
            Value b = pop();
            Value a = pop();
            if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() * b.as_int()));
            } else if (a.is_number() && b.is_number()) {
                push(Value(a.as_number() * b.as_number()));
            } else {
                runtime_error("Operands for '*' must be numbers");
                return InterpretResult::RUNTIME_ERROR;
            }
        }
        DISPATCH();
    }

    do_OP_DIV: {
        {
            Value b = pop();
            Value a = pop();
            if (b.is_int() && b.as_int() == 0) {
                runtime_error("Division by zero");
                return InterpretResult::RUNTIME_ERROR;
            }
            if (b.is_float() && b.as_float() == 0.0) {
                runtime_error("Division by zero");
                return InterpretResult::RUNTIME_ERROR;
            }
            if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() / b.as_int()));
            } else {
                push(Value(a.as_number() / b.as_number()));
            }
        }
        DISPATCH();
    }

    do_OP_MOD: {
        {
            Value b = pop();
            Value a = pop();
            if (!a.is_int() || !b.is_int()) {
                runtime_error("Operands for '%' must be integers");
                return InterpretResult::RUNTIME_ERROR;
            }
            if (b.as_int() == 0) {
                runtime_error("Modulo by zero");
                return InterpretResult::RUNTIME_ERROR;
            }
            push(Value(a.as_int() % b.as_int()));
        }
        DISPATCH();
    }

    do_OP_NEG: {
        {
            Value val = pop();
            if (val.is_int()) {
                push(Value(-val.as_int()));
            } else if (val.is_float()) {
                push(Value(-val.as_float()));
            } else {
                runtime_error("Operand for unary '-' must be a number");
                return InterpretResult::RUNTIME_ERROR;
            }
        }
        DISPATCH();
    }

    do_OP_BIT_AND: {
        {
            Value b = pop();
            Value a = pop();
            if (!a.is_int() || !b.is_int()) {
                runtime_error("Operands for bitwise AND must be integers");
                return InterpretResult::RUNTIME_ERROR;
            }
            push(Value(a.as_int() & b.as_int()));
        }
        DISPATCH();
    }

    do_OP_BIT_OR: {
        {
            Value b = pop();
            Value a = pop();
            if (!a.is_int() || !b.is_int()) {
                runtime_error("Operands for bitwise OR must be integers");
                return InterpretResult::RUNTIME_ERROR;
            }
            push(Value(a.as_int() | b.as_int()));
        }
        DISPATCH();
    }

    do_OP_BIT_XOR: {
        {
            Value b = pop();
            Value a = pop();
            if (!a.is_int() || !b.is_int()) {
                runtime_error("Operands for bitwise XOR must be integers");
                return InterpretResult::RUNTIME_ERROR;
            }
            push(Value(a.as_int() ^ b.as_int()));
        }
        DISPATCH();
    }

    do_OP_BIT_NOT: {
        {
            Value val = pop();
            if (!val.is_int()) {
                runtime_error("Operand for bitwise NOT must be integer");
                return InterpretResult::RUNTIME_ERROR;
            }
            push(Value(~val.as_int()));
        }
        DISPATCH();
    }

    do_OP_EQ: {
        {
            Value b = pop();
            Value a = pop();
            push(Value(a == b));
        }
        DISPATCH();
    }

    do_OP_NEQ: {
        {
            Value b = pop();
            Value a = pop();
            push(Value(a != b));
        }
        DISPATCH();
    }

    do_OP_LT: {
        {
            Value b = pop();
            Value a = pop();
            if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() < b.as_int()));
            } else {
                push(Value(a.as_number() < b.as_number()));
            }
        }
        DISPATCH();
    }

    do_OP_LTE: {
        {
            Value b = pop();
            Value a = pop();
            if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() <= b.as_int()));
            } else {
                push(Value(a.as_number() <= b.as_number()));
            }
        }
        DISPATCH();
    }

    do_OP_GT: {
        {
            Value b = pop();
            Value a = pop();
            if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() > b.as_int()));
            } else {
                push(Value(a.as_number() > b.as_number()));
            }
        }
        DISPATCH();
    }

    do_OP_GTE: {
        {
            Value b = pop();
            Value a = pop();
            if (a.is_int() && b.is_int()) {
                push(Value(a.as_int() >= b.as_int()));
            } else {
                push(Value(a.as_number() >= b.as_number()));
            }
        }
        DISPATCH();
    }

    do_OP_NOT: {
        {
            Value val = pop();
            push(Value(!val.is_truthy()));
        }
        DISPATCH();
    }

    do_OP_JUMP: {
        int16_t offset = READ_I16();
        if (offset < 0) {
            VMGCHooks::poll_safepoint(*this, safepoint_);
        }
        frame->ip += offset;
        DISPATCH();
    }

    do_OP_JUMP_IF_TRUE: {
        int16_t offset = READ_I16();
        bool cond = false;
        {
            Value val = pop();
            cond = val.is_truthy();
        }
        if (cond) {
            if (offset < 0) {
                VMGCHooks::poll_safepoint(*this, safepoint_);
            }
            frame->ip += offset;
        }
        DISPATCH();
    }

    do_OP_JUMP_IF_FALSE: {
        int16_t offset = READ_I16();
        bool cond = false;
        {
            Value val = pop();
            cond = val.is_truthy();
        }
        if (!cond) {
            if (offset < 0) {
                VMGCHooks::poll_safepoint(*this, safepoint_);
            }
            frame->ip += offset;
        }
        DISPATCH();
    }

    do_OP_HALT: {
        return InterpretResult::OK;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Features 7 & 8: Local Variables, Call Frames, Closures & Upvalues
    // ─────────────────────────────────────────────────────────────────────────
    do_OP_GET_LOCAL: {
        uint16_t slot = READ_U16();
        size_t abs_slot = frame->slot_offset + slot;
        if (abs_slot >= stack_.size()) {
            runtime_error("Local slot index out of bounds: " + std::to_string(slot));
            return InterpretResult::RUNTIME_ERROR;
        }
        push(stack_[abs_slot]);
        DISPATCH();
    }

    do_OP_SET_LOCAL: {
        uint16_t slot = READ_U16();
        size_t abs_slot = frame->slot_offset + slot;
        if (abs_slot >= stack_.size()) {
            runtime_error("Local slot index out of bounds: " + std::to_string(slot));
            return InterpretResult::RUNTIME_ERROR;
        }
        stack_[abs_slot] = peek(0);
        DISPATCH();
    }

    do_OP_CALL: {
        {
            uint8_t arg_count = READ_BYTE();
            if (stack_.size() < static_cast<size_t>(arg_count + 1)) {
                runtime_error("Stack underflow during function call");
                return InterpretResult::RUNTIME_ERROR;
            }
            size_t callee_idx = stack_.size() - 1 - arg_count;

            std::shared_ptr<ObjClosure> callee_closure;
            Value& callee_val = stack_[callee_idx];
            if (callee_val.is_closure()) {
                callee_closure = callee_val.as_closure();
            } else if (callee_val.is_function()) {
                callee_closure = std::make_shared<ObjClosure>(callee_val.as_function());
                callee_val = Value(callee_closure);
            } else {
                runtime_error("Cannot call non-callable value: " + callee_val.to_string());
                return InterpretResult::RUNTIME_ERROR;
            }

            if (callee_closure->function->arity != arg_count) {
                runtime_error("Arity mismatch: expected " + std::to_string(callee_closure->function->arity) +
                              " arguments, got " + std::to_string(arg_count));
                return InterpretResult::RUNTIME_ERROR;
            }

            if (frames_.size() >= FRAMES_MAX) {
                runtime_error("Call stack overflow: exceeded limit of " + std::to_string(FRAMES_MAX) + " frames");
                return InterpretResult::RUNTIME_ERROR;
            }

            frames_.emplace_back(std::move(callee_closure), 0, callee_idx);
        }
        UPDATE_FRAME_CONTEXT();
        DISPATCH();
    }

    do_OP_RETURN: {
        {
            Value result = pop();
            // Close any open upvalues anchored to stack slots in this call frame
            close_upvalues(&stack_[frame->slot_offset]);

            size_t discard_to = frame->slot_offset;
            frames_.pop_back();

            if (frames_.empty()) {
                stack_.clear();
                push(std::move(result));
                return InterpretResult::OK;
            }

            stack_.resize(discard_to);
            push(std::move(result));
        }
        UPDATE_FRAME_CONTEXT();
        VMGCHooks::poll_safepoint(*this, safepoint_);
        DISPATCH();
    }

    do_OP_CLOSURE: {
        {
            uint16_t fn_idx = READ_U16();
            auto fn = constants->get(fn_idx).as_function();
            auto closure = std::make_shared<ObjClosure>(fn);

            for (size_t i = 0; i < fn->upvalue_descs.size(); ++i) {
                uint8_t is_local = READ_BYTE();
                uint16_t idx = READ_U16();
                if (is_local == 1) {
                    Value* slot = &stack_[frame->slot_offset + idx];
                    closure->upvalues[i] = capture_upvalue(slot);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[idx];
                }
            }
            push(Value(std::move(closure)));
        }
        DISPATCH();
    }

    do_OP_GET_UPVALUE: {
        uint16_t idx = READ_U16();
        if (idx >= frame->closure->upvalues.size() || !frame->closure->upvalues[idx]) {
            runtime_error("Invalid upvalue index: " + std::to_string(idx));
            return InterpretResult::RUNTIME_ERROR;
        }
        push(*frame->closure->upvalues[idx]->location);
        DISPATCH();
    }

    do_OP_SET_UPVALUE: {
        uint16_t idx = READ_U16();
        if (idx >= frame->closure->upvalues.size() || !frame->closure->upvalues[idx]) {
            runtime_error("Invalid upvalue index: " + std::to_string(idx));
            return InterpretResult::RUNTIME_ERROR;
        }
        *frame->closure->upvalues[idx]->location = peek(0);
        DISPATCH();
    }

    do_OP_CLOSE_UPVALUE: {
        close_upvalues(&stack_.back());
        pop();
        DISPATCH();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Feature 9: Structured Exception Handling & Frame Unwinding
    // ─────────────────────────────────────────────────────────────────────────
    do_OP_PUSH_TRY: {
        int16_t offset = READ_I16();
        size_t handler_ip = frame->ip + offset;
        try_handlers_.push_back(TryHandler{frames_.size() - 1, handler_ip, stack_.size()});
        DISPATCH();
    }

    do_OP_POP_TRY: {
        if (!try_handlers_.empty()) {
            try_handlers_.pop_back();
        }
        DISPATCH();
    }

    do_OP_THROW: {
        auto handler_opt = ExceptionUnwinder::find_matching_handler(try_handlers_, frames_.size());
        if (handler_opt.has_value()) {
            {
                Value thrown_val = pop();
                const auto& handler = *handler_opt;

                // Unwind call frames down to handler frame
                while (frames_.size() - 1 > handler.frame_index) {
                    close_upvalues(&stack_[frames_.back().slot_offset]);
                    frames_.pop_back();
                }

                // Close any upvalues created above handler stack height
                if (stack_.size() > handler.stack_depth) {
                    close_upvalues(&stack_[handler.stack_depth]);
                }

                // Restore operand stack height and pass exception to catch block
                stack_.resize(handler.stack_depth);
                push(std::move(thrown_val));
            }
            size_t target_ip = handler_opt->handler_ip;
            UPDATE_FRAME_CONTEXT();
            frame->ip = target_ip;
            DISPATCH();
        } else {
            Value thrown_val = pop();
            std::string bt = Backtrace::format(frames_, "Unhandled Exception: " + thrown_val.to_string());
            runtime_error(bt);
            return InterpretResult::RUNTIME_ERROR;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Features 13 & 14: Global Variables & Native/FFI Interface
    // ─────────────────────────────────────────────────────────────────────────
    do_OP_GET_GLOBAL: {
        uint16_t const_idx = READ_U16();
        const std::string& name = constants->get(const_idx).as_string();
        auto it = globals_.find(name);
        if (it == globals_.end()) {
            runtime_error("Undefined global variable '" + name + "'");
            return InterpretResult::RUNTIME_ERROR;
        }
        push(it->second);
        DISPATCH();
    }

    do_OP_SET_GLOBAL: {
        uint16_t const_idx = READ_U16();
        const std::string& name = constants->get(const_idx).as_string();
        globals_[name] = peek(0);
        DISPATCH();
    }

    do_OP_CALL_NATIVE: {
        uint16_t fn_idx = READ_U16();
        uint8_t argc = READ_BYTE();
        if (stack_.size() < static_cast<size_t>(argc)) {
            runtime_error("Stack underflow during native function call");
            return InterpretResult::RUNTIME_ERROR;
        }
        size_t args_start = stack_.size() - argc;

        bool native_threw = false;
        {
            NativeContext ctx(*this, capabilities_, &stack_[args_start], argc);
            Value result;
            std::string err_msg;

            try {
                result = native_registry_.invoke(fn_idx, ctx);
            } catch (const SecurityException& e) {
                native_threw = true;
                err_msg = e.what();
            } catch (const ResourceLimitException&) {
                throw; // Quota breaches bypass bytecode catch blocks
            } catch (const std::exception& e) {
                native_threw = true;
                err_msg = e.what();
            }

            stack_.resize(args_start);
            if (native_threw) {
                push(Value(std::move(err_msg)));
            } else {
                push(std::move(result));
            }
        }

        if (native_threw) {
            goto do_OP_THROW;
        }
        DISPATCH();
    }

    do_OP_GET_PROPERTY: {
        {
            uint16_t const_idx = READ_U16();
            const std::string& prop_name = constants->get(const_idx).as_string();
            Value target = pop();
            if (!target.is_object() || target.as_object() == nullptr ||
                target.as_object()->obj_type() != ObjType::INSTANCE) {
                runtime_error("Only object instances have properties");
                return InterpretResult::RUNTIME_ERROR;
            }
            auto* inst = reinterpret_cast<ObjInstance*>(target.as_object());
            const Value* val = inst->get_field(prop_name);
            if (val) {
                push(*val);
            } else {
                push(Value());
            }
        }
        DISPATCH();
    }

    do_OP_SET_PROPERTY: {
        {
            uint16_t const_idx = READ_U16();
            const std::string& prop_name = constants->get(const_idx).as_string();
            Value val = pop();
            Value target = pop();
            if (!target.is_object() || target.as_object() == nullptr ||
                target.as_object()->obj_type() != ObjType::INSTANCE) {
                runtime_error("Only object instances have properties");
                return InterpretResult::RUNTIME_ERROR;
            }
            auto* inst = reinterpret_cast<ObjInstance*>(target.as_object());
            inst->set_field(prop_name, val);
            inc_gc_.notify_field_write(&inst->header, val);
            push(val);
        }
        DISPATCH();
    }

    do_OP_GET_ELEMENT: {
        {
            Value idx_val = pop();
            Value arr_val = pop();
            if (!idx_val.is_int()) {
                runtime_error("Array index must be an integer");
                return InterpretResult::RUNTIME_ERROR;
            }
            if (!arr_val.is_object() || arr_val.as_object() == nullptr ||
                arr_val.as_object()->obj_type() != ObjType::ARRAY) {
                runtime_error("Target is not an array");
                return InterpretResult::RUNTIME_ERROR;
            }
            auto* arr = reinterpret_cast<ObjArray*>(arr_val.as_object());
            int64_t idx = idx_val.as_int();
            if (idx < 0 || static_cast<size_t>(idx) >= arr->elements.size()) {
                runtime_error("Array index out of bounds: " + std::to_string(idx));
                return InterpretResult::RUNTIME_ERROR;
            }
            push(arr->elements[static_cast<size_t>(idx)]);
        }
        DISPATCH();
    }

    do_OP_SET_ELEMENT: {
        {
            Value val = pop();
            Value idx_val = pop();
            Value arr_val = pop();
            if (!idx_val.is_int()) {
                runtime_error("Array index must be an integer");
                return InterpretResult::RUNTIME_ERROR;
            }
            if (!arr_val.is_object() || arr_val.as_object() == nullptr ||
                arr_val.as_object()->obj_type() != ObjType::ARRAY) {
                runtime_error("Target is not an array");
                return InterpretResult::RUNTIME_ERROR;
            }
            auto* arr = reinterpret_cast<ObjArray*>(arr_val.as_object());
            int64_t idx = idx_val.as_int();
            if (idx < 0) {
                runtime_error("Array index out of bounds: " + std::to_string(idx));
                return InterpretResult::RUNTIME_ERROR;
            }
            size_t uidx = static_cast<size_t>(idx);
            if (uidx >= arr->elements.size()) {
                arr->elements.resize(uidx + 1);
            }
            arr->elements[uidx] = val;
            inc_gc_.notify_field_write(&arr->header, val);
            push(val);
        }
        DISPATCH();
    }

#if !defined(__GNUC__) && !defined(__clang__)
        } // switch
    } // while
#endif

    return InterpretResult::OK;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace bvm
