#include "metal_engine.hpp"

int main(int argc, const char * argv[]) {
    flatland::MTLEngine engine;
    engine.init();
    engine.run();
    engine.cleanup();
    return EXIT_SUCCESS;
}

