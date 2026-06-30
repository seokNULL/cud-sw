#include "../../include/cud/types.h"
#include <algorithm>
#include <map>

namespace {

const std::map<std::string, uint32_t>& ra_bit_positions() {
    static const std::map<std::string, uint32_t> kBits = {
        {"RA0", 0}, {"RA1", 1}, {"RA2", 2},
        {"RA3", 3}, {"RA4", 4}, {"RA5", 5}
    };
    return kBits;
}

std::vector<uint32_t> generate_offsets(const std::vector<uint32_t>& bit_positions) {
    std::vector<uint32_t> offsets = {0};
    for (uint32_t pos : bit_positions) {
        uint32_t val = 1u << pos;
        size_t current_size = offsets.size();
        for (size_t i = 0; i < current_size; ++i) {
            offsets.push_back(offsets[i] + val);
        }
    }
    std::sort(offsets.begin(), offsets.end());
    return offsets;
}

} // namespace

RaGroup::RaGroup(std::string n, std::vector<std::string> bits)
    : name(std::move(n)), bit_names(std::move(bits))
{
    const auto& ra_bits = ra_bit_positions();
    for (const auto& b : bit_names) {
        auto it = ra_bits.find(b);
        if (it != ra_bits.end()) {
            bit_positions.push_back(it->second);
        }
    }
    offsets = generate_offsets(bit_positions);
    offset_set = std::set<uint32_t>(offsets.begin(), offsets.end());
}

std::vector<RaGroup> SUPPORTED_GROUPS = {
    RaGroup("RA3,RA0",       {"RA3", "RA0"}),
    RaGroup("RA4,RA0",       {"RA4", "RA0"}),
    RaGroup("RA5,RA0",       {"RA5", "RA0"}),
    RaGroup("RA4,RA3",       {"RA4", "RA3"}),
    RaGroup("RA5,RA3",       {"RA5", "RA3"}),
    RaGroup("RA5,RA4",       {"RA5", "RA4"}),
    RaGroup("RA2,RA1",       {"RA2", "RA1"}),
    RaGroup("RA4,RA3,RA0",   {"RA4", "RA3", "RA0"}),
    RaGroup("RA5,RA3,RA0",   {"RA5", "RA3", "RA0"}),
    RaGroup("RA5,RA4,RA0",   {"RA5", "RA4", "RA0"}),
    RaGroup("RA5,RA4,RA3",   {"RA5", "RA4", "RA3"}),
    RaGroup("RA2,RA1,RA0",   {"RA2", "RA1", "RA0"}),
    RaGroup("RA3,RA2,RA1",   {"RA3", "RA2", "RA1"}),
    RaGroup("RA4,RA2,RA1",   {"RA4", "RA2", "RA1"}),
    RaGroup("RA5,RA2,RA1",   {"RA5", "RA2", "RA1"}),
};

RowAnalysisResult analyze_rows(std::vector<uint32_t> row_ids) {
    RowAnalysisResult result{0, 0, {}, false};

    if (row_ids.empty()) return result;

    std::sort(row_ids.begin(), row_ids.end());

    uint32_t min_input = row_ids[0];
    std::vector<uint32_t> pattern;
    for (uint32_t rid : row_ids) {
        pattern.push_back(rid - min_input);
    }

    for (size_t g = 0; g < SUPPORTED_GROUPS.size(); g++) {
        const auto& group = SUPPORTED_GROUPS[g];

        for (uint32_t o : group.offsets) {
            bool match = true;
            std::vector<uint32_t> used_offsets;

            for (uint32_t delta : pattern) {
                uint32_t target = o + delta;
                if (group.offset_set.find(target) == group.offset_set.end()) {
                    match = false;
                    break;
                }
                used_offsets.push_back(target);
            }

            if (match) {
                result.base_row = min_input - o;
                result.ra_group_index = g;
                result.frac_rows = used_offsets;
                result.valid = true;
                return result;
            }
        }
    }

    return result;
}
