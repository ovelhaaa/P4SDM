#pragma once
#include <cstdint>
#include <cstddef>
namespace display {
constexpr int WIDTH=800, HEIGHT=480, NATIVE_WIDTH=480, NATIVE_HEIGHT=800;
constexpr size_t FRAME_BYTES=800u*480u*2u;
struct Point { int x,y; };
// PPA 270 degrees counter-clockwise = 90 clockwise, matching the BSP.
constexpr Point to_native(int x,int y) { return {NATIVE_WIDTH-1-y,x}; }
constexpr Point to_logical(int x,int y) { return {y,NATIVE_WIDTH-1-x}; }
}
