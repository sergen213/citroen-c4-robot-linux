#pragma once

#include "GraphicSettings.hpp"

#include <string>

namespace c4::rendering::quality {

// JSON objects describing actual backend capabilities and resolved quality.
std::string capabilities_json(const GraphicsCapabilities&);
// Rejects nonfinite float members with std::invalid_argument.
std::string resolved_graphics_json(const ResolvedGraphics&);

} // namespace c4::rendering::quality
