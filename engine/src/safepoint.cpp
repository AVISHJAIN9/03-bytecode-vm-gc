#include "safepoint.hpp"
#include "vm.hpp"

namespace bvm {

void SafepointMechanism::enter_safepoint(VirtualMachine& vm) {
    SafepointAction action_to_run = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        safepoints_hit_.fetch_add(1, std::memory_order_relaxed);
        mutator_parked_.store(true, std::memory_order_release);

        // If an STW action was queued, grab it
        action_to_run = std::move(pending_action_);
        pending_action_ = nullptr;

        // If an action was queued, execute it while mutator is in a known-consistent safepoint
        if (action_to_run) {
            lock.unlock();
            action_to_run(vm);
            lock.lock();

            // Clear safepoint request and unpark
            safepoint_requested_.store(false, std::memory_order_release);
            mutator_parked_.store(false, std::memory_order_release);
            cv_.notify_all();
            return;
        }

        // Otherwise wait until resume() is invoked
        cv_.wait(lock, [this]() {
            return !safepoint_requested_.load(std::memory_order_acquire);
        });
        mutator_parked_.store(false, std::memory_order_release);
    }
}

} // namespace bvm
