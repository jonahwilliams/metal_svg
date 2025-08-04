#include "triangulator.hpp"

#include <iostream>
#include <vector>

#include "wangs_formula.hpp"
#include "convexicator.hpp"

#include "../third_party/libtess2/Include/tesselator.h"

namespace flatland {

namespace {

constexpr int kVertexSize = 2;
constexpr int kPolygonSize = 3;

template <typename T> int sgn(T val) { return (T(0) < val) - (val < T(0)); }

size_t NextPowerOfTwoSize(size_t x) {
    if (x == 0) {
        return 1;
    }

    --x;

    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    if constexpr (sizeof(size_t) > 4) {
        x |= x >> 32;
    }

    return x + 1;
}

static constexpr size_t kDefaultArenaSize = 4096 * 16;

} // namespace

Triangulator::Triangulator()
    : points_(std::vector<Point>(kDefaultArenaSize)),
      indices_(std::vector<uint16_t>(kDefaultArenaSize)) {}



std::pair<size_t, size_t> Triangulator::expensiveTriangulate(const Path &path,
                                               Scalar scale_factor) {
    Point contour_start = Point(0, 0);
    size_t contour_start_index = 0;
    
    ::TESStesselator* tess = tessNewTess(nullptr);

    path.iterate([&](SegmentType type, const Point *data) {
        switch (type) {
        case SegmentType::kStart: {
            contour_start = data[0];
            contour_start_index = vertex_size_;
            EnsurePointStorage(1);
            points_[vertex_size_++] = contour_start;
            break;
        }
        case SegmentType::kLinear: {
            Point point = data[1];
            EnsurePointStorage(1);
            points_[vertex_size_++] = point;
            break;
        }
        case SegmentType::kQuad: {
            Point p0 = data[0];
            Point cp = data[1];
            Point p1 = data[2];

            // (1 - t)^2 * P0 + 2t(1 - t) * CP + t^2 * P1
            //
            // note: we don't include t=0 or t=1 as these points
            // will always be P0 and P1 which have already been
            // computed.
            Scalar divisions = std::ceilf(
                ComputeQuadradicSubdivisions(scale_factor, p0, cp, p1));
            EnsurePointStorage(divisions + 1);
            for (int i = 1; i < divisions; i++) {
                Scalar t = i / divisions;
                Point pt = SolveQuad(t, p0, cp, p1);
                points_[vertex_size_++] = pt;
            }
            points_[vertex_size_++] = p1;
            break;
        }
        case SegmentType::kCubic: {
            Point p0 = data[0];
            Point cp1 = data[1];
            Point cp2 = data[2];
            Point p1 = data[3];

            // (1 - t)^3 * P0 + 3t(1 - t)^2 * CP1 + 3(1 - t)t^2 * CP2 + t^3 * P2
            //
            // note: we don't include t=0 or t=1 as these points
            // will always be P0 and P1 which have already been
            // computed.
            Scalar divisions = std::ceilf(
                ComputeCubicSubdivisions(scale_factor, p0, cp1, cp2, p1));
            EnsurePointStorage(divisions + 1);
            for (int i = 1; i < divisions; i++) {
                Scalar t = i / divisions;
                Point pt = SolveCubic(t, p0, cp1, cp2, p1);
                points_[vertex_size_++] = pt;
            }
            points_[vertex_size_++] = p1;
            break;
        }
        case SegmentType::kClose:
//           points_[vertex_size_++] = contour_start;
            ::tessAddContour(tess, kVertexSize, points_.data() + contour_start_index, sizeof(Point), vertex_size_ - contour_start_index);
            break;
        }
        return true;
    });
    ::tessTesselate(tess, ::TESS_WINDING_NONZERO, ::TESS_POLYGONS, kPolygonSize, kVertexSize, nullptr);
    int element_item_count = tessGetElementCount(tess) * kPolygonSize;
    int vertex_item_count = tessGetVertexCount(tess);
    
    vertex_size_ = vertex_item_count;
    index_size_ = element_item_count;
    EnsureIndexStorage(index_size_);
    EnsurePointStorage(vertex_size_);

    const float* vertices = ::tessGetVertices(tess);
    
    for (int i = 0, j = 0; i < vertex_item_count * 2; i += 2) {
        points_[j++] = Point(vertices[i], vertices[i + 1]);
    }
    auto* elements = tessGetElements(tess);
    for (int i = 0; i < element_item_count; i++) {
        indices_[i] = elements[i];
    }
    ::tessDeleteTess(tess);
    return std::make_pair(vertex_size_, index_size_);
}


std::pair<size_t, size_t> Triangulator::triangulate(const Path &path,
                                                    Scalar scale_factor) {
    Point contour_start = Point(0, 0);
    size_t contour_start_index = 0;

    path.iterate([&](SegmentType type, const Point *data) {
        switch (type) {
        case SegmentType::kStart: {
            contour_start = data[0];
            contour_start_index = vertex_size_;
            EnsurePointStorage(1);
            points_[vertex_size_++] = contour_start;
            break;
        }
        case SegmentType::kLinear: {
            Point point = data[1];
            EnsurePointStorage(1);
            points_[vertex_size_++] = point;
            break;
        }
        case SegmentType::kQuad: {
            Point p0 = data[0];
            Point cp = data[1];
            Point p1 = data[2];

            // (1 - t)^2 * P0 + 2t(1 - t) * CP + t^2 * P1
            //
            // note: we don't include t=0 or t=1 as these points
            // will always be P0 and P1 which have already been
            // computed.
            Scalar divisions = std::ceilf(
                ComputeQuadradicSubdivisions(scale_factor, p0, cp, p1));
            EnsurePointStorage(divisions + 1);
            for (int i = 1; i < divisions; i++) {
                Scalar t = i / divisions;
                Point pt = SolveQuad(t, p0, cp, p1);
                points_[vertex_size_++] = pt;
            }
            points_[vertex_size_++] = p1;
            break;
        }
        case SegmentType::kCubic: {
            Point p0 = data[0];
            Point cp1 = data[1];
            Point cp2 = data[2];
            Point p1 = data[3];

            // (1 - t)^3 * P0 + 3t(1 - t)^2 * CP1 + 3(1 - t)t^2 * CP2 + t^3 * P2
            //
            // note: we don't include t=0 or t=1 as these points
            // will always be P0 and P1 which have already been
            // computed.
            Scalar divisions = std::ceilf(
                ComputeCubicSubdivisions(scale_factor, p0, cp1, cp2, p1));
            EnsurePointStorage(divisions + 1);
            for (int i = 1; i < divisions; i++) {
                Scalar t = i / divisions;
                Point pt = SolveCubic(t, p0, cp1, cp2, p1);
                points_[vertex_size_++] = pt;
            }
            points_[vertex_size_++] = p1;
            break;
        }
        case SegmentType::kClose:
            // Write indices that generate a triangle fan like structure.
            size_t required = (vertex_size_ - (contour_start_index + 2)) * 3;
            EnsureIndexStorage(required);

            // Computer centroid (only weighted on vertices, todo use surface
            // formula).
            Scalar cx = 0.0;
            Scalar cy = 0.0;
            Scalar n = vertex_size_ - contour_start_index;
            for (size_t i = contour_start_index; i < vertex_size_; i++) {
                cx += points_[i].x / n;
                cy += points_[i].y / n;
            }
            EnsurePointStorage(1);
            points_[vertex_size_++] = Point(cx, cy);

            // While we can technically use any point as the origin of the
            // triangle fan, triangulating from the centroid gives slightly
            // better performance as it tends to create fewer skinny triangles.
            // On an M* macbook rendering ghostscript tiger, I measured 177us
            // for rasterization with centroid and 215 us for rasterization
            // without.
            for (auto i = contour_start_index + 1; i < vertex_size_ - 1; i++) {
                indices_[index_size_++] = vertex_size_ - 1;
                indices_[index_size_++] = i - 1;
                indices_[index_size_++] = i;
            }
            break;
        }
        return true;
    });
    return std::make_pair(vertex_size_, index_size_);
}

bool Triangulator::write(void *vertices, void *indices) {
    if (vertices == nullptr || indices == nullptr) {
        return true;
    }
    ::memcpy(vertices, points_.data(), vertex_size_ * sizeof(Point));
    ::memcpy(indices, indices_.data(), index_size_ * sizeof(uint16_t));

    vertex_size_ = 0;
    index_size_ = 0;
    return true;
}

void Triangulator::EnsurePointStorage(size_t n) {
    if (vertex_size_ + n >= points_.size()) {
        points_.resize(NextPowerOfTwoSize(points_.size()));
    }
}

void Triangulator::EnsureIndexStorage(size_t n) {
    if (index_size_ + n >= indices_.size()) {
        indices_.resize(NextPowerOfTwoSize(indices_.size()));
    }
}


std::pair<size_t, size_t> Triangulator::triangulateStroke(const Path &path,
                                                          Scalar stroke_width,
                                                          Scalar scale_factor) {
    Point contour_start = Point(0, 0);
    size_t contour_start_index = 0;
    // strokes less than one pixel must be clamped to the pixel width.
    stroke_width = std::max(stroke_width, 1.0f);
    Scalar half_width = stroke_width / 2.0f;
    
    // Given two points, we can compute the perpendicular
    // vector. That requires A dot B = 0.
    auto add_rect = [&](const Point& from, const Point& to) -> void {
        Point v = to - from;
        Scalar magnitude = std::sqrtf(std::powf(v.x, 2.0f) + std::powf(v.y, 2.0f));
        Point p = Point(v.y / magnitude, -v.x / magnitude);
        // Now we have the perpendicular vector, move half the stroke
        // width in each direction and add the triangulated mesh.
        // R1 = from + p.
        // R2 = to + p.
        Point step = Point(p.x * half_width, p.y * half_width);
        Point a = from + step;
        Point b = from - step;
        Point c = to + step;
        Point d = to - step;
        EnsurePointStorage(4);
        
        points_[vertex_size_++] = a;
        points_[vertex_size_++] = b;
        points_[vertex_size_++] = c;
        points_[vertex_size_++] = d;
    };

    path.iterate([&](SegmentType type, const Point *data) {
        switch (type) {
        case SegmentType::kStart: {
            contour_start = data[0];
            contour_start_index = vertex_size_;
            break;
        }

        case SegmentType::kLinear: {
            add_rect(data[0], data[1]);
            break;
        }
        case SegmentType::kQuad: {
            Point p0 = data[0];
            Point cp = data[1];
            Point p1 = data[2];

            // (1 - t)^2 * P0 + 2t(1 - t) * CP + t^2 * P1
            //
            // note: we don't include t=0 or t=1 as these points
            // will always be P0 and P1 which have already been
            // computed.
            Scalar divisions = std::ceilf(
                ComputeQuadradicSubdivisions(scale_factor, p0, cp, p1));
            Point prev_point = p0;
            for (int i = 1; i < divisions; i++) {
                Scalar t = i / divisions;
                Point pt = SolveQuad(t, p0, cp, p1);
                add_rect(prev_point, pt);
                prev_point = pt;
            }
            add_rect(prev_point, p1);
            break;
        }
        case SegmentType::kCubic: {
            Point p0 = data[0];
            Point cp1 = data[1];
            Point cp2 = data[2];
            Point p1 = data[3];

            // (1 - t)^3 * P0 + 3t(1 - t)^2 * CP1 + 3(1 - t)t^2 * CP2 + t^3 * P2
            //
            // note: we don't include t=0 or t=1 as these points
            // will always be P0 and P1 which have already been
            // computed.
            Scalar divisions = std::ceilf(
                ComputeCubicSubdivisions(scale_factor, p0, cp1, cp2, p1));
            Point prev_point = p0;
            for (int i = 1; i < divisions; i++) {
                Scalar t = i / divisions;
                Point pt = SolveCubic(t, p0, cp1, cp2, p1);
                add_rect(prev_point, pt);
                prev_point = pt;
            }
            add_rect(prev_point, p1);
            break;
        }
        case SegmentType::kClose:
            // Each rect includes 4 points that need to expand into 6. Therefore the
            // total number of required indicies is (rect_count / 4 * 6).
            size_t required = (vertex_size_ - contour_start_index) / 4 * 6;

            EnsureIndexStorage(required);
                
            for (auto i = contour_start_index; i < vertex_size_; i += 4) {
                indices_[index_size_++] = i;
                indices_[index_size_++] = i + 1;
                indices_[index_size_++] = i + 2;
                
                indices_[index_size_++] = i + 1;
                indices_[index_size_++] = i + 2;
                indices_[index_size_++] = i + 3;
            }
            break;
        }
        return true;
    });
    return std::make_pair(vertex_size_, index_size_);
}

} // namespace flatland
