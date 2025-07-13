#ifndef HOST_BUFFER
#define HOST_BUFFER

#include <array>
#include <vector>

#include <Metal/Metal.hpp>

namespace flatland {

struct BufferView {
    MTL::Buffer* buffer = nullptr;
    size_t offset = 0;
    
    void* contents() {
        return reinterpret_cast<uint8_t*>(buffer->contents()) + offset;
    }
    
    operator bool() const {
        return buffer;
    }
};

class HostBuffer {
public:
    HostBuffer(MTL::Device* metal_device);
    
    ~HostBuffer();
    
    /// @brief Return a pointer to a buffer of at least [required_bytes], pre-aligned to [alignment_bytes].
    BufferView GetTransientArena(size_t required_bytes, size_t alignment);
    
    /// @brief increment the internal buffer arena.
    void IncrementTransientBuffer();
    
    struct Result {
        size_t id;
        BufferView position;
        BufferView index;
    };
    
    Result AllocatePersistent(size_t required_vertices, size_t required_indices, size_t alignment_bytes);
    
    std::optional<Result> LookupPersistent(size_t id);
    
    std::pair<MTL::Texture*, size_t> AllocateTexture(MTL::TextureDescriptor* desc);
    
    MTL::Texture* AllocateTempTexture(MTL::TextureDescriptor* desc);
    
    MTL::Texture* GetTexture(size_t id);
    
    // Returns MSAA and DS.
    std::pair<MTL::Texture*, MTL::Texture*> CreateMSAATextures(uint32_t width, uint32_t height);
  
private:
    // Persistent data.
    // TODO: currently this will never free data.
    size_t next_id_ = 0;
    struct BufferMetadata {
        MTL::Buffer* buffer;
        size_t offset;
        size_t size;
    };
    std::vector<BufferMetadata> persistent_buffers_;
    std::unordered_map<size_t, Result> allocated_meshes_;
    
    // Transient data
    std::array<std::vector<MTL::Buffer*>, 3> transient_arena_;
    size_t current_index_ = 0;
    size_t current_offset_ = 0;
    size_t current_buffer_ = 0;
    MTL::Device* metal_device_;
    
    // Texture Data.
    size_t next_texture_id_ = 0;
    std::unordered_map<size_t, MTL::Texture*> textures_;
    
    // MSAA Stuff.
    std::unordered_map<uint64_t, std::pair<MTL::Texture*, MTL::Texture*>> cached_msaa_;
    
    void addNewBuffer(size_t required_bytes);
    
    void AddPersistentBuffer(size_t required_bytes);
    
    BufferMetadata* FindPersistentStorageOfSize(size_t required_bytes);
    
    HostBuffer(const HostBuffer &) = delete;
    HostBuffer(HostBuffer &&) = delete;
    HostBuffer &operator=(const HostBuffer &) = delete;
};

} // namespace flatland

#endif // HOST_BUFFER
