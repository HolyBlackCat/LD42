#include "master.h"

#include <deque>
#include <iomanip>
#include <iostream>
#include <vector>

#define main SDL_main

Program::Parachute error_parachute;

constexpr ivec2 screen_sz = ivec2(1920,1080)/4;
Interface::Window win("Alpha", screen_sz*2, Interface::Window::windowed, Interface::Window::Settings{}.MinSize(screen_sz));
Audio::Context audio;
Metronome metronome;
Interface::Mouse mouse;

constexpr ivec2 tile_size = ivec2(16,16);

namespace Sounds
{
    #define SOUND_LIST \
        SOUND( dash               , 0.3  ) \

    namespace Buffers
    {
        #define SOUND(NAME, RAND) \
            Audio::Buffer NAME = Audio::Sound::WAV("assets/" #NAME ".wav");
        SOUND_LIST
        #undef SOUND
    }

    #define SOUND(NAME, RAND) \
        auto NAME(fvec2 pos, float vol = 1, float pitch = 0) \
        { \
            return Buffers::NAME(vol, std::pow(2, pitch + random_real_range(-1,1) * RAND)).pos(pos.to_vec3()); \
        }
    SOUND_LIST
    #undef SOUND

    #undef SOUND_LIST

//    Audio::Buffer theme_buf;
//    Audio::Source theme;
//    constexpr float theme_vol = 1/9.;
//    bool theme_enabled = 1;
//
    void Init()
    {
        Audio::Source::DefaultRefDistance(200);
        Audio::Source::DefaultRolloffFactor(1);
        Audio::Volume(3);

//        theme_buf.Create();
//        theme_buf.SetData(Audio::Sound::OGG("assets/theme.ogg"));
//        theme.Create(theme_buf);
//        theme.loop(1).volume(theme_vol).play();
    }
}

namespace Draw
{
    Graphics::Texture texture_main;
    Graphics::TextureUnit texture_unit_main = Graphics::TextureUnit(texture_main).Interpolation(Graphics::linear).Wrap(Graphics::clamp).SetData(Graphics::Image("assets/texture.png"));

    Graphics::Texture texture_light;
    Graphics::TextureUnit texture_unit_light = Graphics::TextureUnit(texture_light).Interpolation(Graphics::nearest).Wrap(Graphics::clamp).SetData(screen_sz);
    Graphics::FrameBuffer fbuf_light(texture_light);

    Graphics::Texture texture_dither;
    Graphics::TextureUnit texture_unit_dither = Graphics::TextureUnit(texture_dither).Interpolation(Graphics::linear).Wrap(Graphics::repeat).SetData(Graphics::Image("assets/dither.png"));

    Graphics::Texture fbuf_scale_tex;
    Graphics::TextureUnit fbuf_scale_tex_unit = Graphics::TextureUnit(fbuf_scale_tex).Interpolation(Graphics::nearest).Wrap(Graphics::clamp).SetData(screen_sz);
    Graphics::FrameBuffer fbuf_scale(fbuf_scale_tex);
    Graphics::Texture fbuf_scale_tex2;
    Graphics::TextureUnit fbuf_scale_tex_unit2 = Graphics::TextureUnit(fbuf_scale_tex2).Interpolation(Graphics::linear).Wrap(Graphics::clamp);
    Graphics::FrameBuffer fbuf_scale2(fbuf_scale_tex2);
    Graphics::Texture fbuf_scale_tex_bg;
    Graphics::TextureUnit fbuf_scale_tex_unit_bg = Graphics::TextureUnit(fbuf_scale_tex_bg).Interpolation(Graphics::nearest).Wrap(Graphics::clamp).SetData(screen_sz);
    Graphics::FrameBuffer fbuf_scale_bg(fbuf_scale_tex_bg);

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

        Graphics::Shader::Program shader("Main", {}, {}, Meta::tag<attribs_t>{}, uniforms,
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

    namespace ShaderLight
    {
        struct attribs_t
        {
            Reflect(attribs_t)
            (
                (fvec2)(pos),
                (fvec3)(color),
                (fvec2)(texcoord),
            )
        };
        struct uniforms_t
        {
            Reflect(uniforms_t)
            (
                (Graphics::Shader::VertUniform<fmat4>)(matrix),
                (Graphics::Shader::VertUniform<fvec2>)(tex_size),
                (Graphics::Shader::FragUniform<Graphics::TextureUnit>)(texture),
            )
        };
        uniforms_t uniforms;

        Graphics::Shader::Program shader("Light", {}, {}, Meta::tag<attribs_t>{}, uniforms,
        //{
        R"(
        varying vec3 v_color;
        varying vec2 v_texcoord;
        void main()
        {
            v_color = a_color;
            v_texcoord = a_texcoord / u_tex_size;
            gl_Position = u_matrix * vec4(a_pos, 0, 1);
        }
        )" //}
        ,
        //{
        R"(
        varying vec3 v_color;
        varying vec2 v_texcoord;
        void main()
        {
            gl_FragColor = vec4(texture2D(u_texture, v_texcoord).rgb * v_color, 1);
        }
        )"
        //}
        );
    }

    namespace ShaderLightApply
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
                (Graphics::Shader::FragUniform<Graphics::TextureUnit>)(dither),
            )
        };
        uniforms_t uniforms;

        Graphics::Shader::Program shader("LightApply", {}, {}, Meta::tag<attribs_t>{}, uniforms,
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
            vec3 light = texture2D(u_texture, v_texcoord).rgb;
            vec3 dither = texture2D(u_dither, gl_FragCoord.xy/8./4.).rgb;
            const float step = 1. / 4.;
            light += (dither - 0.5) * step;
            light = round(light / step) * step;
            gl_FragColor = vec4(light, 1);
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
        constexpr int size = 3000;
        static_assert(size % 3 == 0);

        void Flush()
        {
            static Graphics::VertexBuffer<Attribs> buffer(size);
            buffer.SetDataPart(0, array.size(), array.data());
            buffer.Draw(Graphics::triangles, array.size());
            array.clear();
        }

        void Push(fvec2 pos, fvec4 color, fvec2 texcoord, fvec3 factors)
        {
            if (array.size() >= size)
                Flush();
            array.push_back({pos, color, texcoord, factors});
        }
    }
    namespace LightQueue
    {
        using Attribs = ShaderLight::attribs_t;
        std::vector<Attribs> array;
        constexpr int size = 3000;
        static_assert(size % 3 == 0);

        void Flush()
        {
            static Graphics::VertexBuffer<Attribs> buffer(size);
            buffer.SetDataPart(0, array.size(), array.data());
            buffer.Draw(Graphics::triangles, array.size());
            array.clear();
        }

        void Push(fvec2 pos, fvec3 color, fvec2 texcoord)
        {
            if (array.size() >= size)
                Flush();
            array.push_back({pos, color, texcoord});
        }
    }

    template <int N> struct Src
    {
        template <typename T> using Arr = std::array<T, N>;
        Arr<fvec4> colors{};
        Arr<fvec2> texcoords{};
        Arr<fvec3> factors{};

        Src(fvec4 color, float beta = 1)
        {
            for (int i = 0; i < N; i++)
            {
                colors[i] = color;
                texcoords[i] = fvec2(0);
                factors[i] = fvec3(0,0,beta);
            }
        }
        Src(ivec2 tpos, ivec2 tsz, float alpha = 1, float beta = 1)
        {
            for (int i = 0; i < N; i++)
            {
                colors[i] = fvec4(0);
                factors[i] = fvec3(1,alpha,beta);
            }
            if constexpr (N >= 1) texcoords[0] = tpos;
            if constexpr (N >= 2) texcoords[1] = tpos.add_x(tsz.x);
            if constexpr (N >= 3) texcoords[2] = tpos.add_y(tsz.y);
            if constexpr (N >= 4) texcoords[3] = tpos + tsz;
        }
        Src(float mix, fvec3 color, ivec2 tpos, ivec2 tsz, float alpha = 1, float beta = 1)
        {
            for (int i = 0; i < N; i++)
            {
                colors[i] = color.to_vec4(0);
                factors[i] = fvec3(mix,alpha,beta);
            }
            if constexpr (N >= 1) texcoords[0] = tpos;
            if constexpr (N >= 2) texcoords[1] = tpos.add_x(tsz.x);
            if constexpr (N >= 3) texcoords[2] = tpos.add_y(tsz.y);
            if constexpr (N >= 4) texcoords[3] = tpos + tsz;
        }
    };
    using Src3 = Src<3>;
    using Src4 = Src<4>;

    void Tri(fvec2 pos, fvec2 a, fvec2 b, fvec2 c, Src<3> src)
    {
        Queue::Push(pos + a, src.colors[0], src.texcoords[0], src.factors[0]);
        Queue::Push(pos + b, src.colors[1], src.texcoords[1], src.factors[1]);
        Queue::Push(pos + c, src.colors[2], src.texcoords[2], src.factors[2]);
    }
    void Quad(fvec2 pos, fvec2 a, fvec2 b, Src<4> src)
    {
        Queue::Push(pos        , src.colors[0], src.texcoords[0], src.factors[0]);
        Queue::Push(pos + a    , src.colors[1], src.texcoords[1], src.factors[1]);
        Queue::Push(pos     + b, src.colors[2], src.texcoords[2], src.factors[2]);
        Queue::Push(pos     + b, src.colors[2], src.texcoords[2], src.factors[2]);
        Queue::Push(pos + a    , src.colors[1], src.texcoords[1], src.factors[1]);
        Queue::Push(pos + a + b, src.colors[3], src.texcoords[3], src.factors[3]);
    }
    void Quad(fvec2 pos, fvec2 size, Src<4> src)
    {
        Quad(pos, size.set_y(0), size.set_x(0), src);
    }
    void Text(fvec2 pos, std::string str, fvec3 color, float alpha = 1, float beta = 1)
    {
        float orig_x = pos.x;
        for (char ch : str)
        {
            if ((signed char)ch < 0)
                ch = '?';

            if (ch == '\n')
            {
                pos.x = orig_x;
                pos.y += 15;
            }
            else
            {
                Quad(pos, ivec2(6,15), Src4(0, color, ivec2(ch % 16 * 6, ch / 16 * 15), ivec2(6,15), alpha, beta));
                pos.x += 6;
            }
        }
    }

    void Light(fvec2 pos, float rad, fvec3 color)
    {
        constexpr int m = 8; // Margin.
        LightQueue::Push(pos + fvec2(-rad, -rad), color, fvec2(m    ,1024+m    ));
        LightQueue::Push(pos + fvec2(+rad, -rad), color, fvec2(512-m,1024+m    ));
        LightQueue::Push(pos + fvec2(-rad, +rad), color, fvec2(m    ,1024-m+512));
        LightQueue::Push(pos + fvec2(-rad, +rad), color, fvec2(m    ,1024-m+512));
        LightQueue::Push(pos + fvec2(+rad, -rad), color, fvec2(512-m,1024+m    ));
        LightQueue::Push(pos + fvec2(+rad, +rad), color, fvec2(512-m,1024-m+512));
    }

    void Background(int index, ivec2 offset)
    {
        offset = mod_ex(offset, screen_sz);
        for (int y = -1; y <= 0; y++)
        for (int x = -1; x <= 0; x++)
            Quad(ivec2(x,y)*screen_sz - screen_sz/2 + offset, screen_sz, Src4(ivec2(544,754 - screen_sz.y * index), screen_sz));
    }

    constexpr fmat4 view_mat = fmat4::ortho(screen_sz / ivec2(-2,2), screen_sz / ivec2(2,-2), -1, 1);


    void Init()
    {
        ShaderMain::uniforms.matrix = view_mat;
        ShaderMain::uniforms.tex_size = texture_main.Size();
        ShaderMain::uniforms.texture = Draw::texture_unit_main;
        ShaderMain::uniforms.color_matrix = fmat4();

        ShaderLight::uniforms.matrix = view_mat;
        ShaderLight::uniforms.tex_size = texture_main.Size();
        ShaderLight::uniforms.texture = Draw::texture_unit_main;

        ShaderLightApply::uniforms.texture = Draw::texture_unit_light;
        ShaderLightApply::uniforms.dither = Draw::texture_unit_dither;

        Queue::array.reserve(Queue::size);

        Graphics::Blending::Enable();
        Graphics::Blending::FuncNormalPre();
    }

    void Resize()
    {
        Graphics::Viewport(win.Size());

        scale_factor = (win.Size() / fvec2(screen_sz)).min();
        scale_factor_int = floor(scale_factor);
        fbuf_scale_tex_unit2.SetData(screen_sz * scale_factor_int);

        mouse.matrix = fmat3::translate(-win.Size()/2) * fmat3::scale(fvec3(1 / scale_factor));
    }
}
using Draw::Tri;
using Draw::Quad;
using Draw::Text;
using Draw::Src3;
using Draw::Src4;

namespace Tiles
{
    struct Data
    {
        const char *name;
        int tex_index;
        bool solid;
        bool slow;
    };
    constexpr int invis = std::numeric_limits<int>::max();

    std::map<int, Data> map
    {
        {00,{"air"     ,invis,0,0}},
        {30,{"shadow"  ,2    ,0,0}},
        {20,{"grass"   ,1    ,0,0}},
        {10,{"wall"    ,0    ,1,0}},
        {40,{"wall_top",3    ,1,0}},
        {50,{"stairs"  ,4    ,0,1}},
    };

    const Data &Info(int index)
    {
        if (auto it = map.find(index); it != map.end())
            return it->second;
        else
            Program::Error("Unknown tile index ", index, ".");
    }

    const std::vector<int> indices = []
    {
        std::vector<int> ret;
        for (const auto &it : map)
            ret.push_back(it.first);
        return ret;
    }();

    const std::map<int, int> tile_to_index = []
    {
        std::map<int,int> ret;
        for (int i = 0; i < int(indices.size()); i++)
            ret.insert({indices[i], i});
        return ret;
    }();
}

class Map
{
    struct Tile
    {
        Reflect(Tile)
        (
            (optional)(int)(n)(=0), // Index
            (optional)(ivec2)(v)(=ivec2(0)), // Variant
        )

        Tile() {}
        Tile(int n) : n(n) {}
        Tile(int n, ivec2 v) : n(n), v(v) {}
    };

    Reflect(Map)
    (
        (ivec2)(size)(=ivec2(0)),
        (optional)(std::vector<Tile>)(tiles,tiles_back, tiles_shadow)(),
        (optional)(ivec2)(spawn_tile, boss_tile)(=ivec2(0)),
    )

    inline static constexpr std::vector<Tile> Map::*layers[] = {&Map::tiles_back, &Map::tiles_shadow, &Map::tiles};
    inline static constexpr int layer_count = std::extent_v<decltype(layers)>;

    struct Editor
    {
        using Btn = Interface::Button;
        using Inp = Interface::Inputs::Enum;
        inline static Btn
            b_prev      = Btn(Inp::num_plus),
            b_next      = Btn(Inp::num_enter),
            b_prev_la   = Btn(Inp::num_0),
            b_next_la   = Btn(Inp::num_period),
            b_put       = Btn(Inp::mouse_left),
            b_clear     = Btn(Inp::mouse_right),
            b_mod_get   = Btn(Inp::l_shift),
            b_v1        = Btn(Inp::num_1),
            b_v2        = Btn(Inp::num_2),
            b_v3        = Btn(Inp::num_3),
            b_v4        = Btn(Inp::num_4),
            b_v5        = Btn(Inp::num_5),
            b_v6        = Btn(Inp::num_6),
            b_v7        = Btn(Inp::num_7),
            b_v8        = Btn(Inp::num_8),
            b_v9        = Btn(Inp::num_9),
            b_mod_hkey  = Btn(Inp::l_ctrl),
            b_save      = Btn(Inp::s),
            b_load      = Btn(Inp::space),
            b_set_spawn = Btn(Inp::_1),
            b_set_boss  = Btn(Inp::_2);

        ivec2 cursor = ivec2(0);
        int cur_index = 0;
        int cur_tile = 0;
        int cur_layer = 0;
        ivec2 cur_variant = ivec2(0);
    };
    Editor editor;
    std::string filename;

  public:
    bool enable_editor = 0;

    static Map FromFile(std::string name)
    {
        Map ret{};
        auto file = MemoryFile(name);
        Refl::Interface(ret).from_string(std::string(file.begin(), file.end()));

        bool ok = 1;
        for (auto la : layers)
        {
            auto &layer = ret.*la;
            if (ret.size.prod() != int(layer.size()))
            {
                if (1 || layer.empty())
                {
                    layer.resize(ret.size.prod());
                }
                else if (ret.size.prod() != int(layer.size()))
                {
                    ok = 0;
                    break;
                }
            }
        }
        if (!ok)
            Program::Error("Size mismatch in map `", name, "`.");

        ret.filename = name;
        return ret;
    }
    void Save() // Also makes a backup.
    {
        auto time = std::time(0);
        std::string time_string = std::asctime(std::localtime(&time));
        for (auto &ch : time_string)
            if (ch == ':')
                ch = '-';
        std::string backup_name = filename + "." + time_string + ".backup";
        std::rename(filename.c_str(), backup_name.c_str());
        std::string refl = Refl::Interface(*this).to_string();
        MemoryFile::Save(filename, (uint8_t*)refl.data(), (uint8_t*)refl.data() + refl.size());
    }
    void Reload()
    {
        bool had_editor = enable_editor;
        *this = FromFile(filename);
        enable_editor = had_editor;
    }

    ivec2 SpawnTile() const {return spawn_tile;}
    ivec2 BossTile() const {return boss_tile;}

    ivec2 PixelToTile(ivec2 pix)
    {
        return div_ex(pix, tile_size);
    }

    bool TilePosValid(ivec2 pos) const
    {
        return (pos >= 0).all() && (pos < size).all();
    }

    Tile Get(ivec2 pos, int layer)
    {
        if (!TilePosValid(pos) || layer < 0 || layer >= layer_count)
            return {};
        return (this->*layers[layer])[size.x * pos.y + pos.x];
    }
    void Set(ivec2 pos, int layer, const Tile &tile)
    {
        if (!TilePosValid(pos) || layer < 0 || layer >= layer_count)
            return;
        (this->*layers[layer])[size.x * pos.y + pos.x] = tile;
    }

    void Tick(ivec2 cam_pos)
    {
        if (enable_editor)
        {
            // Get tile pos
            editor.cursor = PixelToTile(mouse.pos() + cam_pos);

            // Set locations
            if (editor.b_set_spawn.pressed()) spawn_tile = editor.cursor;
            if (editor.b_set_boss.pressed()) boss_tile = editor.cursor;

            // Change layer index
            if (editor.b_prev_la.repeated() && editor.cur_layer > 0)
                editor.cur_layer--;
            if (editor.b_next_la.repeated() && editor.cur_layer+1 < layer_count)
                editor.cur_layer++;

            // Change tile index
            if (editor.b_prev.repeated() && editor.cur_index > 0)
                editor.cur_index--;
            if (editor.b_next.repeated() && editor.cur_index+1 < int(Tiles::indices.size()))
                editor.cur_index++;

            // Get tile from index
            editor.cur_tile = Tiles::indices[editor.cur_index];

            // Change variant
            if (editor.b_v1.pressed()) editor.cur_variant = ivec2(-1,+1);
            if (editor.b_v2.pressed()) editor.cur_variant = ivec2( 0,+1);
            if (editor.b_v3.pressed()) editor.cur_variant = ivec2(+1,+1);
            if (editor.b_v4.pressed()) editor.cur_variant = ivec2(-1, 0);
            if (editor.b_v5.pressed()) editor.cur_variant = ivec2( 0, 0);
            if (editor.b_v6.pressed()) editor.cur_variant = ivec2(+1, 0);
            if (editor.b_v7.pressed()) editor.cur_variant = ivec2(-1,-1);
            if (editor.b_v8.pressed()) editor.cur_variant = ivec2( 0,-1);
            if (editor.b_v9.pressed()) editor.cur_variant = ivec2(+1,-1);

            // Actions
            std::vector<ivec2> offsets;
            if (editor.b_mod_hkey.up())
            {
                offsets.push_back(ivec2(0));
            }
            else
            {
                for (int y = -2; y <= 2; y++)
                for (int x = -2; x <= 2; x++)
                    offsets.push_back(ivec2(x,y));
            }

            if (editor.b_put.down())
            {
                if (editor.b_mod_get.up()) // Draw
                {
                    for (auto o : offsets)
                        Set(editor.cursor + o, editor.cur_layer, Tile(editor.cur_tile, editor.cur_variant));
                }
                else // Pick
                {
                    auto new_tile = Get(editor.cursor, editor.cur_layer);
                    editor.cur_index = Tiles::tile_to_index.find(new_tile.n)->second;
                    editor.cur_tile = new_tile.n;
                    editor.cur_variant = new_tile.v;
                }
            }
            else if (editor.b_clear.down()) // Erase
            {
                for (auto o : offsets)
                    Set(editor.cursor + o, editor.cur_layer, Tile());
            }

            // Save/load
            if (editor.b_mod_hkey.down())
            {
                if (editor.b_save.pressed())
                    Save();
                else if (editor.b_load.pressed())
                    Reload();
            }
        }
    }

    void Render(ivec2 cam_pos)
    {
        ivec2 base_tile = PixelToTile(cam_pos);
        ivec2 half_extent = screen_sz/2 / tile_size + 1;
        for (int z = 0; z < layer_count; z++)
        for (int y = base_tile.y - half_extent.y; y <= base_tile.y + half_extent.y; y++)
        for (int x = base_tile.x - half_extent.x; x <= base_tile.x + half_extent.x; x++)
        {
            if (enable_editor && editor.b_mod_hkey.down() && z > editor.cur_layer)
                continue;

            auto tile = Get(ivec2(x,y), z);
            const auto &info = Tiles::Info(tile.n);
            if (info.tex_index != Tiles::invis)
            {
                Quad(ivec2(x,y) * tile_size - cam_pos, tile_size, Src4(ivec2((info.tex_index * 3 + 1 + tile.v.x) * tile_size.x, 512 + (1 + tile.v.y) * tile_size.x), tile_size));
            }
        }

        if (enable_editor) // Editor GUI
        {
            bool tile_pos_valid = TilePosValid(editor.cursor);

            Quad(editor.cursor * tile_size - cam_pos - 2, tile_size + 4, Src4(tile_pos_valid ? fvec4(0.2,0.2,0.2,0.5) : fvec4(0.9,0.3,0.1,0.5)));

            int tex_index = Tiles::Info(editor.cur_tile).tex_index;
            if (tile_pos_valid && tex_index != Tiles::invis)
            {
                Quad(editor.cursor * tile_size - cam_pos, tile_size, Src4(ivec2((tex_index * 3 + 1 + editor.cur_variant.x) * tile_size.x, 512 + (1 + editor.cur_variant.y) * tile_size.x), tile_size));
            }

            Text(-screen_sz/2, Str("Layer:   ", editor.cur_layer, "\n"
                                   "Tile:    ", editor.cur_index, " ", Tiles::Info(editor.cur_tile).name, "\n"
                                   "Variant: ", editor.cur_variant, "\n"), ivec3(1));

            Text(spawn_tile * tile_size + tile_size/2 - cam_pos - ivec2(0,7), "<- spawn", fvec3(0,0,1));
            Text(boss_tile * tile_size + tile_size/2 - cam_pos - ivec2(0,7), "<- boss", fvec3(0,0,1));
        }
    }
};


struct Player
{
    fvec2 pos = fvec2(0);
    fvec2 vel = fvec2(0);

    int anim_state = 0;
    int anim_frame = 0;

    int movement_ticks = 0;
};

struct World
{
    using Btn = Interface::Button;
    using Inp = Interface::Inputs::Enum;

    Btn button_up    = Inp::w;
    Btn button_down  = Inp::s;
    Btn button_left  = Inp::a;
    Btn button_right = Inp::d;

    Player p;

    fvec2 cam_pos = fvec2(0);
    fvec2 cam_vel = fvec2(0);
    fvec2 cam_pos_i = fvec2(0);

    fvec2 cloud_offset = fvec2(0);

    Map map;


    struct Light
    {
        fvec3 color;
        fvec2 pos;
        float dir, av;
        float speed;
        float size;
        int life;
        int cur_life = 0;
    };

    std::deque<Light> light_list;

    void AddLight(fvec3 color, fvec2 pos, float dir, float av, float speed, float size, int life)
    {
        Light tmp;
        tmp.color = color;
        tmp.pos = pos;
        tmp.dir = dir;
        tmp.av = av;
        tmp.speed = speed;
        tmp.size = size;
        tmp.life = life;
        light_list.push_back(tmp);
    }


    void LoadMap(std::string name)
    {
        map = Map::FromFile("assets/" + name);
        p.pos = map.SpawnTile() * tile_size + tile_size/2;
        cam_pos = p.pos;
        cam_pos_i = iround(cam_pos);
    }

    bool Solid(ivec2 pos, const std::vector<ivec2> &hitbox)
    {
        for (const auto &offset : hitbox)
        {
            if (Tiles::Info(map.Get(map.PixelToTile(pos + offset), 2).n).solid)
                return 1;
        }
        return 0;
    }
    bool Slowed(ivec2 pos, const std::vector<ivec2> &hitbox)
    {
        for (const auto &offset : hitbox)
        {
            if (Tiles::Info(map.Get(map.PixelToTile(pos + offset), 0).n).slow)
                return 1;
        }
        return 0;
    }
};


int main(int, char**)
{
    constexpr float plr_vel_step = 0.35, plr_vel_cap = 2.2;
    constexpr int plr_anim_frame_len = 10;
    const std::vector<ivec2> plr_hitbox = {ivec2(-3,-3), ivec2(-3,2), ivec2(2,2), ivec2(2,-3)};

    World w;
    w.LoadMap("map.txt");

    auto Tick = [&]
    {
        { // Map
            w.map.Tick(w.cam_pos_i);

            if (Interface::Button(Interface::Inputs::grave).pressed())
                w.map.enable_editor = !w.map.enable_editor;
        }

        { // Player
            // Get input direction
            ivec2 dir = ivec2(w.button_right.down() - w.button_left.down(), w.button_down.down() - w.button_up.down());

            { // Update animation
                if (dir)
                {
                    w.p.movement_ticks++;

                    int old_state = w.p.anim_state;

                    if (dir == ivec2(0,1))
                        w.p.anim_state = 0;
                    else if (dir == ivec2(1,1))
                        w.p.anim_state = 1;
                    else if (dir == ivec2(0,-1))
                        w.p.anim_state = 2;
                    else if (dir == ivec2(-1,1))
                        w.p.anim_state = 3;
                    else if (dir == ivec2(1,0))
                        w.p.anim_state = 4;
                    else if (dir == ivec2(-1,0))
                        w.p.anim_state = 5;
                    else if (dir == ivec2(-1,-1))
                        w.p.anim_state = 6;
                    else if (dir == ivec2(1,-1))
                        w.p.anim_state = 7;

                    if (old_state != w.p.anim_state)
                        w.p.anim_frame = 0;

                    if (w.p.movement_ticks % plr_anim_frame_len == 0)
                        w.p.anim_frame = (w.p.anim_frame + 1) % 4;
                }
                else
                {
                    w.p.movement_ticks = 0;
                    w.p.anim_frame = 0;
                }
            }

            { // Movement control
                if (dir)
                {
                    float vel_cap = plr_vel_cap;
                    if (w.Slowed(w.p.pos, plr_hitbox))
                        vel_cap *= 0.6;
                    if (w.map.enable_editor)
                        vel_cap *= 3;

                    w.p.vel += dir * plr_vel_step;
                    if (w.p.vel.len() > vel_cap)
                        w.p.vel = w.p.vel.norm() * vel_cap;
                }
                else
                {
                    if (w.p.vel.len() <= plr_vel_step)
                        w.p.vel = fvec2(0);
                    else
                        w.p.vel -= w.p.vel.norm() * plr_vel_step;
                }
            }

            { // Update position
                constexpr float step = 0.7;
                fvec2 vel_norm = w.p.vel.norm();
                for (float s = w.p.vel.len(); s >= 0; s -= step)
                {
                    float t = min(s, step);
                    fvec2 v = vel_norm * t;

                    if (!w.Solid(iround(w.p.pos.add_x(v.x)), plr_hitbox) || w.map.enable_editor)
                        w.p.pos.x += v.x;
                    if (!w.Solid(iround(w.p.pos.add_y(v.y)), plr_hitbox) || w.map.enable_editor)
                        w.p.pos.y += v.y;
                }
            }

            { // Update audio pos
                Audio::ListenerPos(w.p.pos.to_vec3(-250));
            }
        }

        { // Camera
            fvec2 target = w.p.pos;
            fvec2 delta = target - w.cam_pos;
            fvec2 dir = delta.norm();
            float dist = delta.len();
            w.cam_vel += dir * pow(dist / 60, 2);
            w.cam_vel *= 0.8;
            w.cam_pos += w.cam_vel;
            w.cam_pos_i = iround(w.cam_pos);
        }

        { // Light particles
            auto it = w.light_list.begin();
            while (it != w.light_list.end())
            {
                it->cur_life++;
                if (it->cur_life > it->life)
                {
                    it = w.light_list.erase(it);
                    continue;
                }
                it->pos += fvec2::dir(it->dir, it->speed);
                it->dir += it->av;
                it++;
            }
        }

        { // Clouds
            constexpr int period = 2000;
            constexpr float speed = 1;
            float angle = sin(metronome.ticks % period / float(period) * 2 * f_pi) * 0.55;
            w.cloud_offset += fvec2::dir(angle, speed);
        }

        { // Make light particles
            float d = (w.map.BossTile() * tile_size + tile_size/2 - w.p.pos).len();
            float f = d / 80;
            float ra = random_real_range(f_pi);
            w.AddLight((fvec3(sin(ra), sin(ra+f_pi*2/3), sin(ra-f_pi*2/3))*0.5+0.5) * 0.96 + 0.05, w.map.BossTile() * tile_size + tile_size/2, random_real_range(f_pi), random_real_range(0.1), random_real_range(0,0.25+sqrt(f*4)), random_real_range(10, 100)*f+30, random_int_range(0, 40)*f+20);
        }
    };

    auto Render = [&]
    {
        { // Map
            w.map.Render(w.cam_pos_i);
        }

        { // Player
            constexpr ivec2 size(32);
            Quad(iround(w.p.pos - size/2).sub_y(8) - w.cam_pos_i, size, Src4(ivec2(0,128).add_x(size.x * (w.p.anim_state * 4 + w.p.anim_frame)), size));
        }
    };

    auto Background = [&]
    {
        ivec2 v = iround(w.cloud_offset - w.cam_pos / 2);
        Draw::Background(0, iround(v / 10.));
        Draw::Background(1, iround(v / 4.));
        Draw::Background(2, iround(v / 2.));
    };

    auto Light = [&]
    {
        for (const auto &li : w.light_list)
            Draw::Light(li.pos - w.cam_pos, li.size * (1 - li.cur_life / float(li.life)), li.color);
    };

    Sounds::Init();
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

            audio.CheckErrors();
            Audio::Source::RemoveUnused();
        }

        // Render in original scale
        // - Background
        Draw::fbuf_scale_bg.Bind();
        Graphics::Viewport(screen_sz);
        Draw::ShaderMain::shader.Bind();

        Graphics::Clear();
        Background();
        Draw::Queue::Flush();

        // - Everything else
        Draw::fbuf_scale.Bind();

        Graphics::SetClearColor(fvec4(0));
        Graphics::Clear();
        Graphics::SetClearColor(fvec3(0));
        Render();
        Draw::Queue::Flush();

        // Render light
        Draw::fbuf_light.Bind();
        Draw::ShaderLight::shader.Bind();

        Graphics::Clear();
        Graphics::Blending::FuncAdd();
        Light();
        Draw::LightQueue::Flush();

        // Apply light
        Draw::fbuf_scale.Bind();
        Draw::ShaderLightApply::shader.Bind();

        Graphics::Blending::Func(Graphics::Blending::dst, Graphics::Blending::zero);
        Draw::FullscreenQuad();
        Graphics::Blending::FuncNormalPre();

        // Move objects onto background
        Draw::fbuf_scale_bg.Bind();
        Graphics::Viewport(screen_sz);
        Draw::ShaderIdentity::shader.Bind();
        Draw::ShaderIdentity::uniforms.texture = Draw::fbuf_scale_tex_unit;

        Draw::FullscreenQuad();

        // Upscale 1, to framebuffer
        Draw::fbuf_scale2.Bind();
        Graphics::Viewport(screen_sz * Draw::scale_factor_int);
        Draw::ShaderIdentity::shader.Bind();
        Draw::ShaderIdentity::uniforms.texture = Draw::fbuf_scale_tex_unit_bg;

        Graphics::Clear();
        Draw::FullscreenQuad();

        // Upscale 2, to screen
        Graphics::FrameBuffer::BindDefault();
        Graphics::Viewport(win.Size());
        Draw::ShaderIdentity::uniforms.texture = Draw::fbuf_scale_tex_unit2;

        Graphics::Clear();
        Draw::FullscreenQuad(Draw::scale_factor * screen_sz / fvec2(win.Size()));

        Graphics::CheckErrors();

        win.SwapBuffers();
    }

    return 0;
}
