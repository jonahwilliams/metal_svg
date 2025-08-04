#include "canvas.hpp"

#include <iostream>

namespace flatland {

namespace {
size_t CreateGradientTexture(Color colors[], size_t color_size,
                             HostBuffer *host_buffer) {
    MTL::TextureDescriptor *desc = MTL::TextureDescriptor::alloc();
    desc->setWidth(color_size);
    desc->setHeight(1);
    desc->setDepth(1);
    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setArrayLength(1);
    desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    desc->setMipmapLevelCount(1);
    desc->setStorageMode(MTL::StorageModeShared);
    desc->setSampleCount(1);
    desc->setTextureType(MTL::TextureType2D);
    desc->setSwizzle(MTL::TextureSwizzleChannels()); // WTF Metal CPP.

    auto [texture, id] = host_buffer->AllocateTexture(desc);

    std::vector<uint8_t> bytes(4 * color_size);
    for (int i = 0, j = 0; i < color_size; i++) {
        Color color = colors[i].Premultiply();
        bytes[j++] = static_cast<uint8_t>(255 * color.b);
        bytes[j++] = static_cast<uint8_t>(255 * color.g);
        bytes[j++] = static_cast<uint8_t>(255 * color.r);
        bytes[j++] = static_cast<uint8_t>(255 * color.a);
    }

    texture->replaceRegion(
        /*region=*/MTL::Region(0, 0, 0, color_size, 1, 1),
        /*level=*/0,
        /*slice=*/0,
        /*withBytes=*/reinterpret_cast<uint8_t *>(bytes.data()),
        /*bytesPerRow=*/bytes.size(),
        /*bytesPerImage=*/0);
    texture->allowGPUOptimizedContents();

    desc->release();

    return id;
}

bool IsBlur(const ImageFilter &filter) {
    return !std::holds_alternative<std::monostate>(filter);
}

} // namespace

RenderProgram::RenderProgram(std::vector<Command> commands,
                             std::vector<Data> offscreens)
    : commands_(std::move(commands)), offscreens_(std::move(offscreens)) {}

const std::vector<Command> &RenderProgram::GetCommands() const {
    return commands_;
}

const std::vector<RenderProgram::Data> &RenderProgram::GetOffscreens() const {
    return offscreens_;
}

///

Canvas::Canvas(HostBuffer *host_buffer, Triangulator *triangulator)
    : host_buffer_(host_buffer), triangulator_(triangulator) {
    clip_stack_.push_back({});
    pending_states_.push_back(CommandState{.is_onscreen = true});
}

// Transform Management.

void Canvas::Translate(Scalar tx, Scalar ty) {
    clip_stack_.back().transform =
        clip_stack_.back().transform * Matrix::MakeTranslate(tx, ty);
}

void Canvas::Scale(Scalar sx, Scalar sy) {
    clip_stack_.back().transform =
        clip_stack_.back().transform * Matrix::MakeScale(sx, sy);
}

void Canvas::Rotate(Scalar r) {
    clip_stack_.back().transform =
        clip_stack_.back().transform * Matrix::MakeRotate(r);
}

void Canvas::Transform(const Matrix &m) {
    clip_stack_.back().transform = clip_stack_.back().transform * m;
}

// Drawing Management.

void Canvas::DrawRect(const Rect &rect, Paint paint) {
    auto result =
        host_buffer_->AllocatePersistent(6 * sizeof(simd::float2), 0, 16);
    std::array<Scalar, 12> bounds = rect.GetQuad();
    std::memcpy(result.position.contents(), bounds.data(), 6 * sizeof(Point));

    Record(Command{
        .paint = paint,
        .depth_count = clip_stack_.back().draw_count,
        .index_count = 6,
        .type = CommandType::kDraw,
        .vertex_buffer = result.position,
        .index_buffer = {},
        .bounds = rect,
        .is_convex = true,
        .transform = clip_stack_.back().transform,
    });
    clip_stack_.back().draw_count++;
}

void Canvas::DrawPath(const Path &path, Paint paint) {
    size_t vertex_count = 0;
    size_t index_count = 0;
   if (!paint.stroke) {
        auto [p_vertex_count, p_index_count] =
            triangulator_->triangulate(path, /*scale_factor=*/1);
        vertex_count = p_vertex_count;
        index_count = p_index_count;
    } else {
        auto [p_vertex_count, p_index_count] =
            triangulator_->triangulateStroke(path, /*stroke_width=*/paint.stroke_width, /*scale_factor=*/1);
        vertex_count = p_vertex_count;
        index_count = p_index_count;
    }
    if (vertex_count == 0 || index_count == 0) {
        return;
    }
    auto result =
        host_buffer_->AllocatePersistent(vertex_count * sizeof(simd::float2),
                                         index_count * sizeof(uint16_t), 16);
    triangulator_->write(result.position.contents(), result.index.contents());
    if (!result.position || !result.index) {
        std::cerr << "Failed to allocate persistent." << std::endl;
        return;
    }

    Record(Command{
        .paint = paint,
        .depth_count = clip_stack_.back().draw_count,
        .index_count = index_count,
        .type = CommandType::kDraw,
        .vertex_buffer = result.position,
        .index_buffer = result.index,
        .bounds = path.GetBounds(),
        .is_convex = path.IsConvex() || paint.stroke,
        .transform = clip_stack_.back().transform,
    });
    clip_stack_.back().draw_count++;
}

void Canvas::ClipPath(const Path &path, ClipStyle style) {
    auto [vertex_count, index_count] =
        triangulator_->triangulate(path, /*scale_factor=*/1);
    auto result =
        host_buffer_->AllocatePersistent(vertex_count * sizeof(simd::float2),
                                         index_count * sizeof(uint16_t), 16);
    triangulator_->write(result.position.contents(), result.index.contents());

    Record(Command{
        .paint = Paint(),
        .depth_count = 0,
        .index_count = index_count,
        .type = CommandType::kClip,
        .vertex_buffer = result.position,
        .index_buffer = result.index,
        .bounds = path.GetBounds(),
        .is_convex = path.IsConvex(),
        .transform = clip_stack_.back().transform,
        .style = style,
    });
    clip_stack_.back().pending_clips.push_back(GetCurrent().commands.size() -
                                               1);
    clip_stack_.back().draw_count++;
}

void Canvas::DrawTexture(const Rect &dest, MTL::Texture *texture,
                         Scalar alpha) {
    Record(Command{.paint = Paint{.color = Color(0, 0, 0, alpha)},
                   .depth_count = clip_stack_.back().draw_count,
                   .index_count = 0,
                   .type = CommandType::kTexture,
                   .vertex_buffer = {},
                   .index_buffer = {},
                   .bounds = dest,
                   .is_convex = true,
                   .transform = clip_stack_.back().transform,
                   .texture = texture});
    clip_stack_.back().draw_count++;
}

void Canvas::SaveLayer(Scalar alpha, ImageFilter image_filter,
                       ColorFilter color_filter) {
    ClipStackEntry entry{
        .draw_count = clip_stack_.back().draw_count,
        .transform = clip_stack_.back().transform,
        .is_save_layer = true,
        .alpha = alpha,
    };
    clip_stack_.push_back(entry);
    pending_states_.push_back(CommandState{
        .image_filter = image_filter,
        .color_filter = color_filter,
    });
}

RenderProgram Canvas::Prepare() {
    auto &state = GetCurrent();
    if (!state.pending_commands.empty()) {
        std::cerr << "Insert " << state.pending_commands.size() << " commands" << std::endl;
        state.commands.insert(state.commands.begin() + state.flush_index,
                              state.pending_commands.rbegin(),
                              state.pending_commands.rend());
        state.pending_commands.clear();
    }
    while (!clip_stack_.empty()) {
        Restore();
    }
    std::vector<Command> temp;
    std::swap(state.commands, temp);
    std::vector<RenderProgram::Data> offscreens;

    int index = 0;
    for (auto offscreen_state : finalized_states_) {
        if (!offscreen_state.pending_commands.empty()) {
            offscreen_state.commands.insert(
                offscreen_state.commands.begin() + offscreen_state.flush_index,
                offscreen_state.pending_commands.rbegin(),
                offscreen_state.pending_commands.rend());
            offscreen_state.pending_commands.clear();
        }

        offscreens.push_back(RenderProgram::Data{
            .commands = offscreen_state.commands,
            .texture = textures_[index++],
            .filter_texture = offscreen_state.filter_texture,
            .image_filter = offscreen_state.image_filter,
            .color_filter = offscreen_state.color_filter,
            .bounds = offscreen_state.bounds_estimate.value_or(
                Rect::MakeLTRB(0, 0, 1, 1)),
        });
    }
    return RenderProgram(temp, std::move(offscreens));
}

// Save Layer Management.

void Canvas::Save() {
    // Begin recording a new clip entry. Any clips written after this save will
    // have a clip depth set to the minimum of the clip depth of this save,
    // inclusive of any nested layers. This is computed by accumulated the
    // number of draws into each clip stack entry.
    ClipStackEntry entry{
        .draw_count = clip_stack_.back().draw_count,
        .transform = clip_stack_.back().transform,
    };
    clip_stack_.push_back(entry);
}

void Canvas::Restore() {
    if (!clip_stack_.empty()) {
        // Once we restore a clip stack entry, we've computed the depth value
        // that needs to be assigned to all clips within this save layer.
        // we recorded the indices of any pending clips that need to be updated.
        const ClipStackEntry &entry = clip_stack_.back();
        auto &state = GetCurrent();
        for (size_t clip_index : entry.pending_clips) {
            state.commands[clip_index].depth_count = entry.draw_count;
        }
        clip_stack_.pop_back();
        if (!clip_stack_.empty()) {
            clip_stack_.back().draw_count = entry.draw_count;
        }
        if (entry.is_save_layer) {
            if (!state.pending_commands.empty()) {
                state.commands.insert(state.commands.begin() +
                                          state.flush_index,
                                      state.pending_commands.rbegin(),
                                      state.pending_commands.rend());
                state.pending_commands.clear();
            }

            finalized_states_.push_back(
                std::move(pending_states_[pending_states_.size() - 1]));
            pending_states_.pop_back();
            // Done Command Flushing.
            bool is_blur = IsBlur(finalized_states_.back().image_filter);

            Rect dest = finalized_states_.back().bounds_estimate.value_or(
                Rect::MakeLTRB(0, 0, 1, 1));

            if (auto *gaussian = std::get_if<GaussianFilter>(
                    &finalized_states_.back().image_filter)) {
                dest = dest.Expand(3 * gaussian->sigma, 3 * gaussian->sigma);
                finalized_states_.back().bounds_estimate = dest;
            }

            // Create texture for offscreen.
            MTL::TextureDescriptor *desc = MTL::TextureDescriptor::alloc();
            desc->setWidth(std::ceil(dest.GetWidth()));
            desc->setHeight(std::ceil(dest.GetHeight()));
            desc->setDepth(1);
            desc->setUsage(MTL::TextureUsageShaderRead |
                           MTL::TextureUsageRenderTarget);
            desc->setArrayLength(1);
            desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
            desc->setMipmapLevelCount(1);
            desc->setStorageMode(MTL::StorageModePrivate);
            desc->setSampleCount(1);
            desc->setTextureType(MTL::TextureType2D);
            desc->setSwizzle(MTL::TextureSwizzleChannels()); // WTF Metal CPP.
            desc->setCompressionType(MTL::TextureCompressionTypeLossy);
            desc->setAllowGPUOptimizedContents(true);

            auto texture = host_buffer_->AllocateTempTexture(desc);
            desc->release();

            textures_.push_back(texture);

            if (is_blur) {
                MTL::TextureDescriptor *desc = MTL::TextureDescriptor::alloc();
                desc->setWidth(std::ceil(dest.GetWidth() / 2));
                desc->setHeight(std::ceil(dest.GetHeight() / 2));
                desc->setDepth(1);
                desc->setUsage(MTL::TextureUsageShaderRead |
                               MTL::TextureUsageRenderTarget);
                desc->setArrayLength(1);
                desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
                desc->setMipmapLevelCount(1);
                desc->setStorageMode(MTL::StorageModePrivate);
                desc->setSampleCount(1);
                desc->setTextureType(MTL::TextureType2D);
                desc->setSwizzle(
                    MTL::TextureSwizzleChannels()); // WTF Metal CPP.
                desc->setCompressionType(MTL::TextureCompressionTypeLossy);
                desc->setAllowGPUOptimizedContents(true);

                auto texture = host_buffer_->AllocateTempTexture(desc);
                desc->release();

                finalized_states_.back().filter_texture = texture;
                DrawTexture(dest, texture, 1.0);
            } else {
                DrawTexture(dest, texture, entry.alpha);
            }
        }
    }
}

// Command Recording

void Canvas::Record(Command &&cmd) {
    auto &state = GetCurrent();
    if (state.bounds_estimate.has_value()) {
        state.bounds_estimate = state.bounds_estimate->Union(
            cmd.transform.TransformBounds(cmd.bounds));
    } else {
        state.bounds_estimate = cmd.transform.TransformBounds(cmd.bounds);
    }

    if (cmd.type == CommandType::kClip) {
        // Flush and record
        if (!state.pending_commands.empty()) {
            state.commands.insert(state.commands.begin() + state.flush_index,
                                  state.pending_commands.rbegin(),
                                  state.pending_commands.rend());
            state.pending_commands.clear();
        }
        state.commands.push_back(std::move(cmd));
        state.flush_index = state.commands.size();
    } else if (cmd.paint.IsOpaque() && cmd.type == CommandType::kDraw) {
        state.pending_commands.push_back(std::move(cmd));
    } else {
        state.commands.push_back(std::move(cmd));
    }
}

// Allocation

Gradient Canvas::CreateLinearGradient(Point from, Point to, Color colors[],
                                      size_t color_size) {

    LinearGradient gradient;
    gradient.start = from;
    gradient.end = to;
    gradient.texture_index =
        CreateGradientTexture(colors, color_size, host_buffer_);
    return gradient;
}

Gradient Canvas::CreateRadialGradient(Point center, Scalar radius,
                                      Color colors[], size_t color_size) {
    RadialGradient gradient;
    gradient.center = center;
    gradient.radius = radius;
    gradient.texture_index =
        CreateGradientTexture(colors, color_size, host_buffer_);
    return gradient;
}

} // namespace flatland
