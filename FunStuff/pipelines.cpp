#include "pipelines.hpp"

namespace flatland {
namespace {

void makeForBlendMode(BlendMode mode,
                      MTL::RenderPipelineColorAttachmentDescriptor *desc) {
    switch (mode) {
    case BlendMode::kSrc: {
        desc->setWriteMask(MTL::ColorWriteMaskAll);
        desc->setBlendingEnabled(false);
        desc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        desc->setSourceRGBBlendFactor(MTL::BlendFactorOne);
        desc->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);
        desc->setDestinationRGBBlendFactor(MTL::BlendFactorZero);
        desc->setAlphaBlendOperation(MTL::BlendOperationAdd);
        desc->setRgbBlendOperation(MTL::BlendOperationAdd);
    } break;
    case BlendMode::kSrcOver: {
        desc->setWriteMask(MTL::ColorWriteMaskAll);
        desc->setBlendingEnabled(true);
        desc->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        desc->setSourceRGBBlendFactor(MTL::BlendFactorOne);
        desc->setDestinationAlphaBlendFactor(
            MTL::BlendFactorOneMinusSourceAlpha);
        desc->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        desc->setAlphaBlendOperation(MTL::BlendOperationAdd);
        desc->setRgbBlendOperation(MTL::BlendOperationAdd);
    } break;
    }
}

MTL::RenderPipelineDescriptor *makeDefaultDescriptor(bool enable_msaa) {
    MTL::RenderPipelineDescriptor *desc =
        MTL::RenderPipelineDescriptor::alloc()->init();
    desc->colorAttachments()->object(0)->setPixelFormat(
        MTL::PixelFormat::PixelFormatBGRA8Unorm);
    desc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
    desc->setStencilAttachmentPixelFormat(
        MTL::PixelFormatDepth32Float_Stencil8);
    desc->setSampleCount(enable_msaa ? 4 : 1);
    return desc;
}

} // namespace

Pipelines::Pipelines(MTL::Device *metal_device, bool enable_msaa) {
    MTL::Library *library = metal_device->newDefaultLibrary();

    // Downsample
    {
        MTL::RenderPipelineDescriptor *desc =
            MTL::RenderPipelineDescriptor::alloc()->init();
        desc->colorAttachments()->object(0)->setPixelFormat(
            MTL::PixelFormat::PixelFormatBGRA8Unorm);
        desc->setSampleCount(1);

        MTL::Function *vertexShader = library->newFunction(
            NS::String::string("textureVertexShader", NS::ASCIIStringEncoding));
        MTL::Function *fragmentShader = library->newFunction(NS::String::string(
            "textureFragmentShader", NS::ASCIIStringEncoding));
        desc->setLabel(
            NS::String::string("Downsample Shader", NS::ASCIIStringEncoding));
        desc->setVertexFunction(vertexShader);
        desc->setFragmentFunction(fragmentShader);

        NS::Error *error;
        makeForBlendMode(BlendMode::kSrc, desc->colorAttachments()->object(0));
        downsample_pipeline_ =
            metal_device->newRenderPipelineState(desc, &error);
        desc->release();
    }
    // Blur
    {
        MTL::RenderPipelineDescriptor *desc =
            MTL::RenderPipelineDescriptor::alloc()->init();
        desc->colorAttachments()->object(0)->setPixelFormat(
            MTL::PixelFormat::PixelFormatBGRA8Unorm);
        desc->setSampleCount(1);

        MTL::Function *vertexShader = library->newFunction(
            NS::String::string("filterVertexShader", NS::ASCIIStringEncoding));
        MTL::Function *fragmentShader = library->newFunction(
            NS::String::string("blurFragmentShader", NS::ASCIIStringEncoding));
        desc->setLabel(
            NS::String::string("Blur Filter", NS::ASCIIStringEncoding));
        desc->setVertexFunction(vertexShader);
        desc->setFragmentFunction(fragmentShader);

        NS::Error *error;
        makeForBlendMode(BlendMode::kSrc, desc->colorAttachments()->object(0));
        blur_pipelines_ = metal_device->newRenderPipelineState(desc, &error);
        desc->release();
    }
    // Solid Fill
    {
        MTL::RenderPipelineDescriptor *desc = makeDefaultDescriptor(enable_msaa);
        MTL::Function *vertexShader = library->newFunction(
            NS::String::string("vertexShader", NS::ASCIIStringEncoding));
        MTL::Function *fragmentShader = library->newFunction(
            NS::String::string("fragmentShader", NS::ASCIIStringEncoding));
        desc->setLabel(
            NS::String::string("Solid Fill", NS::ASCIIStringEncoding));
        desc->setVertexFunction(vertexShader);
        desc->setFragmentFunction(fragmentShader);

        for (int i = 0; i < 2; i++) {
            NS::Error *error;
            makeForBlendMode(static_cast<BlendMode>(i),
                             desc->colorAttachments()->object(0));
            solid_color_[i] =
                metal_device->newRenderPipelineState(desc, &error);
        }
        desc->release();
    }
    // Linear Gradient.
    {
        MTL::RenderPipelineDescriptor *desc = makeDefaultDescriptor(enable_msaa);
        MTL::Function *vertexShader = library->newFunction(NS::String::string(
            "gradientVertexShader", NS::ASCIIStringEncoding));
        MTL::Function *fragmentShader = library->newFunction(NS::String::string(
            "linearGradientFragmentShader", NS::ASCIIStringEncoding));
        desc->setLabel(
            NS::String::string("Linear Gradient", NS::ASCIIStringEncoding));
        desc->setVertexFunction(vertexShader);
        desc->setFragmentFunction(fragmentShader);

        for (int i = 0; i < 2; i++) {
            NS::Error *error;
            makeForBlendMode(static_cast<BlendMode>(i),
                             desc->colorAttachments()->object(0));
            linear_gradient_[i] =
                metal_device->newRenderPipelineState(desc, &error);
        }
        desc->release();
    }
    // Radial Gradient.
    {
        MTL::RenderPipelineDescriptor *desc = makeDefaultDescriptor(enable_msaa);
        MTL::Function *vertexShader = library->newFunction(NS::String::string(
            "gradientVertexShader", NS::ASCIIStringEncoding));
        MTL::Function *fragmentShader = library->newFunction(NS::String::string(
            "radialGradientFragmentShader", NS::ASCIIStringEncoding));
        desc->setLabel(
            NS::String::string("Radial Gradient", NS::ASCIIStringEncoding));
        desc->setVertexFunction(vertexShader);
        desc->setFragmentFunction(fragmentShader);

        for (int i = 0; i < 2; i++) {
            NS::Error *error;
            makeForBlendMode(static_cast<BlendMode>(i),
                             desc->colorAttachments()->object(0));
            radial_gradient_[i] =
                metal_device->newRenderPipelineState(desc, &error);
        }
        desc->release();
    }
    // Texture Fill.
    {
        MTL::RenderPipelineDescriptor *desc = makeDefaultDescriptor(enable_msaa);
        MTL::Function *vertexShader = library->newFunction(
            NS::String::string("textureVertexShader", NS::ASCIIStringEncoding));
        MTL::Function *fragmentShader = library->newFunction(NS::String::string(
            "textureFragmentShader", NS::ASCIIStringEncoding));
        desc->setLabel(
            NS::String::string("Texture Fill", NS::ASCIIStringEncoding));
        desc->setVertexFunction(vertexShader);
        desc->setFragmentFunction(fragmentShader);

        for (int i = 0; i < 2; i++) {
            NS::Error *error;
            makeForBlendMode(static_cast<BlendMode>(i),
                             desc->colorAttachments()->object(0));
            texture_Fill_[i] =
                metal_device->newRenderPipelineState(desc, &error);
        }
        desc->release();
    }

    // Stencil pipeline
    {
        MTL::RenderPipelineDescriptor *desc = makeDefaultDescriptor(enable_msaa);
        MTL::Function *vertexShader = library->newFunction(
            NS::String::string("stencilVertexShader", NS::ASCIIStringEncoding));
        MTL::Function *fragmentShader = library->newFunction(NS::String::string(
            "stencilFragmentShader", NS::ASCIIStringEncoding));
        desc->setLabel(
            NS::String::string("Stencil Shader", NS::ASCIIStringEncoding));
        desc->setVertexFunction(vertexShader);
        desc->setFragmentFunction(fragmentShader);
        desc->colorAttachments()->object(0)->setWriteMask(
            MTL::ColorWriteMaskNone);
        desc->colorAttachments()->object(0)->setBlendingEnabled(false);

        NS::Error *error;
        stencil_pipeline_ = metal_device->newRenderPipelineState(desc, &error);
        desc->release();
    }
}

Pipelines::~Pipelines() {
    stencil_pipeline_->release();
    blur_pipelines_->release();
    for (int i = 0; i < 2; i++) {
        solid_color_[i]->release();
        linear_gradient_[i]->release();
        radial_gradient_[i]->release();
        texture_Fill_[i]->release();
    }
}

MTL::RenderPipelineState *Pipelines::GetSolidColor(BlendMode mode) const {
    return solid_color_[static_cast<int>(mode)];
}

MTL::RenderPipelineState *Pipelines::GetLinearGradient(BlendMode mode) const {
    return linear_gradient_[static_cast<int>(mode)];
}

MTL::RenderPipelineState *Pipelines::GetRadialGradient(BlendMode mode) const {
    return radial_gradient_[static_cast<int>(mode)];
}

MTL::RenderPipelineState *Pipelines::GetTextureFill(BlendMode mode) const {
    return texture_Fill_[static_cast<int>(mode)];
}

MTL::RenderPipelineState *Pipelines::GetBlur() const { return blur_pipelines_; }

MTL::RenderPipelineState *Pipelines::GetStencil() const {
    return stencil_pipeline_;
}

MTL::RenderPipelineState *Pipelines::GetDownsample() const {
    return downsample_pipeline_;
}


} // namespace flatland
