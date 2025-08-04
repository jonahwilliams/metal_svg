#ifndef PIPELINES
#define PIPELINES

#include <array>

#include <Metal/Metal.hpp>

namespace flatland {

enum class BlendMode {
    kSrc = 0,
    kSrcOver = 1,
};

/// @brief owns pipeline state objects and manages variants.
///
/// All pipelines and variants are created on construction.
class Pipelines {
  public:
    explicit Pipelines(MTL::Device *metal_device, bool enable_msaa);

    ~Pipelines();

    MTL::RenderPipelineState *GetSolidColor(BlendMode mode) const;
    
    MTL::RenderPipelineState *GetLinearGradient(BlendMode mode) const;
    
    MTL::RenderPipelineState *GetRadialGradient(BlendMode mode) const;
    
    MTL::RenderPipelineState *GetTextureFill(BlendMode mode) const;
    
    MTL::RenderPipelineState *GetBlur() const;
    
    MTL::RenderPipelineState *GetBoxBlur() const;
    
    MTL::RenderPipelineState *GetDownsample() const;

    // Draw is not impacted by blend mode
    MTL::RenderPipelineState *GetStencil() const;

  private:
    Pipelines(const Pipelines &) = delete;
    Pipelines &operator=(const Pipelines &) = delete;
    Pipelines(Pipelines &&) = delete;

    MTL::RenderPipelineState *solid_color_[2];
    MTL::RenderPipelineState *linear_gradient_[2];
    MTL::RenderPipelineState *radial_gradient_[2];
    MTL::RenderPipelineState *texture_Fill_[2];
    MTL::RenderPipelineState *downsample_pipeline_;
    MTL::RenderPipelineState *stencil_pipeline_;
    MTL::RenderPipelineState *blur_pipelines_;
};

} // namespace flatland

#endif // PIPELINES
