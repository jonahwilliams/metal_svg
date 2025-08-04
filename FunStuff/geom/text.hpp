#ifndef GEOM_TEXT
#define GEOM_TEXT

#include <vector>
#include "bezier.hpp"
#include "basic.hpp"

namespace flatland {


std::vector<uint8_t> RasterizePath(const Path& path, ISize size);


} // namespace flatland

#endif // GEOM_TEXT
