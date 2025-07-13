#ifndef GEOM_GRID
#define GEOM_GRID

#include "basic.hpp"

namespace flatland {

// A grid is a simd array of bounding boxes that we intersect a triangulated
// path with to quickly reject most segments.

static constexpr uint32_t kGridSize = 16;

struct Grid {
    std::vector<Rect> tiles;
};

Grid GenerateGridOfSize(ISize size);


} // namespace flatland

#endif // GEOM_GRID
