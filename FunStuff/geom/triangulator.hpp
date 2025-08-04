#ifndef GEOM_TRIANGULATOR
#define GEOM_TRIANGULATOR

#include <simd/simd.h>

#include "bezier.hpp"

namespace flatland {

/// @brief A triangulator consumes [Path] objects and produces a triangulated
/// mesh for
///        rasterization in a triangle layout.
///
/// The triangulator is a stateful object, it expects to first be given a [Path]
/// which will be triangulated, returning the resulting size of the mesh. Then
/// the client is reponsible for allocating a buffer of sufficient size and
/// calling [write] to copy out the mesh data.
///
/// The triangulator has internal storage to write out intermediate points.
/// Performing the triangulation into temporary storage ensures that we have
/// sufficient device buffer capacity to hold all vertices.
class Triangulator {
  public:
    Triangulator();

    ~Triangulator() = default;

    using TriangulatorCallback = std::function<void()>;

    /// @brief Triangulate [path] with the given scale factor, returning the
    /// number of vertices  in the resulting mesh.
    ///
    /// @returns the number of Points in the mesh (not the number of floats) and
    /// the number of indices.
    std::pair<size_t, size_t> triangulate(const Path &path,
                                          Scalar scale_factor);

    /// @brief Triangulate [path] into a stroked path with [stroke_width].
    ///
    /// @returns the number of Points in the mesh (not the number of floats) and
    /// the number of indices.
    std::pair<size_t, size_t> triangulateStroke(const Path &path,
                                                Scalar stroke_width,
                                                Scalar scale_factor);

    std::pair<size_t, size_t> expensiveTriangulate(const Path &path,
                                                   Scalar scale_factor);

    /// @brief Write out the triangulated mesh into the provided [out] buffer
    /// with a limit of [size].
    ///
    /// Providing nullptr to [vertices] or [indices] will cause the triangulator
    /// to discard the mesh.
    ///
    /// @returns Whether the write was successful.
    bool write(void *vertices, void *indices);

  private:
    std::vector<Point> points_;
    std::vector<uint16_t> indices_;
    size_t vertex_size_ = 0;
    size_t index_size_ = 0;

    void EnsurePointStorage(size_t n);

    void EnsureIndexStorage(size_t n);

    Triangulator(const Triangulator &) = delete;
    Triangulator(Triangulator &&) = delete;
    Triangulator &operator=(const Triangulator &) = delete;
};

} // namespace flatland

#endif // GEOM_TRIANGULATOR
