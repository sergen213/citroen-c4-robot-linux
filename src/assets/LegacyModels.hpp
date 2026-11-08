#pragma once

#include "Animation.hpp"

namespace c4::assets {
// Internal MDL4 decoder; decode_model is the public format dispatcher.
Model decode_legacy_model(std::span<const std::uint8_t> bytes);
} // namespace c4::assets
