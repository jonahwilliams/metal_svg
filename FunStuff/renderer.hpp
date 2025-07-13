#ifndef RENDERER
#define RENDERER

#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.h>
#include <QuartzCore/CAMetalLayer.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "canvas.hpp"
#include "geom/grid.hpp"
#include "geom/triangulator.hpp"
#include "host_buffer.hpp"
#include "pipelines.hpp"

struct NSVGimage;

namespace flatland {

class BufferBindingCache {
  public:
    BufferBindingCache(MTL::RenderCommandEncoder *encoder);

    void Bind(MTL::Buffer *buffer, size_t offset, size_t index);
    
    void BindFragment(MTL::Buffer *buffer, size_t offset, size_t index);
    
    void BindPipeline(MTL::RenderPipelineState* state);
    
    void BindDepthStencil(MTL::DepthStencilState* state);

  private:
    MTL::RenderCommandEncoder *encoder_;
    MTL::Buffer *bound_buffers_vertex_[6];
    MTL::Buffer *bound_buffers_fragment_[6];
    MTL::RenderPipelineState* last_state_ = nullptr;
    MTL::DepthStencilState* last_ds_state_ = nullptr;
};

class Renderer {
  public:
    explicit Renderer(MTL::Device *metal_device);

    ~Renderer();

    MTL::CommandBuffer *render(MTL::Texture *onscreen);

  private:
    MTL::Device *metal_device_;
    MTL::CommandQueue *command_queue_;
    std::unique_ptr<Triangulator> triangulator_;
    std::unique_ptr<HostBuffer> host_buffer_;
    std::unique_ptr<Pipelines> pipelines_;
    struct NSVGimage *image_;
    struct NSVGimage *star_;

    void InitPicture();

    RenderProgram picture_;

    MTL::RenderCommandEncoder *
    SetUpRenderPass(MTL::Texture *onscreen, MTL::CommandBuffer *command_buffer,
                    Color clear_color);

    MTL::RenderCommandEncoder *
    SetUpBlurRenderPass(MTL::Texture *onscreen,
                        MTL::CommandBuffer *command_buffer);

    void DrawPathTriangulated(MTL::RenderCommandEncoder *encoder,
                              BufferBindingCache &cache, const Matrix &mvp,
                              const Command &command);

    void ClipPathTriangulated(MTL::RenderCommandEncoder *encoder,
                              BufferBindingCache &cache, const Matrix &mvp,
                              const Command &command, ClipStyle style,
                              const Rect &screen_size);

    void DrawTexture(MTL::RenderCommandEncoder *encoder,
                     BufferBindingCache &cache, const Matrix &mvp,
                     const Rect &dest, Scalar depth, Scalar alpha,
                     MTL::Texture *texture);

    void DrawBlur(MTL::CommandBuffer *command_buffer, MTL::Texture *source,
                  MTL::Texture *dest, Scalar sigma);

    void DrawColorFilter(MTL::CommandEncoder *encoder, MTL::Texture *source,
                         Scalar m[20]);

    void BindBlurInfo(MTL::RenderCommandEncoder *encoder, const Matrix &mvp,
                      const Rect &dest, Scalar depth, MTL::Texture *source,
                      bool horizontal, Scalar sigma);

    void PrepareColorSource(MTL::RenderCommandEncoder *encoder,
                            BufferBindingCache& cache,
                            const Paint &paint);

    // Depth/Stencil State
    MTL::DepthStencilState *noop_stencil_;
    MTL::DepthStencilState *non_zero_stencil_;
    MTL::DepthStencilState *convex_draw_;
    MTL::DepthStencilState *transparent_convex_draw_;
    MTL::DepthStencilState *cover_stencil_opaque_;
    MTL::DepthStencilState *cover_stencil_transparent_;
    MTL::DepthStencilState *clip_depth_write_;

    // Labels
    NS::String *convex_label_ = nullptr;
    NS::String *complex_label_ = nullptr;
    NS::String *clip_label_ = nullptr;
    NS::String *save_label_ = nullptr;

    // Gradients.
    MTL::SamplerState *gradient_sampler_ = nullptr;
    MTL::SamplerState *save_layer_sampler_ = nullptr;
    MTL::SamplerState *blur_filter_sampler_ = nullptr;

    Renderer(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &) = delete;
};

} // namespace flatland

#endif // RENDERER
