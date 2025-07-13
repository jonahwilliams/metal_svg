#include <metal_stdlib>
using namespace metal;

#define M_PI        3.14159265358979323846264338327950288

struct FilterVertInfo {
    float4x4 mvp;
    float depth;
    float padding;
};

struct FilterVertInput {
    simd::float2 position;
};

struct FilterVaryings {
    simd::float4 position [[position]];
    simd::float2 uv;
};

struct BlurFilterVertInfo {
    // LTRB format.
    simd::float4 dest_rect;
};

vertex FilterVaryings filterVertexShader(uint vertexID [[vertex_id]], constant FilterVertInput* vert_input, constant FilterVertInfo& vert_info, constant BlurFilterVertInfo& texture_vert_info) {
    FilterVaryings varyings;
    varyings.position = vert_info.mvp * float4(vert_input[vertexID].position.x,
                                               vert_input[vertexID].position.y,
                                       0.0f,
                                       1.0f);
    varyings.position.z = vert_info.depth;
    
    float2 s = 1.0f / float2(
        texture_vert_info.dest_rect.z - texture_vert_info.dest_rect.x,
        texture_vert_info.dest_rect.w - texture_vert_info.dest_rect.y);
    float2 t = texture_vert_info.dest_rect.xy * -s;
    varyings.uv = vert_input[vertexID].position  * s + t;
    return varyings;
}

/// Gaussian Blur

struct BlurFragInfo {
    // How much to adjust uvs to move the sample point 1 pixel.
    float2 uv_offset;
    // the blur standard deviation. 90% of blur is approximately 3 * sigma
    // pixels in each direction.
    float weights[32];
    int length;
};

float2 Gaussian(float2 x, float2 sigma) {
    return exp(-0.5f * (x * x) / (sigma * sigma)) /
                  (sqrt(2.0f * M_PI) * sigma);
}

fragment half4 blurFragmentShader(FilterVaryings varyings [[stage_in]], constant BlurFragInfo& frag_info, texture2d<half> texture [[texture(0)]],
    sampler sampler [[sampler(0)]]) {
    
    half4 result = texture.sample(sampler, varyings.uv) * frag_info.weights[0];
    float2 half_offset = frag_info.uv_offset / 2;
    
    // Iterate by a stride of 2 to exploit linear sampling of source texture.
    // This requires averaging the gaussian coef from both original points.
    for (int i = 1; i < frag_info.length; i += 2) {
        float2 offset = (frag_info.uv_offset * i) + half_offset;
        float coef = frag_info.weights[i] + frag_info.weights[i + 1];
        
        result += texture.sample(sampler, varyings.uv + offset) * coef;
        result += texture.sample(sampler, varyings.uv - offset) * coef;
    }
    return result;
}

// Color Filter

struct ColorFilterFragInfo {
    float4x4 m;
    float4 v;
};

float4 Unpremultiply(float4 color) {
  if (color.a == 0.0) {
    return float4(0.0);
  }
  return float4(color.rgb / color.a, color.a);
}

// A color filter that transforms colors through a 4x5 color matrix.
//
// This filter can be used to change the saturation of pixels, convert from YUV
// to RGB, etc.
//
// 4x5 matrix for transforming the color and alpha components of a Bitmap.
// The matrix can be passed as single array, and is treated as follows:
//
//   [ a, b, c, d, e,
//     f, g, h, i, j,
//     k, l, m, n, o,
//     p, q, r, s, t ]
//
// When applied to a color [R, G, B, A], the resulting color is computed as:
//
//    R’ = a*R + b*G + c*B + d*A + e;
//    G’ = f*R + g*G + h*B + i*A + j;
//    B’ = k*R + l*G + m*B + n*A + o;
//    A’ = p*R + q*G + r*B + s*A + t;
//
// That resulting color [R’, G’, B’, A’] then has each channel clamped to the 0
// to 255 range.
//fragment float4 colorFilterFragmentShader(FilterVaryings varyings [[stage_in]], constant ColorFilterFragInfo& frag_info, texture2d<float4> texture [[texture(0)]],
//    sampler sampler [[sampler(0)]]) {
//    
//    float4 color = Unpremultiply(texture.sample(sampler, varyings.uv));
//    float4 c = clamp(frag_info.m * color + frag_info.v, 0, 1);
//    return float4(c.xyz * c.w, c.w);
//}
