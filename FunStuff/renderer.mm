#include "renderer.hpp"

#include <iostream>
#include <simd/simd.h>

#include "geom/bezier.hpp"
#include "geom/svg.hpp"
#include "pipelines.hpp"

#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define NANOSVG_ALL_COLOR_KEYWORDS // Include full list of color keywords.
#define NANOSVG_IMPLEMENTATION     // Expands implementation
#include "third_party/nanosvg/src/nanosvg.h"

namespace flatland {

BufferBindingCache::BufferBindingCache(MTL::RenderCommandEncoder *encoder)
    : encoder_(encoder) {
    for (int i = 0; i < 6; i++) {
        bound_buffers_vertex_[i] = nullptr;
        bound_buffers_fragment_[i] = nullptr;
    }
}

void BufferBindingCache::Bind(MTL::Buffer *buffer, size_t offset,
                              size_t index) {
    if (bound_buffers_vertex_[index] == buffer) {
        encoder_->setVertexBufferOffset(offset, index);
        return;
    }
    bound_buffers_vertex_[index] = buffer;
    encoder_->setVertexBuffer(buffer, offset, index);
}

void BufferBindingCache::BindFragment(MTL::Buffer *buffer, size_t offset,
                                      size_t index) {
    if (bound_buffers_fragment_[index] == buffer) {
        encoder_->setFragmentBufferOffset(offset, index);
        return;
    }
    bound_buffers_fragment_[index] = buffer;
    encoder_->setFragmentBuffer(buffer, offset, index);
}

void BufferBindingCache::BindPipeline(MTL::RenderPipelineState *state) {
    if (state == last_state_) {
        return;
    }
    last_state_ = state;
    encoder_->setRenderPipelineState(state);
}

void BufferBindingCache::BindDepthStencil(MTL::DepthStencilState *state) {
    if (state == last_ds_state_) {
        return;
    }
    last_ds_state_ = state;
    encoder_->setDepthStencilState(state);
}

///

Renderer::Renderer(MTL::Device *metal_device)
    : metal_device_(metal_device),
      triangulator_(std::make_unique<Triangulator>()),
      host_buffer_(std::make_unique<HostBuffer>(metal_device)),
      pipelines_(std::make_unique<Pipelines>(metal_device, kEnableMSAA)) {
    command_queue_ = metal_device->newCommandQueue();

    convex_label_ = NS::String::string("Convex Draw", NS::ASCIIStringEncoding);
    complex_label_ =
        NS::String::string("NonConvex Draw", NS::ASCIIStringEncoding);
    clip_label_ = NS::String::string("Clip Draw", NS::ASCIIStringEncoding);
    save_label_ = NS::String::string("Save Layer", NS::ASCIIStringEncoding);

    // Samplers
    {
        MTL::SamplerDescriptor *sampler_desc = MTL::SamplerDescriptor::alloc();
        sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
        sampler_desc->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
        sampler_desc->setRAddressMode(
            MTL::SamplerAddressModeClampToBorderColor);
        sampler_desc->setSAddressMode(
            MTL::SamplerAddressModeClampToBorderColor);
        sampler_desc->setTAddressMode(
            MTL::SamplerAddressModeClampToBorderColor);
        sampler_desc->setNormalizedCoordinates(true);
        sampler_desc->setMaxAnisotropy(1);
        sampler_desc->setCompareFunction(MTL::CompareFunctionAlways);
        sampler_desc->setBorderColor(MTL::SamplerBorderColorTransparentBlack);

        blur_filter_sampler_ = metal_device_->newSamplerState(sampler_desc);
        sampler_desc->release();
    }
    {
        MTL::SamplerDescriptor *sampler_desc = MTL::SamplerDescriptor::alloc();
        sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
        sampler_desc->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
        sampler_desc->setRAddressMode(MTL::SamplerAddressModeClampToEdge);
        sampler_desc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
        sampler_desc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
        sampler_desc->setNormalizedCoordinates(true);
        sampler_desc->setMaxAnisotropy(1);
        sampler_desc->setCompareFunction(MTL::CompareFunctionAlways);

        gradient_sampler_ = metal_device_->newSamplerState(sampler_desc);
        sampler_desc->release();
    }
    {
        MTL::SamplerDescriptor *sampler_desc = MTL::SamplerDescriptor::alloc();
        sampler_desc->setMagFilter(MTL::SamplerMinMagFilterNearest);
        sampler_desc->setMinFilter(MTL::SamplerMinMagFilterNearest);
        sampler_desc->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
        sampler_desc->setRAddressMode(
            MTL::SamplerAddressModeClampToBorderColor);
        sampler_desc->setSAddressMode(
            MTL::SamplerAddressModeClampToBorderColor);
        sampler_desc->setTAddressMode(
            MTL::SamplerAddressModeClampToBorderColor);
        sampler_desc->setBorderColor(MTL::SamplerBorderColorTransparentBlack);
        sampler_desc->setNormalizedCoordinates(true);
        sampler_desc->setMaxAnisotropy(1);
        sampler_desc->setCompareFunction(MTL::CompareFunctionAlways);

        save_layer_sampler_ = metal_device_->newSamplerState(sampler_desc);
        sampler_desc->release();
    }

    // Descriptors
    {
        // For a non-zero fill mode, front facing triangles increment the
        // stencil value, back facing triangles decrement it. All non-zero
        // fragments will be shaded.
        auto desc = MTL::DepthStencilDescriptor::alloc()->init();

        MTL::StencilDescriptor *front_desc =
            MTL::StencilDescriptor::alloc()->init();
        front_desc->setStencilCompareFunction(MTL::CompareFunctionAlways);
        front_desc->setDepthStencilPassOperation(
            MTL::StencilOperationIncrementWrap);

        MTL::StencilDescriptor *back_desc =
            MTL::StencilDescriptor::alloc()->init();
        back_desc->setStencilCompareFunction(MTL::CompareFunctionAlways);
        back_desc->setDepthStencilPassOperation(
            MTL::StencilOperationDecrementWrap);

        desc->setFrontFaceStencil(front_desc);
        desc->setBackFaceStencil(back_desc);
        desc->setDepthWriteEnabled(false);
        desc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);

        non_zero_stencil_ = metal_device_->newDepthStencilState(desc);

        front_desc->release();
        back_desc->release();
        desc->release();
    }
    // Even Odd Stencil.
    {
        auto desc = MTL::DepthStencilDescriptor::alloc()->init();

        MTL::StencilDescriptor *front_desc =
            MTL::StencilDescriptor::alloc()->init();
        front_desc->setStencilCompareFunction(MTL::CompareFunctionEqual);
        front_desc->setDepthStencilPassOperation(
            MTL::StencilOperationIncrementWrap);
        front_desc->setDepthFailureOperation(
            MTL::StencilOperationDecrementWrap);

        MTL::StencilDescriptor *back_desc =
            MTL::StencilDescriptor::alloc()->init();
        back_desc->setStencilCompareFunction(MTL::CompareFunctionEqual);
        back_desc->setDepthStencilPassOperation(
            MTL::StencilOperationIncrementWrap);
        back_desc->setDepthFailureOperation(MTL::StencilOperationDecrementWrap);

        desc->setFrontFaceStencil(front_desc);
        desc->setBackFaceStencil(back_desc);
        desc->setDepthWriteEnabled(false);
        desc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);

        even_odd_stencil_ = metal_device_->newDepthStencilState(desc);

        front_desc->release();
        back_desc->release();
        desc->release();
    }

    {
        auto desc = MTL::DepthStencilDescriptor::alloc()->init();
        desc->setDepthWriteEnabled(false);
        noop_stencil_ = metal_device_->newDepthStencilState(desc);
        desc->release();
    }
    {

        MTL::DepthStencilDescriptor *desc =
            MTL::DepthStencilDescriptor::alloc()->init();
        desc->setDepthWriteEnabled(true);
        desc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);

        convex_draw_ = metal_device_->newDepthStencilState(desc);
        desc->release();
    }
    {

        MTL::DepthStencilDescriptor *desc =
            MTL::DepthStencilDescriptor::alloc()->init();
        desc->setDepthWriteEnabled(false);
        desc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);

        transparent_convex_draw_ = metal_device_->newDepthStencilState(desc);
        desc->release();
    }
    {
        // For the cover draw, we need to replace the stencil value with the
        // reference, and ensure only non-zero fragments are drawn.
        MTL::DepthStencilDescriptor *desc =
            MTL::DepthStencilDescriptor::alloc()->init();
        MTL::StencilDescriptor *front_desc =
            MTL::StencilDescriptor::alloc()->init();
        front_desc->setStencilCompareFunction(MTL::CompareFunctionNotEqual);
        front_desc->setDepthStencilPassOperation(MTL::StencilOperationReplace);
        desc->setFrontFaceStencil(front_desc);
        desc->setBackFaceStencil(front_desc);
        desc->setDepthWriteEnabled(true);
        desc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);

        cover_stencil_opaque_ = metal_device_->newDepthStencilState(desc);
        front_desc->release();
        desc->release();
    }
    {
        // For Intersect clips, write only where the stencil is zero (reference)
        MTL::DepthStencilDescriptor *desc =
            MTL::DepthStencilDescriptor::alloc()->init();
        MTL::StencilDescriptor *front_desc =
            MTL::StencilDescriptor::alloc()->init();
        front_desc->setStencilCompareFunction(MTL::CompareFunctionEqual);
        front_desc->setDepthStencilPassOperation(MTL::StencilOperationReplace);
        front_desc->setStencilFailureOperation(MTL::StencilOperationReplace);
        desc->setFrontFaceStencil(front_desc);
        desc->setBackFaceStencil(front_desc);
        desc->setDepthWriteEnabled(true);
        desc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);

        clip_depth_write_ = metal_device_->newDepthStencilState(desc);
        front_desc->release();
        desc->release();
    }
    {
        // For the cover draw, we need to replace the stencil value with the
        // reference, and ensure only non-zero fragments are drawn.
        MTL::DepthStencilDescriptor *desc =
            MTL::DepthStencilDescriptor::alloc()->init();
        MTL::StencilDescriptor *front_desc =
            MTL::StencilDescriptor::alloc()->init();
        front_desc->setStencilCompareFunction(MTL::CompareFunctionNotEqual);
        front_desc->setDepthStencilPassOperation(MTL::StencilOperationReplace);
        desc->setFrontFaceStencil(front_desc);
        desc->setBackFaceStencil(front_desc);
        desc->setDepthWriteEnabled(false);
        desc->setDepthCompareFunction(MTL::CompareFunctionAlways);

        cover_stencil_transparent_ = metal_device_->newDepthStencilState(desc);
        front_desc->release();
        desc->release();
    }

//    image_ = ::nsvgParseFromFile(
//        "/Users/jaydog/Downloads/inputs/svg/paris-30k.svg", "px", 96);
   image_ = ::nsvgParse(GetGhostscript().data(), "px", 96);
    star_ = ::nsvgParse(GetStar().data(), "px", 96);

    InitPicture();
}

Renderer::~Renderer() {
    gradient_sampler_->release();
    cover_stencil_opaque_->release();
    cover_stencil_transparent_->release();
    non_zero_stencil_->release();
    clip_depth_write_->release();
    ::nsvgDelete(image_);
    ::nsvgDelete(star_);
}

void Renderer::InitPicture() {
    Canvas canvas(host_buffer_.get(), triangulator_.get());

    //    std::array<Color, 3> gradient_colors = {kRed, kGreen, kBlue};
    //    auto linear_gradient = canvas.CreateRadialGradient(
    //        {1000, 1000}, 1200, gradient_colors.data(),
    //        gradient_colors.size());
    //
    //    canvas.DrawRect(Rect::MakeLTRB(0, 0, 2000, 2000),
    //                    {.color = kRed, .gradient = linear_gradient});

        canvas.Save();
        canvas.Translate(500, 500);

    //    canvas.SaveLayer(0.7, GaussianFilter{.sigma = 8});

    // Test Clip.

//    canvas.Save();
//    PathBuilder clip_rect;
//    clip_rect.AddRect(Rect::MakeLTRB(200, 200, 800, 800));
//    canvas.ClipPath(clip_rect.takePath(), ClipStyle::kDifference);
//    canvas.DrawRect(Rect::MakeLTRB(0, 0, 1000, 1000), {.color = kRed});
//    canvas.Restore();

    // canvas.DrawRect(Rect::MakeLTRB(0, 0, 100, 100), {.color = kBlue});

    //    canvas.Rotate(0.5);
        Scalar index = 0.0f;
        for (auto shape = image_->shapes; shape != NULL;
             shape = shape->next, index++) {
            PathBuilder builder;
            for (auto path = shape->paths; path != NULL; path = path->next) {
                Scalar scale = 4;
                for (int i = 0; i < path->npts - 1; i += 3) {
                    float *p = &path->pts[i * 2];
                    if (i == 0) {
                        builder.moveTo(p[0] * scale, p[1] * scale);
                    }
                    builder.cubicTo(Point{p[2], p[3]} * scale,
                                    Point{p[4], p[5]} * scale,
                                    Point{p[6], p[7]} * scale);
                }
                builder.close();
            }
    
            auto path = builder.takePath();
            if (shape->fill.type == NSVGpaintType::NSVG_PAINT_COLOR) {
                canvas.DrawPath(
                                path,
                                {.color =
                                    Color::FromRGB(shape->fill.color).WithAlpha(shape->opacity),
                                .fill_rule = shape->fillRule ==
                                NSVGfillRule::NSVG_FILLRULE_NONZERO ?
                                FillRule::kNonZero : FillRule::kEvenOdd});
            }
            if (shape->stroke.type == NSVGpaintType::NSVG_PAINT_COLOR) {
                canvas.DrawPath(path, {.color =
                Color::FromRGB(shape->stroke.color)
                                                    .WithAlpha(shape->opacity),
                                       .stroke = true,
                                       .stroke_width = shape->strokeWidth});
            }
        }
        canvas.Restore();

    RenderProgram program = canvas.Prepare();
    picture_ = std::move(program);
}

static constexpr Scalar kDepthEpsilon = 1.0f / 262144.0;

void Renderer::DrawPathTriangulated(MTL::RenderCommandEncoder *encoder,
                                    BufferBindingCache &cache,
                                    const Matrix &mvp, const Command &command) {
    bool is_opaque_draw = command.paint.IsOpaque();
    struct UniformData {
        Matrix mvp;
        float depth;
        float padding;
    };

    // Fill uniform buffer for transform.
    BufferView vert_uniform_buffer =
        host_buffer_->GetTransientArena(sizeof(UniformData), 16u);
    UniformData data;
    data.mvp = mvp;
    data.depth = 1 - (command.depth_count * kDepthEpsilon);
    ::memcpy(vert_uniform_buffer.contents(), &data, sizeof(UniformData));

    // Draw shape. First by stenciling interior and then by restoring
    // via cover draw.

    // If path is convex, stenciling can be skipped.
    if (command.is_convex) {
        if (command.paint.stroke) {
            encoder->pushDebugGroup(
                NS::String::string("Stroke Draw", NS::ASCIIStringEncoding));
        } else {
            encoder->pushDebugGroup(convex_label_);
        }
        PrepareColorSource(encoder, cache, command.paint);
        cache.Bind(vert_uniform_buffer.buffer, vert_uniform_buffer.offset, 1);

        cache.Bind(command.vertex_buffer.buffer, command.vertex_buffer.offset,
                   0);

        if (is_opaque_draw) {
            cache.BindDepthStencil(convex_draw_);
        } else {
            cache.BindDepthStencil(transparent_convex_draw_);
        }

        if (command.index_buffer) {
            encoder->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle, command.index_count,
                MTL::IndexTypeUInt16, command.index_buffer.buffer,
                command.index_buffer.offset);
        } else {
            NS::UInteger start = 0;
            NS::UInteger count = command.index_count;
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
        }
        encoder->popDebugGroup();
        return;
    }

    // Stencil
    encoder->pushDebugGroup(complex_label_);
    cache.Bind(vert_uniform_buffer.buffer, vert_uniform_buffer.offset, 1);
    {
        cache.BindPipeline(pipelines_->GetStencil());
        cache.Bind(command.vertex_buffer.buffer, command.vertex_buffer.offset,
                   0);
        if (command.paint.fill_rule == FillRule::kNonZero) {
            cache.BindDepthStencil(non_zero_stencil_);
        } else {
            cache.BindDepthStencil(even_odd_stencil_);
        }

        if (command.index_buffer) {
            encoder->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle, command.index_count,
                MTL::IndexTypeUInt16, command.index_buffer.buffer,
                command.index_buffer.offset);
        } else {
            NS::UInteger start = 0;
            NS::UInteger count = command.index_count;
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
        }
    }

    // Cover
    // Generate quad for cover stencil restore + fill.
    BufferView cover_buffer =
        host_buffer_->GetTransientArena(6 * sizeof(Point), 16u);
    std::array<Scalar, 12> bounds = command.bounds.GetQuad();
    std::memcpy(cover_buffer.contents(), bounds.data(), 6 * sizeof(Point));

    {
        PrepareColorSource(encoder, cache, command.paint);
        cache.Bind(cover_buffer.buffer, cover_buffer.offset, 0);

        if (is_opaque_draw) {
            cache.BindDepthStencil(cover_stencil_opaque_);
        } else {
            cache.BindDepthStencil(cover_stencil_transparent_);
        }

        NS::UInteger start = 0;
        NS::UInteger count = 6;
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
    }
    encoder->popDebugGroup();
}

void Renderer::PrepareColorSource(MTL::RenderCommandEncoder *encoder,
                                  BufferBindingCache &cache,
                                  const Paint &paint) {
    if (const LinearGradient *gradient =
            std::get_if<LinearGradient>(&paint.gradient)) {
        BufferView frag_uniform_buffer =
            host_buffer_->GetTransientArena(sizeof(simd::float4), 16u);

        // TODO: pre-transform points.
        //        simd::float4 transformed = matrix * simd::float4(point, 0, 1);
        //        return transformed.xy / transformed.w;
        //

        simd::float4 data{gradient->start.x, gradient->start.y, gradient->end.x,
                          gradient->end.y};
        ::memcpy(frag_uniform_buffer.contents(), &data, sizeof(simd::float4));

        cache.BindPipeline(pipelines_->GetLinearGradient(BlendMode::kSrcOver));
        cache.BindFragment(frag_uniform_buffer.buffer,
                           frag_uniform_buffer.offset, 0);

        encoder->setFragmentTexture(
            host_buffer_->GetTexture(gradient->texture_index), 0);
        encoder->setFragmentSamplerState(gradient_sampler_, 0);
    } else if (const RadialGradient *gradient =
                   std::get_if<RadialGradient>(&paint.gradient)) {
        BufferView frag_uniform_buffer =
            host_buffer_->GetTransientArena(sizeof(simd::float4), 16u);

        // TODO: pre-transform points.
        //        simd::float4 transformed = matrix * simd::float4(point, 0, 1);
        //        return transformed.xy / transformed.w;
        //

        simd::float4 data{gradient->center.x, gradient->center.y,
                          gradient->radius, 0};
        ::memcpy(frag_uniform_buffer.contents(), &data, sizeof(simd::float4));

        cache.BindPipeline(pipelines_->GetRadialGradient(BlendMode::kSrcOver));
        cache.BindFragment(frag_uniform_buffer.buffer,
                           frag_uniform_buffer.offset, 0);

        encoder->setFragmentTexture(
            host_buffer_->GetTexture(gradient->texture_index), 0);
        encoder->setFragmentSamplerState(gradient_sampler_, 0);
    } else {
        // Solid color draw.
        BufferView frag_uniform_buffer =
            host_buffer_->GetTransientArena(sizeof(Color), 16u);
        Color p_color = paint.color.Premultiply();
        ::memcpy(frag_uniform_buffer.contents(), &p_color, sizeof(Color));

        cache.BindPipeline(pipelines_->GetSolidColor(
            paint.color.is_opaque() ? BlendMode::kSrc : BlendMode::kSrcOver));
        cache.BindFragment(frag_uniform_buffer.buffer,
                           frag_uniform_buffer.offset, 0);
    }
}

void Renderer::DrawTexture(MTL::RenderCommandEncoder *encoder,
                           BufferBindingCache &cache, const Matrix &mvp,
                           const Rect &dest, Scalar depth, Scalar alpha,
                           MTL::Texture *texture) {
    struct UniformData {
        Matrix mvp;
        float depth;
        float padding;
    };

    // Fill uniform buffer for transform.
    UniformData data;
    data.mvp = mvp;
    data.depth = 1 - (depth * kDepthEpsilon);
    BufferView vert_uniform_buffer =
        host_buffer_->GetTransientArena(sizeof(data), 16u);
    ::memcpy(vert_uniform_buffer.contents(), &data, sizeof(data));

    struct TextureVertInfo {
        // LTRB format.
        Rect dest_rect;
    };
    TextureVertInfo texture_data;
    texture_data.dest_rect = dest;
    BufferView texture_vert_uniform_buffer =
        host_buffer_->GetTransientArena(sizeof(texture_data), 16u);
    ::memcpy(texture_vert_uniform_buffer.contents(), &texture_data,
             sizeof(texture_data));

    struct FragUniformData {
        float alpha;
    };

    FragUniformData frag_data;
    frag_data.alpha = alpha;

    BufferView frag_uniform_buffer =
        host_buffer_->GetTransientArena(sizeof(frag_data), 16u);
    ::memcpy(frag_uniform_buffer.contents(), &frag_data, sizeof(frag_data));

    BufferView cover_buffer =
        host_buffer_->GetTransientArena(6 * sizeof(Point), 16u);
    std::array<Scalar, 12> bounds = dest.GetQuad();
    std::memcpy(cover_buffer.contents(), bounds.data(), 6 * sizeof(Point));

    encoder->pushDebugGroup(save_label_);
    cache.BindPipeline(pipelines_->GetTextureFill(BlendMode::kSrcOver));
    cache.Bind(cover_buffer.buffer, cover_buffer.offset, 0);
    cache.Bind(vert_uniform_buffer.buffer, vert_uniform_buffer.offset, 1);
    cache.Bind(texture_vert_uniform_buffer.buffer,
               texture_vert_uniform_buffer.offset, 2);
    encoder->setFragmentTexture(texture, 0);
    encoder->setFragmentSamplerState(blur_filter_sampler_, 0);
    encoder->setFragmentBuffer(frag_uniform_buffer.buffer,
                               frag_uniform_buffer.offset, 0);
    cache.BindDepthStencil(convex_draw_);

    NS::UInteger start = 0;
    NS::UInteger count = 6;
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
    encoder->popDebugGroup();
}

void Renderer::BindBlurInfo(MTL::RenderCommandEncoder *encoder,
                            const Matrix &mvp, const Rect &dest, Scalar depth,
                            MTL::Texture *source, bool horizontal,
                            Scalar sigma) {
    struct UniformData {
        Matrix mvp;
        float depth;
        float padding;
    };

    struct TextureVertInfo {
        // LTRB format.
        Rect dest_rect;
    };

    struct BlurFragInfo {
        // How much to adjust uvs to move the sample point 1 pixel.
        simd::float2 uv_offset;
        float weights[32];
        int length;
    };
    BlurFragInfo frag_info;

    if (horizontal) {
        frag_info.uv_offset = {1.0f / source->width(), 0.0f};
    } else {
        frag_info.uv_offset = {0.0f, 1.0f / source->height()};
    }

    Scalar total = 0.0;
    int i = 0;
    for (; i < 32; i++) {
        Scalar x = i;
        Scalar coef = exp(-0.5f * (x * x) / (sigma * sigma)) /
                      (sqrt(2.0f * M_PI) * sigma);
        if (coef < 0.01) {
            break;
        }
        total += frag_info.weights[i] = coef;
    }
    total *= 2;
    for (int j = 0; j < i; j++) {
        frag_info.weights[j] /= total;
    }
    frag_info.length = i;

    UniformData data;
    data.mvp = mvp;
    data.depth = 1 - (depth * kDepthEpsilon);
    BufferView vert_uniform_buffer =
        host_buffer_->GetTransientArena(sizeof(data), 16u);
    ::memcpy(vert_uniform_buffer.contents(), &data, sizeof(data));

    TextureVertInfo texture_data;
    texture_data.dest_rect = dest;
    BufferView texture_vert_uniform_buffer =
        host_buffer_->GetTransientArena(sizeof(texture_data), 16u);
    ::memcpy(texture_vert_uniform_buffer.contents(), &texture_data,
             sizeof(texture_data));

    BufferView cover_buffer =
        host_buffer_->GetTransientArena(6 * sizeof(Point), 16u);
    std::array<Scalar, 12> bounds = dest.GetQuad();
    std::memcpy(cover_buffer.contents(), bounds.data(), 6 * sizeof(Point));

    BufferView blur_frag_info_buffer =
        host_buffer_->GetTransientArena(sizeof(frag_info), 16u);
    ::memcpy(blur_frag_info_buffer.contents(), &frag_info, sizeof(frag_info));

    encoder->pushDebugGroup(
        NS::String::string("Blur", NS::ASCIIStringEncoding));

    encoder->setRenderPipelineState(pipelines_->GetBlur());
    encoder->setVertexBuffer(cover_buffer.buffer, cover_buffer.offset, 0);
    encoder->setVertexBuffer(vert_uniform_buffer.buffer,
                             vert_uniform_buffer.offset, 1);
    encoder->setVertexBuffer(texture_vert_uniform_buffer.buffer,
                             texture_vert_uniform_buffer.offset, 2);

    encoder->setFragmentTexture(source, 0);
    encoder->setFragmentSamplerState(blur_filter_sampler_, 0);
    encoder->setFragmentBuffer(blur_frag_info_buffer.buffer,
                               blur_frag_info_buffer.offset, 0);

    NS::UInteger start = 0;
    NS::UInteger count = 6;
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
    encoder->popDebugGroup();
}

void DrawColorFilter(MTL::CommandEncoder *encoder, MTL::Texture *source,
                     Scalar m[20]) {}

void Renderer::DrawBlur(MTL::CommandBuffer *command_buffer,
                        MTL::Texture *source, MTL::Texture *dest,
                        Scalar sigma) {

    // Downsample Render pass.
    {
        MTL::RenderCommandEncoder *encoder =
            SetUpBlurRenderPass(dest, command_buffer);
        struct UniformData {
            Matrix mvp;
            float depth;
            float padding;
        };

        // Fill uniform buffer for transform.
        UniformData data;
        data.mvp =
            Matrix::MakeOrthographic(Size(dest->width(), dest->height()));
        data.depth = 0;
        BufferView vert_uniform_buffer =
            host_buffer_->GetTransientArena(sizeof(data), 16u);
        ::memcpy(vert_uniform_buffer.contents(), &data, sizeof(data));

        struct TextureVertInfo {
            // LTRB format.
            Rect dest_rect;
        };
        TextureVertInfo texture_data;
        texture_data.dest_rect =
            Rect::MakeLTRB(0, 0, dest->width(), dest->height());
        BufferView texture_vert_uniform_buffer =
            host_buffer_->GetTransientArena(sizeof(texture_data), 16u);
        ::memcpy(texture_vert_uniform_buffer.contents(), &texture_data,
                 sizeof(texture_data));

        struct FragUniformData {
            float alpha;
        };

        FragUniformData frag_data;
        frag_data.alpha = 1.0;

        BufferView frag_uniform_buffer =
            host_buffer_->GetTransientArena(sizeof(frag_data), 16u);
        ::memcpy(frag_uniform_buffer.contents(), &frag_data, sizeof(frag_data));

        BufferView cover_buffer =
            host_buffer_->GetTransientArena(6 * sizeof(Point), 16u);
        std::array<Scalar, 12> bounds =
            Rect::MakeLTRB(0, 0, dest->width(), dest->height()).GetQuad();
        std::memcpy(cover_buffer.contents(), bounds.data(), 6 * sizeof(Point));

        encoder->pushDebugGroup(save_label_);
        encoder->setRenderPipelineState(pipelines_->GetDownsample());
        encoder->setVertexBuffer(cover_buffer.buffer, cover_buffer.offset, 0);
        encoder->setVertexBuffer(vert_uniform_buffer.buffer,
                                 vert_uniform_buffer.offset, 1);
        encoder->setVertexBuffer(texture_vert_uniform_buffer.buffer,
                                 texture_vert_uniform_buffer.offset, 2);
        encoder->setFragmentTexture(source, 0);
        encoder->setFragmentSamplerState(blur_filter_sampler_, 0);
        encoder->setFragmentBuffer(frag_uniform_buffer.buffer,
                                   frag_uniform_buffer.offset, 0);

        NS::UInteger start = 0;
        NS::UInteger count = 6;
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
        encoder->popDebugGroup();
        encoder->endEncoding();
    }

    // Perform First Pass. This requires another command encoder.
    MTL::TextureDescriptor *desc = MTL::TextureDescriptor::alloc();
    desc->setWidth(dest->width());
    desc->setHeight(dest->height());
    desc->setDepth(1);
    desc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    desc->setArrayLength(1);
    desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    desc->setMipmapLevelCount(1);
    desc->setStorageMode(MTL::StorageModePrivate);
    desc->setSampleCount(1);
    desc->setTextureType(MTL::TextureType2D);
    desc->setSwizzle(MTL::TextureSwizzleChannels()); // WTF Metal CPP.
    desc->setAllowGPUOptimizedContents(true);
    desc->setCompressionType(MTL::TextureCompressionTypeLossy);
    MTL::Texture *temp = metal_device_->newTexture(desc);

    {
        MTL::Texture *src = dest;
        MTL::Texture *dst = temp;
        auto *encoder = SetUpBlurRenderPass(dst, command_buffer);
        BindBlurInfo(
            encoder,
            Matrix::MakeOrthographic(Size(source->width(), source->height())),
            Rect::MakeLTRB(0, 0, source->width(), source->height()), 0, src,
            /*horizontal=*/true, sigma);
        encoder->endEncoding();
    }

    {
        // Perform Second Pass. This requires another command encoder.
        MTL::Texture *src = temp;
        MTL::Texture *dst = dest;
        auto *encoder = SetUpBlurRenderPass(dst, command_buffer);
        BindBlurInfo(
            encoder,
            Matrix::MakeOrthographic(Size(source->width(), source->height())),
            Rect::MakeLTRB(0, 0, source->width(), source->height()), 0, src,
            /*horizontal=*/false, sigma);
        encoder->endEncoding();
    }

    temp->release();
}

void Renderer::ClipPathTriangulated(MTL::RenderCommandEncoder *encoder,
                                    BufferBindingCache &cache,
                                    const Matrix &mvp, const Command &command,
                                    ClipStyle style, const Rect &screen_size) {

    // A clip is essentially a draw, except that we only write to the
    // depth buffer.
    struct UniformData {
        Matrix mvp;
        float depth;
    };

    // Fill uniform buffer for transform.
    BufferView vert_uniform_buffer =
        host_buffer_->GetTransientArena(sizeof(UniformData), 16u);
    UniformData data;
    data.mvp = mvp;
    data.depth = 1 - (command.depth_count * kDepthEpsilon);
    ::memcpy(vert_uniform_buffer.contents(), &data, sizeof(UniformData));

    encoder->pushDebugGroup(clip_label_);
    // Draw using stencil to increment the stencil buffer where
    // the path is filled.
    cache.Bind(vert_uniform_buffer.buffer, vert_uniform_buffer.offset, 1);
    cache.BindPipeline(pipelines_->GetStencil());
    cache.Bind(command.vertex_buffer.buffer, command.vertex_buffer.offset, 0);
    cache.BindDepthStencil(non_zero_stencil_);

    if (command.index_buffer) {
        encoder->drawIndexedPrimitives(
            MTL::PrimitiveTypeTriangle, command.index_count,
            MTL::IndexTypeUInt16, command.index_buffer.buffer,
            command.index_buffer.offset);
    } else {
        NS::UInteger start = 0;
        NS::UInteger count = command.index_count;
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
    }

    switch (style) {
    case ClipStyle::kDifference: {
        // Perform a cover draw that updates the depth buffer where the stencil
        // is set. If this was a convex shape, we could do it in one go.
        BufferView cover_buffer =
            host_buffer_->GetTransientArena(6 * sizeof(Point), 16u);
        std::array<Scalar, 12> bounds = command.bounds.GetQuad();
        std::memcpy(cover_buffer.contents(), bounds.data(), 6 * sizeof(Point));

        cache.Bind(cover_buffer.buffer, cover_buffer.offset, 0);
        cache.BindDepthStencil(cover_stencil_opaque_);

        NS::UInteger start = 0;
        NS::UInteger count = 6;
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
        break;
    }
    case ClipStyle::kIntersect: {
        // Now perform a cover draw across the entire screen that writes the
        // depth value everywhere the stencil is not set. The depth value must
        // be the max of all depth values within a given save/restore pair. The
        // performance of this clipping can be improved by combining with a
        // scissor.
        data.mvp = Matrix();
        BufferView intersect_vert_uniform_buffer =
            host_buffer_->GetTransientArena(sizeof(UniformData), 16u);
        ::memcpy(intersect_vert_uniform_buffer.contents(), &data,
                 sizeof(UniformData));
        cache.Bind(intersect_vert_uniform_buffer.buffer,
                   intersect_vert_uniform_buffer.offset, 1);

        BufferView cover_buffer =
            host_buffer_->GetTransientArena(6 * sizeof(Point), 16u);
        // With identity transform, write out normalized device coordinates
        // to cover full screen.
        std::array<Scalar, 12> bounds = {
            -1, -1, //
            -1, 1,  //
            1,  1,  //
            -1, -1, //
            1,  -1, //
            1,  1   //
        };
        std::memcpy(cover_buffer.contents(), bounds.data(), 6 * sizeof(Point));

        cache.Bind(cover_buffer.buffer, cover_buffer.offset, 0);
        cache.BindDepthStencil(clip_depth_write_);

        NS::UInteger start = 0;
        NS::UInteger count = 6;
        encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, start, count);
        break;
    }
    }
    encoder->popDebugGroup();
}

MTL::RenderCommandEncoder *
Renderer::SetUpBlurRenderPass(MTL::Texture *onscreen,
                              MTL::CommandBuffer *command_buffer) {
    MTL::RenderPassDescriptor *desc =
        MTL::RenderPassDescriptor::alloc()->init();

    MTL::RenderPassColorAttachmentDescriptor *color0 =
        desc->colorAttachments()->object(0);
    color0->setTexture(onscreen);
    color0->setLoadAction(MTL::LoadAction::LoadActionDontCare);
    color0->setStoreAction(MTL::StoreAction::StoreActionStore);

    MTL::RenderCommandEncoder *encoder =
        command_buffer->renderCommandEncoder(desc);

    desc->release();
    return encoder;
}

MTL::RenderCommandEncoder *
Renderer::SetUpRenderPass(MTL::Texture *onscreen,
                          MTL::CommandBuffer *command_buffer,
                          Color clear_color) {
    if (kEnableMSAA) {
        auto [msaa_tex, ds_tex] = host_buffer_->CreateMSAATextures(
            static_cast<uint32_t>(onscreen->width()),
            static_cast<uint32_t>(onscreen->height()));

        MTL::RenderPassDescriptor *desc =
            MTL::RenderPassDescriptor::alloc()->init();

        MTL::RenderPassDepthAttachmentDescriptor *depth0 =
            desc->depthAttachment();
        depth0->setClearDepth(1.0);
        depth0->setLoadAction(MTL::LoadActionClear);
        depth0->setStoreAction(MTL::StoreActionDontCare);
        depth0->setTexture(ds_tex);

        MTL::RenderPassStencilAttachmentDescriptor *stencil0 =
            desc->stencilAttachment();
        stencil0->setClearStencil(0);
        stencil0->setLoadAction(MTL::LoadActionClear);
        stencil0->setStoreAction(MTL::StoreActionDontCare);
        stencil0->setTexture(ds_tex);

        MTL::RenderPassColorAttachmentDescriptor *color0 =
            desc->colorAttachments()->object(0);
        color0->setTexture(msaa_tex);
        color0->setLoadAction(MTL::LoadAction::LoadActionClear);
        color0->setStoreAction(MTL::StoreAction::StoreActionMultisampleResolve);

        Color cc_pre = clear_color.Premultiply();
        color0->setClearColor(
            MTL::ClearColor(cc_pre.r, cc_pre.g, cc_pre.b, cc_pre.a));
        color0->setResolveTexture(onscreen);

        MTL::RenderCommandEncoder *encoder =
            command_buffer->renderCommandEncoder(desc);
        encoder->setStencilReferenceValue(0);

        desc->release();
        return encoder;
    } else {
        auto ds_tex = host_buffer_->CreateDepthStencil(
            static_cast<uint32_t>(onscreen->width()),
            static_cast<uint32_t>(onscreen->height()));
        MTL::RenderPassDescriptor *desc =
            MTL::RenderPassDescriptor::alloc()->init();

        MTL::RenderPassDepthAttachmentDescriptor *depth0 =
            desc->depthAttachment();
        depth0->setClearDepth(1.0);
        depth0->setLoadAction(MTL::LoadActionClear);
        depth0->setStoreAction(MTL::StoreActionDontCare);
        depth0->setTexture(ds_tex);

        MTL::RenderPassStencilAttachmentDescriptor *stencil0 =
            desc->stencilAttachment();
        stencil0->setClearStencil(0);
        stencil0->setLoadAction(MTL::LoadActionClear);
        stencil0->setStoreAction(MTL::StoreActionDontCare);
        stencil0->setTexture(ds_tex);

        MTL::RenderPassColorAttachmentDescriptor *color0 =
            desc->colorAttachments()->object(0);
        color0->setTexture(onscreen);
        color0->setLoadAction(MTL::LoadAction::LoadActionClear);
        color0->setStoreAction(MTL::StoreAction::StoreActionStore);

        Color cc_pre = clear_color.Premultiply();
        color0->setClearColor(
            MTL::ClearColor(cc_pre.r, cc_pre.g, cc_pre.b, cc_pre.a));

        MTL::RenderCommandEncoder *encoder =
            command_buffer->renderCommandEncoder(desc);
        encoder->setStencilReferenceValue(0);

        desc->release();
        return encoder;
    }
}

MTL::CommandBuffer *Renderer::render(MTL::Texture *onscreen) {
    host_buffer_->IncrementTransientBuffer();
    MTL::CommandBuffer *command_buffer = command_queue_->commandBuffer();

    for (auto &offscreen : picture_.GetOffscreens()) {
        MTL::RenderCommandEncoder *encoder =
            SetUpRenderPass(offscreen.texture, command_buffer, kTransparent);
        BufferBindingCache binding_cache(encoder);

        Matrix mvp =
            Matrix::MakeOrthographic(
                Size(offscreen.texture->width(), offscreen.texture->height())) *
            Matrix::MakeTranslate(-offscreen.bounds.l, -offscreen.bounds.t);

        for (auto i = 0; i < offscreen.commands.size(); i++) {
            const Command &command = offscreen.commands[i];
            switch (command.type) {
            case CommandType::kClip: {
                ClipPathTriangulated(encoder, binding_cache,
                                     mvp * command.transform, command,
                                     command.style,
                                     Rect(0, 0, offscreen.bounds.GetWidth(),
                                          offscreen.bounds.GetHeight()));
                break;
            }
            case CommandType::kTexture: {
                DrawTexture(encoder, binding_cache, mvp * command.transform,
                            command.bounds, command.depth_count,
                            command.paint.color.a, command.texture);
                break;
            }
            case CommandType::kDraw: {
                DrawPathTriangulated(encoder, binding_cache,
                                     mvp * command.transform, command);
                break;
            }
            }
        }
        encoder->endEncoding();

        MTL::Texture *filter_source = offscreen.texture;
        if (auto *color_filter =
                std::get_if<ColorMatrixFilter>(&offscreen.color_filter)) {
        }

        if (auto *gaussian =
                std::get_if<GaussianFilter>(&offscreen.image_filter)) {
            DrawBlur(command_buffer, filter_source, offscreen.filter_texture,
                     gaussian->sigma / 2);
        }
    }

    MTL::RenderCommandEncoder *encoder =
        SetUpRenderPass(onscreen, command_buffer, kTransparent);
    Matrix mvp =
        Matrix::MakeOrthographic(Size(onscreen->width(), onscreen->height()));
    const auto &cmds = picture_.GetCommands();
    if (cmds.empty()) {
        encoder->endEncoding();
        return command_buffer;
    }

    BufferBindingCache binding_cache(encoder);
    for (auto i = 0; i < cmds.size(); i++) {
        const Command &command = cmds[i];
        switch (command.type) {
        case CommandType::kClip: {
            ClipPathTriangulated(
                encoder, binding_cache, mvp * command.transform, command,
                command.style,
                Rect(0, 0, onscreen->width(), onscreen->height()));
            break;
        }
        case CommandType::kTexture: {
            DrawTexture(encoder, binding_cache, mvp * command.transform,
                        command.bounds, command.depth_count,
                        command.paint.color.a, command.texture);
            break;
        }
        case CommandType::kDraw: {
            DrawPathTriangulated(encoder, binding_cache,
                                 mvp * command.transform, command);
            break;
        }
        }
    }

    encoder->endEncoding();

    return command_buffer;
}

} // namespace flatland
