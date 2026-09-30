#pragma once

// Developer inspection output for the mandatory native P6 human review. Saves
// are ordinary command-earned v18 snapshots; this does not add gameplay state.
#include <filesystem>

namespace deep {
void writeP6HumanReviewPack(const std::filesystem::path& destination);
} // namespace deep
