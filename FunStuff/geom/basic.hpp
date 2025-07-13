#ifndef GEOM_BASIC
#define GEOM_BASIC

#include <array>
#include <cmath>
#include <iostream>
#include <ostream>
#include <stdint.h>
#include <type_traits>

#define CHECK_INTERSECT(al, at, ar, ab, bl, bt, br, bb)                        \
    Scalar RL = std::max(al, bl);                                              \
    Scalar RR = std::min(ar, br);                                              \
    Scalar RT = std::max(at, bt);                                              \
    Scalar RB = std::min(ab, bb);                                              \
    do {                                                                       \
        if (!(RL < RR && RT < RB))                                             \
            return std::nullopt;                                               \
    } while (0)
// do the !(opposite) check so we return false if either arg is NaN

#define MIN(a, b, c, d) std::min(std::min(a, b), std::min(c, d))
#define MAX(a, b, c, d) std::max(std::max(a, b), std::max(c, d))

namespace flatland {
using Scalar = float;

struct Point {
    constexpr Point() = default;

    constexpr Point(Scalar x, Scalar y) : x(x), y(y) {}

    constexpr Point operator+(const Point &other) const {
        return Point(other.x + x, other.y + y);
    }

    constexpr Point operator-(const Point &other) const {
        return Point(x - other.x, y - other.y);
    }

    constexpr Point operator*(Scalar s) const { return Point(x * s, y * s); }

    constexpr Point operator*(const Point &other) const {
        return Point(x * other.x, y * other.y);
    }

    constexpr Point operator-=(const Point &other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    constexpr Point operator+=(const Point &other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr Point Abs() const { return {std::fabs(x), std::fabs(y)}; }

    /// @brief Compute the dot product of this and `other`.
    constexpr Scalar Dot(const Point &other) const {
        return x * other.x + y * other.y;
    }

    /// @brief Compute the cross product of this and `other`.
    constexpr Scalar Cross(const Point &other) const {
        return x * other.y - y * other.x;
    }

    /// @brief Create a new point that has the component-wise maximums of this
    /// and `other`.
    constexpr Point Max(const Point &other) const {
        return Point(std::fmax(x, other.x), std::fmax(y, other.y));
    }

    /// @brief Create a new point that has the component-wise minimums of this
    /// and `other`.
    constexpr Point Min(const Point &other) const {
        return Point(std::fmin(x, other.x), std::fmin(y, other.y));
    }

    constexpr bool operator==(const Point &other) {
        return x == other.x && y == other.y;
    }

    constexpr bool operator!=(const Point &other) {
        return x != other.x || y != other.y;
    }

    Scalar x = 0;
    Scalar y = 0;
};

template <typename T> struct TSize {
    TSize(T w, T h) : w(w), h(h) {}

    constexpr TSize operator+(const TSize &other) const {
        return TSize(other.w + w, other.h + h);
    }

    constexpr TSize operator-(const TSize &other) const {
        return TSize(w - other.w, h - other.h);
    }

    template <typename U = T,
              typename = std::enable_if_t<std::is_floating_point_v<U>>>
    TSize operator*(Scalar s) const {
        return TSize(w * s, h * s);
    }

    T w = 0;
    T h = 0;
};

using Size = TSize<Scalar>;
using ISize = TSize<int32_t>;

struct Rect {
    constexpr static Rect MakeLTRB(Scalar l, Scalar t, Scalar r, Scalar b) {
        return Rect(l, t, r, b);
    }

    constexpr static Rect MakePointBounds(const Point &l, const Point &r) {
        return Rect(std::min(l.x, r.x), std::min(l.y, r.y), std::max(l.x, r.x),
                    std::max(l.y, r.y));
    }

    constexpr static Rect MakePointBounds(const Point &l, const Point &r,
                                          const Point &s, const Point &t) {
        return Rect(MIN(l.x, r.x, s.x, t.x), MIN(l.y, r.y, s.y, t.y),
                    MAX(l.x, r.x, s.x, t.x), MAX(l.y, r.y, s.y, t.y));
    }

    constexpr Rect operator+(const Rect &other) const {
        return Rect(l + other.l, t + other.t, r + other.r, b + other.b);
    }

    constexpr Rect operator-(const Rect &other) const {
        return Rect(l - other.l, t - other.t, r - other.r, b - other.b);
    }

    constexpr Scalar GetWidth() const { return r - l; }

    constexpr Scalar GetHeight() const { return b - t; }

    constexpr std::array<Scalar, 12> GetQuad() const {
        return {
            l, t, //
            r, t, //
            l, b, //
            r, t, //
            l, b, //
            r, b  //
        };
    }

    constexpr std::optional<Rect> Intersection(const Rect &other) const {
        CHECK_INTERSECT(other.l, other.t, other.r, other.b, l, t, r, b);
        return Rect(RL, RT, RR, RB);
    }

    constexpr Rect Union(const Rect &other) const {
        return Rect(std::min(other.l, l), std::min(other.t, t),
                    std::max(other.r, r), std::max(other.b, b));
    }

    /// @brief Expand the rectangle in the horizontal and vertical directions.
    ///
    /// Expanding by negative amounts will shrink the rectangle. The total
    /// change in width will be `2 * h` and the height will be `2 * v`.
    constexpr Rect Expand(Scalar h, Scalar v) const {
        return Rect(l - h, t - v, r + h, b + v);
    }

    Scalar l = 0;
    Scalar t = 0;
    Scalar r = 0;
    Scalar b = 0;
};

/// RHS Point operations
constexpr Point operator*(Scalar s, const Point &p) { return p * s; };

/// @brief A column-major 4x4 matrix.
struct Matrix {
    /// @brief Construct an identity matrix.
    constexpr Matrix()
        : m(1, 0, 0, 0, //
            0, 1, 0, 0, //
            0, 0, 0, 1, //
            0, 0, 0, 1) {}

    constexpr Matrix(Scalar a1, Scalar a2, Scalar a3, Scalar a4, Scalar b1,
                     Scalar b2, Scalar b3, Scalar b4, Scalar c1, Scalar c2,
                     Scalar c3, Scalar c4, Scalar d1, Scalar d2, Scalar d3,
                     Scalar d4)
        : m{a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, c3, d1, d2, d3, d4} {}

    static constexpr Matrix MakeOrthographic(const Size &size) {
        return Matrix(2.0f / size.w, 0.0, 0.0, 0.0,  // col 1
                      0.0, -2.0f / size.h, 0.0, 0.0, // col 2
                      0.0, 0.0, 1.0, 0.0,            // col 3
                      -1.0, 1.0, 0.5, 1.0            // col 4
        );
    }

    static constexpr Matrix MakeTranslate(Scalar x, Scalar y = 0,
                                          Scalar z = 0) {
        return Matrix(1, 0, 0, 0, //
                      0, 1, 0, 0, //
                      0, 0, 1, 0, //
                      x, y, z, 1  //
        );
    }

    constexpr Point GetTranslation() const { return Point(m[12], m[13]); }

    static constexpr Matrix MakeScale(Scalar sx, Scalar sy = 1, Scalar sz = 1) {
        return Matrix(sx, 0, 0, 0, //
                      0, sy, 0, 0, //
                      0, 0, sz, 0, //
                      0, 0, 0, 1   //
        );
    }

    static constexpr Matrix MakeRotate(Scalar r) {
        return Matrix(std::cos(r), std::sin(r), 0, 0,  //
                      -std::sin(r), std::cos(r), 0, 0, //
                      0, 0, 1, 0,                      //
                      0, 0, 0, 1                       //
        );
    }

    constexpr Rect TransformBounds(const Rect &bounds) const {
        Point lt = TransformPoint(Point{bounds.l, bounds.t});
        Point rt = TransformPoint(Point{bounds.r, bounds.t});
        Point lb = TransformPoint(Point{bounds.l, bounds.b});
        Point rb = TransformPoint(Point{bounds.r, bounds.b});
        return Rect(MIN(lt.x, rt.x, lb.x, rb.x), MIN(lt.y, rt.y, lb.y, rb.y),
                    MAX(lt.x, rt.x, lb.x, rb.x), MAX(lt.y, rt.y, lb.y, rb.y));
    }

    constexpr Point TransformPoint(const Point &v) const {
        Scalar w = v.x * m[3] + v.y * m[7] + m[15];
        Point result(v.x * m[0] + v.y * m[4] + m[12],
                     v.x * m[1] + v.y * m[5] + m[13]);

        // This is Skia's behavior, but it may be reasonable to allow UB for the
        // w=0 case.
        if (w) {
            w = 1 / w;
        }
        return result * w;
    }

    Matrix operator*(const Matrix &o) const {
        // clang-format off
        return Matrix(
             m[0] * o.m[0]  + m[4] * o.m[1]  + m[8]  * o.m[2]  + m[12] * o.m[3],
             m[1] * o.m[0]  + m[5] * o.m[1]  + m[9]  * o.m[2]  + m[13] * o.m[3],
             m[2] * o.m[0]  + m[6] * o.m[1]  + m[10] * o.m[2]  + m[14] * o.m[3],
             m[3] * o.m[0]  + m[7] * o.m[1]  + m[11] * o.m[2]  + m[15] * o.m[3],
             m[0] * o.m[4]  + m[4] * o.m[5]  + m[8]  * o.m[6]  + m[12] * o.m[7],
             m[1] * o.m[4]  + m[5] * o.m[5]  + m[9]  * o.m[6]  + m[13] * o.m[7],
             m[2] * o.m[4]  + m[6] * o.m[5]  + m[10] * o.m[6]  + m[14] * o.m[7],
             m[3] * o.m[4]  + m[7] * o.m[5]  + m[11] * o.m[6]  + m[15] * o.m[7],
             m[0] * o.m[8]  + m[4] * o.m[9]  + m[8]  * o.m[10] + m[12] * o.m[11],
             m[1] * o.m[8]  + m[5] * o.m[9]  + m[9]  * o.m[10] + m[13] * o.m[11],
             m[2] * o.m[8]  + m[6] * o.m[9]  + m[10] * o.m[10] + m[14] * o.m[11],
             m[3] * o.m[8]  + m[7] * o.m[9]  + m[11] * o.m[10] + m[15] * o.m[11],
             m[0] * o.m[12] + m[4] * o.m[13] + m[8]  * o.m[14] + m[12] * o.m[15],
             m[1] * o.m[12] + m[5] * o.m[13] + m[9]  * o.m[14] + m[13] * o.m[15],
             m[2] * o.m[12] + m[6] * o.m[13] + m[10] * o.m[14] + m[14] * o.m[15],
             m[3] * o.m[12] + m[7] * o.m[13] + m[11] * o.m[14] + m[15] * o.m[15]);
        // clang-format on
    }

    const Scalar *GetStorage() const { return m; }

  private:
    Scalar m[16];
};

/// @brief A four channel color in an SRGB or extended SRGB format.
///
/// SRGB is typically converted to a linear color space by the driver, we hope.
struct Color {
    constexpr explicit Color() : r(0), g(0), b(0), a(0) {}

    constexpr Color(Scalar r, Scalar g, Scalar b, Scalar a)
        : r(r), g(g), b(b), a(a) {}

    static constexpr Color FromRGB(int value) {
        return Color(((value & 0xFF) / 255.0),       //
                     ((value >> 8) & 0xFF) / 255.0,  //
                     ((value >> 16) & 0xFF) / 255.0, //
                     1.0);
    }

    constexpr Color Premultiply() const {
        return Color(r * a, g * a, b * a, a);
    }

    constexpr Color Unpremultiply() const {
        return Color(r / a, g / a, b / a, a);
    }

    constexpr Color WithAlpha(Scalar f) const { return Color(r, g, b, f); }

    constexpr bool is_opaque() const { return a >= 1.0; }
    
    bool operator==(const Color& other) {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    
    bool operator!=(const Color& other) {
        return r != other.r || g != other.g || b != other.b || a != other.a;
    }

    Scalar r;
    Scalar g;
    Scalar b;
    Scalar a;
};

static constexpr Color kRed = Color(1, 0, 0, 1);
static constexpr Color kGreen = Color(0, 1, 0, 1);
static constexpr Color kBlue = Color(0, 0, 1, 1);
static constexpr Color kBlack = Color(0, 0, 0, 1);
static constexpr Color kTransparent = Color(0, 0, 0, 0);
static constexpr Color kWhite = Color(1, 1, 1, 1);

} // namespace flatland

namespace std {

inline std::ostream &operator<<(std::ostream &out, const flatland::Point &p) {
    out << "Point(" << p.x << ", " << p.y << ")";
    return out;
}

inline std::ostream &operator<<(std::ostream &out, const flatland::Rect &r) {
    out << "Rect::LTRB(" << r.l << "," << r.t << "," << r.r << "," << r.b
        << ")";
    return out;
}

inline std::ostream &operator<<(std::ostream &out, const flatland::Matrix &m) {
    const flatland::Scalar *s = m.GetStorage();
    out << "M(" << s[0] << ", " << s[1] << ", " << s[2] << ", " << s[3]
        << std::endl                                                         //
        << s[4] << ", " << s[5] << ", " << s[6] << ", " << s[7] << std::endl //
        << s[8] << ", " << s[9] << ", " << s[10] << ", " << s[11]
        << std::endl //
        << s[12] << ", " << s[13] << ", " << s[14] << ", " << s[15]
        << std::endl;
    return out;
}

inline std::ostream &operator<<(std::ostream &out, const flatland::Color& c) {
    out << "RGBA(" << c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")";
    return out;
}

} // namespace std

#undef CHECK_INTERSECT
#undef MIN
#undef MAX
#endif // GEOM_BASIC
