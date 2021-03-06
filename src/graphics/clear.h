#ifndef GRAPHICS_CLEAR_H_INCLUDED
#define GRAPHICS_CLEAR_H_INCLUDED

#include <GLFL/glfl.h>

#include "utils/mat.h"

namespace Graphics
{
    void Clear(bool color = 1, bool depth = 0, bool stencil = 0)
    {
        glClear(color * GL_COLOR_BUFFER_BIT | depth * GL_DEPTH_BUFFER_BIT | stencil * GL_STENCIL_BUFFER_BIT);
    }

    void SetClearColor(fvec4 color)
    {
        glClearColor(color.r, color.g, color.b, color.a);
    }
    void SetClearColor(fvec3 color)
    {
        SetClearColor(color.to_vec4(1));
    }
}

#endif
