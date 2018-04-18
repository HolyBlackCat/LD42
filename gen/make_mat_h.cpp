#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <sstream>
#include <type_traits>

#define VERSION "3.0.0 dev"

namespace data
{
    const struct {std::string tag, name;} type_list[]
    {
        "b",   "bool",
        "c",   "char",
        "uc",  "unsigned char",
        "sc",  "signed char",
        "s",   "short",
        "us",  "unsigned short",
        "i",   "int",
        "u",   "unsigned int",
        "l",   "long",
        "ul",  "unsigned long",
        "ll",  "long long",
        "ull", "unsigned long long",
        "f",   "float",
        "d",   "double",
        "ld",  "long double",
        "i8",  "int8_t",
        "u8",  "uint8_t",
        "i16", "int16_t",
        "u16", "uint16_t",
        "i32", "int32_t",
        "u32", "uint32_t",
        "i64", "int64_t",
        "u64", "uint64_t",
    };
    constexpr int type_list_len = std::extent_v<decltype(type_list)>;

    const std::string fields[4] {"x","y","z","w"};
    constexpr int fields_alt_count = 2;
    const std::string fields_alt[fields_alt_count][4]
    {
        fields[0], fields[1], fields[2], fields[3],
        "r","g","b","a",
        // "s","t","p","q", // Who uses this anyway.
    };
}

namespace impl
{
    std::ofstream output_file("mat.h");

    std::stringstream ss;
    const std::stringstream::fmtflags stdfmt = ss.flags();

    bool at_line_start = 1;
    int indentation = 0;
    int section_depth = 0;

    constexpr const char *indentation_string = "    ";
}

template <typename ...P> [[nodiscard]] std::string make_str(const P &... params)
{
    impl::ss.clear();
    impl::ss.str("");
    impl::ss.flags(impl::stdfmt);
    (impl::ss << ... << params);
    return impl::ss.str();
}

void output_str(const std::string &str)
{
    for (const char *ptr = str.c_str(); *ptr; ptr++)
    {
        char ch = *ptr;

        if ((ch == '}' || ch == ')') && impl::indentation > 0)
            impl::indentation--;

        if (impl::at_line_start)
        {
            if (std::strchr(" \t\r", ch))
                continue;

            for (int i = 0; i < impl::indentation; i++)
                impl::output_file << impl::indentation_string;
            impl::at_line_start = 0;
        }

        impl::output_file.put(ch == '$' ? ' ' : ch);

        if (ch == '{' || ch == '(')
            impl::indentation++;

        if (ch == '\n')
            impl::at_line_start = 1;
    }
}

template <typename ...P> void output(const P &... params)
{
    output_str(make_str(params...));
}

void section(std::string header, std::function<void()> func)
{
    output(header, "\n{\n");
    func();
    output("}\n");
}
void section_sc(std::string header, std::function<void()> func) // 'sc' stands for 'end with semicolon'
{
    output(header, "\n{\n");
    func();
    output("};\n");
}

void decorative_section(std::string name, std::function<void()> func)
{
    output("//{", std::string(impl::section_depth+1, ' '), name, "\n");
    impl::indentation--;
    impl::section_depth++;
    func();
    impl::section_depth--;
    output("//}", std::string(impl::section_depth+1, ' '), name, "\n");
    impl::indentation++;
}

void next_line()
{
    output("\n");
}

int main()
{
    if (!impl::output_file)
        return -1;

    { // Header
        output(1+R"(
            // mat.h
            // Vector and matrix math
            // Version )", VERSION, R"(
            // Autogenerated, don't touch.
        )");
        next_line();
    }

    { // Includes
        output(1+R"(
            #include <algorithm>
            #include <cmath>
            #include <cstddef>
            #include <cstdint>
            #include <istream>
            #include <ostream>
            #include <type_traits>
        )");
        next_line();
    }

    section("namespace Math", []
    {
        section("inline namespace Vector // Declarations", []
        {
            { // Main templates
                output(1+R"(
                    template <int D, typename T> struct vec;
                    template <int W, int H, typename T> using mat = vec<W, vec<H, T>>;
                )");
            }

            { // Type-generic
                // Vectors of specific size
                for (int i = 2; i <= 4; i++)
                    output(" template <typename T> using vec", i, " = vec<", i, ",T>;");
                next_line();

                // Matrices of specific size
                for (int h = 2; h <= 4; h++)
                {
                    for (int w = 2; w <= 4; w++)
                        output(" template <typename T> using mat", w, "x", h, " = mat<", w, ",", h, ",T>;");
                    next_line();
                }

                // Square matrices of specific size
                for (int i = 2; i <= 4; i++)
                    output(" template <typename T> using mat", i, " = mat", i, "x", i, "<T>;");
                next_line();
            }
            next_line();

            { // For specific types
                for (int i = 0; i < data::type_list_len; i++)
                {
                    const auto &type = data::type_list[i];

                    // Any size
                    output("template <int D> using ", type.tag, "vec = vec<D,", type.name, ">;\n"
                           "template <int W, int H> using ", type.tag, "mat = mat<W,H,", type.name, ">;\n");

                    // Fixed size
                    for (int d = 2; d <= 4; d++)
                        output(" using ", type.tag, "vec", d, " = vec<", d, ',', type.name, ">;");
                    next_line();
                    for (int h = 2; h <= 4; h++)
                    {
                        for (int w = 2; w <= 4; w++)
                            output(" using ", type.tag, "mat", w, "x", h, " = mat<", w, ",", h, ",", type.name, ">;");
                        next_line();
                    }
                    for (int i = 2; i <= 4; i++)
                        output(" using ", type.tag, "mat", i, " = ", type.tag, "mat", i, "x", i, ";");
                    next_line();

                    if (i != data::type_list_len-1)
                        next_line();
                }
            }
        });

        next_line();

        section("inline namespace Utility", []
        {
            output(1+R"(
                template <typename T> struct properties
                {
                    static constexpr bool
                    $   is_scalar     = 1,
                    $   is_vec        = 0,
                    $   is_mat        = 0,
                    $   is_vec_or_mat = 0;
                };
                template <int D, typename T> struct properties<vec<D,T>>
                {
                    static constexpr bool
                    $   is_scalar     = 0,
                    $   is_vec        = 1,
                    $   is_mat        = 0,
                    $   is_vec_or_mat = 1;
                };
                template <int W, int H, typename T> struct properties<vec<W,vec<H,T>>>
                {
                    static constexpr bool
                    $   is_scalar     = 0,
                    $   is_vec        = 0,
                    $   is_mat        = 1,
                    $   is_vec_or_mat = 1;
                };

                template <typename A, typename B> inline constexpr bool same_size_v = properties<A>::w == properties<B>::w && properties<A>::h == properties<B>::h;

                template <typename T> struct base_impl {using type = T;};
                template <int D, typename T> struct base_impl<vec<D,T>> {using type = typename base_impl<T>::type;};
                template <typename T> using base_t = typename base_impl<T>::type;

                template <typename T, typename TT> struct change_base_impl {using type = base_t<TT>;};
                template <int D, typename T, typename TT> struct change_base_impl<vec<D,T>,TT> {using type = vec<D, typename change_base_impl<T, TT>::type>;};
                template <typename T, typename TT> using change_base_t = typename change_base_impl<T,TT>::type;

                template <typename A, typename B> inline constexpr bool same_size_v = std::is_same_v<A, change_base_t<B,A>>;

                template <typename T> struct floating_point_impl {using type = std::conditional_t<std::is_floating_point_v<base_t<T>>, T, change_base_t<T, double>>;};
                template <typename T> using floating_point_t = typename floating_point_impl<T>::type;

                template <typename T> struct type_weight
                {
                    static constexpr std::size_t
                    $   high = std::is_floating_point_v<base_t<T>> * 0x100,
                    $   mid  = sizeof(base_t<T>),
                    $   low  = 0x100;
                };

                template <typename A, typename B> inline constexpr int compare_weight_v =
                $   type_weight<A>::high < type_weight<B>::high ? -1 :
                $   type_weight<A>::high > type_weight<B>::high ?  1 :
                $   type_weight<A>::mid  < type_weight<B>::mid  ? -1 :
                $   type_weight<A>::mid  > type_weight<B>::mid  ?  1 :
                $   type_weight<A>::low  < type_weight<B>::low  ? -1 :
                $   type_weight<A>::low  > type_weight<B>::low  ?  1 : 0;

                template <typename ...P> struct larger_impl {using type = void;};
                template <typename T> struct larger_impl<T> {using type = T;};
                template <typename T, typename ...P> struct larger_impl<T,P...> {using type = typename larger_impl<T, typename larger_impl<P...>::type>::type;};
                template <typename A, typename B> struct larger_impl<A,B> {using type = std::conditional_t<compare_weight_v<A,B> == 0, std::conditional_t<(compare_weight_v<A,B> > 0), A, B>, std::conditional_t<std::is_same_v<A,B>, A, void>>;};
                template <int D, typename A, typename B> struct larger_impl<vec<D,A>,B> {using type = change_base_t<vec<D,A>, typename larger_impl<A,B>::type>;};
                template <int D, typename A, typename B> struct larger_impl<B,vec<D,A>> {using type = change_base_t<vec<D,A>, typename larger_impl<A,B>::type>;};
                template <int DA, int DB, typename A, typename B> struct larger_impl<vec<DA,A>,vec<DB,B>>
                {using type = std::conditional_t<same_size_v<vec<DA,A>,vec<DB,B>>, change_base_t<vec<DA,A>, typename larger_impl<A,B>::type>, void>;};

                // Void on failure
                template <typename ...P> using opt_larger_t = typename larger_impl<P...>::type; // void on failure

                template <typename ...P> inline constexpr bool have_larger_type_v = !std::is_void_v<opt_larger_t<P...>>;

                // Soft error on failure
                template <typename ...P> using soft_larger_t = std::enable_if_t<have_larger_type_v<P...>, opt_larger_t<P...>>;

                template <typename ...P> struct hard_larger_impl
                {
                    static_assert(have_larger_type_v<P...>, "Can't determine larger type.");
                    using type = opt_larger_t<P...>;
                };

                // Hard error on failure
                template <typename ...P> using larger_t = typename hard_larger_impl<P...>::type;

                template <typename Structure, typename T> [[nodiscard]] auto vec_from_scalar(const T &value)
                {
                    static_assert(properties<T>::is_scalar, "Must be a scalar.");
                    return change_base_t<std::remove_cv_t<std::remove_reference_t<Structure>>, T>(value);
                }

                template <typename A, typename B = void> using if_scalar_t = std::enable_if_t<properties<A>::is_scalar, B>;
            )");
        });

        next_line();

        section("inline namespace Vector", []
        {
            auto Make = [&](int w, int h)
            {
                bool is_vector = (h == 1),
                     is_matrix = !is_vector;

                auto LargeFields = [&](std::string fold_op, std::string pre = "", std::string post = "") -> std::string
                {
                    std::string ret;
                    for (int i = 0; i < w; i++)
                    {
                        if (i != 0)
                            ret += fold_op;
                        ret += pre + data::fields[i] + post;
                    }
                    return ret;
                };
                auto SmallFields = [&](std::string fold_op, std::string pre = "", std::string post = "", std::string mid = ".") -> std::string
                {
                    if (is_vector)
                        return LargeFields(fold_op, pre, post);
                    std::string ret;
                    for (int x = 0; x < w; x++)
                    for (int y = 0; y < h; y++)
                    {
                        if (x != 0 || y != 0)
                            ret += fold_op;
                        ret += pre + data::fields[x] + mid + data::fields[y] + post;
                    }
                    return ret;
                };
                auto SmallFields_alt = [&](std::string fold_op, std::string pre = "", std::string post = "", std::string mid = ".") -> std::string
                {
                    if (is_vector)
                        return LargeFields(fold_op, pre, post);
                    std::string ret;
                    for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                    {
                        if (x != 0 || y != 0)
                            ret += fold_op;
                        ret += pre + data::fields[x] + mid + data::fields[y] + post;
                    }
                    return ret;
                };

                std::string typeless_name, size_name;
                if (is_vector)
                {
                    typeless_name = make_str("vec", w);
                    size_name = "size";
                }
                else
                {
                    typeless_name = make_str("mat", w, "x", h);
                    size_name = "width";
                }


                { // Static assertions
                    output("static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>, \"The base type must have no cv-qualifiers.\");\n");
                    output("static_assert(!std::is_reference_v<T>, \"The base type must not be a reference.\");\n");
                }

                { // Dimensions
                    if (is_vector)
                        output("static constexpr int size = ", w, ";\n");
                    else
                    {
                        output("static constexpr int width = ", w, ", height = ", h, ";\n");
                        if (w == h)
                            output("static constexpr int size = ", w, ";\n");
                    }
                }

                { // Aliases
                    output("using type = T;\n");
                    if (is_vector)
                        output("using member_type = T;\n");
                    else
                        output("using member_type = vec", h, "<T>;\n");
                }

                { // Members
                    for (int i = 0; i < w; i++)
                    {
                        output("union {member_type ");
                        for (int j = 0; j < data::fields_alt_count; j++)
                        {
                            if (j != 0)
                                output(", ");
                            output(data::fields_alt[j][i]);
                        }
                        output(";};\n");
                    }
                }

                { // Constructors
                    // Default
                    output("constexpr vec() = default;\n");

                    // Fill with a single value
                    output("explicit constexpr vec(member_type obj) : ", LargeFields(", ", "", "(obj)"), " {}\n");

                    // Element-wise
                    output("constexpr vec(", LargeFields(", ", "member_type "), ") : ");
                    for (int i = 0; i < w; i++)
                    {
                        if (i != 0)
                            output(", ");
                        output(data::fields[i], "(", data::fields[i], ")");
                    }
                    output(" {}\n");

                    // Matrix-specific constructors
                    if (is_matrix)
                    {
                        // Matrix fill with a single value
                        output("explicit constexpr vec(type obj) : ", LargeFields(", ", "", "(obj)"), " {}\n");

                        // Matrix element-wise
                        output("constexpr vec(", SmallFields_alt(", ", "type ", "", ""), ") : ");
                        for (int x = 0; x < w; x++)
                        {
                            if (x != 0)
                                output(", ");
                            output(data::fields[x], "(");
                            for (int y = 0; y < h; y++)
                            {
                                if (y != 0)
                                    output(",");
                                output(data::fields[x], data::fields[y]);
                            }
                            output(")");
                        }
                        output(" {}\n");
                    }

                    // Converting
                    output("template <typename TT> constexpr vec(const ", typeless_name, "<TT> &obj) : ");
                    for (int i = 0; i < w; i++)
                    {
                        if (i != 0)
                            output(", ");
                        output(data::fields[i], "(obj.", data::fields[i], ")");
                    }
                    output(" {}\n");
                }

                { // Convert to type
                    output("template <typename TT> [[nodiscard]] constexpr ", typeless_name, "<TT> to() const {return ", typeless_name, "<TT>(", SmallFields_alt(", ", "TT(", ")"), ");}\n");
                }

                { // Member access
                    /*
                    // Member pointers array
                    output("static constexpr member_type vec::*pointers[", size_name, "] {");
                    for (int i = 0; i < w; i++)
                    {
                        if (i != 0)
                            output(", ");
                        output("&vec::", data::fields[i]);
                    }
                    output("};\n");
                    */

                    // Operator []
                    output("[[nodiscard]] constexpr member_type &operator[](int i) {return *(member_type *)((char *)this + sizeof(member_type)*i);}\n");
                    output("[[nodiscard]] constexpr const member_type &operator[](int i) const {return *(member_type *)((char *)this + sizeof(member_type)*i);}\n");

                    // As array
                    if (is_vector)
                    {
                        output("[[nodiscard]] type *as_array() {return &x;};\n");
                        output("[[nodiscard]] const type *as_array() const {return &x;};\n");
                    }
                    else
                    {
                        output("[[nodiscard]] type *as_array() {return &x.x;};\n");
                        output("[[nodiscard]] const type *as_array() const {return &x.x;};\n");
                    }
                }

                { // Boolean
                    // Convert to bool
                    output("[[nodiscard]] explicit constexpr operator bool() const {return this->any(); static_assert(!std::is_same_v<type, bool>, \"Use .none(), .any(), or .all() for vectors/matrices of bool.\");}\n");

                    // None of
                    output("[[nodiscard]] constexpr bool none() const {return !this->any();}\n");

                    // Any of
                    output("[[nodiscard]] constexpr bool any() const {return ", SmallFields(" || "), ";}\n");

                    // All of
                    output("[[nodiscard]] constexpr bool all() const {return ", SmallFields(" && "), ";}\n");
                }

                { // Apply operators
                    if (is_vector)
                    {
                        // Sum
                        output("[[nodiscard]] constexpr auto sum() const {return ", LargeFields(" + "), ";}\n");

                        // Product
                        output("[[nodiscard]] constexpr auto prod() const {return ", LargeFields(" * "), ";}\n");

                        // Ratio
                        if (w == 2)
                            output("[[nodiscard]] constexpr auto ratio() const {return ", LargeFields(" / ","floating_point_t<member_type>(",")"), ";}\n");
                    }

                    // Min
                    output("[[nodiscard]] constexpr type min() const {return std::min({", SmallFields(","), "});}\n");
                    // Max
                    output("[[nodiscard]] constexpr type max() const {return std::max({", SmallFields(","), "});}\n");
                }

                { // Copy with modified members
                    if (is_vector)
                    {
                        struct Operator
                        {
                            std::string name, str;
                        };
                        const Operator operators[]{{"set",""},{"add","+"},{"sub","-"},{"mul","*"},{"div","/"}};

                        for (const auto &op : operators)
                        for (int i = 0; i < data::fields_alt_count; i++)
                        {
                            for (int j = 0; j < w; j++)
                            {
                                bool op_set = op.str == "";
                                output(" template <typename N> [[nodiscard]] constexpr ",(op_set ? "vec" : "auto")," ",op.name,"_",data::fields_alt[i][j],"(const N &n) const {return ",
                                       (op_set ? "vec" : make_str("vec",w,"<decltype(x",op.str,"n)>")),"(");
                                for (int k = 0; k < w; k++)
                                {
                                    if (k != 0)
                                        output(", ");
                                    if (k == j)
                                        output(op_set ? "n" : data::fields_alt[i][k] + op.str + "n");
                                    else
                                        output(data::fields_alt[i][k]);
                                }
                                output(");}");
                            }
                            next_line();
                        }
                    }
                }

                { // Resize
                    // One-dimensional, for both vectors and matrices
                    for (int i = 2; i <= 4; i++)
                    {
                        if (i == w)
                            continue;
                        output("[[nodiscard]] constexpr vec",i,"<member_type> to_vec",i,"(");
                        for (int j = w; j < i; j++)
                        {
                            if (j != w)
                                output(", ");
                            output("member_type n",data::fields[j]);
                        }
                        output(") const {return {");
                        for (int j = 0; j < i; j++)
                        {
                            if (j != 0)
                                output(", ");
                            if (j >= w)
                                output("n");
                            output(data::fields[j]);
                        }
                        output("};}\n");
                    }
                    for (int i = w+1; i <= 4; i++)
                    {
                        output("[[nodiscard]] constexpr vec",i,"<member_type> to_vec",i,"() const {return to_vec",i,"(");
                        for (int j = w; j < i; j++)
                        {
                            if (j != w)
                                output(", ");
                            output("{}");
                        }
                        output(");}\n");
                    }

                    // Two-dimensional, for matrices only
                    if (is_matrix)
                    {
                        for (int hhh = 2; hhh <= 4; hhh++)
                        {
                            for (int www = 2; www <= 4; www++)
                            {
                                if (www == w && hhh == h)
                                    continue;
                                output("[[nodiscard]] constexpr mat",www,'x',hhh,"<type> to_mat",www,'x',hhh,"() const {return {");
                                for (int hh = 0; hh < hhh; hh++)
                                {
                                    for (int ww = 0; ww < www; ww++)
                                    {
                                        if (ww != 0 || hh != 0)
                                            output(',');
                                        if (ww < w && hh < h)
                                            output(data::fields[ww],'.',data::fields[hh]);
                                        else
                                            output("01"[ww == hh]);
                                    }
                                }
                                output("};}\n");
                                if (www == hhh)
                                    output("[[nodiscard]] constexpr mat",www,"<type> to_mat",www,"() const {return to_mat",www,'x',www,"();}\n");
                            }
                        }
                    }
                }

                { // Length and normalization
                    if (is_vector)
                    {
                        // Squared length
                        output("[[nodiscard]] constexpr auto len_sqr() const {return ");
                        for (int i = 0; i < w; i++)
                        {
                            if (i != 0)
                                output(" + ");
                            output(data::fields[i],"*",data::fields[i]);
                        }
                        output(";}\n");

                        // Length
                        output("[[nodiscard]] constexpr auto len() const {return std::sqrt(len_sqr());}\n");

                        // Normalize
                        output("[[nodiscard]] constexpr auto norm() const -> vec",w,"<decltype(type{}/len())> {if (auto l = len(); l != 0) return *this / l; else return vec(0);}\n");
                    }
                }

                { // Dot and cross products
                    if (is_vector)
                    {
                        // Dot product
                        output("template <typename TT> [[nodiscard]] constexpr auto dot(const vec",w,"<TT> &o) const {return ");
                        for (int i = 0; i < w; i++)
                        {
                            if (i != 0)
                                output(" + ");
                            output(data::fields[i]," * o.",data::fields[i]);
                        }
                        output(";}\n");

                        // Cross product
                        if (w == 3)
                            output("template <typename TT> [[nodiscard]] constexpr auto cross(const vec3<TT> &o) const -> vec3<decltype(x * o.x - x * o.x)> {return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};}\n");

                        // Cross product z component
                        if (w == 2)
                            output("template <typename TT> [[nodiscard]] constexpr auto cross(const vec2<TT> &o) const {return x * o.y - y * o.x;}\n");
                    }
                }

                { // Matrix multiplication
                    auto Matrix = [&](int x, int y, std::string t) -> std::string
                    {
                        if (x == 1 && y == 1)
                            return t;
                        if (x == 1)
                            return make_str("vec",y,"<",t,">");
                        if (y == 1)
                            return make_str("vec",x,"<",t,">");
                        return make_str("mat",x,"x",y,"<",t,">");
                    };
                    auto Field = [&](int x, int y, int w, int h) -> std::string
                    {
                        if (w == 1 && h == 1)
                            return "";
                        if (w == 1)
                            return data::fields[y];
                        if (h == 1)
                            return data::fields[x];
                        return make_str(data::fields[x], ".", data::fields[y]);
                    };

                    for (int i = 1; i <= 4; i++)
                    {
                        output("template <typename TT> [[nodiscard]] constexpr ",Matrix(i,h,"larger_t<type,TT>")," mul(const ",Matrix(i,w,"TT")," &m) const {return {");
                        for (int y = 0; y < h; y++)
                        for (int x = 0; x < i; x++)
                        {
                            if (y != 0 || x != 0)
                                output(", ");
                            for (int j = 0; j < w; j++)
                            {
                                if (j != 0)
                                    output(" + ");
                                output(Field(j,y,w,h),"*m.",Field(x,j,i,w));
                            }
                        }
                        output("};}\n");
                    }
                }
            };

            decorative_section("Vectors", [&]
            {
                for (int d = 2; d <= 4; d++)
                {
                    if (d != 2)
                        next_line();

                    section_sc(make_str("template <typename T> struct vec<", d, ",T> // vec", d), [&]{
                        Make(d, 1);
                    });
                }
            });

            next_line();

            decorative_section("Matrices", [&]
            {
                for (int w = 2; w <= 4; w++)
                for (int h = 2; h <= 4; h++)
                {
                    if (w != 2 || h != 2)
                        next_line();

                    section_sc(make_str("template <typename T> struct vec<", w, ",vec<", h, ",T>> // mat", w, "x", h), [&]{
                        Make(w, h);
                    });
                }
            });

            next_line();

            decorative_section("Operators", []
            {
                constexpr const char
                    *ops2[]{"+","-","*","/","%","^","&","|","<<",">>","<",">","<=",">=","==","!="},
                    *ops2bool[]{"&&","||"},
                    *ops1[]{"~","+","-"},
                    *ops1incdec[]{"++","--"},
                    *ops1bool[]{"!"},
                    *ops2as[]{"+=","-=","*=","/=","%=","^=","&=","|=","<<=",">>="};

                for (int d = 2; d <= 4; d++)
                {
                    if (d != 2)
                        next_line();

                    decorative_section(make_str("vec", d), [&]
                    {
                        for (auto op : ops2)
                        {
                            bool all_of = (op == std::string("==")),
                                 any_of = (op == std::string("!=")),
                                 boolean = all_of || any_of;

                            // vec @ vec
                            output("template <typename A, typename B> [[nodiscard]] constexpr ",(boolean ? "bool" : "auto")," operator",op,"(const vec",d,"<A> &a, const vec",d,"<B> &b)",
                                   (boolean ? "" : make_str(" -> vec",d,"<decltype(a.x ",op," b.x)>"))," {return ",(boolean ? "" : "{"));
                            for (int i = 0; i < d; i++)
                            {
                                if (i != 0)
                                    output(all_of ? " && " :
                                           any_of ? " || " : ", ");
                                output("a.",data::fields[i]," ",op," b.", data::fields[i]);
                            }
                            output((boolean ? "" : "}"),";}\n");

                            // vec @ scalar
                            output("template <typename V, typename S, typename = if_scalar_t<S>> [[nodiscard]] constexpr ",(boolean ? "bool" : "auto")," operator",op,"(const vec",d,"<V> &v, const S &s) {return v ",op," vec_from_scalar<decltype(v)>(s);}\n");

                            // scalar @ vec
                            output("template <typename S, typename V, typename = if_scalar_t<S>> [[nodiscard]] constexpr ",(boolean ? "bool" : "auto")," operator",op,"(const S &s, const vec",d,"<V> &v) {return vec_from_scalar<decltype(v)>(s) ",op," v;}\n");
                        }

                        for (auto op : ops2bool)
                        {
                            // vec @ vec
                            output("template <typename A, typename B> [[nodiscard]] constexpr bool operator",op,"(const vec",d,"<A> &a, const vec",d,"<B> &b) {return bool(a) ",op," bool(b);}\n");

                            // vec @ any
                            output("template <typename A, typename B> [[nodiscard]] constexpr bool operator",op,"(const vec",d,"<A> &a, const B &b) {return bool(a) ",op," bool(b);}\n");

                            // any @ vec
                            output("template <typename A, typename B> [[nodiscard]] constexpr bool operator",op,"(const A &a, const vec",d,"<B> &b) {return bool(a) ",op," bool(b);}\n");
                        }

                        for (auto op : ops1)
                        {
                            // @ vec
                            output("template <typename T> [[nodiscard]] constexpr auto operator",op,"(const vec",d,"<T> &v) -> vec",d,"<decltype(",op,"v.x)> {return {");
                            for (int i = 0; i < d; i++)
                            {
                                if (i != 0)
                                    output(", ");
                                output(op, "v.", data::fields[i]);
                            }
                            output("};}\n");
                        }

                        for (auto op : ops1bool)
                        {
                            // @ vec
                            output("template <typename T> [[nodiscard]] constexpr bool operator",op,"(const vec",d,"<T> &v) {return ",op,"bool(v);}\n");
                        }

                        for (auto op : ops1incdec)
                        {
                            // @ vec
                            output("template <typename T> constexpr vec",d,"<T> &operator",op,"(vec",d,"<T> &v) {");
                            for (int i = 0; i < d; i++)
                                output(op,"v.",data::fields[i],"; ");
                            output("return v;}\n");

                            // vec @
                            output("template <typename T> constexpr vec",d,"<T> operator",op,"(vec",d,"<T> &v, int) {return {");
                            for (int i = 0; i < d; i++)
                            {
                                if (i != 0)
                                    output(", ");
                                output("v.",data::fields[i],op);
                            }
                            output("};}\n");
                        }

                        for (auto op : ops2as)
                        {
                            // vec @ vec
                            output("template <typename A, typename B> constexpr vec",d,"<A> &operator",op,"(vec",d,"<A> &a, const vec",d,"<B> &b) {");
                            for (int i = 0; i < d; i++)
                                output("a.",data::fields[i]," ",op," b.",data::fields[i],"; ");
                            output("return a;}\n");

                            // vec @ scalar
                            output("template <typename V, typename S, typename = if_scalar_t<S>> constexpr vec",d,"<V> &operator",op,"(vec",d,"<V> &v, const S &s) {return v ",op," vec_from_scalar<decltype(v)>(s);}\n");
                        }
                    });
                }

                next_line();

                decorative_section("input/output", [&]
                {
                    output(
                    R"( template <typename A, typename B, int D, typename T> std::basic_ostream<A,B> &operator<<(std::basic_ostream<A,B> &s, const vec<D,T> &v)
                        {
                            s << '[';
                            for (int i = 0; i < D; i++)
                            {
                                if (i != 0)
                                    s << ',';
                                s << v[i];
                            }
                            s << ']';
                            return s;
                        }
                        template <typename A, typename B, int W, int H, typename T> std::basic_ostream<A,B> &operator<<(std::basic_ostream<A,B> &s, const vec<W,vec<H,T>> &v)
                        {
                            s << '[';
                            for (int y = 0; y < H; y++)
                            {
                                if (y != 0)
                                    s << ';';
                                for (int x = 0; x < W; x++)
                                {
                                    if (x != 0)
                                        s << ',';
                                    s << v[x][y];
                                }
                            }
                            s << ']';
                            return s;
                        }
                        template <typename A, typename B, int D, typename T> std::basic_istream<A,B> &operator>>(std::basic_istream<A,B> &s, vec<D,T> &v)
                        {
                            for (int i = 0; i < D; i++)
                            $   s >> v[i];
                            return s;
                        }
                        template <typename A, typename B, int W, int H, typename T> std::basic_istream<A,B> &operator>>(std::basic_istream<A,B> &s, vec<W,vec<H,T>> &v)
                        {
                            for (int y = 0; y < H; y++)
                            for (int x = 0; x < W; x++)
                            $   s >> v[x][y];
                            return s;
                        }
                    )");
                });
            });
        });
    });

    if (!impl::output_file)
        return -1;
}
