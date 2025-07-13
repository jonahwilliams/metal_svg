#include <metal_stdlib>
using namespace metal;

struct VertInfo {
    float4x4 mvp;
    float depth;
    float padding;
};

struct VertInput {
    simd::float2 position;
};

struct Varyings {
    simd::float4 position [[position]];
};

vertex Varyings stencilVertexShader(uint vertexID [[vertex_id]],
                           constant VertInput* vert_input,
                           constant VertInfo& vert_info) {
    Varyings varyings;
    varyings.position = vert_info.mvp * float4(vert_input[vertexID].position.x,
                                               vert_input[vertexID].position.y,
                                       0.0f,
                                       1.0f);
    varyings.position.z = vert_info.depth;
    return varyings;
}

fragment void stencilFragmentShader(Varyings varyings [[stage_in]]) {}
