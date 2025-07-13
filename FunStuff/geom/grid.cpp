#include "grid.hpp"
#include "bezier.hpp"

namespace flatland {

// First, generate an N * N Grid for the given frame size.
Grid GenerateGridOfSize(ISize size) {
    std::vector<Rect> tiles;
    for (int i = 0; i < std::ceil(size.w / 16); i++) {
        for (int j = 0; j < std::ceil(size.h / 16); j++) {
            tiles.push_back(
                Rect::MakeLTRB(i * 16, j * 16, i * 16 + 16, j * 16 + 16));
        }
    }
    return Grid{.tiles = std::move(tiles)};
}

// Next, we iterate through our paths and compute the intersected path segments for
// each tile. We first perform a quick reject with the conservative path bounds.

void GenerateWorkPerTile(Grid& grid, Path& path) {
    Rect bounds = path.GetBounds();
    for (int i = 0; i < grid.tiles.size(); i++) {
        const Rect& tile = grid.tiles[i];
        // No intersection, ignore.
        if (!tile.Intersection(bounds).has_value()) {
            continue;
        }
        // If there is an intersection, for each segment in the path,
        // we need to compute the clipped path segment.
        path.iterate([&](SegmentType type, const Point *data) {
            switch (type) {
            case SegmentType::kStart: {
                
                break;
            }
            case SegmentType::kLinear: {
                Point p0 = data[0];
                Point p1 = data[1];
                if (!Rect::MakePointBounds(p0, p1).Intersection(tile).has_value()) {
                    break;
                }
                break;
            }
            case SegmentType::kQuad: {
                Point p0 = data[0];
                Point cp = data[1];
                Point p1 = data[2];

                // (1 - t)^2 * P0 + 2t(1 - t) * CP + t^2 * P1
                if (Rect::MakePointBounds(p0, p1, cp, cp).Intersection(tile).has_value()) {
                    break;
                }

                break;
            }
            case SegmentType::kCubic: {
                Point p0 = data[0];
                Point cp1 = data[1];
                Point cp2 = data[2];
                Point p1 = data[3];

                // (1 - t)^3 * P0 + 3t(1 - t)^2 * CP1 + 3(1 - t)t^2 * CP2 + t^3 * P2
                if (Rect::MakePointBounds(p0, p1, cp1, cp2).Intersection(tile).has_value()) {
                    break;
                }
               
                break;
            }
            case SegmentType::kClose:
                
                break;
            }
            return true;
        });
    }
}


} // namespace flatland.
