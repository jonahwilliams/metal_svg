#include "host_buffer.hpp"

#include <iostream>

namespace flatland {

namespace {
// Returns the required padding, if any
size_t AlignTo(size_t offset_bytes, size_t alignment_bytes) {
    size_t rem = offset_bytes % alignment_bytes;
    size_t padding = 0;
    if (rem > 0) {
        padding = alignment_bytes - rem;
    }
    return padding;
}
} // namespace

static constexpr NS::UInteger kMinArenaSize = 1024 * 32; // bytes;

HostBuffer::HostBuffer(MTL::Device *metal_device)
    : metal_device_(metal_device) {
    for (auto i = 0u; i < 3; i++) {
        current_index_ = i;
        addNewBuffer(0);
    }
}

HostBuffer::~HostBuffer() {
    for (auto i = 0u; i < 3; i++) {
        for (auto *buffer : transient_arena_[i]) {
            buffer->release();
        }
    }
}

// Persistent Data

HostBuffer::Result HostBuffer::AllocatePersistent(size_t required_vertices,
                                                  size_t required_indices,
                                                  size_t alignment_bytes) {
    
    size_t padding_est = AlignTo(0, alignment_bytes);
    size_t required_size = required_vertices + padding_est + required_indices;
    HostBuffer::Result result;
    result.id = next_id_++;

    HostBuffer::BufferMetadata *candidate = FindPersistentStorageOfSize(required_size);
    if (candidate == nullptr) {
        AddPersistentBuffer(required_size);
        candidate = &persistent_buffers_.back();
    }
    result.position.buffer = candidate->buffer;
    result.position.offset = candidate->offset;
    candidate->offset += required_vertices + padding_est;

    result.index.buffer = candidate->buffer;
    result.index.offset = candidate->offset;
    
    candidate->offset += required_indices;
    
    // Note this extra alignment technically can cause offset > size,
    // but at that point we won't attempt to allocate from this buffer
    // anymore so its harmless.
    padding_est = AlignTo(candidate->offset, alignment_bytes);
    candidate->offset += padding_est;
    
    return result;
}

std::optional<HostBuffer::Result> HostBuffer::LookupPersistent(size_t id) {
    auto it = allocated_meshes_.find(id);
    if (it == allocated_meshes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

HostBuffer::BufferMetadata *
HostBuffer::FindPersistentStorageOfSize(size_t required_bytes) {
    for (auto i = 0u; i < persistent_buffers_.size(); i++) {
        const BufferMetadata &d = persistent_buffers_[i];
        if (d.offset < d.size && d.size - d.offset >= required_bytes) {
            return &persistent_buffers_[i];
        }
    }
    return nullptr;
}

void HostBuffer::AddPersistentBuffer(size_t required_bytes) {
    size_t new_size = std::max(kMinArenaSize, required_bytes);
    MTL::Buffer *buffer =
        metal_device_->newBuffer(new_size, MTL::ResourceStorageModeShared);
    persistent_buffers_.emplace_back(buffer, 0, new_size);
}

// Transient Data

BufferView HostBuffer::GetTransientArena(size_t required_bytes, size_t alignment) {
    size_t padding = AlignTo(current_offset_, alignment);
    if (current_offset_ + required_bytes + padding >
        transient_arena_[current_index_][current_buffer_]->length()) {
        addNewBuffer(required_bytes);
    }
    current_offset_ += padding;
    size_t offset = current_offset_;
    current_offset_ += required_bytes;

    return BufferView{transient_arena_[current_index_][current_buffer_],
                      offset};
}

void HostBuffer::IncrementTransientBuffer() {
    current_index_ = (current_index_ + 1) % 3;
    current_offset_ = 0;
    current_buffer_ = 0;
}

void HostBuffer::addNewBuffer(size_t required_bytes) {
    current_buffer_++;
    if (current_buffer_ < transient_arena_[current_index_].size()) {
        current_offset_ = 0;
        return;
    }
    MTL::Buffer *buffer =
        metal_device_->newBuffer(std::max(kMinArenaSize, required_bytes),
                                 MTL::ResourceStorageModeShared);
    transient_arena_[current_index_].push_back(buffer);
    current_offset_ = 0;
}

MTL::Texture* HostBuffer::AllocateTempTexture(MTL::TextureDescriptor* desc) {
    return  metal_device_->newTexture(desc);
}

std::pair<MTL::Texture*, size_t> HostBuffer::AllocateTexture(MTL::TextureDescriptor* desc) {
    MTL::Texture* texture = metal_device_->newTexture(desc);
    size_t id = next_texture_id_;
    next_texture_id_++;
    textures_[id] = texture;
    return std::make_pair(texture, id);
}

MTL::Texture* HostBuffer::GetTexture(size_t id) {
    return textures_[id];
}

std::pair<MTL::Texture*, MTL::Texture*> HostBuffer::CreateMSAATextures(uint32_t width, uint32_t height) {
    uint64_t cache_key = static_cast<uint64_t>(width) << 32 | height;
    
    if (cached_msaa_.count(cache_key)) {
        return cached_msaa_[cache_key];
    }
    
    MTL::TextureDescriptor *msaa_desc = MTL::TextureDescriptor::alloc()->init();
    msaa_desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    msaa_desc->setUsage(MTL::TextureUsageRenderTarget);
    msaa_desc->setWidth(width);
    msaa_desc->setHeight(height);
    msaa_desc->setSampleCount(4);
    msaa_desc->setStorageMode(MTL::StorageModeMemoryless);
    msaa_desc->setTextureType(MTL::TextureType2DMultisample);

    MTL::Texture *msaa_tex = metal_device_->newTexture(msaa_desc);

    MTL::TextureDescriptor *ds_desc = MTL::TextureDescriptor::alloc()->init();
    ds_desc->setPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
    ds_desc->setUsage(MTL::TextureUsageRenderTarget);
    ds_desc->setWidth(width);
    ds_desc->setHeight(height);
    ds_desc->setSampleCount(4);
    ds_desc->setStorageMode(MTL::StorageModeMemoryless);
    ds_desc->setTextureType(MTL::TextureType2DMultisample);

    MTL::Texture *ds_tex = metal_device_->newTexture(ds_desc);
    
    ds_desc->release();
    msaa_desc->release();
    
    return cached_msaa_[cache_key] = std::make_pair(msaa_tex, ds_tex);
}


} // namespace flatland
