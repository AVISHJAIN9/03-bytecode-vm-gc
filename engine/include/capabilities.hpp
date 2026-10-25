#ifndef BYTECODE_VM_CAPABILITIES_HPP
#define BYTECODE_VM_CAPABILITIES_HPP

#include <cstdint>
#include <string>
#include <stdexcept>
#include <sstream>

namespace bvm {

/// Granular Capability Flags for Sandboxed Execution (Feature 14)
enum class Capability : uint32_t {
    NONE        = 0,
    FS_READ     = 1 << 0,
    FS_WRITE    = 1 << 1,
    NETWORK     = 1 << 2,
    CLOCK       = 1 << 3,
    SUBPROCESS  = 1 << 4,
    ENV_VARS    = 1 << 5,
    ALL         = 0xFFFFFFFF
};

[[nodiscard]] inline constexpr Capability operator|(Capability a, Capability b) noexcept {
    return static_cast<Capability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

[[nodiscard]] inline constexpr Capability operator&(Capability a, Capability b) noexcept {
    return static_cast<Capability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

[[nodiscard]] inline constexpr Capability operator~(Capability a) noexcept {
    return static_cast<Capability>(~static_cast<uint32_t>(a));
}

inline std::string capability_name(Capability cap) {
    switch (cap) {
        case Capability::NONE: return "CAP_NONE";
        case Capability::FS_READ: return "CAP_FS_READ";
        case Capability::FS_WRITE: return "CAP_FS_WRITE";
        case Capability::NETWORK: return "CAP_NETWORK";
        case Capability::CLOCK: return "CAP_CLOCK";
        case Capability::SUBPROCESS: return "CAP_SUBPROCESS";
        case Capability::ENV_VARS: return "CAP_ENV_VARS";
        case Capability::ALL: return "CAP_ALL";
    }
    return "CAP_UNKNOWN";
}

/// Security exception thrown when sandboxed bytecode attempts an unauthorized operation
class SecurityException : public std::runtime_error {
private:
    Capability missing_capability_;

public:
    explicit SecurityException(Capability missing)
        : std::runtime_error("Security violation: operation requires ungranted capability [" + capability_name(missing) + "]"),
          missing_capability_(missing) {}

    explicit SecurityException(const std::string& custom_msg, Capability missing)
        : std::runtime_error(custom_msg + " [" + capability_name(missing) + "]"),
          missing_capability_(missing) {}

    [[nodiscard]] Capability missing_capability() const noexcept {
        return missing_capability_;
    }
};

/// Capability Context tracking granted capabilities for the execution environment
class CapabilityContext {
private:
    uint32_t granted_mask_{static_cast<uint32_t>(Capability::NONE)};

public:
    constexpr CapabilityContext() = default;
    explicit constexpr CapabilityContext(Capability initial)
        : granted_mask_(static_cast<uint32_t>(initial)) {}

    void grant(Capability cap) noexcept {
        granted_mask_ |= static_cast<uint32_t>(cap);
    }

    void revoke(Capability cap) noexcept {
        granted_mask_ &= ~static_cast<uint32_t>(cap);
    }

    [[nodiscard]] bool has(Capability cap) const noexcept {
        uint32_t required = static_cast<uint32_t>(cap);
        return (granted_mask_ & required) == required;
    }

    void require(Capability cap) const {
        if (!has(cap)) {
            throw SecurityException(cap);
        }
    }

    [[nodiscard]] uint32_t raw_mask() const noexcept {
        return granted_mask_;
    }

    void reset() noexcept {
        granted_mask_ = static_cast<uint32_t>(Capability::NONE);
    }
};

} // namespace bvm

#endif // BYTECODE_VM_CAPABILITIES_HPP
