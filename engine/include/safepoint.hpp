#ifndef BYTECODE_VM_SAFEPOINT_HPP
#define BYTECODE_VM_SAFEPOINT_HPP

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <cstdint>

namespace bvm {

class VirtualMachine;

/// Stop-The-World (STW) Safepoint Coordinator (Feature 23)
class SafepointMechanism {
public:
    using SafepointAction = std::function<void(VirtualMachine&)>;

private:
    std::atomic<bool> safepoint_requested_{false};
    std::atomic<bool> mutator_parked_{false};
    std::atomic<uint64_t> safepoints_hit_{0};

    std::mutex mutex_;
    std::condition_variable cv_;
    SafepointAction pending_action_{nullptr};

public:
    SafepointMechanism() = default;
    ~SafepointMechanism() = default;

    SafepointMechanism(const SafepointMechanism&) = delete;
    SafepointMechanism& operator=(const SafepointMechanism&) = delete;

    /// Requests all mutators to pause at the next instruction safepoint
    void request_safepoint(SafepointAction action = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_action_ = std::move(action);
        safepoint_requested_.store(true, std::memory_order_release);
    }

    /// Resumes mutators after STW phase completion
    void resume() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            safepoint_requested_.store(false, std::memory_order_release);
            mutator_parked_.store(false, std::memory_order_release);
            pending_action_ = nullptr;
        }
        cv_.notify_all();
    }

    /// Fast-path query executed at loop back-edges and call/return boundaries
    [[nodiscard]] inline bool is_safepoint_requested() const noexcept {
        return safepoint_requested_.load(std::memory_order_relaxed);
    }

    /// Slow-path called when is_safepoint_requested() is true
    void enter_safepoint(VirtualMachine& vm);

    [[nodiscard]] bool is_mutator_parked() const noexcept {
        return mutator_parked_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t safepoint_hit_count() const noexcept {
        return safepoints_hit_.load(std::memory_order_relaxed);
    }

    void reset_hit_count() noexcept {
        safepoints_hit_.store(0, std::memory_order_relaxed);
    }
};

} // namespace bvm

#endif // BYTECODE_VM_SAFEPOINT_HPP
