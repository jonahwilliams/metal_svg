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

vertex Varyings vertexShader(uint vertexID [[vertex_id]],
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

struct FragInfo {
    simd::float4 color;
};

fragment float4 fragmentShader(Varyings varyings [[stage_in]], constant FragInfo& frag_info) {
    return frag_info.color;
}

// Texture Sampler Shader

struct TextureVaryings {
    simd::float4 position [[position]];
    simd::float2 uv;
};

struct TextureVertInfo {
    // LTRB format.
    simd::float4 dest_rect;
};


vertex TextureVaryings textureVertexShader(uint vertexID [[vertex_id]], constant VertInput* vert_input, constant VertInfo& vert_info, constant TextureVertInfo& texture_vert_info) {
    TextureVaryings varyings;
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

struct TextureFragInfo {
    float alpha;
};

fragment float4 textureFragmentShader(TextureVaryings varyings [[stage_in]], constant TextureFragInfo& frag_info, texture2d<float> texture [[texture(0)]],
    sampler sampler [[sampler(0)]]) {
    return texture.sample(sampler, varyings.uv) * frag_info.alpha;
}


// Linear Gradient Shader

struct GradientVaryings {
    simd::float4 position [[position]];
    simd::float2 canvas_position;
};

vertex GradientVaryings gradientVertexShader(uint vertexID [[vertex_id]],
                                             constant VertInput* vert_input,
                                             constant VertInfo& vert_info) {
    GradientVaryings varyings;
    varyings.position = vert_info.mvp * float4(vert_input[vertexID].position.x,
                                                       vert_input[vertexID].position.y,
                                       0.0f,
                                       1.0f);
    varyings.position.z = vert_info.depth;
    varyings.canvas_position = vert_input[vertexID].position;
    return varyings;
}

struct LinearGradientFragInfo {
    simd::float4 start_end;
};

fragment float4 linearGradientFragmentShader(
    GradientVaryings varyings [[stage_in]],
    constant LinearGradientFragInfo& frag_info,
    texture2d<float> colorTexture [[texture(0)]],
    sampler gradientSampler [[sampler(0)]]) {
  simd::float2 start_to_end = frag_info.start_end.zw - frag_info.start_end.xy;
  simd::float2 start_to_position = varyings.canvas_position - frag_info.start_end.xy;
  float t = dot(start_to_position, start_to_end) /
    dot(start_to_end, start_to_end);
  
  return colorTexture.sample(gradientSampler, simd::float2(t, 0.5));
}

// Radial Gradient Shader

struct RadialGradientFragInfo {
    simd::float4 center_and_radius;
};

fragment float4 radialGradientFragmentShader(
    GradientVaryings varyings [[stage_in]],
    constant RadialGradientFragInfo& frag_info,
    texture2d<float> colorTexture [[texture(0)]],
    sampler gradientSampler [[sampler(0)]]) {
  float t = length(varyings.canvas_position - frag_info.center_and_radius.xy) /
    frag_info.center_and_radius.z;
  
  return colorTexture.sample(gradientSampler, simd::float2(t, 0.5));
}

