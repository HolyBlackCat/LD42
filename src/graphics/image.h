#ifndef GRAPHICS_IMAGE_H_INCLUDED
#define GRAPHICS_IMAGE_H_INCLUDED

#include <vector>
#include <utility>

#include "program/errors.h"
#include "utils/finally.h"
#include "utils/mat.h"
#include "utils/memory_file.h"

#include <stb_image.h>
#include <stb_image_write.h>

namespace Graphics
{
    class Image
    {
        // Note that moved-from instance is left in an invalid (yet destructable) state.

        ivec2 size = ivec2(0);
        std::vector<u8vec4> data;

      public:
        enum Format {png, tga};
        enum FlipMode {no_flip, flip_y};

        Image() {}
        Image(ivec2 size, const uint8_t *bytes = 0) : size(size)
        {
            data = std::vector<u8vec4>((u8vec4 *)bytes, (u8vec4 *)bytes + size.prod());
        }
        Image(MemoryFile file, FlipMode flip_mode = no_flip) // Throws on failure.
        {
            stbi_set_flip_vertically_on_load(flip_mode == flip_y);
            ivec2 img_size;
            uint8_t *bytes = stbi_load_from_memory(file.data(), file.size(), &img_size.x, &img_size.y, 0, 4);
            if (!bytes)
                Program::Error("Unable to parse image: ", file.name());
            FINALLY( stbi_image_free(bytes); )
            *this = Image(img_size, bytes);
        }

        explicit operator bool() const {return data.size() > 0;}

        const u8vec4 *Pixels() const {return data.data();}
        const uint8_t *Data() const {return (const uint8_t *)Pixels();}
        ivec2 Size() const {return size;}

        void Save(std::string file_name, Format format = png) // Throws on failure.
        {
            if (!*this)
                Program::Error("Attempt to save an empty image to a file.");

            int ok = 0;
            switch (format)
            {
              case png:
                ok = stbi_write_png(file_name.c_str(), size.x, size.y, 4, data.data(), 0);
                break;
              case tga:
                ok = stbi_write_tga(file_name.c_str(), size.x, size.y, 4, data.data());
                break;
            }

            if (!ok)
                Program::Error("Unable to write image to file: ", file_name);
        }
    };
}

#endif
