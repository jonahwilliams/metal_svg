#ifndef GEOM_CONVEXICATOR
#define GEOM_CONVEXICATOR

#include "basic.hpp"

#include <vector>

namespace flatland {

struct Path;

enum class Direction {
    kLeft,
    kRight,
    kStraight,
    kInvalid,
};

enum class Winding {
    kCW,
    kCCW,
};

class Convexicator {
public:
    bool ComputeIsConvex(const Path& path, const Point& last_point);
    
    static Direction ComputeDirection(const Point& prev, const Point& p0, const Point& p1);
    
private:
    bool AddVector(const Point& prev, const Point& p0, const Point& p1);
    
    Point prev_vec_ = Point(0, 0);
    std::optional<Direction> expected_direction_ = std::nullopt;
    bool is_convex_ = true;
};

} // namespace flatland

#endif // GEOM_CONVEXICATOR

