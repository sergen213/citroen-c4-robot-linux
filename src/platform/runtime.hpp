#pragma once

namespace c4 {

struct Settings;

// Runs the source-native game by default; explicit selectors retain asset inspection.
// Source simulation advances at 32 Hz independently of presentation.
// --source-frames stops at N completed source ticks, then presents/captures that
// immutable frame (zero-based tick N-1). An earlier present budget/quit returns 1
// and omits the target capture; ordinary --frames still counts presentations.
// Create the actual backend/device before preparing source CPU GPU-image records.
// Observe RGBA8 mip storage on that created device and use the author's original
// cold-context factory before CPU assets/UI/session construction. This bridge is
// a native-equivalent inference, not an observed original D3D capability word;
// an unproven mip witness must not select the original zero-word/1024 branch.
// Immutable upload extents must fit an Apply/F5 candidate device before commit.
// Native Graphics Apply rebuilds/persists display resources without resetting the session.
// Native-game F5 rebuilds the display/backend in place, including finite --frames runs.
// Returns 75 only for interactive inspection F5 relaunch; finite inspection ignores F5.
int run_runtime(Settings& settings);

} // namespace c4
