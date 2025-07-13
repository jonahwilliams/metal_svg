#include <metal_stdlib>
using namespace metal;

// Kernel width = (2 * kernel_radius) + 1
constant int kernel_radius = 16;

struct BoxBlurParameters {
    float kernel_weight;
};

kernel void boxBlurKernelW(
                          texture2d<float, access::read> source [[texture(0)]],
                          texture2d<float, access::write> dest [[texture(1)]],
                          constant BoxBlurParameters& params,
                          uint2 gid [[thread_position_in_grid]]
                          ) {
    // Step 1: initialize the rolling average. once this is done we no longer
    // need to care about the kernel weight, because moving forward 1 px will always
    // drop one prev value.
    float4 rolling_average = 0.0h;
    for (int i = -kernel_radius; i < kernel_radius; i++) {
        rolling_average += source.read(uint2(i, gid.y));
    }
    dest.write(rolling_average * params.kernel_weight, uint2(0, gid.y));
    
    // Now move the window forward, letting the left most value fall off and
    // adding in the right most value.
    int width = source.get_width();
    for (int i = 1; i < width; i++) {
        int2 coord = int2(i, gid.y);
        uint left_x = i - kernel_radius;
        uint right_x = i + kernel_radius;

        rolling_average -= source.read(uint2(left_x, gid.y));
        rolling_average += source.read(uint2(right_x, gid.y));
        dest.write(rolling_average * params.kernel_weight, uint2(coord));
    }
}

kernel void boxBlurKernelH(texture2d<float, access::read> source [[texture(0)]],
                           texture2d<float, access::write> dest [[texture(1)]],
                          constant BoxBlurParameters& params,
                          uint2 gid [[thread_position_in_grid]]
                          ) {
    // Copy Paste of above.
    float4 rolling_average = 0.0h;
    for (int i = -kernel_radius; i < kernel_radius; i++) {
        rolling_average += source.read(uint2(gid.x, i));
    }
    dest.write(rolling_average * params.kernel_weight, uint2(gid.x, 0));
    
    // Now move the window forward, letting the left most value fall off and
    // adding in the right most value.
    int height = source.get_height();
    for (int i = 1; i < height; i++) {
        int2 coord = int2(gid.x, i);
        uint left_y = i - kernel_radius;
        uint right_y = i + kernel_radius;

        rolling_average -= source.read(uint2(gid.x, left_y));
        rolling_average += source.read(uint2(gid.x, right_y));
        dest.write(rolling_average * params.kernel_weight, uint2(coord));
    }
}


/// Regular Gaussian.

constant int kMaxKernelRadius = 16;
constant int kThreadGroupSize = 1024;

struct GaussianBlurParameters {
    int2 length;
    float weights[kMaxKernelRadius];
};

kernel void gaussianBlurKernelW(
                          texture2d<float, access::sample> source [[texture(0)]],
                          texture2d<float, access::write> dest [[texture(1)]],
                          sampler sampler [[sampler(0)]],
                          constant GaussianBlurParameters& params,
                          uint2 gid [[thread_position_in_grid]],
                          uint tid [[ thread_index_in_threadgroup ]]
                        ) {
    float2 uv_step = 1.0f / float2(dest.get_width(), dest.get_height());
    float2 uv = (float2(gid) + float2(0.5)) * uv_step;
    float4 result = source.sample(sampler, uv) * params.weights[0];
    for (int i = 1; i < params.length.x; i++) {
        float2 offset = i * float2(uv_step.x, 0);
        result += source.sample(sampler, uv + offset) * params.weights[i];
        result += source.sample(sampler, uv - offset) * params.weights[i];
    }
    dest.write(result, gid);
}


kernel void gaussianBlurKernelH(
                          texture2d<float, access::sample> source [[texture(0)]],
                          texture2d<float, access::write> dest [[texture(1)]],
                          sampler sampler [[sampler(0)]],
                          constant GaussianBlurParameters& params,
                          uint2 gid [[thread_position_in_grid]],
                          uint tid [[ thread_index_in_threadgroup ]]
                        ) {
    float2 uv_step = 1.0f / float2(dest.get_width(), dest.get_height());
    float2 uv = (float2(gid) + float2(0.5)) * uv_step;
    float4 result = source.sample(sampler, uv) * params.weights[0];
    for (int i = 1; i < params.length.x; i++) {
        float2 offset = i * float2(0, uv_step.y);
        result += source.sample(sampler, uv + offset) * params.weights[i];
        result += source.sample(sampler, uv - offset) * params.weights[i];
    }
    dest.write(result, gid);
}

struct DownsampleParams {
    float2 uv_step;
};

kernel void computeDownsample(
                      texture2d<half, access::sample> source [[texture(0)]],
                      texture2d<half, access::write> dest [[texture(1)]],
                      sampler linear_downsample [[sampler(0)]],
                      constant DownsampleParams& params [[buffer(0)]],
                      uint2 s_gid [[thread_position_in_grid]]
                      ) {
    uint2 gid = s_gid * 2;
    float2 f_gid = float2(gid);
    
    dest.write(source.sample(linear_downsample, (f_gid + 1) * params.uv_step), gid);
    dest.write(source.sample(linear_downsample, (f_gid + float2(2, 1)) * params.uv_step), gid + uint2(1, 0));
    dest.write(source.sample(linear_downsample, (f_gid + float2(1, 2)) * params.uv_step), gid + uint2(0, 1));
    dest.write(source.sample(linear_downsample, (f_gid + 2) * params.uv_step), gid + uint2(1, 1));
}

struct BufferParams {
    uint2 size;
};

kernel void computeDownsampleToBuffer(
                      texture2d<float, access::sample> source [[texture(0)]],
                      sampler linear_downsample [[sampler(0)]],
                      constant BufferParams& params [[buffer(0)]],
                      device float4* dest [[buffer(1)]],
                     uint2 gid [[thread_position_in_grid]]
                      ) {
    float2 uv_step = 1.0f / float2(params.size.x, params.size.y);
    float2 uv = (float2(gid) + 1) * uv_step;
    float4 result = source.sample(linear_downsample, uv);
    // Write in row major format.
    uint index = gid.y * params.size.x + gid.x;
    dest[index] = result;
}

kernel void gaussianBlurKernelBufferW(
                          device float4* src [[buffer(0)]],
                          constant GaussianBlurParameters& params [[buffer(1)]],
                          constant BufferParams& buffer_params [[buffer(2)]],
                          device float4* dest [[buffer(3)]],
                          uint2 gid [[thread_position_in_grid]],
                          uint tid [[ thread_index_in_threadgroup ]]
                        ) {
    uint index = gid.y * buffer_params.size.x + gid.x;
    float4 result = src[index] * params.weights[0];
    for (int i = 1; i < 4; i++) {
//        if (index + 1 < (buffer_params.size.x * buffer_params.size.y)) {
            result += src[index + i] * params.weights[i];
//        }
//        if (index - i > -1) {
            result += src[index - i] * params.weights[i];
//        }
    }
    // Now transpose to column major format.
    
    uint result_index = gid.x * buffer_params.size.y + gid.y;
    dest[result_index] = result;
}

kernel void gaussianBlurKernelBufferH(
                          device float4* src [[buffer(0)]],
                          constant GaussianBlurParameters& params [[buffer(1)]],
                          constant BufferParams& buffer_params [[buffer(2)]],
                          texture2d<float, access::write> dest [[texture(0)]],
                          uint2 gid [[thread_position_in_grid]],
                          uint tid [[ thread_index_in_threadgroup ]]
                        ) {
    uint index = gid.x * buffer_params.size.y + gid.y;
    float4 result = src[index] * params.weights[0];
    for (int i = 1; i < 4; i++) {
       // if (index + i < (buffer_params.size.x * buffer_params.size.y)) {
            result += src[index + i] * params.weights[i];
     //   }
        
    //    if (index - i > -1) {
            result += src[index - i] * params.weights[i];
      //  }
    }
   
    dest.write(result, gid);
}
