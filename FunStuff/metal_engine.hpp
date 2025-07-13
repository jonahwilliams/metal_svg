#ifndef METAL_ENGINE
#define METAL_ENGINE

#define GLFW_INCLUDE_NONE
#import <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3native.h>

#include "renderer.hpp"

namespace flatland {

class MTLEngine {
public:
    MTLEngine();
    
    ~MTLEngine();
    void init();
    void run();
    void cleanup();
    
private:
    void initDevice();
    void initWindow();
    void createTriangle(int w, int h);
    
    MTL::Device* metalDevice;
    GLFWwindow* glfwWindow;
    NSWindow* metalWindow;
    CAMetalLayer* metalLayer;
    CA::MetalDrawable* metalDrawable;
    std::unique_ptr<Renderer> renderer;
    int width = 0;
    int height = 0;
};

} // namespace flatland

#endif // METAL_ENGINE

