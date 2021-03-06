#ifndef INTERFACE_INPUT_H_INCLUDED
#define INTERFACE_INPUT_H_INCLUDED

#include <initializer_list>
#include <vector>

#include "utils/mat.h"
#include "window.h"

namespace Interface
{
    class Button
    {
        Inputs::Enum index = Inputs::None;

        mutable uint64_t update_time = 0;
        mutable bool is_pressed = 0, is_released = 0, is_down = 0, is_repeated = 0;

        void Update() const
        {
            auto &window = Window::Instance();

            uint64_t tick = window.Ticks();
            if (update_time == tick)
                return;
            update_time = tick;

            auto times = window.InputTimes(index);

            is_pressed = times.press == tick;
            is_released = times.release == tick;
            is_down = times.press > times.release;
            is_repeated = times.repeat == tick;
        }

        bool Assign(Inputs::Enum begin, Inputs::Enum end)
        {
            auto &window = Window::Instance();
            uint64_t tick = window.Ticks();

            for (auto i = begin; i < end; i = Inputs::Enum(i+1))
            {
                if (window.InputTimes(i).press == tick)
                {
                    index = i;
                    return 1;
                }
            }

            return 0;
        }

      public:
        Button() {}
        Button(Inputs::Enum index) : index(index) {}

        [[nodiscard]] bool pressed () const {Update(); return is_pressed;}
        [[nodiscard]] bool released() const {Update(); return is_released;}
        [[nodiscard]] bool repeated() const {Update(); return is_repeated;}
        [[nodiscard]] bool down    () const {Update(); return is_down;}
        [[nodiscard]] bool up      () const {Update(); return !is_down;}

        [[nodiscard]] explicit operator bool() const {return index != Inputs::None;}

        [[nodiscard]] Inputs::Enum Index() const
        {
            return index;
        }

        [[nodiscard]] std::string Name() const // Returns a layout-dependent name, which
        {
            if (index == Inputs::None)
            {
                return "None";
            }
            else if (index >= Inputs::BeginKeys && index < Inputs::EndKeys)
            {
                const char *ret;

                if (SDL_Keycode keycode = SDL_GetKeyFromScancode(SDL_Scancode(index)))
                    ret = SDL_GetKeyName(keycode);
                else
                    ret = SDL_GetScancodeName(SDL_Scancode(index));

                if (*ret) // We don't need to check for null pointers, since above functions never return those.
                    return ret;
                else
                    return "Unknown " + std::to_string(index);
            }
            else if (index >= Inputs::BeginMouseButtons && index < Inputs::EndMouseButtons)
            {
                switch (index)
                {
                  case Inputs::mouse_left:
                    return "Left Mouse Button";
                  case Inputs::mouse_middle:
                    return "Middle Mouse Button";
                  case Inputs::mouse_right:
                    return "Right Mouse Button";
                  case Inputs::mouse_x1:
                    return "X1 Mouse Button";
                  case Inputs::mouse_x2:
                    return "X2 Mouse Button";
                  default:
                    return "Mouse Button " + std::to_string(index - Inputs::mouse_left + 1);
                }
            }
            else
            {
                switch (index)
                {
                  case Inputs::mouse_wheel_up:
                    return "Mouse Wheel Up";
                  case Inputs::mouse_wheel_down:
                    return "Mouse Wheel Down";
                  case Inputs::mouse_wheel_left:
                    return "Mouse Wheel Left";
                  case Inputs::mouse_wheel_right:
                    return "Mouse Wheel Right";
                  default:
                    return "Invalid " + std::to_string(index);
                }
            }
        }

        bool AssignKey()
        {
            return Assign(Inputs::BeginKeys, Inputs::EndKeys);
        }
        bool AssignMouseButton()
        {
            return Assign(Inputs::BeginMouseButtons, Inputs::EndMouseButtons);
        }
        bool AssignMouseWheel()
        {
            return Assign(Inputs::BeginMouseWheel, Inputs::EndMouseWheel);
        }
    };

    struct Mouse
    {
        fmat3 matrix = fmat3();

        ivec2 pos() const
        {
            return iround((matrix * Window::Instance().MousePos().to_vec3(1)).to_vec2());
        }
        ivec2 pos_delta() const
        {
            return iround(matrix.to_mat2() * Window::Instance().MousePosDelta());
        }

        Button left   = Button(Inputs::mouse_left);
        Button middle = Button(Inputs::mouse_middle);
        Button right  = Button(Inputs::mouse_right);
        Button x1     = Button(Inputs::mouse_x1);
        Button x2     = Button(Inputs::mouse_x2);


        void HideCursor(bool hide = 1)
        {
            Window::Instance().HideCursor(hide);
        }

        void RelativeMouseMode(bool relative = 1)
        {
            Window::Instance().RelativeMouseMode(relative);
        }
    };
}

#endif
