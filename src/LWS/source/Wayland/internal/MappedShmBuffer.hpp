#pragma once
#include <wayland-client.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <utility>
namespace LWS::internal
{
    inline int createAnonymousFile()
    {
#ifdef SYS_memfd_create
        return static_cast<int>(syscall(SYS_memfd_create, "lws-wayland-buffer", 0x0001U));
#else
        errno = ENOSYS;
        return -1;
#endif
    }
    // Own only the native storage. Busy flags, release callbacks, and submission policy stay with the
    // presenter, caption, background, or icon that retains this buffer. No additional heap object is needed.
    class MappedShmBuffer final
    {
      public:

        MappedShmBuffer() = default;
        MappedShmBuffer(const MappedShmBuffer&) = delete;
        MappedShmBuffer& operator=(const MappedShmBuffer&) = delete;
        ~MappedShmBuffer() { reset(); }
        bool create(wl_shm* memory, int32_t width, int32_t height)
        {
            reset();
            if (width <= 0 || height <= 0)
                return false;
            const uint64_t bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4U;
            if (bytes > std::numeric_limits<int32_t>::max())
                return false;
            const int fd = createAnonymousFile();
            if (fd < 0)
                return false;
            if (ftruncate(fd, static_cast<off_t>(bytes)) == 0)
            {
                mappingSize = static_cast<size_t>(bytes);
                mapping = mmap(nullptr, mappingSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (mapping != MAP_FAILED)
                {
                    wl_shm_pool* pool = wl_shm_create_pool(memory, fd, static_cast<int32_t>(bytes));
                    if (pool)
                    {
                        buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888);
                        wl_shm_pool_destroy(pool);
                    }
                }
            }
            close(fd);
            if (!buffer)
                reset();
            return buffer != nullptr;
        }
        void reset()
        {
            if (buffer)
                wl_buffer_destroy(std::exchange(buffer, nullptr));
            if (mapping != MAP_FAILED)
                munmap(std::exchange(mapping, MAP_FAILED), mappingSize);
            mappingSize = 0;
        }
        wl_buffer* buffer = nullptr;
        void* mapping = MAP_FAILED;
        size_t mappingSize = 0;
    };
}  // namespace LWS::internal
