#include "exceptions.hpp"

namespace bvm {

std::optional<TryHandler> ExceptionUnwinder::find_matching_handler(
    std::vector<TryHandler>& handlers,
    size_t current_frames_count
) {
    while (!handlers.empty()) {
        TryHandler h = handlers.back();
        handlers.pop_back();

        // Ensure the handler belongs to an active frame on the call stack
        if (h.frame_index < current_frames_count) {
            return h;
        }
    }
    return std::nullopt;
}

} // namespace bvm
