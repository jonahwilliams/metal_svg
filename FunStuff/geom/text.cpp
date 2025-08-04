#include "text.hpp"

#include "triangulator.hpp"
#include "wangs_formula.hpp"

namespace flatland {

using OutCodeValue = uint8_t;

enum OutCode : uint8_t {
    kInside = 0b0000,
    kLeft = 0b0001,
    kRight = 0b0010,
    kBottom = 0b0100,
    kTop = 0b1000,
};

// Compute outcode based on the position of pt with respect to the bounding
// rectangle.
static OutCodeValue ComputeOutCode(const Rect &bounds, const Point &pt) {
    OutCodeValue result = OutCode::kInside;
    if (pt.x < bounds.l) {
        result |= OutCode::kLeft;
    } else if (pt.x > bounds.r) {
        result |= OutCode::kRight;
    }

    if (pt.y < bounds.t) {
        result |= OutCode::kTop;
    } else if (pt.y > bounds.b) {
        result |= OutCode::kBottom;
    }

    return result;
}

struct LineResult {
    Point p1;
    Point p2;
};

// Clip a line from [pt1] to [pt2] against [bounds].
std::optional<LineResult> CohenSutherlandLineClip(const Rect &bounds,
                                                  const Point &pt1,
                                                  const Point &pt2) {
    OutCodeValue outcode0 = ComputeOutCode(bounds, pt1);
    OutCodeValue outcode1 = ComputeOutCode(bounds, pt2);
    bool accept = false;

    Scalar x0 = pt1.x;
    Scalar x1 = pt2.x;
    Scalar y0 = pt1.y;
    Scalar y1 = pt2.y;

    while (true) {
        if (!(outcode0 | outcode1)) {
            // bitwise OR is 0: both points are inside bounds already.
            accept = true;
            break;
        } else if (outcode0 & outcode1) {
            // bitwise AND is not 0: both points share an outside zone so both
            // must be outside the window. That is, there is no intersection.
            break;
        } else {
            // At least one endpoint is outside the clip rectangle. Pick
            // whichever one is non-zero.
            OutCodeValue outcodeOut = outcode1 > outcode0 ? outcode1 : outcode0;

            // Now find the intersection point.
            //   m = (y1 - y0) / (x1 - x0)
            //   x = x0 + (1 / slope) * (ym - y0), where ym is ymin or ymax
            //   y = y0 + slope * (xm - x0), where xm is xmin or xmax
            // No need to worry about divide-by-zero because, in each case, the
            // outcode bit being tested guarantees the denominator is non-zero
            Scalar x = 0;
            Scalar y = 0;
            if (outcodeOut & OutCode::kTop) {
                // point is above the clip window
                x = x0 + (x1 - x0) * (bounds.b - y0) / (y1 - y0);
                y = bounds.b;
            } else if (outcodeOut & OutCode::kBottom) {
                // point is below the clip window
                x = x0 + (x1 - x0) * (bounds.t - y0) / (y1 - y0);
                y = bounds.t;
            } else if (outcodeOut & OutCode::kRight) {
                // point is to the right of clip window
                y = y0 + (y1 - y0) * (bounds.r - x0) / (x1 - x0);
                x = bounds.r;
            } else if (outcodeOut & OutCode::kLeft) {
                // point is to the left of clip window
                y = y0 + (y1 - y0) * (bounds.l - x0) / (x1 - x0);
                x = bounds.l;
            }

            // Now we move outside point to intersection point to clip
            // and get ready for next pass.
            if (outcodeOut == outcode0) {
                x0 = x;
                y0 = y;
                outcode0 = ComputeOutCode(bounds, Point(x0, y0));
            } else {
                x1 = x;
                y1 = y;
                outcode1 = ComputeOutCode(bounds, Point(x1, y1));
            }
        }
    }

    if (accept) {
        return LineResult{Point{x0, y0}, Point{x1, y1}};
    }
    return std::nullopt;
}

std::vector<uint8_t> RasterizePath(const Path &path, ISize size) {
    std::vector<uint8_t> result(size.w * size.h);
    std::vector<std::vector<LineResult>> lines(size.w * size.h,
                                               std::vector<LineResult>());

    for (int i = 0; i < size.w; i++) {
        for (int j = 0; j < size.h; j++) {
            // Create a rectangle from [i, j] to (i + 1, j + 1).
            // Compute the intersection with each bezier segment.
            // Using Cohen-Sutherland clipping:
            // https://en.wikipedia.org/wiki/Cohen%E2%80%93Sutherland_algorithm

            size_t index = i + (j * size.w);
            Point start = Point(0, 0);
            Point current = start;
            Rect bounds = Rect(i, j, i + 1, j + 1);
            path.iterate([&](SegmentType type, const Point *data) {
                switch (type) {
                case SegmentType::kStart:
                    current = start = data[0];
                    break;
                case SegmentType::kLinear: {
                    if (auto result =
                            CohenSutherlandLineClip(bounds, data[0], data[1]);
                        result.has_value()) {
                        lines[index].push_back(result.value());
                    }
                    current = data[1];
                    break;
                }
                case SegmentType::kQuad: {
                    // TODO: check intersection before linearization.
                    Point p0 = data[0];
                    Point cp = data[1];
                    Point p1 = data[2];

                    // (1 - t)^2 * P0 + 2t(1 - t) * CP + t^2 * P1
                    //
                    // note: we don't include t=0 or t=1 as these points
                    // will always be P0 and P1 which have already been
                    // computed.
                    Scalar divisions = std::ceilf(ComputeQuadradicSubdivisions(
                        /*scale_factor=*/1.0, p0, cp, p1));
                    Point prev_point = p0;
                    for (int i = 1; i < divisions; i++) {
                        Scalar t = i / divisions;
                        Point pt = SolveQuad(t, p0, cp, p1);
                        if (auto result =
                                CohenSutherlandLineClip(bounds, prev_point, pt);
                            result.has_value()) {
                            lines[index].push_back(result.value());
                        }
                        prev_point = pt;
                    }
                    break;
                }
                case SegmentType::kCubic: {
                    Point p0 = data[0];
                    Point cp1 = data[1];
                    Point cp2 = data[2];
                    Point p1 = data[3];

                    // (1 - t)^3 * P0 + 3t(1 - t)^2 * CP1 + 3(1 - t)t^2 * CP2 +
                    // t^3 * P2
                    //
                    // note: we don't include t=0 or t=1 as these points
                    // will always be P0 and P1 which have already been
                    // computed.
                    Scalar divisions = std::ceilf(ComputeCubicSubdivisions(
                        /*scale_factor=*/1.0, p0, cp1, cp2, p1));

                    Point prev_point = p0;
                    for (int i = 1; i < divisions; i++) {
                        Scalar t = i / divisions;
                        Point pt = SolveCubic(t, p0, cp1, cp2, p1);
                        if (auto result =
                                CohenSutherlandLineClip(bounds, prev_point, pt);
                            result.has_value()) {
                            lines[index].push_back(result.value());
                        }
                        prev_point = pt;
                    }

                    break;
                }
                case SegmentType::kClose:
                    // Treat close as a linear back to start if they're not
                    // the same point.
                    if (start != current) {
                        if (auto result =
                                CohenSutherlandLineClip(bounds, current, start);
                            result.has_value()) {
                            lines[index].push_back(result.value());
                        }
                    }
                    break;
                }
                return true;
            });
        }
    }
    return result;
}

} // namespace flatland
