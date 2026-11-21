// handle.h - generation-checked handles for renderer resources.
// A handle is (generation << 16) | index. Removing a resource bumps the slot's
// generation, so a stale handle is rejected instead of hitting a reused slot.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ran::render {

using Handle = uint32_t;
constexpr Handle kInvalidHandle = 0;  // generations start at 1, so 0 is never a live handle

template <typename T>
class HandleTable {
public:
    // Returns kInvalidHandle when the table is full (65535 slots).
    Handle Insert(const T& value) {
        uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            if (slots_.size() >= 0xFFFFu) return kInvalidHandle;
            index = static_cast<uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        }
        Slot& s = slots_[index];
        s.value = value;
        s.alive = true;
        return (static_cast<Handle>(s.gen) << 16) | index;
    }

    T* Get(Handle h) {
        const uint32_t index = h & 0xFFFFu;
        const uint16_t gen = static_cast<uint16_t>(h >> 16);
        if (h == kInvalidHandle || index >= slots_.size()) return nullptr;
        Slot& s = slots_[index];
        if (!s.alive || s.gen != gen) return nullptr;
        return &s.value;
    }

    const T* Get(Handle h) const {
        return const_cast<HandleTable*>(this)->Get(h);
    }

    bool Remove(Handle h) {
        if (Get(h) == nullptr) return false;
        const uint32_t index = h & 0xFFFFu;
        Slot& s = slots_[index];
        s.alive = false;
        s.gen = static_cast<uint16_t>(s.gen + 1);
        if (s.gen == 0) s.gen = 1;  // skip 0 on wrap
        free_.push_back(index);
        return true;
    }

    std::size_t LiveCount() const { return slots_.size() - free_.size(); }

private:
    struct Slot {
        T value{};
        uint16_t gen = 1;
        bool alive = false;
    };
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
};

}  // namespace ran::render
