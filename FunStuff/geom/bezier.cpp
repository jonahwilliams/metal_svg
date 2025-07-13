#include "bezier.hpp"

#include "convexicator.hpp"
#include <iostream>

namespace flatland {

/// Path Implementation.
Path::Path(std::vector<Point> segments, Rect bounds)
    : segments_(std::move(segments)), bounds_(bounds) {}

void Path::iterate(const Path::PathCallback &cb) const {
    constexpr std::array<int, 5> type_offsets = {2, 3, 4, 5, 1};
    size_t offset = 0;
    while (offset < segments_.size()) {
        int ix = std::round((segments_[offset].x));
        SegmentType type = static_cast<SegmentType>(ix);
        if (!cb(type, segments_.data() + offset + 1)) {
            return;
        }
        offset += type_offsets[ix];
    }
}

bool Path::Empty() const { return segments_.size() < 2; }

Rect Path::GetBounds() const { return bounds_; }

bool Path::IsConvex() const { return is_convex_; }

// PathBuilder implementation.

void PathBuilder::moveTo(Scalar x, Scalar y) {
    if (x == current_.x && y == current_.y) {
        return;
    }
    if (contour_length_ > 0) {
        close();
    }
    current_ = Point(x, y);
}

void PathBuilder::moveTo(const Point &pt) { moveTo(pt.x, pt.y); }

void PathBuilder::lineTo(Scalar x, Scalar y) {
    if (contour_length_ == 0) {
        start();
    }
    updateEdge({x, y});
    segments_.emplace_back(static_cast<Scalar>(SegmentType::kLinear), 0);
    segments_.emplace_back(current_.x, current_.y);
    segments_.emplace_back(x, y);
    current_ = Point(x, y);
    contour_length_++;
}

void PathBuilder::lineTo(const Point &pt) { lineTo(pt.x, pt.y); }

void PathBuilder::quadTo(const Point &cp, const Point &p2) {
    if (contour_length_ == 0) {
        start();
    }
    updateEdge(cp);
    updateEdge(p2);
    segments_.emplace_back(static_cast<Scalar>(SegmentType::kQuad), 0);
    segments_.emplace_back(current_.x, current_.y);
    segments_.emplace_back(cp.x, cp.y);
    segments_.emplace_back(p2.x, p2.y);
    current_ = p2;
    contour_length_++;
}

void PathBuilder::cubicTo(const Point &cp1, const Point &cp2, const Point &p2) {
    if (contour_length_ == 0) {
        start();
    }
    updateEdge(cp1);
    updateEdge(cp2);
    updateEdge(p2);
    segments_.emplace_back(static_cast<Scalar>(SegmentType::kCubic), 0);
    segments_.emplace_back(current_.x, current_.y);
    segments_.emplace_back(cp1.x, cp1.y);
    segments_.emplace_back(cp2.x, cp2.y);
    segments_.emplace_back(p2.x, p2.y);
    current_ = p2;
    contour_length_++;
}

void PathBuilder::horizontalTo(Scalar x) { lineTo(x, current_.y); }

void PathBuilder::verticalTo(Scalar y) { lineTo(current_.x, y); }

void PathBuilder::close() {
    if (contour_length_ == 0) {
        return;
    }
    if (contour_begin_ != current_) {
        lineTo(contour_begin_);
    }
    segments_.emplace_back(static_cast<Scalar>(SegmentType::kClose), 0);
    contour_length_ = 0;
    contour_count_++;
}

void PathBuilder::start() {
    segments_.emplace_back(static_cast<Scalar>(SegmentType::kStart), 0);
    segments_.emplace_back(current_.x, current_.y);
    updateEdge(current_);
    contour_begin_ = current_;
}

void PathBuilder::updateEdge(const Point &pt) {
    left_edge_ = std::min(pt.x, left_edge_);
    top_edge_ = std::min(pt.y, top_edge_);
    right_edge_ = std::max(pt.x, right_edge_);
    bottom_edge_ = std::max(pt.y, bottom_edge_);
}

void PathBuilder::AddRect(const Rect &rect) {
    close();
    moveTo(rect.l, rect.t);
    lineTo(rect.r, rect.t);
    lineTo(rect.r, rect.b);
    lineTo(rect.l, rect.b);
    close();
}

Path PathBuilder::takePath() {
    Path result(std::move(segments_),
                Rect(left_edge_, top_edge_, right_edge_, bottom_edge_));
    Convexicator convexicator;

    // Only single contour paths are allowed to be convex. Different convex
    // paths overlapping with different winding orders can still require
    // stenciling. We can fast path this if we know that
    // 1. All contours are convex and 2. all winding orders are identical and 3.
    // The fill mode is non-zero. Technically we could support more cases if we
    // knew no shapes intersected, however that computation is quadradic with
    // the number of segments.
    result.last_point_ = current_;
    result.is_convex_ =
        contour_count_ <= 1 && convexicator.ComputeIsConvex(result, current_);
    segments_ = {};
    contour_length_ = 0;
    current_ = Point(0, 0);
    contour_begin_ = Point(0, 0);
    left_edge_ = std::numeric_limits<float>::infinity();
    top_edge_ = std::numeric_limits<float>::infinity();
    right_edge_ = -std::numeric_limits<float>::infinity();
    bottom_edge_ = -std::numeric_limits<float>::infinity();
    return result;
}

} // namespace flatland
