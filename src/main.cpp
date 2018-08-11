#include <iomanip>
#include <iostream>
#include <vector>

#include <SDL2/SDL.h>
#include <GLFL/glfl.h>

#include "graphics/blending.h"
#include "graphics/image.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "graphics/vertex_buffer.h"
#include "graphics/viewport.h"
#include "interface/input.h"
#include "interface/messagebox.h"
#include "interface/window.h"
#include "program/errors.h"
#include "program/exit.h"
#include "program/parachute.h"
#include "utils/archive.h"
#include "utils/dynamic_storage.h"
#include "utils/finally.h"
#include "utils/macro.h"
#include "utils/mat.h"
#include "utils/memory_file.h"
#include "reflection/complete.h"
#include "utils/strings.h"

#define main SDL_main

Program::Parachute error_parachute;
Interface::Window win("Alpha", vec(800, 600));
Graphics::Image img("test.png");

struct Attribs
{
    Reflect(Attribs)
    (
        (fvec2)(pos),
        (fvec3)(color),
    )
};

struct Uniforms
{
    Reflect(Uniforms)
    (
        (Graphics::Shader::VertUniform<fmat4>)(matrix),
    )
};

Uniforms uni;

Graphics::Shader::Program shader_main("Main", {}, {}, Meta::tag<Attribs>{}, uni, R"(

varying vec3 v_color;

void main()
{
    v_color = a_color;
    gl_Position = u_matrix * vec4(a_pos, 0, 1);
})", R"(
varying vec3 v_color;

void main()
{
    gl_FragColor = vec4(v_color, 1);
})");

struct A
{
    Reflect(A)
    (
        (int)(x,y),
        (float)(z)(=42),
        (optional)(float)(w)(),
    )
};

struct B
{
    Reflect(B)
    (
        (A)(a),
        (std::vector<float>)(alpha)(={1.1,2.2,3.3}),
    )
};

int main(int, char**)
{
    Graphics::VertexBuffer<Attribs> buf;

    Attribs array[]
    {
        {fvec2(0,0), fvec3(1,1,0)},
        {fvec2(1,0), fvec3(0,1,1)},
        {fvec2(0,1), fvec3(1,0,1)},
    };

    shader_main.Bind();

    buf.SetData(3, array);

    while (1)
    {
        win.ProcessEvents();

        if (win.Resized())
        {
            std::cout << "Resized\n";
            glViewport(0, 0, win.Size().x, win.Size().y);
        }
        if (win.ExitRequested())
            return 0;

        glClear(GL_COLOR_BUFFER_BIT);

        uni.matrix = fmat4::rotate(fvec3(0,0,1), win.Ticks() / 100.0);
        buf.Draw(Graphics::triangles);

        //Program::Exit();

//        glBegin(GL_TRIANGLES);
//        glColor3f(0.9,0.1,0.6);
//        glVertex2f(-0.5,0.5);
//        glVertex2f(0.5,0.5);
//        glVertex2f(0,-0.5);
//        glEnd();

        win.SwapBuffers();
    }

    return 0;
}
