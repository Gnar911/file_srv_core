#pragma once

/// 20260711 NOTE: This class is reponsible for adding context data from user input and a 
///                 reference for all storages
#include <cstdint>
#include <unordered_map>

class MetadataTracker
{
    static constexpr uint32_t kInvalidRow = UINT32_MAX;
public:
    struct Metadata
    {
        bool changed = false;
        double last_timestamp = 0.0;
    };

    MetadataTracker() = default;

    Metadata update(uint32_t can_id,
                    const uint8_t* data,
                    double timestamp);

    uint32_t append(uint32_t row_index, uint32_t can_id);

    void clear();

    [[nodiscard]]
    bool empty() const;

private:
    struct PrevState
    {
        uint8_t data[64]{};
        double timestamp = 0.0;
    };

    std::unordered_map<uint32_t, PrevState> last_state_by_id_;
    std::unordered_map<uint32_t, uint32_t> last_row_by_can_id_;
};