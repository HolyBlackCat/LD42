#include "master.h"

#include <iomanip>
#include <iostream>
#include <vector>

#define main SDL_main

Program::Parachute error_parachute;

constexpr ivec2 screen_sz = ivec2(1920,1080)/4;
Interface::Window win("Alpha", screen_sz*2, Interface::Window::windowed, Interface::Window::Settings{}.MinSize(screen_sz));
Metronome metronome;

namespace Draw
{
    Graphics::Texture texture_main;
    Graphics::TextureUnit texture_unit_main = Graphics::TextureUnit(texture_main).Interpolation(Graphics::linear).SetData(Graphics::Image("test.png"));

    Graphics::Texture fbuf_scale_tex, fbuf_scale_tex2;
    Graphics::TextureUnit fbuf_scale_tex_unit = Graphics::TextureUnit(fbuf_scale_tex).Interpolation(Graphics::nearest).Wrap(Graphics::clamp).SetData(screen_sz);
    Graphics::TextureUnit fbuf_scale_tex_unit2 = Graphics::TextureUnit(fbuf_scale_tex2).Interpolation(Graphics::linear).Wrap(Graphics::clamp);
    Graphics::FrameBuffer fbuf_scale(fbuf_scale_tex);
    Graphics::FrameBuffer fbuf_scale2(fbuf_scale_tex2);

    float scale_factor = 1;
    int scale_factor_int = 1;

    namespace ShaderIdentity
    {
        struct attribs_t
        {
            Reflect(attribs_t)
            (
                (fvec2)(pos),
                (fvec2)(texcoord),
            )
        };
        struct uniforms_t
        {
            Reflect(uniforms_t)
            (
                (Graphics::Shader::FragUniform<Graphics::TextureUnit>)(texture),
            )
        };
        uniforms_t uniforms;

        Graphics::Shader::Program shader("Identity", {}, {}, Meta::tag<attribs_t>{}, uniforms,
        //{
        R"(
        varying vec2 v_texcoord;
        void main()
        {
            v_texcoord = a_texcoord;
            gl_Position = vec4(a_pos, 0, 1);
        }
        )" //}
        ,
        //{
        R"(
        varying vec2 v_texcoord;
        void main()
        {
            gl_FragColor = texture2D(u_texture, v_texcoord);
        }
        )"
        //}
        );
    }

    namespace ShaderMain
    {
        struct attribs_t
        {
            Reflect(attribs_t)
            (
                (fvec2)(pos),
                (fvec4)(color),
                (fvec2)(texcoord),
                (fvec3)(factors),
            )
        };
        struct uniforms_t
        {
            Reflect(uniforms_t)
            (
                (Graphics::Shader::VertUniform<fmat4>)(matrix),
                (Graphics::Shader::VertUniform<fvec2>)(tex_size),
                (Graphics::Shader::FragUniform<Graphics::TextureUnit>)(texture),
                (Graphics::Shader::FragUniform<fmat4>)(color_matrix),
            )
        };
        uniforms_t uniforms;

        Graphics::Shader::Program shader("Identity", {}, {}, Meta::tag<attribs_t>{}, uniforms,
        //{
        R"(
        varying vec4 v_color;
        varying vec2 v_texcoord;
        varying vec3 v_factors;
        void main()
        {
            v_color = a_color;
            v_texcoord = a_texcoord / u_tex_size;
            v_factors = a_factors;
            gl_Position = u_matrix * vec4(a_pos, 0, 1);
        }
        )" //}
        ,
        //{
        R"(
        varying vec4 v_color;
        varying vec2 v_texcoord;
        varying vec3 v_factors;
        void main()
        {
            vec4 tex_color = texture2D(u_texture, v_texcoord);
            gl_FragColor = vec4(v_color.rgb * (1. - v_factors.x) + tex_color.rgb * v_factors.x,
                                v_color.a   * (1. - v_factors.y) + tex_color.a   * v_factors.y);
            vec4 modified = u_color_matrix * vec4(gl_FragColor.rgb, 1);
            gl_FragColor.a *= modified.a;
            gl_FragColor.rgb = modified.rgb * gl_FragColor.a;
            gl_FragColor.a *= v_factors.z;
        }
        )"
        //}
        );
    }

    void FullscreenQuad(fvec2 size = fvec2(1))
    {
        using Attribs = ShaderIdentity::attribs_t;

        static Graphics::VertexBuffer<Attribs> buffer(4);

        Attribs data[4]
        {
            {fvec2(-size.x, -size.y), fvec2(0,0)},
            {fvec2( size.x, -size.y), fvec2(1,0)},
            {fvec2( size.x,  size.y), fvec2(1,1)},
            {fvec2(-size.x,  size.y), fvec2(0,1)},
        };

        buffer.SetDataPart(0, 4, data);
        buffer.Draw(Graphics::triangle_fan);
    }

    namespace Queue
    {
        using Attribs = ShaderMain::attribs_t;
        std::vector<Attribs> array;
        constexpr int size = 9;
        static_assert(size % 3 == 0);

        void Flush()
        {
            static Graphics::VertexBuffer<Attribs> buffer(size);
            buffer.SetDataPart(0, array.size(), array.data());
            buffer.Draw(Graphics::triangles, array.size());
        }

        void Push(fvec2 pos, fvec4 color, fvec2 texcoord, fvec3 factors)
        {
            if (array.size() >= size)
                Flush();
            array.push_back({pos, color, texcoord, factors});
        }
    }

    void Init()
    {
        ShaderMain::uniforms.matrix = fmat4();
        ShaderMain::uniforms.tex_size = texture_main.Size();
        ShaderMain::uniforms.texture = Draw::texture_unit_main;
        ShaderMain::uniforms.color_matrix = fmat4();

        Queue::array.reserve(Queue::size);

        Graphics::Blending::Enable();
        Graphics::Blending::FuncNormalPre();
    }

    void Resize()
    {
        std::cout << "Resized\n";

        Graphics::Viewport(win.Size());

        scale_factor = (win.Size() / fvec2(screen_sz)).min();
        scale_factor_int = floor(scale_factor);
        fbuf_scale_tex_unit2.SetData(screen_sz * scale_factor_int);
    }
}

struct A
{
    Reflect(A)
    (
        (optional)(int)(a)(),
        (int)(b),
    )
};
struct B
{
    Reflect(B)
    (
        (A)(a),
        (float)(z),
        (std::vector<int>)(v),
    )
};

int main(int, char**)
{
    auto Tick = [&]
    {

    };

    auto Render = [&]
    {
        ivec2 sz = Draw::texture_main.Size();
        Draw::Queue::Push(fvec2(-0.5,-0.5), fvec4(1,1,0,1), fvec2(0,1) * sz, fvec3(1,1,1));
        Draw::Queue::Push(fvec2( 0.5,-0.5), fvec4(0,1,1,1), fvec2(1,1) * sz, fvec3(1,1,1));
        Draw::Queue::Push(fvec2(-0.5, 0.5), fvec4(1,0,1,1), fvec2(0,0) * sz, fvec3(1,1,1));
        Draw::Queue::Push(fvec2(-0.5, 0.5), fvec4(1,0,1,1), fvec2(0,0) * sz, fvec3(1,1,1));
        Draw::Queue::Push(fvec2( 0.5,-0.5), fvec4(0,1,1,1), fvec2(1,1) * sz, fvec3(1,1,1));
        Draw::Queue::Push(fvec2( 0.5, 0.5), fvec4(1,1,1,1), fvec2(1,0) * sz, fvec3(1,1,1));
        Draw::Queue::Flush();
    };

    Draw::Init();
    Draw::Resize();

    uint64_t frame_start = Clock::Time();

    while (1)
    {
        uint64_t time = Clock::Time(), frame_delta = time - frame_start;
        frame_start = time;

        while (metronome.Tick(frame_delta))
        {
            win.ProcessEvents();

            if (win.Resized())
                Draw::Resize();
            if (win.ExitRequested())
                Program::Exit();

            Tick();
        }

        Draw::fbuf_scale.Bind();
        Graphics::Viewport(screen_sz);
        Draw::ShaderMain::shader.Bind();

        Graphics::Clear();
        Render();

        Draw::fbuf_scale2.Bind();
        Graphics::Viewport(screen_sz * Draw::scale_factor_int);

        Draw::ShaderIdentity::shader.Bind();
        Draw::ShaderIdentity::uniforms.texture = Draw::fbuf_scale_tex_unit;

        Graphics::Clear();
        Draw::FullscreenQuad();

        Graphics::FrameBuffer::BindDefault();
        Graphics::Viewport(win.Size());
        Draw::ShaderIdentity::uniforms.texture = Draw::fbuf_scale_tex_unit2;

        Graphics::Clear();
        Draw::FullscreenQuad(win.Size() / (Draw::scale_factor_int * screen_sz));

        win.SwapBuffers();
    }

    return 0;
}
