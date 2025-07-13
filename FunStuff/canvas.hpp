#ifndef CANVAS
#define CANVAS

#include <variant>

#include "geom/basic.hpp"
#include "geom/bezier.hpp"
#include "geom/triangulator.hpp"
#include "host_buffer.hpp"

namespace flatland {

enum class ClipStyle {
    // Only render shapes that intersect with the clip path while active.
    kIntersect,
    /// Only render shapes that do not intersect with the clip path while
    /// active.
    ///
    /// Cuts out a hole in the current clip stack.
    kDifference
};

enum class CommandType {
    kDraw,
    kTexture,
    kClip,
};

struct LinearGradient {
    Point start;
    Point end;
    size_t texture_index;
};

struct RadialGradient {
    Point center;
    Scalar radius;
    size_t texture_index;
};

using Gradient = std::variant<std::monostate, LinearGradient, RadialGradient>;

struct Paint {
    Color color;
    Gradient gradient = std::monostate();

    constexpr bool HasGradient() const {
        return !std::holds_alternative<std::monostate>(gradient);
    }

    constexpr bool IsOpaque() const {
        return !HasGradient() && color.is_opaque();
    }
};

struct GaussianFilter {
    Scalar sigma = 1.0;
};

using ImageFilter = std::variant<std::monostate, GaussianFilter>;

struct ColorMatrixFilter {
    // 4 * 5 matrix
    Scalar m[20];
};

using ColorFilter = std::variant<std::monostate, ColorMatrixFilter>;

// Internal data.
struct Command {
    Paint paint;

    // Non-normalized depth value that can be converted to actual
    // depth by dividing by the total number of commands or by multiplying
    // by a precomputed depth epsilon i.e. depth = 1 - (depth_count / n)
    // or depth = 1 - (depth_count * E).
    int depth_count = 0;
    size_t index_count = 0;
    CommandType type;
    BufferView vertex_buffer = {};
    BufferView index_buffer = {};
    Rect bounds = {};
    Matrix transform = Matrix();
    bool is_convex = false;
    ClipStyle style;
    MTL::Texture *texture = nullptr;
};

class RenderProgram {
  public:
    struct Data {
        std::vector<Command> commands;
        MTL::Texture *texture = nullptr;
        MTL::Texture *filter_texture = nullptr;
        ImageFilter image_filter = std::monostate{};
        ColorFilter color_filter = std::monostate{};
        Rect bounds;
    };

    RenderProgram() = default;
    RenderProgram(std::vector<Command> commands, std::vector<Data> offscreens);

    RenderProgram(RenderProgram &&) = default;
    RenderProgram &operator=(RenderProgram &&) = default;

    ~RenderProgram() = default;

    const std::vector<Command> &GetCommands() const;

    const std::vector<Data> &GetOffscreens() const;

  private:
    std::vector<Data> offscreens_;
    std::vector<Command> commands_;
    bool onscreen_;

    RenderProgram(const RenderProgram &) = delete;
    RenderProgram &operator=(const RenderProgram &) = delete;
};

class Canvas {
  public:
    Canvas(HostBuffer *host_buffer, Triangulator *triangulator);

    ~Canvas() = default;

    void DrawPath(const Path &path, Paint paint);

    void DrawRect(const Rect &rect, Paint paint);

    void ClipPath(const Path &path, ClipStyle style);

    void Translate(Scalar tx, Scalar ty);

    void DrawTexture(const Rect &dest, MTL::Texture *texture, Scalar alpha = 1);

    void Scale(Scalar sx, Scalar sy);

    // unit is radians.
    void Rotate(Scalar r);

    void Transform(const Matrix &m);

    /// @brief Push an entry onto the clip stack.
    ///
    /// Any clipping commands applied after this save will be removed once there
    /// is a matched call to [Restore].
    void Save();

    void SaveLayer(Scalar alpha, ImageFilter image_filter = std::monostate{},
                   ColorFilter color_filter = std::monostate{});

    /// @brief Pop the current clip stack.
    void Restore();

    RenderProgram Prepare();

    // Allocation. Should This Go Here?
    Gradient CreateLinearGradient(Point from, Point to, Color colors[],
                                  size_t color_size);

    Gradient CreateRadialGradient(Point center, Scalar radius, Color colors[],
                                  size_t color_size);

  private:
    HostBuffer *host_buffer_ = nullptr;
    Triangulator *triangulator_ = nullptr;

    struct ClipStackEntry {
        Matrix transform = Matrix();
        int draw_count = 0;
        std::vector<size_t> pending_clips;
        bool is_save_layer = false;
        Scalar alpha = 1.0f;
    };
    std::vector<ClipStackEntry> clip_stack_;

    void Record(Command &&cmd);

    struct CommandState {
        // Two command lists are mainted for recording. The set of recorded
        // commands, and a set of pending commands. The latter holds any draws
        // that require blending with the backdrop. These will be deferred as
        // long as possible, so that opaque occluding draws can be executed
        // first. The opaque draws are held in pending_commands_. Once a drawing
        // command is executed that that requires a flush, the opaque commands
        // are inserted in reverse order at the insert_point index.
        //
        // Example (O - opaque, T - transparent, C - clip)
        //
        //  Command        Pending            Recorded               Flush Index
        //     O1           ->O1                                          0
        //     O2           O1 ->O2                                       0
        //     T1           O1 O2                  ->T1                   0
        //     T2           O1 O2               T1 ->T2                   0
        //     C                               O2 O1 T1 T2 -> C           5
        //
        // Above: opaque and transparent commands are recorded separately. When
        // a clip is encountered, the opaque commands are inserted in reverse
        // order at the start of the command list. This also sets the flush
        // index to be after the recorded clip.
        std::vector<Command> pending_commands;
        std::vector<Command> commands;
        size_t flush_index = 0;

        // Union of the estimated bounds of all draws.
        std::optional<Rect> bounds_estimate = std::nullopt;

        bool is_onscreen = false;

        ImageFilter image_filter = std::monostate{};
        ColorFilter color_filter = std::monostate{};
        MTL::Texture *filter_texture = nullptr;
    };

    std::vector<CommandState> pending_states_;
    std::vector<CommandState> finalized_states_;
    std::vector<MTL::Texture *> textures_;

    CommandState &GetCurrent() { return pending_states_.back(); }

    Canvas(Canvas &&) = delete;
    Canvas(const Canvas &) = delete;
    Canvas &operator=(const Canvas &) = delete;
};

} // namespace flatland

#endif // CANVAS
