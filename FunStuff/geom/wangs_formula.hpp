#ifndef GEOM_WANGS_FORMULA
#define GEOM_WANGS_FORMULA

#include "basic.hpp"

// Wang's formula gives the minimum number of evenly spaced (in the parametric
// sense) line segments that a bezier curve must be chopped into in order to
// guarantee all lines stay within a distance of "1/precision" pixels from the
// true curve. Its definition for a bezier curve of degree "n" is as follows:
//
//     maxLength = max([length(p[i+2] - 2p[i+1] + p[i]) for (0 <= i <= n-2)])
//     numParametricSegments = sqrt(maxLength * precision * n*(n - 1)/8)
//
// (Goldman, Ron. (2003). 5.6.3 Wang's Formula. "Pyramid Algorithms: A Dynamic
// Programming Approach to Curves and Surfaces for Geometric Modeling". Morgan
// Kaufmann Publishers.)

namespace flatland {

/// Returns the minimum number of evenly spaced (in the parametric sense) line
/// segments that the cubic must be chopped into in order to guarantee all lines
/// stay within a distance of "1/intolerance" pixels from the true curve.
///
/// The scale_factor should be the max basis XY of the current transform.
Scalar ComputeCubicSubdivisions(Scalar scale_factor,
                                Point p0,
                                Point p1,
                                Point p2,
                                Point p3);

/// Returns the minimum number of evenly spaced (in the parametric sense) line
/// segments that the quadratic must be chopped into in order to guarantee all
/// lines stay within a distance of "1/intolerance" pixels from the true curve.
///
/// The scale_factor should be the max basis XY of the current transform.
Scalar ComputeQuadradicSubdivisions(Scalar scale_factor,
                                    Point p0,
                                    Point p1,
                                    Point p2);

/// Returns the minimum number of evenly spaced (in the parametric sense) line
/// segments that the conic must be chopped into in order to guarantee all
/// lines stay within a distance of "1/intolerance" pixels from the true curve.
///
/// The scale_factor should be the max basis XY of the current transform.
Scalar ComputeConicSubdivisions(Scalar scale_factor,
                                Point p0,
                                Point p1,
                                Point p2,
                                Scalar w);
} // namespace flatland

#endif //GEOM_WANGS_FORMULA
