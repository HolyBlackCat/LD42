#include "master.h"

#include <deque>
#include <iomanip>
#include <iostream>
#include <vector>

#define main SDL_main

Program::Parachute error_parachute;

constexpr ivec2 screen_sz = ivec2(1920,1080)/4;
Interface::Window win("The last witch-knight", screen_sz*2, Interface::Window::windowed, Interface::Window::Settings{}.MinSize(screen_sz));
Audio::Context audio;
Metronome metronome;
Interface::Mouse mouse;

constexpr ivec2 tile_size = ivec2(16,16);

namespace Sounds
{
    #define SOUND_LIST \
        SOUND( player_shoots      , 0.3  ) \
        SOUND( death              , 0.3  ) \
        SOUND( crystal_shoots     , 0.3  ) \
        SOUND( boss_hit           , 0.3  ) \
        SOUND( boss_big_hit       , 0.3  ) \
        SOUND( dash               , 0.3  ) \
        SOUND( graze              , 0.3  ) \
        SOUND( phase_changes      , 0.3  ) \
        SOUND( stopped_by_shield  , 0.3  ) \
        SOUND( boss_aims          , 0.3  ) \
        SOUND( boss_shield        , 0.3  ) \
        SOUND( boss_shield_magic  , 0.3  ) \
        SOUND( boss_shield_breaks , 0.3  ) \
        SOUND( boss_charges       , 0.3  ) \
        SOUND( boss_laser         , 0.3  ) \
        SOUND( boss_dash          , 0.3  ) \
        SOUND( boss_dies          , 0.3  ) \

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
                (Graphics::Shader::FragUniform<float>)(opacity),
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
            light = 1. - ((1. - light) * u_opacity);
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
    template <int A = -1> void Text(fvec2 pos, std::string str, fvec3 color, float alpha = 1, float beta = 1)
    {
        float orig_x = pos.x;

        if (A != -1)
        {
            for (char ch : str)
            {
                if (ch == '\n')
                    break;
                pos.x -= (A == 0 ? 3 : 6);
            }
        }

        for (const char &ch_ref : str)
        {
            char ch = ch_ref;
            if ((signed char)ch < 0)
                ch = '?';

            if (ch == '\n')
            {
                pos.x = orig_x;
                pos.y += 15;

                if (A != -1)
                {
                    const char *ptr = &ch_ref + 1;

                    while (*ptr)
                    {
                        if (*ptr == '\n')
                            break;
                        pos.x -= (A == 0 ? 3 : 6);
                        ptr++;
                    }
                }
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
        ShaderLightApply::uniforms.opacity = 0.9;

        Queue::array.reserve(Queue::size);
        LightQueue::array.reserve(Queue::size);

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
    fvec2 dir = fvec2(0,1);

    int fire_cooldown = 20; // Arbitrary starting value.
    int fire_len = 0;
    fvec2 fire_source = fvec2(0);
    fvec2 fire_dir = fvec2(0);

    int anim_state = 0;
    int anim_frame = 0;

    int dash_cooldown = 20; // Arbitrary starting value.
    int dash_len = 0;

    int movement_ticks = 0;

    bool grazed_this_tick = 0;

    bool dead = 0;
    int death_timer = 0;
    int death_msg_index = 0;
    bool respawning = 0;

    fvec2 Center() const
    {
        return pos.sub_y(8);
    }
};

struct Boss
{
    enum class Phase
    {
        crystals,
        first,
        more_crystals,
        magic,
        fin,
    };

    Phase phase = Phase::crystals;
    int phase_time = 1; // Sic!

    fvec2 target_pos = fvec2(0);
    fvec2 pos = fvec2(0);
    int invin = 0;

    bool safe_shield = 1;
    bool magic_shield = 0;
    bool magic_shield_broken = 0;

    bool crystals_wait = 1;
    bool first_preparing = 1;
    fvec2 first_prep_dir = fvec2(0,-1);
    int first_rot_sign = 1;
    float first_rad = 0;
    float first_angle = 0;
    float first_laser_angle = 0;
    float first_laser_len = 0;
    bool first_enable_laser = 0;
    bool first_laser_deadly = 0;
    bool mcr_moving_back = 1;
    bool mgc_preparing = 1;
    fvec2 mgc_target = fvec2(0);
    fvec2 mgc_dest = fvec2(0);
    int mgc_unit = 0;
    int mgc_time = 0;

    bool dead = 0;
    int death_timer = 0;

    int hits_taken = 0;
    int prev_hits_taken = 0;

    struct Crystal
    {
        ivec2 pos;
        int hp = 3;
        int invin = 0;
    };

    std::vector<Crystal> crystal_list;

    struct MagicOrb
    {
        fvec2 pos = fvec2(0);
        fvec2 target_pos = fvec2(0);
        int hp = 3;
        int invin = 0;

        MagicOrb(fvec2 pos) : pos(pos), target_pos(pos) {}
    };

    std::vector<MagicOrb> magic_orb_list;
};

struct World
{
    using Btn = Interface::Button;
    using Inp = Interface::Inputs::Enum;

    Btn button_up    = Inp::up;
    Btn button_down  = Inp::down;
    Btn button_left  = Inp::left;
    Btn button_right = Inp::right;
    Btn button_fire  = Inp::x;
    Btn button_dash  = Inp::z;

    Player p;
    Boss b;

    fvec2 cam_pos = fvec2(0);
    fvec2 cam_vel = fvec2(0);
    fvec2 cam_pos_i = fvec2(0);

    fvec2 cloud_offset = fvec2(0);

    float target_light_rad = 1000; // Arbitrary starting value.
    float cur_light_rad = 1000; // Arbitrary starting value.

    float darkness = 1;
    float whiteness = 0;

    float dark_force_timer = 1;
    fvec2 dark_force_pos[2] {};

    bool enable_light = 0;

    Map map;

    struct Bullet
    {
        enum Type {player, crystal};
        Type type;

        fvec2 pos;
        fvec2 vel;

        float LightSize() const
        {
            return 40;
        }
        fvec3 LightColor() const
        {
            switch (type)
            {
              case player:
                return fvec3(0.05,0.2,1);
              case crystal:
                return fvec3(1, 0.3, 0.05) * 2.5;
            }
            return fvec3(1);
        }
        void Render(ivec2 cam_pos) const
        {
            constexpr ivec2 size(32);
            switch (type)
            {
              case player:
                Quad(iround(pos) - cam_pos - size/2, size, Src4(ivec2(96,64), size, 1, 0));
                break;
              case crystal:
                Quad(iround(pos) - cam_pos - size/2, size, Src4(ivec2(96+32,64), size, 1, 0));
                break;
            }
        }
        void DeathEffect(World &w) const
        {
            switch (type)
            {
              case player:
                for (int i = 0; i < 2; i++)
                {
                    fvec2 p = pos + vel.norm() * random_real_range(1) + vel.norm().rot90() * random_real_range(1);
                    fvec3 c(random_real_range(0.5,1), 1, 1);
                    w.AddParticle(c, 1, 0.5, p, vel.angle() + f_pi + random_real_range(0.1), random_real_range(0.16), random_real_range(1,3), random_real_range(12,18), random_int_range(10,30));
                }
                break;
              case crystal:
                for (int i = 0; i < 16; i++)
                {
                    fvec2 p = pos + vel.norm() * random_real_range(1) + vel.norm().rot90() * random_real_range(1);
                    fvec3 c(1, random_real_range(0.2,1), 0);
                    w.AddParticle(c, 1, 0.5, p, random_real_range(f_pi), random_real_range(0.3), random_real_range(1,3), random_real_range(16,22), random_int_range(10,30));
                }
                break;
            }
        }
        bool Enemy() const
        {
            return type != player;
        }
        float Homing() const
        {
            switch (type)
            {
              case player:
                return -1;
              case crystal:
                return 0.01;
            }
            return -1;
        }
        float CollisionRadius() const
        {
            switch (type)
            {
              case player:
                return 3;
              case crystal:
                return 6;
            }
            return 1;
        }
    };

    std::deque<Bullet> bullet_list;

    void AddBullet(fvec2 pos, fvec2 vel, Bullet::Type type)
    {
        bullet_list.push_back(Bullet{type, pos, vel});
    }

    struct Particle
    {
        fvec3 color;
        float alpha, beta;
        fvec2 pos;
        float dir, av;
        float speed;
        float size;
        int life;
        int cur_life = 0;
    };

    std::deque<Particle> particle_list;

    void AddParticle(fvec3 color, float alpha, float beta, fvec2 pos, float dir, float av, float speed, float size, int life)
    {
        Particle tmp;
        tmp.color = color;
        tmp.alpha = alpha;
        tmp.beta = beta;
        tmp.pos = pos;
        tmp.dir = dir;
        tmp.av = av;
        tmp.speed = speed;
        tmp.size = size;
        tmp.life = life;
        particle_list.push_back(tmp);
    }


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
        b.pos = b.target_pos = BossHome();
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

    fvec2 BossHome() const
    {
        return map.BossTile() * tile_size + tile_size/2;
    }
};

int death_counter = 0;
int min_y = 9000;

int main(int, char**)
{
    constexpr float
        plr_vel_step = 0.35, plr_vel_cap = 2.2, crystal_dist = 215, crystal_dist_2 = 170, crystal_anim_offset = 2, plr_bullet_speed = 4,
        plr_hitbox_rad = 6, scr_transition_step = 0.006, scr_transition_step_fast = 0.01, plr_dash_speed = 6,
        dark_force_step = 0.004, dark_force_max_dist = 200, boss_laser_offset = 20, boss_laser_hitbox_w = 10, boss_hitbox_rad = 18, orb_hitbox_rad = 9;
    constexpr int
        plr_anim_frame_len = 10, crystal_anim_period = 180, plr_fire_cooldown = 18, plr_fire_len = 8, enemy_invin_frames = 20, plr_dash_cooldown = 30, plr_dash_len = 8;
    const std::vector<ivec2> plr_hitbox = {ivec2(-3,-3), ivec2(-3,2), ivec2(2,2), ivec2(2,-3)},
                             bullet_hitbox = {ivec2(-2,-2), ivec2(-2,1), ivec2(1,1), ivec2(1,-2)},
                             point_hitbox = {ivec2(0)};

    const std::vector<std::string> death_messages
    {
        "",
        "Again...",
        "Once again",
        "Alright, new tactic: git gud",
        "Rekt",
        "Oh noes!",
        "Don't give up!",
        "Try shooting the enemy",
        "You were close",
        "Is it even possible?",
        "Darkness is dangerous!",
        "How!?",
        "Wanna try again?",
    };

    World w;
    w.LoadMap("map.txt");

    World saved_world = w;

    auto Tick = [&]
    {
        auto &boss = w.b;
        min_y = min(min_y, w.p.pos.y);

        w.p.grazed_this_tick = 0;

        auto KillPlayer = [&]
        {
            if (w.p.dead)
                return;
            if (boss.dead)
                return;
            if (w.p.dash_len > 0)
            {
                if (!w.p.grazed_this_tick)
                {
                    w.p.grazed_this_tick = 1;
                    Sounds::graze(w.p.Center());
                }
                return;
            }

            Sounds::death(fvec2(0)).relative();

            death_counter++;

            w.p.dead = 1;
            w.whiteness = 1;
            w.p.death_msg_index = random_int(death_messages.size());

            for (int i = 0; i < 24; i++)
            {
                float c = random_real_range(0,1);
                w.AddParticle(fvec3(c, 4/5. + 1/5. * c, 1), 1, 0.5, w.p.Center() + fvec2::dir(random_real_range(f_pi), 4), random_real_range(f_pi), random_real_range(0.2),
                              random_real_range(0.2,2), random_real_range(2,6), random_int_range(30,80));
            }
        };

        { // Map
            w.map.Tick(w.cam_pos_i);

            if (Interface::Button(Interface::Inputs::grave).pressed())
                w.map.enable_editor = !w.map.enable_editor;
        }

        { // Player
            // Get input direction
            ivec2 dir = ivec2(w.button_right.down() - w.button_left.down(), w.button_down.down() - w.button_up.down());
            if (w.p.dead) dir = ivec2(0);

            { // Update animation
                if (dir && !w.p.dash_len)
                {
                    w.p.dir = dir.norm();
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

            { // Dash
                if (!w.p.dead)
                {
                    if (w.p.dash_cooldown == 0)
                    {
                        if (w.button_dash.pressed())
                        {
                            w.p.dash_cooldown = plr_dash_cooldown;
                            w.p.dash_len = plr_dash_len;
                            w.p.vel = w.p.dir * plr_dash_speed;
                            Sounds::dash(w.p.Center());
                        }
                    }
                    else
                    {
                        w.p.dash_cooldown--;
                    }
                }
            }

            { // Movement control
                if (w.p.dash_len > 0)
                {
                    w.p.dash_len--;
                    for (int i = 0; i < 6; i++)
                    {
                        fvec2 pos = w.p.Center() + w.p.dir * random_real_range(0, 6) + w.p.dir.rot90() * random_real_range(2);
                        fvec3 color(random_real_range(0.5,1), 1, 1);
                        w.AddParticle(color, 1, 0.5, pos, w.p.dir.angle(), random_real_range(0.5), random_real_range(0.5,1.2), random_real_range(6,10), random_int_range(10,30));
                    }
                }
                else if (dir)
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
                    else if (w.p.vel.len() > plr_vel_cap)
                        w.p.vel = w.p.vel.norm() * plr_vel_cap;
                    else
                        w.p.vel -= w.p.vel.norm() * plr_vel_step;
                }
            }

            { // Shooting
                if (!w.p.dead)
                {
                    if (w.p.fire_cooldown)
                        w.p.fire_cooldown--;

                    if (w.button_fire.pressed() && w.p.fire_cooldown == 0)
                    {
                        Sounds::player_shoots(w.p.Center() + w.p.dir * 20);
                        w.p.fire_cooldown = plr_fire_cooldown;
                        w.p.fire_len = plr_fire_len;
                        w.p.fire_source = w.p.Center();
                        w.p.fire_dir = (dir ? fvec2(dir).norm() : w.p.dir);

                        for (int i = 0; i < 10; i++)
                        {
                            fvec2 pos = w.p.fire_source + w.p.fire_dir * random_real_range(1) + w.p.fire_dir.rot90() * random_real_range(1);
                            fvec3 color(random_real_range(0.5,1), 1, 1);
                            w.AddParticle(color, 1, 0.5, pos, w.p.fire_dir.angle(), random_real_range(0.1), random_real_range(1,3), random_real_range(10,15), random_int_range(10,30));
                        }
                    }
                    if (w.p.fire_len)
                    {
                        w.p.fire_len--;
                        fvec2 pos = w.p.fire_source + w.p.fire_dir * 4;
                        w.AddBullet(pos, w.p.fire_dir * plr_bullet_speed, World::Bullet::player);
                    }
                }
            }

            { // Update position
                if (!w.p.dead)
                {
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
            }

            { // Update audio pos
                Audio::ListenerPos(w.p.Center().to_vec3(-250));
            }

            { // Death effects
                if (w.p.dead)
                {
                    w.p.death_timer++;
                }
            }

            { // Light
                w.AddLight(fvec3(0.9), w.p.Center(), 0, 0, 0, random_real_range(60,70), 2);
            }
        }

        { // Boss
            Boss::Phase old_phase = boss.phase;

            bool damaged = 0;

            { // Check health
                if (boss.hits_taken > boss.prev_hits_taken)
                {
                    if (boss.hits_taken >= 3)
                    {
                        Sounds::boss_big_hit(boss.pos);
                        boss.hits_taken = 0;
                        damaged = 1;
                    }
                    else
                    {
                        Sounds::boss_hit(boss.pos);
                    }
                }

                boss.prev_hits_taken = boss.hits_taken;
            }

            // Primary logic
            switch (boss.phase)
            {
              case Boss::Phase::crystals:
                {
                    if (boss.phase_time == 1)
                    {
                        w.target_light_rad = 440;
                        w.cur_light_rad = 600;

                        // Make crystals
                        for (int i = 0; i < 4; i++)
                        {
                            float a = f_pi/4 + f_pi/2 * i;
                            Boss::Crystal tmp;
                            tmp.pos = iround(fvec2::dir(a, crystal_dist)).sub_y(16) + w.BossHome();
                            boss.crystal_list.push_back(tmp);
                        }
                    }
                    if (boss.crystal_list.empty() && boss.safe_shield)
                    {
                        boss.safe_shield = 0;
                        Sounds::boss_shield_breaks(boss.pos);
                    }

                    if (damaged)
                    {
                        boss.phase = Boss::Phase::first;
                    }
                }
                break;

              case Boss::Phase::first:
                {
                    if (boss.phase_time == 1)
                    {
                        w.target_light_rad = 300;
                        boss.first_prep_dir = fvec2(1,0).rot90(random_int(4));
                        boss.first_rot_sign = random_sign();
                        boss.safe_shield = 1;
                        Sounds::boss_shield(boss.pos);
                    }
                    if (boss.first_preparing)
                    {
                        boss.target_pos += boss.first_prep_dir * 0.75;
                        fvec2 delta = boss.pos - w.BossHome();
                        float dist = delta.len();
                        if (dist > 250)
                        {
                            boss.first_preparing = 0;
                            boss.first_rad = dist;
                            boss.first_angle = delta.angle();
                            boss.first_laser_angle = (w.p.Center() - boss.pos).angle();
                        }
                    }
                    else
                    {
                        // Timing
                        if (boss.phase_time == 500)
                        {
                            boss.first_enable_laser = 1;
                            Sounds::boss_charges(boss.pos);
                        }

                        if (boss.phase_time == 660)
                        {
                            boss.first_laser_deadly = 1;
                            boss.safe_shield = 0;
                            Sounds::boss_laser(boss.pos);
                            // No shield break sound, because it would interfere with laser sound.
                        }

                        // Move
                        constexpr float angle_step = 0.003;
                        while (boss.first_angle < -f_pi) boss.first_angle += 2*f_pi;
                        while (boss.first_angle >  f_pi) boss.first_angle -= 2*f_pi;
                        boss.first_angle += angle_step * boss.first_rot_sign;
                        boss.first_laser_angle += angle_step * boss.first_rot_sign;
                        boss.target_pos = w.BossHome() + fvec2::dir(boss.first_angle, boss.first_rad);
                    }

                    if (damaged)
                    {
                        boss.phase = Boss::Phase::more_crystals;
                    }
                }
                break;

              case Boss::Phase::more_crystals:
                {
                    if (boss.phase_time == 1)
                    {
                        w.target_light_rad = 170;
                        Sounds::boss_shield(boss.pos);
                        boss.safe_shield = 1;
                        boss.first_laser_deadly = 0;

                        boss.crystals_wait = 1;

                        bool vert = abs((w.p.Center() - w.BossHome()).ratio()) > 1;

                        // Make crystals
                        for (int i = 0; i < 2; i++)
                        {
                            float a = f_pi * i + vert * f_pi/2;
                            Boss::Crystal tmp;
                            tmp.pos = iround(fvec2::dir(a, crystal_dist_2)).sub_y(16) + w.BossHome();
                            boss.crystal_list.push_back(tmp);

                            for (int i = 0; i < 16; i++)
                            {
                                fvec3 c(1, random_real_range(0.2,1), 0);
                                w.AddParticle(c, 1, 0.5, tmp.pos + fvec2(random_real_range(1), random_real_range(1)), random_real_range(f_pi), random_real_range(0.2),
                                              random_real_range(1,3), random_real_range(18,24), random_int_range(10,35));
                            }
                        }
                    }

                    if (boss.phase_time == 30)
                    {
                        boss.first_enable_laser = 0;
                    }

                    // Move to center
                    if (boss.mcr_moving_back)
                    {
                        constexpr float step = 0.75;

                        fvec2 delta = w.BossHome() - boss.target_pos;
                        float len = delta.len();

                        if (len <= step * 1.01)
                        {
                            boss.target_pos = w.BossHome();
                            boss.mcr_moving_back = 0;
                            boss.crystals_wait = 0;
                        }
                        else
                        {
                            boss.target_pos += delta.norm() * step;
                        }
                    }

                    if (boss.crystal_list.empty() && boss.safe_shield)
                    {
                        boss.safe_shield = 0;
                        Sounds::boss_shield_breaks(boss.pos);
                    }

                    if (damaged)
                    {
                        boss.phase = Boss::Phase::magic;
                    }
                }
                break;

              case Boss::Phase::magic:
                {
                    if (boss.phase_time == 1)
                    {
                        w.target_light_rad = 130;
                        boss.safe_shield = 0;
                        boss.magic_shield = 1;
                        Sounds::boss_shield_magic(boss.pos);
                        for (int s = -1; s <= 1; s += 2)
                            boss.magic_orb_list.push_back(Boss::MagicOrb(w.BossHome() + fvec2::dir(-f_pi/2 + s*f_pi/4, 113).add_x(s*16).sub_y(32)));
                    }

                    if (boss.mgc_preparing)
                    {
                        boss.target_pos.y -= 1;

                        if ((boss.pos - w.BossHome()).len() > 250)
                            boss.mgc_preparing = 0;
                    }
                    else
                    {
                        constexpr int period = 80;
                        constexpr float overshoot_dist = 120, dash_speed = 5;

                        int unit_count = 1 + boss.magic_orb_list.size();
                        int unit = boss.mgc_time / period % unit_count;
                        int time = boss.mgc_time % period;

                        auto UnitPos = [&]() -> fvec2 &
                        {
                            if (unit == unit_count - 1)
                                return boss.target_pos;
                            else
                                return boss.magic_orb_list[unit].target_pos;
                        };

                        if (time == 10 && w.p.dead)
                            boss.mgc_time--;

                        if (time == 30)
                        {
                            boss.mgc_target = w.p.Center();
                            boss.mgc_dest = boss.mgc_target + (boss.mgc_target - UnitPos()).norm() * overshoot_dist;
                            Sounds::boss_aims(w.p.pos);
                        }

                        if (time == 64)
                        {
                            // We play sound here because `time == 65` will be stuck for the entire dash.
                            Sounds::boss_dash(boss.pos);
                        }
                        if (time == 65)
                        {
                            fvec2 delta = boss.mgc_dest - UnitPos();
                            float len = delta.len();
                            if (len <= dash_speed * 1.01)
                            {
                                UnitPos() = boss.mgc_dest;

                                fvec2 dir = (w.p.Center() - UnitPos()).norm();

                                if (boss.magic_orb_list.size() < 2)
                                {
                                    bool many = boss.magic_orb_list.empty();
                                    for (int i = -many; i <= many; i++)
                                    {
                                        fvec2 d = fvec2::dir(dir.angle() + i * f_pi/12, dir.len());
                                        w.AddBullet(UnitPos() + d * (unit == unit_count - 1 ? 50 : 6), d * 2, World::Bullet::crystal);
                                    }
                                    Sounds::crystal_shoots(UnitPos());
                                }
                            }
                            else
                            {
                                UnitPos() += delta.norm() * dash_speed;
                                boss.mgc_time--;
                            }
                        }

                        if (time == period-1)
                            boss.mgc_unit++;

                        boss.mgc_unit %= unit_count;

                        boss.mgc_time++;
                    }

                    if (boss.magic_orb_list.empty() && !boss.magic_shield_broken)
                    {
                        Sounds::boss_shield_breaks(boss.pos);
                        boss.magic_shield_broken = 1;
                    }

                    if (damaged)
                    {
                        boss.phase = Boss::Phase::fin;
                    }
                }
                break;

              case Boss::Phase::fin:
                {
                    if (boss.phase_time == 1)
                    {
                        Sounds::boss_dies(boss.pos);
                        boss.dead = 1;
                        boss.magic_shield = 0;

                        for (int i = 0; i < 64; i++)
                        {
                            float c = random_real_range(0,1);
                            fvec3 color(1, 0.6+c*0.4, 0.2+c*0.8);
                            w.AddParticle(color, 1, 0.5, boss.pos + fvec2(random_real_range(8), random_real_range(8)), random_real_range(f_pi), random_real_range(0.2),
                                          random_real_range(0.5,8), random_real_range(22,50), random_int_range(120,200));
                        }
                    }

                    float l = smoothstep(clamp(min(boss.death_timer*2., 60 - boss.death_timer/5.)/60.));

                    w.AddLight(fvec3(1,0.8,0.3) * 2 * l, boss.pos, 0, 0, 0, random_real_range(160,180), 2);

                    if (boss.death_timer < 500)
                        w.target_light_rad = 100 + boss.death_timer;
                    else
                        w.enable_light = 0;

                    boss.death_timer++;
                }
                break;
            }

            // Effects of changing phase
            if (boss.phase != old_phase)
            {
                Sounds::phase_changes(w.p.pos, 10); // We play it at player pos, otherwise it's too hard to hear
                boss.phase_time = 0;
            }

            { // Move to target
                boss.pos += (boss.target_pos - boss.pos) * 0.1;
            }

            { // Crystals
                int period = boss.crystal_list.size() * 40 - 15;
                for (int i = 0; i < int(boss.crystal_list.size()); i++)
                {
                    auto &it = boss.crystal_list[i];

                    { // Effect
                        float c = random_real_range(0, 1);
                        w.AddParticle(fvec3(1, c, 0.5 + c/2), 0.3, 0.2, it.pos + fvec2(random_real_range(2), random_real_range(2)), 0, 0, 0, random_real_range(70,75), 3);
                    }

                    // Light
                    w.AddLight(fvec3(1,0.8,0.2)*2, it.pos, 0, 0, 0, random_real_range(60,70), 2);

                    // Spawn bullet
                    if (!boss.crystals_wait && !w.p.dead && int(metronome.ticks % period) == iround(i / float(boss.crystal_list.size()) * period))
                    {
                        fvec2 dir = (w.p.Center() - it.pos).norm();
                        w.AddBullet(it.pos + dir * 6, dir * 2, World::Bullet::crystal);
                        Sounds::crystal_shoots(it.pos);
                    }

                    // Kill player on collision
                    if ((it.pos - w.p.pos).len() < plr_hitbox_rad + 16)
                        KillPlayer();

                    if (it.invin > 0)
                        it.invin--;
                }
            }

            // Laser
            if (boss.first_enable_laser)
            {
                { // Rotate laser
                    float step = boss.first_laser_deadly ? 0.006 : 0.02;

                    fvec2 target_dir = (w.p.Center() - boss.pos).norm();
                    fvec2 dir = fvec2::dir(boss.first_laser_angle);
                    float angle_delta = acos(dir /dot/ target_dir) * sign(dir /cross/ target_dir);
                    if (abs(angle_delta) < step)
                        boss.first_laser_angle = target_dir.angle();
                    else
                        boss.first_laser_angle += sign(angle_delta) * step;
                }

                { // Fire laser
                    fvec2 laser_dir = fvec2::dir(boss.first_laser_angle);
                    fvec2 laser_end = boss.pos;
                    float laser_step = 6;
                    for (int i = 0; i < 1000; i++)
                    {
                        fvec2 test = laser_end + laser_dir * laser_step;
                        if (w.Solid(test, point_hitbox))
                        {
                            laser_step /= 2;
                            if (laser_step < 0.05)
                                break;
                        }
                        else
                        {
                            laser_end = test;
                        }
                    }
                    boss.first_laser_len = (laser_end - boss.pos).len();
                }

                { // Laser particles
                    if (boss.first_enable_laser)
                    {
                        fvec2 dir = fvec2::dir(boss.first_laser_angle);
                        fvec2 pos = boss.pos + dir * boss.first_laser_len;
                        for (int i = 0; i < 2; i++)
                        {
                            fvec2 p = pos + dir * random_real_range(1) +dir.rot90() * random_real_range(1);
                            fvec3 c(1, random_real_range(0.5,1), 0.5);
                            float size = boss.first_laser_deadly ? random_real_range(10,20) : random_real_range(2,5);
                            int life = random_int_range(10,30);
                            w.AddParticle(c, 1, 0.5, p, random_real_range(f_pi), random_real_range(0.3), random_real_range(1,3), size, life);
                        }
                        pos = boss.pos + dir * boss_laser_offset;
                        for (int i = 0; i < 2; i++)
                        {
                            fvec2 p = pos + dir * random_real_range(1) +dir.rot90() * random_real_range(1);
                            fvec3 c(1, random_real_range(0.5,1), 0.5);
                            float size = boss.first_laser_deadly ? random_real_range(10,20) : random_real_range(2,5);
                            int life = random_int_range(10,30);
                            w.AddParticle(c, 1, 0.5, p, boss.first_laser_angle + random_real_range(f_pi / 16), random_real_range(0.15), random_real_range(1,3), size, life);
                        }
                    }
                }

                { // Kill player with laser
                    if (boss.first_laser_deadly)
                    {
                        fvec2 d = fvec2::dir(boss.first_laser_angle), n = d.rot90();
                        float half = boss_laser_hitbox_w / 2;
                        fvec2 delta = w.p.Center() - boss.pos;
                        float dist_a = delta /dot/ d, dist_b = delta /dot/ n;
                        if (dist_a > boss_laser_offset && dist_a < boss.first_laser_len + half && abs(dist_b) < half)
                        {
                            KillPlayer();
                        }
                    }

                    if (w.p.dead && w.p.death_timer > 30)
                        boss.first_laser_deadly = 0;
                }
            }

            { // Magic orbs
                for (auto &it : boss.magic_orb_list)
                {
                    it.pos += (it.target_pos - it.pos) * 0.1;

                    if (it.invin > 0)
                        it.invin--;

                    if ((w.p.pos - it.pos).len() < plr_hitbox_rad + orb_hitbox_rad)
                        KillPlayer();
                }
            }

            { // Kill player on body collision
                if (!boss.dead && (boss.pos - w.p.pos).len() < plr_hitbox_rad + boss_hitbox_rad)
                    KillPlayer();
            }

            constexpr float shield_rad = 48;

            { // Safe shield

                if (boss.safe_shield)
                {
                    // Push player
                    fvec2 delta = w.p.pos - boss.pos;
                    if (delta.len() < shield_rad)
                    {
                        delta = delta.norm();
                        fvec2 new_pos = boss.pos + (delta * (shield_rad + 1));
                        if (!w.Solid(iround(new_pos), plr_hitbox))
                            w.p.pos = new_pos;
                        Sounds::stopped_by_shield(boss.pos);
                        for (int i = 0; i < 2; i++)
                        {
                            float c = random_real_range(0.4,0.9);
                            int sign = random_sign();
                            w.AddParticle(fvec3(c), 0.5, 0.2, w.p.pos - delta*5 + fvec2(random_real_range(2), random_real_range(2)), delta.angle() - f_pi/2*sign, random_real(0.15)*sign,
                                          random_real_range(2,3), random_real_range(4,6), random_real_range(20,40));
                        }
                    }

                    // Deflect bullets
                    for (auto &it : w.bullet_list)
                    {
                        fvec2 delta = boss.pos - it.pos;
                        if (delta.len() > shield_rad)
                            continue;
                        if (it.vel /dot/ delta < 0)
                            continue;
                        delta = delta.norm();
                        it.vel -= it.vel /dot/ delta * delta * 2;
                        Sounds::stopped_by_shield(boss.pos);
                    }
                }
            }

            { // Magic shield
                if (boss.magic_shield)
                {
                    // Visual effect
                    float c = random_real_range(0, 1);
                    w.AddParticle(fvec3(1, c, 0.5 + c/2), 0.3, 0.2, boss.pos + fvec2(random_real_range(2), random_real_range(2)), 0, 0, 0, random_real_range(75,80), 3);
                    w.AddLight(fvec3(1,0.8,0.3)*2, boss.pos, 0, 0, 0, random_real_range(160,180), 2);

                    if (!boss.magic_shield_broken)
                    {
                        // Kill player and push him
                        fvec2 delta = w.p.pos - boss.pos;
                        if (delta.len() < shield_rad)
                        {
                            KillPlayer();

                            delta = delta.norm();
                            fvec2 new_pos = boss.pos + (delta * (shield_rad + 1));
                            if (!w.Solid(iround(new_pos), plr_hitbox))
                                w.p.pos = new_pos;
                            Sounds::stopped_by_shield(boss.pos);
                            for (int i = 0; i < 2; i++)
                            {
                                float c = random_real_range(0.4,0.9);
                                int sign = random_sign();
                                w.AddParticle(fvec3(c), 0.5, 0.2, w.p.pos - delta*5 + fvec2(random_real_range(2), random_real_range(2)), delta.angle() - f_pi/2*sign, random_real(0.15)*sign,
                                              random_real_range(2,3), random_real_range(4,6), random_real_range(20,40));
                            }
                        }

                        // Deflect bullets
                        for (auto &it : w.bullet_list)
                        {
                            fvec2 delta = boss.pos - it.pos;
                            if (delta.len() > shield_rad)
                                continue;
                            if (it.vel /dot/ delta < 0)
                                continue;
                            delta = delta.norm();
                            it.vel -= it.vel /dot/ delta * delta * 2;
                            Sounds::stopped_by_shield(boss.pos);
                        }
                    }
                }

                // Visual effect for orbs
                for (const auto &it : boss.magic_orb_list)
                {
                    float c = random_real_range(0, 1);
                    w.AddParticle(fvec3(1, c, 0.5 + c/2), 0.3, 0.2, it.pos + fvec2(random_real_range(2), random_real_range(2)), 0, 0, 0, random_real_range(60,65), 3);
                    w.AddLight(fvec3(1,0.8,0.3)*2, it.pos, 0, 0, 0, random_real_range(45,65), 2);
                }
            }

            { // Invincibility frames
                if (boss.invin)
                    boss.invin--;
            }

            boss.phase_time++;
        }

        { // Dark force
            if (w.enable_light)
            {
                fvec2 delta = w.p.Center() - w.BossHome();
                float dist = delta.len();
                bool out = dist > w.cur_light_rad;
                if (boss.dead)
                    out = 0;
                if (out && w.dark_force_timer < 0.01)
                    KillPlayer();
                float angle = delta.angle();

                // Update position
                clamp_var(w.dark_force_timer += dark_force_step * (out ? -1 : 1));

                // Add prticles
                if (w.dark_force_timer < 1)
                {
                    float t = w.dark_force_timer;
                    float d = max(dist, w.cur_light_rad);

                    float max_a_offset = dark_force_max_dist / w.cur_light_rad;
                    if (max_a_offset > f_pi)
                        max_a_offset = f_pi;

                    for (int s = -1; s <= 1; s += 2)
                    {
                        float a = angle + s*t*max_a_offset;
                        fvec2 pos = w.dark_force_pos[s != -1] = w.BossHome() + fvec2::dir(a, d);
                        for (int i = 0; i < 5; i++)
                        {
                            w.AddParticle(fvec3(0), 1-t, 1, pos + fvec2::dir(a, random_real_range(8)) + fvec2::dir(a+f_pi/2, random_real_range(8)),
                                          random_real_range(f_pi), random_real_range(0.3), random_real_range(0,0.5), random_real_range(5,15), random_int_range(20,40));
                        }
                        w.AddLight(fvec3(1-t,0,0), pos, 0, 0, 0, random_real_range(100,120), 2);
                    }
                }
            }
        }

        { // Bullets
            auto it = w.bullet_list.begin();
            while (it != w.bullet_list.end())
            {
                it->pos += it->vel;

                // Change direction if homing
                if (it->Homing() > 0 && !w.p.dead)
                {
                    float speed = it->vel.len();
                    float angle = it->vel.angle();
                    fvec2 target_dir = (w.p.Center() - it->pos).norm();
                    fvec2 dir = it->vel.norm();
                    float angle_delta = acos(dir /dot/ target_dir) * sign(dir /cross/ target_dir);
                    if (abs(angle_delta) < it->Homing())
                        angle = target_dir.angle();
                    else
                        angle += sign(angle_delta) * it->Homing();
                    it->vel = fvec2::dir(angle, speed);
                }

                // Hit terrain
                if (w.Solid(iround(it->pos), bullet_hitbox))
                {
                    it->DeathEffect(w);
                    it = w.bullet_list.erase(it);
                    continue;
                }

                // Kill player
                if (!w.p.dead && it->Enemy() && (it->pos - w.p.Center()).len() < plr_hitbox_rad + it->CollisionRadius())
                {
                    it->DeathEffect(w);
                    it = w.bullet_list.erase(it);
                    KillPlayer();
                    continue;
                }

                // Kill enemies
                if (!it->Enemy())
                {
                    bool hit = 0;

                    { // Crystals
                        auto enemy = boss.crystal_list.begin();
                        while (enemy != boss.crystal_list.end())
                        {
                            if ((enemy->pos - it->pos).len() < 14)
                            {
                                hit = 1;
                                boss.crystals_wait = 0;
                                w.enable_light = 1;

                                if (enemy->invin == 0)
                                {
                                    enemy->invin = enemy_invin_frames;
                                    enemy->hp--;

                                    if (enemy->hp > 0)
                                    {
                                        Sounds::boss_hit(enemy->pos);
                                    }
                                    else
                                    {
                                        Sounds::boss_big_hit(enemy->pos);
                                        for (int i = 0; i < 16; i++)
                                        {
                                            fvec3 c(1, random_real_range(0.2,1), 0);
                                            w.AddParticle(c, 1, 0.5, enemy->pos + fvec2(random_real_range(1), random_real_range(1)), random_real_range(f_pi), random_real_range(0.2),
                                                          random_real_range(1,3), random_real_range(18,24), random_int_range(10,35));
                                        }
                                        boss.crystal_list.erase(enemy);
                                    }
                                }

                                break;
                            }
                            enemy++;
                        }
                    }

                    // Orbs
                    if (!hit)
                    {
                        auto enemy = boss.magic_orb_list.begin();
                        while (enemy != boss.magic_orb_list.end())
                        {
                            if ((enemy->pos - it->pos).len() < 14)
                            {
                                hit = 1;

                                if (enemy->invin == 0)
                                {
                                    enemy->invin = enemy_invin_frames;
                                    enemy->hp--;

                                    if (enemy->hp > 0)
                                    {
                                        Sounds::boss_hit(enemy->pos, 4);
                                    }
                                    else
                                    {
                                        if (boss.magic_orb_list.size() > 1) // The condition is here because otherwise this sound would interfere with some boss sound.
                                            Sounds::boss_big_hit(enemy->pos);

                                        for (int i = 0; i < 16; i++)
                                        {
                                            float c = random_real_range(0,1);
                                            fvec3 color(1, 0.6+c*0.4, 0.2+c*0.8);
                                            w.AddParticle(color, 1, 0.5, enemy->pos + fvec2(random_real_range(1), random_real_range(1)), random_real_range(f_pi), random_real_range(0.2),
                                                          random_real_range(1,3), random_real_range(18,24), random_int_range(10,35));
                                        }
                                        boss.magic_orb_list.erase(enemy);
                                    }
                                }

                                break;
                            }
                            enemy++;
                        }
                    }

                    // Boss
                    if (!hit)
                    {
                        if ((it->pos - boss.pos).len() < boss_hitbox_rad + it->CollisionRadius())
                        {
                            hit = 1;
                            if (!boss.safe_shield && boss.invin == 0)
                            {
                                boss.invin = enemy_invin_frames;
                                boss.hits_taken++;
                            }
                        }
                    }

                    if (hit)
                    {
                        it->DeathEffect(w);
                        it = w.bullet_list.erase(it);
                        continue;
                    }
                }

                it++;
            }
        }

        { // Camera
            fvec2 target = w.p.Center();
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

        { // Normal particles
            auto it = w.particle_list.begin();
            while (it != w.particle_list.end())
            {
                it->cur_life++;
                if (it->cur_life > it->life)
                {
                    it = w.particle_list.erase(it);
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

        { // Light circle
            // Change radius
            w.cur_light_rad -= (w.cur_light_rad - w.target_light_rad) * 0.01;

            // Particles
            if (w.enable_light)
            {
                float d = w.cur_light_rad;
                float f = max(0, d-32) / 90;
                float ra = random_real_range(f_pi);
                w.AddLight((fvec3(sin(ra), sin(ra+f_pi*2/3), sin(ra-f_pi*2/3))*0.5+0.5) * 0.96 + 0.05, w.BossHome(), random_real_range(f_pi), random_real_range(0.1), random_real_range(0,0.25+sqrt(f*6)), random_real_range(10, 100)*f+30, random_int_range(0, 40)*f+20);
            }
        }

        { // Respawn
            if (w.p.dead && w.p.death_timer > 20 && Interface::Button().AssignKey())
                w.p.respawning = 1;
        }

        { // Screen transition timer
            if (!w.p.respawning)
            {
                if (w.darkness > 0)
                {
                    w.darkness -= scr_transition_step;
                    if (w.darkness < 0)
                        w.darkness = 0;
                }
            }
            else
            {
                if (w.darkness < 1)
                {
                    w.darkness += scr_transition_step_fast;
                    if (w.darkness > 1)
                    {
                        w = saved_world;
                    }
                }
            }

            w.whiteness = w.whiteness * 0.95 - 0.001;
            if (w.whiteness < 0)
                w.whiteness = 0;
        }

        { // Cheats
            if (Interface::Button(Interface::Inputs::f1).pressed())
            {
                w.b.crystal_list.clear();
                w.b.magic_orb_list.clear();
                w.enable_light = 1;
            }
            if (Interface::Button(Interface::Inputs::f2).pressed())
            {
                w.b.hits_taken = 3;
            }
        }
    };

    auto Render = [&]
    {
        auto &boss = w.b;

        { // Map
            w.map.Render(w.cam_pos_i);
        }

        { // Player
            float alpha = max(0, 1 - w.p.death_timer / 10.);

            constexpr ivec2 size(32);
            // Alive
            Quad(iround(w.p.pos - size/2).sub_y(8) - w.cam_pos_i, size, Src4(ivec2(0,128).add_x(size.x * (w.p.anim_state * 4 + w.p.anim_frame)), size, alpha));

            // Dead
            if (w.p.dead)
                Quad(iround(w.p.pos - size/2).sub_y(8) - w.cam_pos_i, size, Src4(ivec2(0,128+32), size, 1-alpha));

            // Dash glow
            if (w.p.dash_len)
                Quad(ivec2(w.p.Center() - size/2) - w.cam_pos_i, size, Src4(ivec2(96,96), size, 1, 0.5));
        }

        { // Boss
            // Crystals
            for (const auto &it : boss.crystal_list)
            {
                constexpr ivec2 size(64);
                // Shadow
                Quad(it.pos - size/2 - w.cam_pos_i, size, Src4(ivec2(160,0), size));
                // Body
                ivec2 body_pos = it.pos.add_y(iround(sin(metronome.ticks % crystal_anim_period / float(crystal_anim_period) * 2 * f_pi) * crystal_anim_offset));
                Quad(body_pos - size/2 - w.cam_pos_i, size, Src4(ivec2(96,0), size));
                // Effect
                float sz = random_real_range(1,1.06);
                Quad(body_pos - size/2 - w.cam_pos_i + iround(fvec2(random_real_range(1.4), random_real_range(1.4))) - size*(sz-1)/2, size*sz, Src4(ivec2(96,0)+2, size-4, 0.5, 0.2));
            }

            { // Safe shield
                if (boss.safe_shield)
                {
                    constexpr ivec2 size(96);
                    float s = random_real_range(0.95,1.05);
                    Quad(iround(boss.pos) - size/2*s - w.cam_pos_i, size*s, Src4(ivec2(0,288)+1, size-2, 0.9, 0.2));
                }
            }

            { // Magic shield
                if (boss.magic_shield)
                {
                    constexpr ivec2 size(96);

                    if (!boss.magic_shield_broken)
                    {
                        float s = random_real_range(0.98,1.03);
                        Quad(iround(boss.pos) - size/2*s - w.cam_pos_i, size*s, Src4(ivec2(96,288)+1, size-2, 0.9, 0.2));
                    }

                    constexpr int period = 300;
                    fvec2 d = fvec2::dir(f_pi*4*sin(int(metronome.ticks % period) / float(period) * f_pi * 2), size.x/2);
                    Quad(iround(boss.pos) - d - d.rot90() - w.cam_pos_i, d*2, d.rot90()*2, Src4(ivec2(194,288)+2, size-4, 0.9, 0.2));
                }
            }

            { // Magic orbs
                for (const auto &it : boss.magic_orb_list)
                {
                    constexpr ivec2 size(32-2);
                    float s = random_real_range(0.94,1.06);
                    Quad(iround(it.pos) - size/2*s - w.cam_pos_i, size*s, Src4(ivec2(160+1,96+1), size, 0.9, 0.2));
                }
            }

            { // Dash target
                if (boss.phase == Boss::Phase::magic)
                {
                    constexpr ivec2 size(96);
                    float s = random_real_range(0.98,1.03);
                    Quad(iround(boss.mgc_target) - size/2*s - w.cam_pos_i, size*s, Src4(ivec2(0,384)+1, size-2, 0.9, 0.2));
                }
            }

            { // Laser
                if (boss.first_enable_laser)
                {
                    bool deadly = boss.first_laser_deadly;

                    float width = (deadly ? 8 : 1);

                    fvec2 dir = fvec2::dir(boss.first_laser_angle);
                    fvec2 n = dir.rot90();
                    float len = boss.first_laser_len;

                    fvec4 color = (deadly ? fvec4(1,0.5,0.2,1) : fvec4(1,0.5,0.5,1));
                    for (int i = 0; i < (deadly ? 3 : 1); i++)
                    {
                        Quad(boss.pos + dir * boss_laser_offset - n * width/2 - w.cam_pos, dir * (len - boss_laser_offset), n * width, Src4(color, deadly ? 0 : 0.5));
                        width -= 2;
                    }
                }
            }

            { // Body
                constexpr ivec2 size(64);
                constexpr int period = 180;
                float alpha = 1;
                if (boss.dead)
                    alpha = max(0, 1 - boss.death_timer / 60.);

                // Shadow
                Quad(iround(boss.pos).add_y(10) - size/2 - w.cam_pos_i, size, Src4(ivec2(160,0), size, alpha));

                // Body
                Quad(iround(boss.pos).add_y(iround(sin(float(metronome.ticks % period) / period * f_pi * 2)*2)) - size/2 - w.cam_pos_i, size, Src4(ivec2(0,224), size, alpha));
            }
        }

        { // Bullets
            for (const auto &bullet : w.bullet_list)
                bullet.Render(w.cam_pos_i);
        }

        { // Particles
            for (const auto &it : w.particle_list)
            {
                constexpr int size = 64, m = 4;
                float s = (1 - it.cur_life / float(it.life));
                float sz = s * it.size;
                Quad(it.pos - w.cam_pos - sz/2, fvec2(sz), Src4(0, it.color, ivec2(224,0)+m, ivec2(size-m*2), it.alpha, it.beta));
            }
        }

        { // Dark force
            if (w.enable_light && w.dark_force_timer < 1)
            {
                constexpr ivec2 size(32);
                float alpha = clamp((1-w.dark_force_timer) * 2);
                for (int i = 0; i < 2; i++)
                    Quad(w.dark_force_pos[i] - size/2 - w.cam_pos_i, size, Src4(ivec2(128,96), size, alpha, 1));
            }
        }

        { // Death GUI
            if (w.p.dead)
            {
                float alpha = clamp((w.p.death_timer-60) / 10.);
                constexpr ivec2 size(176,32);

                // "You have failed"
                Quad(-size/2 + ivec2(0,-50), size, Src4(ivec2(0, 192), size, alpha));

                // A funny message
                ivec2 msg_pos(0, 40);
                std::string msg = death_messages[w.p.death_msg_index];
                for (int i = 0; i < 4; i++)
                {
                    Text<0>(msg_pos + ivec2(1,0).rot90(i), msg, fvec3(155,173,183)/255, alpha);
                }
                Text<0>(msg_pos, msg, fvec3(0), alpha);
            }
        }

        { // End of game GUI
            if (boss.dead)
            {
                const std::vector<ivec2> offsets = {ivec2(0,-1), ivec2(0, 1), ivec2(1,0), ivec2(-1,0), ivec2(0,0)};
                for (const auto &offset : offsets)
                {
                    fvec3 color = fvec3(155,173,183)/255;
                    if (offset == ivec2(0))
                        color = fvec3(0);

                    Text<0>(w.BossHome().sub_y(96-24*0) + offset - w.cam_pos_i, "This is it", color, clamp(boss.death_timer/60. - 5));
                    Text<0>(w.BossHome().sub_y(96-24*1) + offset - w.cam_pos_i, "Your mission is over", color, clamp(boss.death_timer/60. - 7));
                    Text<0>(w.BossHome().sub_y(96-24*6) + offset - w.cam_pos_i, "Thanks for playing my game!", color, clamp(boss.death_timer/60. - 12));
                    Text<0>(w.BossHome().sub_y(96-24*7) + offset - w.cam_pos_i, Str("You died ", death_counter, death_counter == 1 ? " time" : " times"), color, clamp(boss.death_timer/60. - 14));
                }
            }
        }

        { // Tutorial GUI
            ivec2 spawn = w.map.SpawnTile() * tile_size + tile_size/2;

            auto Message = [&](bool revisitable, int y, int off, std::string text)
            {
                float alpha = smoothstep(clamp(2.5 - abs(spawn.y - (revisitable ? w.p.pos.y : min_y) - y) / 24.));

                const std::vector<ivec2> offsets = {ivec2(0,-1), ivec2(0, 1), ivec2(1,0), ivec2(-1,0), ivec2(0,0)};
                for (const auto &offset : offsets)
                {
                    fvec3 color = fvec3(155,173,183)/255;
                    if (offset == ivec2(0))
                        color = fvec3(0);

                    Text<0>(spawn.sub_y(y + off) + offset - w.cam_pos_i, text, color, alpha);
                }
            };

            Message(1, 0, -4, Str("Use arrows to move\n",
                             w.button_fire.Name(), " to shoot\n",
                             w.button_dash.Name(), " to dash"));
            Message(1, 116, -40, "Dashing makes you invulnerable\n"
                                 "and allows you to destroy projectiles");

            Message(0, 354, 96, "We found him at last");
            Message(0, 414, 96, "The last witch-knight...");
        }

        { // Screen transition
            if (w.darkness > 0)
                Quad(-screen_sz/2, screen_sz, Src4(fvec4(0,0,0,smoothstep(w.darkness))));

            if (w.whiteness > 0)
                Quad(-screen_sz/2, screen_sz, Src4(fvec4(1,1,1,smoothstep(w.whiteness) * 0.5)));
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
        // Bullets
        for (const auto &bullet : w.bullet_list)
            Draw::Light(bullet.pos - w.cam_pos, bullet.LightSize(), bullet.LightColor());

        // Light particles
        for (const auto &li : w.light_list)
            Draw::Light(li.pos - w.cam_pos, li.size * (1 - li.cur_life / float(li.life)), li.color);
    };

    Sounds::Init();
    Draw::Init();
    Draw::Resize();

    uint64_t frame_start = Clock::Time();

    // Without this line window resize breaks. Ugh.
    w.AddLight(fvec3(0), fvec2(0), 0, 0, 0, 0, 1);

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
        if (w.enable_light)
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
