#include <metal_stdlib>
using namespace metal;

struct VertInfo {
    float4x4 mvp;
    float depth;
    float padding;
};

struct VertInput {
    simd::float2 position;
    simd::float2 vw;
    simd::float2 convex;
};

struct Varyings {
    simd::float4 position [[position]];
    simd::float2 vw;
    simd::float2 convex;
};

struct FragInfo {
    float4 color;
};

vertex Varyings curvesVertexShader(uint vertexID [[vertex_id]],
                           constant VertInput* vert_input,
                           constant VertInfo& vert_info) {
    Varyings varyings;
    varyings.position = vert_info.mvp * float4(vert_input[vertexID].position.x,
                                               vert_input[vertexID].position.y,
                                       0.0f,
                                       1.0f);
    varyings.vw = vert_input[vertexID].vw;
    varyings.convex = vert_input[vertexID].convex;
    varyings.position.z = vert_info.depth;
    return varyings;
}

// https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-25-rendering-vector-art-gpu
fragment float4 curvesFragmentShader(Varyings varyings [[stage_in]], constant FragInfo& frag_info) {
    float t = varyings.vw.x * varyings.vw.x - varyings.vw.y;
    if ((varyings.convex.x == 1 && t > 0.0) || (varyings.convex.x != 1 &&  t < 0.0)) {
        return float4(0, 1, 0, 1);
    }
    return frag_info.color;
//    float2 p = varyings.vw;
//    // Gradients
//    float2 px = dfdx(p);
//    float2 py = dfdy(p);
//    // Chain rule
//    float fx = (2 * p.x) * px.x - px.y;
//    float fy = (2 * p.x) * py.x - py.y;
//    // Signed distance
//    float sd = ((p.x * p.x - p.y) / sqrt(fx * fx + fy * fy));
//    // Linear alpha
//    float alpha = 0.5 - sd;
//
//    if (alpha > 1) {
//      // Inside
//        if (varyings.convex.x >= 1.0) {
//            return frag_info.color;
//        } else {
//            return float4(0, 1, 0, 1);
//        }
//    } else if (alpha < 0) {
//      // Outisde
//        if (varyings.convex.x >= 1.0) {
//            return float4(0, 1, 0, 1);
//        } else {
//            return frag_info.color;
//        }
//    } else {
//      // Boundary
//        if (varyings.convex.x >= 1.0) {
//            return frag_info.color * alpha;
//        } else {
//            return frag_info.color * (1 - alpha);
//        }
//    }
}

