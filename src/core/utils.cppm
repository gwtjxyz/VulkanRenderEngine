module;

#ifdef DISABLE_IMPORT_STD
#include <type_traits>
#endif

#include <cstdint>

export module utils;

#ifndef DISABLE_IMPORT_STD
import std;
#endif

export template <typename T>
struct FlagTraits {
    static constexpr bool isBitmask = false;
};

export template <typename BitType>
class BitFlags {
public:
    using MaskType = std::underlying_type_t<BitType>;

    constexpr BitFlags() noexcept : m_Mask(0) {}

    constexpr BitFlags(BitType bit) noexcept : m_Mask(static_cast<MaskType>(bit)) {}

    constexpr BitFlags(BitFlags const & rhs) noexcept = default;

    explicit constexpr BitFlags(MaskType flags) noexcept : m_Mask(flags) {}

    // Compare op - should take care of the rest of the relational operators
    auto operator<=>(const BitFlags &) const = default;

    // Logical op
    constexpr bool operator!() const noexcept {
        return !m_Mask;
    }

    // Bitwise ops
    constexpr BitFlags operator&(BitFlags const & rhs) const noexcept {
        return BitFlags(m_Mask & rhs.m_Mask);
    }

    constexpr BitFlags operator|(BitFlags const & rhs) const noexcept {
        return BitFlags(m_Mask | rhs.m_Mask);
    }

    constexpr BitFlags operator^(BitFlags const & rhs) const noexcept {
        return BitFlags(m_Mask ^ rhs.m_Mask);
    }

    constexpr BitFlags operator~() const noexcept {
        return BitFlags(m_Mask ^ FlagTraits<BitType>::allFlags.m_Mask);
    }

    // Assignment ops
    constexpr BitFlags & operator=(const BitFlags & hrs) noexcept = default;

    constexpr BitFlags & operator|=(const BitFlags & rhs) noexcept {
        m_Mask |= rhs.m_Mask;
        return *this;
    }

    constexpr BitFlags & operator&=(const BitFlags & rhs) noexcept {
        m_Mask &= rhs.m_Mask;
        return *this;
    }

    constexpr BitFlags & operator^=(const BitFlags & rhs) noexcept {
        m_Mask ^= rhs.m_Mask;
        return *this;
    }

    // Cast ops
    explicit constexpr operator bool() const noexcept {
        return !!m_Mask;
    }

    explicit constexpr operator MaskType() const noexcept {
        return m_Mask;
    }

private:
    MaskType m_Mask;
};

// Bitwise ops
export template<typename BitType>
constexpr BitFlags<BitType> operator&(BitType bit, BitFlags<BitType> const & flags) noexcept {
    return flags.operator&(bit);
}

export template<typename BitType>
constexpr BitFlags<BitType> operator|(BitType bit, BitFlags<BitType> const & flags) noexcept {
    return flags.operator|(bit);
}

export template<typename BitType>
constexpr BitFlags<BitType> operator^(BitType bit, BitFlags<BitType> const & flags) noexcept {
    return flags.operator^(bit);
}

// Bitwise ops on BitType
export template <typename BitType, typename std::enable_if<FlagTraits<BitType>::isBitmask, bool>::type = true>
constexpr BitFlags<BitType> operator&(BitType lhs, BitType rhs) noexcept {
    return BitFlags<BitType>(lhs) & rhs;
}

export template <typename BitType, typename std::enable_if<FlagTraits<BitType>::isBitmask, bool>::type = true>
constexpr BitFlags<BitType> operator|(BitType lhs, BitType rhs) noexcept {
    return BitFlags<BitType>(lhs) | rhs;
}

export template <typename BitType, typename std::enable_if_t<FlagTraits<BitType>::isBitmask, bool>::type = true>
constexpr BitFlags<BitType> operator^(BitType lhs, BitType rhs) noexcept {
    return BitFlags<BitType>(lhs) ^ rhs;
}

export template <typename BitType, typename std::enable_if_t<FlagTraits<BitType>::isBitmask, bool>::type = true>
constexpr BitFlags<BitType> operator~(BitType bit) noexcept {
    return ~BitFlags<BitType>(bit);
}

// Wrapper for type-safe enum bitmasks, following vulkan-hpp's model
