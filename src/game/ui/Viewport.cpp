#include "NativeUi.hpp"

#include <algorithm>
#include <cmath>

namespace c4::ui {
Viewport Viewport::fit(int width, int height, float canvas_width, float canvas_height) noexcept {
    if (width <= 0 || height <= 0 || !std::isfinite(canvas_width) || !std::isfinite(canvas_height) ||
        canvas_width <= 0 || canvas_height <= 0) return {};
    const float scale = std::min(static_cast<float>(width) / canvas_width,
                                 static_cast<float>(height) / canvas_height);
    const float w = canvas_width * scale, h = canvas_height * scale;
    return {(static_cast<float>(width) - w) * 0.5f,
            (static_cast<float>(height) - h) * 0.5f, w, h, scale};
}
bool Viewport::map(float window_x, float window_y, float& canvas_x, float& canvas_y) const noexcept {
    if (scale <= 0 || window_x < x || window_y < y || window_x >= x + width || window_y >= y + height)
        return false;
    canvas_x = (window_x - x) / scale;
    canvas_y = (window_y - y) / scale;
    return true;
}
} // namespace c4::ui
