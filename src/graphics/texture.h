#ifndef GRAPHICS_TEXTURE_H_INCLUDED
#define GRAPHICS_TEXTURE_H_INCLUDED

#include <cstddef>
#include <map>
#include <utility>

#include <GLFL/glfl.h>

#include "graphics/image.h"
#include "utils/finally.h"
#include "utils/mat.h"
#include "utils/resource_allocator.h"

namespace Graphics
{
    enum InterpolationMode
    {
        nearest,
        linear,
        min_nearest_mag_linear,
        min_linear_mag_nearest,
    };

    enum WrapMode
    {
        clamp  = GL_CLAMP_TO_EDGE,
        mirror = GL_MIRRORED_REPEAT,
        repeat = GL_REPEAT,
        OnPC(
        fill   = GL_CLAMP_TO_BORDER,
        )
    };

    class Texture
    {
        struct Data
        {
            GLuint handle = 0;
        };

        Data data;

        inline static std::map<GLuint, ivec2> texture_sizes;

      public:
        Texture()
        {
            glGenTextures(1, &data.handle);
            if (!data.handle)
                Program::Error("Unable to create a texture.");
            // FINALLY_ON_THROW( glDeleteTextures(1, &data.handle); )
        }

        Texture(Texture &&other) noexcept : data(std::exchange(other.data, {})) {}
        Texture &operator=(Texture &&other) noexcept
        {
            std::swap(data, other.data);
            return *this;
        }

        ~Texture()
        {
            texture_sizes.erase(data.handle); // It's a no-op if there is no such handle in the map.
            glDeleteTextures(1, &data.handle);
        }

        GLuint Handle() const
        {
            return data.handle;
        }

        static void SetHandleSize(GLuint handle, ivec2 size)
        {
            texture_sizes[handle] = size;
        }
        static ivec2 GetHandleSize(GLuint handle)
        {
            if (auto it = texture_sizes.find(handle); it != texture_sizes.end())
                return it->second;
            else
                return ivec2(0);
        }
        ivec2 Size() const
        {
            return GetHandleSize(data.handle);
        }
    };

    class TextureUnit
    {
        using res_alloc_t = ResourceAllocator<int>;

        static res_alloc_t &allocator() // Not sure if it's necessary, but the intent of wrapping it into a function is prevent the static init order fiasco.
        {
            static res_alloc_t ret(64);
            return ret;
        }

        struct Data
        {
            int index = res_alloc_t::none;
            GLuint handle = 0;
        };

        Data data;

        inline static int active_index = 0;

      public:
        TextureUnit()
        {
            data.index = allocator().Alloc();
            if (!*this)
                Program::Error("No more texture units.");
        }
        explicit TextureUnit(GLuint handle) : TextureUnit()
        {
            AttachHandle(handle);
        }
        explicit TextureUnit(const Texture &texture) : TextureUnit()
        {
            Attach(texture);
        }

        TextureUnit(TextureUnit &&other) noexcept : data(std::exchange(other.data, {})) {}
        TextureUnit &operator=(TextureUnit &&other) noexcept
        {
            std::swap(data, other.data);
            return *this;
        }

        ~TextureUnit()
        {
            allocator().Free(data.index);
        }

        explicit operator bool() const
        {
            return data.index != res_alloc_t::none;
        }

        int Index() const
        {
            return data.index;
        }

        static void ActivateIndex(int index)
        {
            if (index == res_alloc_t::none)
                return;
            if (active_index == index)
                return;
            glActiveTexture(GL_TEXTURE0 + index);
            active_index = index;
        }
        void Activate()
        {
            ActivateIndex(data.index);
        }
        [[nodiscard]] bool Active()
        {
            return active_index == data.index;
        }

        TextureUnit &&AttachHandle(GLuint handle)
        {
            if (!*this)
                return std::move(*this);

            Activate();
            data.handle = handle;
            glBindTexture(GL_TEXTURE_2D, handle);
            return std::move(*this);
        }
        TextureUnit &&Attach(const Texture &texture)
        {
            AttachHandle(texture.Handle());
            return std::move(*this);
        }
        TextureUnit &&Detach()
        {
            AttachHandle(0);
            return std::move(*this);
        }

        TextureUnit &&Interpolation(InterpolationMode mode)
        {
            if (!*this)
                return std::move(*this);

            Activate();

            GLenum min_mode = (mode == nearest || mode == min_nearest_mag_linear ? GL_NEAREST : GL_LINEAR);
            GLenum mag_mode = (mode == nearest || mode == min_linear_mag_nearest ? GL_NEAREST : GL_LINEAR);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_mode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_mode);

            return std::move(*this);
        }

        TextureUnit &&WrapX(WrapMode mode)
        {
            if (!*this)
                return std::move(*this);
            Activate();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLuint(mode));
            return std::move(*this);
        }
        TextureUnit &&WrapY(WrapMode mode)
        {
            if (!*this)
                return std::move(*this);
            Activate();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLuint(mode));
            return std::move(*this);
        }
        TextureUnit &&Wrap(WrapMode mode)
        {
            WrapX(mode);
            WrapY(mode);
            return std::move(*this);
        }

        ivec2 Size() const
        {
            return Texture::GetHandleSize(data.handle);
        }

        TextureUnit &&SetData(ivec2 size, const uint8_t *pixels = 0)
        {
            SetData(OnPC(GL_RGBA8) OnMobile(GL_RGBA), GL_RGBA, GL_UNSIGNED_BYTE, size, pixels);
            return std::move(*this);
        }
        TextureUnit &&SetData(GLenum internal_format, GLenum format, GLenum type, ivec2 size, const uint8_t *pixels = 0)
        {
            if (!*this)
                return std::move(*this);
            Activate();
            if (data.handle)
                Texture::SetHandleSize(data.handle, size);
            glTexImage2D(GL_TEXTURE_2D, 0, internal_format, size.x, size.y, 0, format, type, pixels);
            return std::move(*this);
        }
        TextureUnit &&SetData(const Image &image)
        {
            SetData(image.Size(), image.Data());
            return std::move(*this);
        }

        TextureUnit &&SetDataPart(ivec2 pos, ivec2 size, const uint8_t *pixels)
        {
            SetDataPart(GL_RGBA, GL_UNSIGNED_BYTE, pos, size, pixels);
            return std::move(*this);
        }
        TextureUnit &&SetDataPart(GLenum format, GLenum type, ivec2 pos, ivec2 size, const uint8_t *pixels)
        {
            if (!*this)
                return std::move(*this);
            Activate();
            glTexSubImage2D(GL_TEXTURE_2D, 0, pos.x, pos.y, size.x, size.y, format, type, pixels);
            return std::move(*this);
        }
    };
}

#endif
