#include "convexicator.hpp"

#include "bezier.hpp"

namespace flatland {

namespace {

Direction ComputeDirectionChange(Point prev_vector, Point current_vector) {
    Scalar cross = prev_vector.Cross(current_vector);
    if (std::isnan(cross)) {
        return Direction::kInvalid;
    }
    // If the cross product is zero the lines are colinear.
    if (cross == 0) {
        // If the dot product is zero, the line is doubling back.
        return prev_vector.Dot(current_vector) < 0 ? Direction::kInvalid
                                                   : Direction::kStraight;
    }
    return cross < 0 ? Direction::kLeft : Direction::kRight;
}
} // namespace

// static
Direction Convexicator::ComputeDirection(const Point& prev, const Point& p0, const Point& p1) {
    Point prev_vec = Point(p0.x - prev.x, p0.y - prev.y);
    Point current_vec = Point(p1.x - p0.x, p1.y - p0.y);
    return ComputeDirectionChange(prev_vec, current_vec);
}

bool Convexicator::ComputeIsConvex(const Path &path, const Point& p_last_point) {
    Point last_point = p_last_point;
    path.iterate([&](SegmentType type, const Point *data) -> bool {
        bool result = true;
        switch (type) {
        case SegmentType::kStart: {
            result = true;
            break;
        }
        case SegmentType::kLinear: {
            result = AddVector(last_point, data[0], data[1]);
            last_point = data[0];
        }
        case SegmentType::kQuad: {
            Point p0 = data[0];
            Point cp = data[1];
            Point p1 = data[2];
            
            result &= AddVector(last_point, p0, cp);
            result &= AddVector(p0, cp, p1);
            last_point = cp;
            break;
        }
        case SegmentType::kCubic: {
            Point p0 = data[0];
            Point cp1 = data[1];
            Point cp2 = data[2];
            Point p1 = data[3];
            
            result &= AddVector(last_point, p0, cp1);
            result &= AddVector(p0, cp1, cp2);
            result &= AddVector(cp1, cp2, p1);
            last_point = cp2;
            break;
        }
        case SegmentType::kClose:
            break;
        }
        return result;
    });
    return is_convex_;
}

bool Convexicator::AddVector(const Point &prev, const Point &p0,
                             const Point &p1) {
    Direction direction = ComputeDirection(prev, p0, p1);

    switch (direction) {
    case Direction::kLeft:
    case Direction::kRight:
        if (!expected_direction_.has_value()) {
            expected_direction_ = direction;
        } else if (expected_direction_.value() != direction) {
            is_convex_ = false;
            expected_direction_ = std::nullopt;
            return false;
        }
        return true;
    case Direction::kStraight:
        // Don't need to do anything, no direction change.
        return true;
    case Direction::kInvalid:
        // Bad stuff.
        return false;
    }
}

} // namespace flatland
