#include "master.h"

#include <iomanip>
#include <iostream>
#include <vector>

#define main SDL_main

Program::Parachute error_parachute;

constexpr ivec2 screen_sz = ivec2(1920,1080)/4;
Interface::Window win("Alpha", screen_sz*2, Interface::Window::windowed, Interface::Window::Settings{}.MinSize(screen_sz));
Metronome metronome;
Interface::Mouse mouse;

constexpr ivec2 tile_size = ivec2(16,16);

namespace Draw
{
    Graphics::Texture texture_main;
    Graphics::TextureUnit texture_unit_main = Graphics::TextureUnit(texture_main).Interpolation(Graphics::linear).SetData(Graphics::Image("assets/texture.png"));

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
//        std::cout << Refl::Interface(array).to_string() << '\n';
//        Program::Exit();
            array.clear();
        }

        void Push(fvec2 pos, fvec4 color, fvec2 texcoord, fvec3 factors)
        {
            if (array.size() >= size)
                Flush();
            array.push_back({pos, color, texcoord, factors});
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

    constexpr fmat4 view_mat = fmat4::ortho(screen_sz / ivec2(-2,2), screen_sz / ivec2(2,-2), -1, 1);


    void Init()
    {
        ShaderMain::uniforms.matrix = view_mat;
        ShaderMain::uniforms.tex_size = texture_main.Size();
        ShaderMain::uniforms.texture = Draw::texture_unit_main;
        ShaderMain::uniforms.color_matrix = fmat4();

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
        (optional)(ivec2)(spawn_tile)(=ivec2(0)),
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
            b_set_spawn = Btn(Inp::_1);

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

            // Set spawn
            if (editor.b_set_spawn.pressed())
                spawn_tile = editor.cursor;

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
    fvec2 cam_pos_i = fvec2(0);

    Map map;

    void LoadMap(std::string name)
    {
        map = Map::FromFile("assets/" + name);
        p.pos = map.SpawnTile() * tile_size + tile_size/2;
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
    constexpr float plr_vel_step = 0.5, plr_vel_cap = 2;
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
            ivec2 dir = ivec2(w.button_right.down() - w.button_left.down(), w.button_down.down() - w.button_up.down());

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

            if (!w.Solid(iround(w.p.pos.add_x(w.p.vel.x)), plr_hitbox) || w.map.enable_editor)
                w.p.pos.x += w.p.vel.x;
            if (!w.Solid(iround(w.p.pos.add_y(w.p.vel.y)), plr_hitbox) || w.map.enable_editor)
                w.p.pos.y += w.p.vel.y;
        }

        { // Camera
            w.cam_pos = w.p.pos;
            w.cam_pos_i = iround(w.cam_pos);
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
        Draw::Queue::Flush();

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
        Draw::FullscreenQuad(Draw::scale_factor * screen_sz / fvec2(win.Size()));

        win.SwapBuffers();
    }

    return 0;
}
