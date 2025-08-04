#ifndef GEOM_BEZIER
#define GEOM_BEZIER

#include <functional>
#include <vector>

#include "basic.hpp"

namespace flatland {

struct Quad {
    Point p0;
    Point cp1;
    Point p1;
};

Point SolveQuad(Scalar t, const Point &p0, const Point &cp,
                       const Point &p1);

struct Cubic {
    Point p0;
    Point cp1;
    Point cp2;
    Point p1;
};

Quad LowerCubic(const Point &p1, const Point &cp1, const Point &cp2,
                       const Point &p2);

// (1 - t)^3 * P0 + 3t(1 - t)^2 * CP1 + 3(1 - t)t^2 * CP2 + t^3 * P2
Point SolveCubic(Scalar t, const Point &p0, const Point &cp1,
                        const Point &cp2, const Point &p1);

enum class SegmentType : int {
    kStart = 0,
    kLinear = 1,
    kQuad = 2,
    kCubic = 3,
    kClose = 4
};

/// @brief A Path is a collection of zero or more contours of linear, quadradic,
/// and cubic bezier segments.
class Path {
  public:
    ~Path() = default;

    Path(Path &&path) = default;

    /// Note: return false to terminate iteration.
    using PathCallback = std::function<bool(SegmentType, const Point *data)>;

    /// @brief iterate over the path segments by type.
    ///
    /// See also:
    ///     * [SegmentType]
    void iterate(const PathCallback &cb) const;
    
    Rect GetBounds() const;
    
    bool Empty() const;
    
    bool IsConvex() const;
    
    Point GetLastPoint() const {
        return last_point_;
    }
    
  private:
    friend class PathBuilder;

    Path(std::vector<Point> segments, Rect bounds);

    Path(const Path &path) = delete;
    Path &operator=(const Path &path) = delete;

    std::vector<Point> segments_;
    Point last_point_;
    bool is_convex_ = false;
    Rect bounds_;
};

/// @brief A PathBuilder is an interface for constructing a [Path] object at
/// runtime.
class PathBuilder {
  public:
    PathBuilder() = default;

    ~PathBuilder() = default;

    void moveTo(Scalar x, Scalar y);
    void moveTo(const Point &pt);
    void lineTo(Scalar x, Scalar y);
    void lineTo(const Point &pt);
    void quadTo(const Point &cp, const Point &p2);
    void cubicTo(const Point &cp1, const Point &cp2, const Point &p2);
    void horizontalTo(Scalar x);
    void verticalTo(Scalar y);

    void close();
    
    /// @brief Add a rectangular shape to the path builder in a new closed contour.
    ///
    /// Any open contours will be closed by this operation. The path winding
    /// for the rectangle is fixed in clockwise ordering.
    void AddRect(const Rect& rect);

    Path takePath();

  private:
    void start();
    
    void updateEdge(const Point& pt);
    
    float left_edge_ = std::numeric_limits<float>::infinity();
    float top_edge_ = std::numeric_limits<float>::infinity();
    float right_edge_ = -std::numeric_limits<float>::infinity();
    float bottom_edge_ = -std::numeric_limits<float>::infinity();
    
    int contour_length_ = 0;
    int contour_count_ = 0;
    Point current_ = Point(0, 0);
    Point contour_begin_ = Point(0, 0);
    std::vector<Point> segments_;
};

} // namespace flatland

#endif // GEOM_BEZIER
