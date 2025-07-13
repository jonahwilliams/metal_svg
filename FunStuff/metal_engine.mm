#include "metal_engine.hpp"

#include <iostream>
#include <simd/simd.h>

#include "geom/bezier.hpp"

namespace flatland {

MTLEngine::MTLEngine() {}

MTLEngine::~MTLEngine() = default;

void MTLEngine::init() {
    initDevice();
    initWindow();
}


void MTLEngine::run() {
    while (!glfwWindowShouldClose(glfwWindow)) {
        @autoreleasepool {
            metalDrawable =
            (__bridge CA::MetalDrawable *)[metalLayer nextDrawable];
            MTL::CommandBuffer* buffer = renderer->render(metalDrawable->texture());
            buffer->presentDrawable(metalDrawable);
            buffer->commit();
        }
        glfwPollEvents();
    }
}

void MTLEngine::cleanup() {
    glfwTerminate();
    metalDevice->release();
}


void MTLEngine::initDevice() {
    metalDevice = MTL::CreateSystemDefaultDevice();
    renderer = std::make_unique<Renderer>(metalDevice);
}


void MTLEngine::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindow = glfwCreateWindow(1000, 1000, "Hello, World!", nullptr, nullptr);
    if (!glfwWindow) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    
    glfwGetFramebufferSize(glfwWindow, &width, &height);
    metalWindow = glfwGetCocoaWindow(glfwWindow);
    metalLayer = [CAMetalLayer layer];
    metalLayer.device = (__bridge id<MTLDevice>)metalDevice;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.drawableSize = CGSizeMake(width, height);
    
    metalWindow.contentView.layer = metalLayer;
    metalWindow.contentView.wantsLayer = YES;
}


} // namespace flatland
