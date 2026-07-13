#include "meta_data_tracker.h"

#include <cstring>

MetadataTracker::Metadata MetadataTracker::update(
    uint32_t can_id,
    const uint8_t* data,
    double timestamp)
{
    // constexpr uint8_t kMaxDataLen =
    //     static_cast<uint8_t>(sizeof(PrevState::data));

    // auto it = last_state_by_id_.find(can_id);

    // //
    // // First occurrence of this CAN ID.
    // //
    // if (it == last_state_by_id_.end())
    // {
    //     PrevState state{};
    //     state.timestamp = timestamp;

    //     if (bounded_len > 0)
    //     {
    //         std::memcpy(state.data, data, bounded_len);
    //     }

    //     last_state_by_id_.emplace(can_id, state);

    //     return Metadata{
    //         false,
    //         timestamp
    //     };
    // }

    // PrevState& prev = it->second;

    // Metadata meta;

    // meta.changed =
    //     (prev.len != bounded_len) ||
    //     (bounded_len > 0 &&
    //      std::memcmp(prev.data, data, bounded_len) != 0);

    // meta.last_timestamp = prev.timestamp;

    // //
    // // Update previous state.
    // //
    // prev.len = bounded_len;
    // prev.timestamp = timestamp;

    // if (bounded_len > 0)
    // {
    //     std::memcpy(prev.data, data, bounded_len);
    // }

    // return meta;
}

uint32_t MetadataTracker::append(uint32_t row_index,
                                 uint32_t can_id)
{
    auto it = last_row_by_can_id_.find(can_id);

    if (it == last_row_by_can_id_.end())
    {
        last_row_by_can_id_.emplace(can_id, row_index);
        return kInvalidRow;
    }

    // Take prev of the current
    const uint32_t previous_row = it->second;

    // Change next row of previous
    it->second = row_index;

    return previous_row;
}

void MetadataTracker::clear()
{
    last_state_by_id_.clear();
    last_row_by_can_id_.clear();
}

bool MetadataTracker::empty() const
{
    return last_row_by_can_id_.empty();
}