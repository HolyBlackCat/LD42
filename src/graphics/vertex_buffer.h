#ifndef GRAPHICS_VERTEX_BUFFER_H_INCLUDED
#define GRAPHICS_VERTEX_BUFFER_H_INCLUDED

#include <type_traits>
#include <utility>

#include <GLFL/glfl.h>

#include "program/errors.h"
#include "reflection/complete.h"
#include "utils/finally.h"
#include "utils/mat.h"

namespace Graphics
{
    enum Primitive
    {
        points    = GL_POINTS,
        lines     = GL_LINES,
        triangles = GL_TRIANGLES,
    };

    enum Usage
    {
        static_draw  = GL_STATIC_DRAW,
        dynamic_draw = GL_DYNAMIC_DRAW,
        stream_draw  = GL_STREAM_DRAW,
    };

    class Buffers
    {
        Buffers() = delete;
        ~Buffers() = delete;

        // Only one buffer can be bound at a time. Optionally it can be draw-bound at the same time.
        inline static GLuint binding = 0;
        inline static GLuint binding_draw = 0;

        inline static int active_attrib_count = 0;

        static void SetActiveAttribCount(int count)
        {
            if (count == active_attrib_count)
                return;
            if (active_attrib_count < count)
                do glEnableVertexAttribArray(active_attrib_count++); while (active_attrib_count < count);
            else
                do glDisableVertexAttribArray(--active_attrib_count); while (active_attrib_count > count);
        }

      public:
        static void BindStorage(GLuint handle)
        {
            if (binding == handle)
                return;
            glBindBuffer(GL_ARRAY_BUFFER, handle);
            binding = handle;
            binding_draw = 0;
        }
        template <typename T> static void BindDraw(GLuint handle)
        {
            if (binding_draw == handle)
                return;
            BindStorage(handle);

            constexpr bool is_reflected = Refl::is_reflected<T>;
            int field_count;
            if constexpr (is_reflected)
                field_count = Refl::Interface<T>::field_count();
            else
                field_count = 0;

            SetActiveAttribCount(field_count);

            if constexpr (is_reflected)
            {
                using refl = Refl::Interface<T>;

                int offset = 0, attrib = 0;

                refl::for_each_field([&](auto index)
                {
                    constexpr int i = index.value;
                    using field_type = typename refl::template field_type<i>;
                    static_assert(std::is_same_v<Math::vec_base_t<field_type>, float>, "Non-float attributes are not supported.");
                    glVertexAttribPointer(attrib++, Math::vec_size_v<field_type>, GL_FLOAT, 0, sizeof(T), (void *)offset);
                    offset += sizeof(field_type);
                });

                if (offset != int(sizeof(T)))
                    Program::Error("Unexpected padding in attribute structure.");
            }

            binding_draw = handle;
        }

        static GLuint StorageBinding()
        {
            return binding;
        }
        static GLuint DrawBinding()
        {
            return binding_draw;
        }
    };

    template <typename T> class VertexBuffer
    {
        static_assert(!std::is_void_v<T>, "Element type can't be void. Use uint8_t instead.");

        struct Data
        {
            GLuint handle = 0;
            int size = 0;
        };
        Data data;

      public:
        static constexpr bool is_reflected = Refl::is_reflected<T>;

        VertexBuffer()
        {
            glGenBuffers(1, &data.handle);
            if (!data.handle)
                Program::Error("Unable to create a vertex buffer.");
            // FINALLY_ON_THROW( glDeleteBuffers(1, &handle); )
        }
        VertexBuffer(int count, const T *source = 0, Usage usage = static_draw) : VertexBuffer() // Binds storage.
        {
            SetData(count, source, usage);
        }

        VertexBuffer(VertexBuffer &&other) noexcept : data(std::exchange(other.data, {})) {}
        VertexBuffer &operator=(VertexBuffer &&other) noexcept // Note the pass by value to utilize copy&swap idiom.
        {
            std::swap(data, other.data);
            return *this;
        }

        ~VertexBuffer()
        {
            glDeleteBuffers(1, &data.handle);
        }

        explicit operator bool() const
        {
            return bool(data.handle);
        }

        GLuint Handle() const
        {
            return data.handle;
        }

        void BindStorage() const
        {
            Buffers::BindStorage(data.handle);
        }
        static void UnbindStorage()
        {
            Buffers::BindStorage(0);
        }
        [[nodiscard]] bool StorageBound() const
        {
            return data.handle == Buffers::StorageBinding();
        }

        void BindDraw() const
        {
            static_assert(is_reflected, "Can't bind for drawing, since element type is not reflected.");
            Buffers::BindDraw<T>(data.handle);
        }
        static void UnbindDraw()
        {
            Buffers::BindDraw<void>(0);
        }
        [[nodiscard]] bool DrawBound() const
        {
            if constexpr (!is_reflected)
                return 0;
            return data.handle == Buffers::DrawBinding();
        }

        int Size() const
        {
            return data.size;
        }

        void SetData(int count, const T *source = 0, Usage usage = static_draw) // Binds storage.
        {
            BindStorage();
            glBufferData(GL_ARRAY_BUFFER, count * sizeof(T), source, usage);
            data.size = count;
        }
        void SetDataPart(int obj_offset, int count, const T *source) // Binds storage.
        {
            SetDataPartBytes(obj_offset * sizeof(T), count * sizeof(T), (const uint8_t *)source);
        }
        void SetDataPartBytes(int offset, int bytes, const uint8_t *source) // Binds storage.
        {
            BindStorage();
            glBufferSubData(GL_ARRAY_BUFFER, offset, bytes, source);
        }

        void Draw(Primitive p, int from, int count) // Binds for drawing.
        {
            BindDraw();
            glDrawArrays(p, from, count);
        }
        void Draw(Primitive p, int count) // Binds for drawing.
        {
            Draw(p, 0, count);
        }
        void Draw(Primitive p) // Binds for drawing.
        {
            Draw(p, 0, Size());
        }
    };
}

#endif
