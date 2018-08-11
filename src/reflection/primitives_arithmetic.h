#ifndef REFLECTION_PRIMITIVES_ARITHMETIC_H_INCLUDED
#define REFLECTION_PRIMITIVES_ARITHMETIC_H_INCLUDED

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

#include "interface.h"

namespace Refl::Custom
{
    template <typename T> struct Primitive<T, std::enable_if_t<std::is_arithmetic_v<T>>>
    {
        static constexpr const char *name = []{
            if constexpr (std::is_same_v<T, bool              >) return "bool";
            if constexpr (std::is_same_v<T, char              >) return "byte";
            if constexpr (std::is_same_v<T, signed char       >) return "byte";
            if constexpr (std::is_same_v<T, unsigned char     >) return "ubyte";
            if constexpr (std::is_same_v<T, short             >) return "short";
            if constexpr (std::is_same_v<T, unsigned short    >) return "ushort";
            if constexpr (std::is_same_v<T, int               >) return "int";
            if constexpr (std::is_same_v<T, unsigned int      >) return "uint";
            if constexpr (std::is_same_v<T, long              >) return "long";
            if constexpr (std::is_same_v<T, unsigned long     >) return "ulong";
            if constexpr (std::is_same_v<T, long long         >) return "llong";
            if constexpr (std::is_same_v<T, unsigned long long>) return "ullong";
            if constexpr (std::is_same_v<T, float             >) return "float";
            if constexpr (std::is_same_v<T, double            >) return "double";
            if constexpr (std::is_same_v<T, long double       >) return "ldouble";
            if constexpr (std::is_integral_v<T>      ) return "integer?";
            if constexpr (std::is_floating_point_v<T>) return "real?";
            return "??";
        }();

        static std::string to_string(const T &object)
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                return object ? "true" : "false";
            }
            if constexpr (std::is_integral_v<T>)
            {
                return std::to_string(object);
            }
            else
            {
                constexpr bool known_type = std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, long double>;
                using use_type = std::conditional_t<known_type, T, long double>;
                constexpr const char *format = std::is_same_v<T, float>  ? "%.*g"  :
                                               std::is_same_v<T, double> ? "%.*lg" :
                                                /*long double or unknown*/ "%.*Lg";
                constexpr int len = std::numeric_limits<use_type>::max_digits10;
                char buffer[len*2];
                std::snprintf(buffer, sizeof buffer, format, len, use_type(object));
                return buffer;
            }
        }

        static bool from_string(T &object, const char *&string)
        {
            char *end;

            if constexpr (std::is_integral_v<T>)
            {
                if constexpr (std::is_same_v<T, bool>)
                {
                    if (!std::strncmp(string, "false", 5))
                    {
                        object = false;
                        string += 5;
                        return 1;
                    }
                    if (!std::strncmp(string, "true", 4))
                    {
                        object = true;
                        string += 4;
                        return 1;
                    }
                }

                using result_t = std::conditional_t<sizeof(T) < sizeof(long), std::conditional_t<std::is_signed_v<T>, long, unsigned long>, T>;
                result_t result;

                constexpr bool is_long = sizeof(T) <= sizeof(long);
                constexpr bool is_signed = std::is_signed_v<T>;

                     if constexpr ( is_long &&  is_signed) result = std::strtol(string, &end, 0);
                else if constexpr ( is_long && !is_signed) result = std::strtoul(string, &end, 0);
                else if constexpr (!is_long &&  is_signed) result = std::strtoll(string, &end, 0);
                else            /* !is_long && !is_signed*/result = std::strtoull(string, &end, 0);

                if (end == string)
                    return 0;

                if constexpr (sizeof(T) < sizeof(result_t))
                    if (result < std::numeric_limits<T>::min() || result > std::numeric_limits<T>::max())
                        return 0;

                object = result;
                string = end;
                return 1;
            }
            else
            {
                     if constexpr (std::is_same_v<T, float      >) object = std::strtof(string, &end);
                else if constexpr (std::is_same_v<T, double     >) object = std::strtod(string, &end);
                else            /* std::is_same_v<T, long double>*/object = std::strtold(string, &end);

                if (end == string)
                    return 0;

                string = end;
                return 1;
            }
        }
    };
}



#endif
