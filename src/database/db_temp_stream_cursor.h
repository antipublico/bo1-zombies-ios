#pragma once

#include <cstddef>
#include <cstdint>

namespace bo1::fastfile::detail
{
// Models and materials can push the temporary stream while their parent
// headers remain live. The original engine restores the previous TEMP cursor
// when each nested pointer load returns; it does not sum sibling allocations.
// This cursor accounts for the longest nested chain within one root.
class TempStreamCursor
{
public:
    explicit TempStreamCursor(std::uint64_t capacity) noexcept : capacity_(capacity) {}

    bool ResetRoot() noexcept
    {
        if (depth_) return false;
        position_ = 0;
        highWater_ = 0;
        return true;
    }

    // Alignment is a power of two, as with DB_AllocStreamPos.
    // Reject overflow and out-of-block allocations before changing state.
    bool Advance(std::uint64_t size, std::uint64_t alignment = 1) noexcept
    {
        if (!alignment || (alignment & (alignment - 1)) || position_ > capacity_)
            return false;
        const auto padding = (alignment - (position_ & (alignment - 1))) & (alignment - 1);
        if (padding > capacity_ - position_) return false;
        const auto aligned = position_ + padding;
        if (size > capacity_ - aligned) return false;
        position_ = aligned + size;
        if (position_ > highWater_) highWater_ = position_;
        return true;
    }

    bool Push() noexcept
    {
        if (depth_ == MaxDepth) return false;
        stack_[depth_++] = position_;
        return true;
    }

    bool Pop() noexcept
    {
        if (!depth_) return false;
        position_ = stack_[--depth_];
        return true;
    }

    std::uint64_t Position() const noexcept { return position_; }
    std::uint64_t HighWater() const noexcept { return highWater_; }
    std::size_t Depth() const noexcept { return depth_; }

private:
    static constexpr std::size_t MaxDepth = 64; // db_stream.cpp's g_streamPosStack
    std::uint64_t capacity_ = 0;
    std::uint64_t position_ = 0;
    std::uint64_t highWater_ = 0;
    std::uint64_t stack_[MaxDepth]{};
    std::size_t depth_ = 0;
};
}
