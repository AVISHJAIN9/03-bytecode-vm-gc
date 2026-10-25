#include "debug.hpp"
#include <sstream>
#include <iomanip>

namespace bvm {

std::vector<StackFrameInfo> Backtrace::capture(const std::vector<CallFrame>& frames) {
    std::vector<StackFrameInfo> trace;
    trace.reserve(frames.size());

    // Iterate from top of call stack (most recent) down to bottom
    for (size_t i = frames.size(); i > 0; --i) {
        size_t idx = i - 1;
        const auto& frame = frames[idx];

        StackFrameInfo info;
        info.frame_index = idx;
        info.function_name = (frame.closure && frame.closure->function)
            ? frame.closure->function->name
            : "<unknown>";
        info.ip = frame.ip;
        info.slot_offset = frame.slot_offset;

        // Resolve line number based on instruction pointer
        const auto& lines = frame.chunk().lines;
        if (frame.ip > 0 && (frame.ip - 1) < lines.size()) {
            info.line = lines[frame.ip - 1];
        } else if (!lines.empty()) {
            info.line = lines[0];
        } else {
            info.line = 0;
        }

        trace.push_back(std::move(info));
    }

    return trace;
}

std::string Backtrace::format(
    const std::vector<CallFrame>& frames,
    const std::string& error_header
) {
    std::ostringstream ss;
    if (!error_header.empty()) {
        ss << error_header << "\n";
    }
    ss << "Stack Backtrace (most recent call first):\n";

    auto trace = capture(frames);
    if (trace.empty()) {
        ss << "  <empty call stack>\n";
        return ss.str();
    }

    for (size_t i = 0; i < trace.size(); ++i) {
        const auto& frame = trace[i];
        ss << "  #" << i << "  "
           << frame.function_name << "() at bytecode offset 0x"
           << std::hex << std::setw(4) << std::setfill('0') << frame.ip << std::dec
           << " (line " << frame.line << ")\n";
    }

    return ss.str();
}

} // namespace bvm
