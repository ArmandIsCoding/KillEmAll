#include "StageBossRenderer.hpp"
#include "Combat.hpp"

#include <SFML/Graphics/RenderTarget.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace
{
using Vec = sf::Vector2f;
using Color = sf::Color;
constexpr float tau = 6.2831853f;
constexpr Color ink{25, 23, 37}, shadow{39, 42, 57}, bolt{202, 198, 167}, amber{255, 185, 82};
struct Paint
{
    Color dark, mid, light, lamp;
};
constexpr std::array<Paint, 9> finishes{{{{84, 56, 52}, {160, 96, 55}, {223, 156, 86}, {117, 252, 207}},
                                         {{53, 63, 74}, {102, 130, 124}, {188, 195, 157}, {255, 157, 75}},
                                         {{43, 65, 88}, {74, 115, 136}, {160, 193, 192}, {106, 245, 235}},
                                         {{82, 51, 50}, {147, 83, 62}, {227, 152, 87}, {255, 209, 92}},
                                         {{42, 77, 64}, {72, 132, 100}, {167, 187, 106}, {255, 110, 96}},
                                         {{74, 42, 52}, {132, 79, 79}, {195, 136, 107}, {255, 150, 48}},
                                         {{59, 47, 98}, {126, 88, 168}, {193, 148, 213}, {91, 255, 229}},
                                         {{54, 68, 92}, {117, 130, 157}, {205, 204, 176}, {255, 189, 88}},
                                         {{60, 41, 60}, {106, 71, 99}, {188, 117, 119}, {255, 96, 88}}}};

Color alpha(Color c, float a)
{
    c.a = static_cast<std::uint8_t>(255.f * std::clamp(a, 0.f, 1.f));
    return c;
}

Color metalShade(Color c, int value)
{
    const auto channel = [value](int component) { return static_cast<std::uint8_t>(std::clamp(component + value, 0, 255)); };
    return {channel(c.r), channel(c.g), channel(c.b)};
}

// All local coordinates are sprite pixels. Scanline plates retain pixel stair steps on diagonal edges.
struct Art
{
    Vec origin;
    Paint paint;
    std::vector<sf::Vertex> verts;

    void quad(Vec a, Vec b, Vec c, Vec d, Color tint)
    {
        verts.insert(verts.end(), {{origin + a, tint},
                                   {origin + b, tint},
                                   {origin + c, tint},
                                   {origin + a, tint},
                                   {origin + c, tint},
                                   {origin + d, tint}});
    }
    void rect(float x, float y, float w, float h, Color c)
    {
        if (w <= 0 || h <= 0)
            return;
        quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, c);
    }
    void line(Vec a, Vec b, float width, Color c)
    {
        Vec d = b - a;
        if (d.lengthSquared() < .005f)
            return;
        Vec n = d.perpendicular().normalized() * (width * .5f);
        quad(a + n, b + n, b - n, a - n, c);
    }
    void oval(Vec p, float rx, float ry, Color c)
    {
        for (int row = -static_cast<int>(ry); row < ry; row += 2)
        {
            const float t = (row + 1.f) / ry;
            const float dx = std::round(rx * std::sqrt(std::max(0.f, 1.f - t * t)));
            rect(p.x - dx, p.y + row, 2 * dx, 2, c);
        }
    }
    void plate(std::initializer_list<Vec> edges, int material = 0)
    {
        const auto [lo, hi] = std::minmax_element(edges.begin(), edges.end(), [](Vec a, Vec b) { return a.y < b.y; });
        const int top = static_cast<int>(std::floor(lo->y)), bottom = static_cast<int>(std::ceil(hi->y));
        for (int y = top; y < bottom; y += 2)
        {
            const float v = std::min(y + .9f, hi->y - .1f);
            float left = 10000.f, right = -10000.f;
            Vec a = *(edges.end() - 1);
            for (Vec b : edges)
            {
                if ((a.y <= v && b.y > v) || (b.y <= v && a.y > v))
                {
                    const float u = a.x + (b.x - a.x) * (v - a.y) / (b.y - a.y);
                    left = std::min(left, u);
                    right = std::max(right, u);
                }
                a = b;
            }
            const float x = std::round(left), w = std::round(right) - x;
            if (w <= 0.f)
                continue;
            const float t = (v - top) / (bottom - top);
            Color face = t < .22f ? paint.light : t < .56f ? paint.mid : paint.dark;
            if (material == 1)
                face = t < .19f ? paint.mid : t < .63f ? paint.dark : shadow;
            rect(x, y, w, 2, ink);
            if (y > top && y + 2 < bottom && w > 4)
            {
                // Each belt is made of staggered metal panels, with a sunlit lip and recessed joins.
                const int row = (y - top) / 2;
                const int offset = (row / 6) % 2 ? 7 : 22;
                const int begin = static_cast<int>(x) + 2, end = static_cast<int>(x + w) - 2;
                for (int left = begin; left < end;)
                {
                    const int panel = (left + 256 + offset) / 29;
                    const int right = std::min(end, (panel + 1) * 29 - 256 - offset);
                    const int shade = (panel * 7 + (row / 6) * 5) % 14 - 7 + (row % 6 == 0 ? 12 : 0);
                    rect(static_cast<float>(left), y, static_cast<float>(right - left), 2, metalShade(face, shade));
                    if (left != begin && right - left > 2)
                    {
                        rect(static_cast<float>(left), y, 1, 2, metalShade(face, -21));
                        if (row % 6 == 1)
                            rect(static_cast<float>(left + 2), y, 2, 1, paint.light);
                    }
                    left = right;
                }
                if (w > 23.f && y % 8 == 0)
                    rect(x + 4, y, std::min(w * .20f, 12.f), 1, paint.light);
            }
        }
    }
    void vents(float x, float y, int count, float height)
    {
        for (int i = 0; i < count; ++i)
        {
            rect(x + i * 5, y, 3, height, ink);
            rect(x + i * 5 + 1, y + height, 3, 1, paint.mid);
        }
    }
    void seam(float x, float y, float width)
    {
        rect(x, y, width, 2, shadow);
        rect(x + 1, y + 2, width - 2, 1, paint.mid);
        for (float dx = 3; dx < width - 2; dx += 15)
            rect(x + dx, y - 1, 2, 2, bolt);
    }
    void cabin(float x, float y, int panes)
    {
        rect(x - 2, y - 2, panes * 7 + 3, 9, ink);
        for (int i = 0; i < panes; ++i)
        {
            rect(x + i * 7, y, 5, 5, {66, 120, 143});
            rect(x + i * 7, y, 5, 2, {155, 216, 208});
        }
    }
    void brace(Vec a, Vec b, float w)
    {
        const Vec d = b - a;
        if (d.lengthSquared() < 1)
            return;
        const Vec side = d.perpendicular().normalized() * w * .5f;
        line(a + side, b + side, 3, ink);
        line(a - side, b - side, 3, paint.light);
        for (int j = 0; j < 5; ++j)
        {
            const Vec base = a + d * (j * .2f);
            line(base - side, base + d * .2f + side, 2, paint.mid);
        }
    }
    void piston(Vec base, Vec head)
    {
        const Vec elbow = base + (head - base) * .55f;
        line(base, head, 10, ink);
        line(base, head, 6, bolt);
        line(base, elbow, 11, paint.dark);
        line(base - Vec{2, 0}, elbow - Vec{2, 0}, 3, paint.light);
        oval(base, 7, 5, shadow);
    }
};

void cizalla(Art &a, const StageBoss &b)
{
    for (unsigned i = 0; i < 2; ++i)
    {
        const float x = i == 0 ? -38.f : 18.f;
        const Vec arm = b.mount(i) - b.position;
        const Vec top = arm + Vec{16, -22};
        a.brace({x, -8}, top, 9);
        a.line({x + 17, -11}, top, 2, bolt);
        a.line(top, arm, 2, bolt);
        a.line(top + Vec{3, 0}, arm + Vec{3, 0}, 1, shadow);
    }
    a.plate({{-106, -10}, {80, -12}, {101, -4}, {87, 15}, {69, 29}, {-73, 29}, {-96, 12}});
    a.rect(-86, 11, 164, 3, {146, 65, 54});
    a.seam(-74, 17, 137);
    a.plate({{-39, -11}, {-35, -34}, {-21, -41}, {33, -39}, {48, -29}, {49, -7}}, 1);
    a.cabin(-23, -33, 6);
    a.vents(-32, -11, 10, 7);
    for (int i = 0; i < 4; ++i)
        a.rect(-75 + i * 11, -6, 6, 3, amber);
    a.rect(48, -15, 20, 7, shadow);
    a.rect(53, -19, 11, 5, a.paint.mid);
}

void albatros(Art &a, const StageBoss &)
{
    a.plate({{-27, -4}, {-40, -44}, {-13, -50}, {41, -8}, {66, -1}, {66, 6}}, 1);
    a.plate({{-32, 2}, {-6, -2}, {57, 32}, {57, 45}, {33, 49}, {-3, 24}}, 1);
    a.plate({{37, -5}, {54, -27}, {74, -33}, {65, -8}, {75, 3}, {52, 8}});
    a.plate(
        {{-80, 0}, {-65, -10}, {-37, -17}, {-22, -23}, {32, -20}, {61, -6}, {73, -1}, {62, 12}, {-45, 16}, {-68, 8}});
    a.cabin(-44, -13, 4);
    a.vents(15, -6, 7, 9);
    a.seam(-40, 7, 49);
    a.rect(-61, 0, 17, 2, amber);
}

void narval(Art &a, const StageBoss &b)
{
    for (unsigned i = 0; i < b.mountCount(); ++i)
    {
        const auto pod = b.mount(i) - b.position;
        a.piston({pod.x + 5, -7}, pod);
    }
    a.plate({{52, -4}, {81, -27}, {97, -30}, {90, -4}, {105, -3}, {102, 8}, {73, 9}}, 1);
    a.plate(
        {{-110, 1}, {-93, -10}, {-61, -18}, {63, -18}, {97, -6}, {107, 3}, {94, 14}, {57, 24}, {-60, 24}, {-96, 12}});
    a.rect(-81, 12, 153, 3, {119, 73, 77});
    a.seam(-65, 16, 113);
    a.plate({{-18, -17}, {-10, -31}, {-4, -36}, {24, -34}, {33, -15}});
    a.rect(3, -48, 3, 13, shadow);
    a.rect(3, -47, 10, 3, a.paint.light);
    a.line({21, -34}, {26, -48}, 2, bolt);
    a.cabin(-6, -29, 4);
    for (int i = 0; i < 9; ++i)
    {
        a.rect(-74 + i * 17, -1, 5, 4, ink);
        a.rect(-73 + i * 17, -1, 3, 2, a.paint.lamp);
    }
    a.vents(39, 5, 5, 5);
}

void taladro(Art &a, const StageBoss &b)
{
    for (unsigned i = 0; i < b.mountCount(); ++i)
    {
        const auto turret = b.mount(i) - b.position;
        a.piston({turret.x + 8, -10}, turret);
        a.line({turret.x - 12, -9}, turret, 3, a.paint.mid);
    }
    a.plate({{-93, 15}, {92, 15}, {86, 30}, {-79, 31}}, 1);
    for (int i = 0; i < 10; ++i)
    {
        const Vec p{-78.f + i * 17.f, 21};
        a.oval(p, 8, 8, ink);
        a.oval(p, 6, 6, a.paint.dark);
        const float t = b.age * 5.f + i;
        a.line(p, p + Vec{5.f * std::cos(t), 5.f * std::sin(t)}, 2, bolt);
    }
    a.plate({{-92, -13}, {-57, -22}, {72, -19}, {99, -8}, {100, 13}, {77, 19}, {-79, 17}});
    a.plate({{-25, -15}, {-19, -38}, {19, -38}, {37, -26}, {38, -14}});
    a.plate({{-115, 0}, {-92, -13}, {-70, -10}, {-68, 10}, {-90, 13}});
    for (int i = 0; i < 3; ++i)
        a.line({-102.f + i * 8, -5.f - i * 2}, {-97.f + i * 8, 7.f}, 2, bolt);
    a.cabin(-12, -32, 4);
    a.vents(37, -6, 9, 9);
    a.seam(-57, 7, 78);
}

void anaconda(Art &a, const StageBoss &b)
{
    const Vec m0 = b.mount(0) - b.position, m1 = b.mount(1) - b.position;
    Vec prev{18, 0};
    for (int i = 1; i <= 5; ++i)
    {
        Vec p = i <= 2   ? prev + (m0 - prev) / static_cast<float>(3 - i)
                : i <= 4 ? prev + (m1 - prev) / static_cast<float>(5 - i)
                         : m1 + Vec{25, 10.f * std::sin(b.age)};
        a.line(prev, p, 23 - i * 2, ink);
        a.line(prev, p, 13 - i, a.paint.light);
        a.oval(p, 20 - i * 1.5f, 19 - i, a.paint.dark);
        a.oval(p - Vec{0, 3}, 15 - i, 7, a.paint.mid);
        a.rect(p.x - 5, p.y - 12, 7, 2, a.paint.light);
        prev = p;
    }
    a.plate({{-46, -3}, {-34, -17}, {-12, -27}, {14, -23}, {33, -10}, {42, 2}, {25, 17}, {-11, 21}, {-35, 12}});
    a.plate({{-42, 10}, {-22, 13}, {-11, 18}, {-22, 27}, {-38, 23}}, 1);
    a.rect(-34, -10, 16, 4, b.critical() ? Color::White : a.paint.lamp);
    a.vents(5, -13, 4, 7);
}

void basilisco(Art &a, const StageBoss &b)
{
    for (unsigned i = 0; i < b.mountCount(); ++i)
    {
        const auto pod = b.mount(i) - b.position;
        a.piston({pod.x * .80f, -15}, pod);
    }
    for (int i = 0; i < 4; ++i)
    {
        const float x = -48.f + 29.f * i;
        const float travel = 7.f * std::sin(3.4f * b.age + i * 2.f);
        const Vec knee{x + (i % 2 ? 17.f : -15.f), 39 + travel}, foot{x + travel * 1.8f, 67 - std::abs(travel)};
        a.line({x, 15}, knee, 13, ink);
        a.line({x, 15}, knee, 7, a.paint.mid);
        a.line(knee, foot, 9, shadow);
        a.line(knee, foot, 4, bolt);
        a.rect(foot.x - 9, foot.y - 3, 18, 5, ink);
    }
    a.plate({{-73, 21}, {-57, -21}, {-35, -38}, {31, -39}, {57, -23}, {65, 8}, {43, 27}, {-28, 33}});
    a.plate({{-49, -20}, {-13, -30}, {16, -29}, {44, -10}, {35, 17}, {-30, 18}}, 1);
    a.rect(-25, -13, 55, 26, ink);
    for (int i = 0; i < 7; ++i)
    {
        const float heat = 6 + 6 * std::sin(7 * b.age + i);
        a.rect(-22 + i * 7, -12, 4, 23, {191, 63, 48});
        a.rect(-22 + i * 7, 9 - heat, 4, heat + 2, amber);
    }
    a.vents(35, 1, 4, 12);
    a.seam(-38, 24, 66);
}

void prisma(Art &a, const StageBoss &b)
{
    for (unsigned i = 0; i < 2; ++i)
    {
        const float sign = i == 0 ? -1.f : 1.f;
        const Vec needle = b.mount(i) - b.position;
        a.plate({{-9, sign * 11}, {3, sign * 47}, {needle.x, needle.y}, {46, sign * 48}, {31, sign * 15}}, 1);
        a.line({6, sign * 20}, needle, 2, a.paint.lamp);
    }
    a.plate({{-64, 1}, {-23, -25}, {4, -47}, {24, -31}, {46, -19}, {66, 0}, {37, 19}, {12, 46}, {-7, 38}, {-34, 18}});
    a.line({-49, 0}, {1, -26}, 3, bolt);
    a.line({-50, 0}, {2, 26}, 3, a.paint.dark);
    a.plate({{-26, 0}, {3, -21}, {28, 0}, {3, 21}}, 1);
    a.line({4, -17}, {20, 0}, 3, a.paint.lamp);
    a.line({20, 0}, {4, 17}, 3, a.paint.lamp);
}

void arcangel(Art &a, const StageBoss &)
{
    a.plate({{-44, -5}, {9, -36}, {43, -38}, {33, -16}, {87, -31}, {105, -26}, {84, -4}}, 1);
    a.plate({{-113, 2},
             {-89, -13},
             {-56, -24},
             {-8, -25},
             {22, -35},
             {42, -29},
             {62, -18},
             {97, -9},
             {108, 0},
             {98, 13},
             {64, 22},
             {-54, 25},
             {-98, 12}});
    a.plate({{-80, 12}, {-12, 13}, {25, 28}, {75, 25}, {87, 30}, {65, 39}, {19, 43}, {-25, 31}, {-71, 27}}, 1);
    a.cabin(-75, -9, 7);
    a.seam(-46, 12, 115);
    a.vents(36, -11, 9, 9);
    for (int i = 0; i < 7; ++i)
        a.rect(-48 + i * 13, 4, 6, 2, amber);
    a.line({10, -27}, {23, -32}, 2, bolt);
}

void corona(Art &a, const StageBoss &b)
{
    for (int i = 0; i < 40; ++i)
    {
        const float u = tau * i / 40.f, v = tau * (i + .85f) / 40.f;
        const Vec from{93 * std::cos(u), 86 * std::sin(u)}, to{93 * std::cos(v), 86 * std::sin(v)};
        a.line(from, to, 8, ink);
        a.line(from, to, 4, i % 5 == 0 ? a.paint.lamp : a.paint.dark);
        if (i % 5 == 0)
            a.line(from, from + (to - from) * .28f, 2, bolt);
    }
    for (unsigned i = 0; i < b.mountCount(); ++i)
        a.brace({}, b.mount(i) - b.position, 8.f);
    a.plate({{-59, -4},
             {-41, -30},
             {-22, -45},
             {7, -49},
             {41, -29},
             {57, -6},
             {54, 17},
             {27, 41},
             {0, 49},
             {-26, 39},
             {-48, 24}});
    a.oval({}, 38, 40, ink);
    a.oval({}, 31, 34, a.paint.dark);
    a.oval({-3, 0}, 26, 28, shadow);
    for (int i = 0; i < 12; ++i)
    {
        const float u = tau * i / 12 + b.age * .42f;
        const Vec p{30 * std::cos(u), 32 * std::sin(u)};
        a.rect(p.x - 2, p.y - 2, 4, 4, i % 3 == 0 ? amber : a.paint.mid);
    }
}

void weapons(Art &a, const StageBoss &b)
{
    for (unsigned i = 0; i < b.mountCount(); ++i)
    {
        const Vec p = b.mount(i) - b.position;
        const bool live = b.mountHealth[i] > 0;
        if (!live)
        {
            a.oval(p, 12, 7, shadow);
            a.line(p - Vec{7, 2}, p + Vec{6, -3}, 3, a.paint.dark);
            a.rect(p.x - 4, p.y, 6, 2, {177, 76, 55});
            continue;
        }
        const bool hit = b.mountFlash[i] > .045f;
        const float t = b.age;
        const Vec aim = b.target - b.mount(i);
        const Vec dir = aim.lengthSquared() > 1.f ? aim.normalized() : Vec{-1, 0};
        const Color metal = hit ? Color::White : a.paint.mid, shine = hit ? Color::White : a.paint.light;
        a.oval(p, 16, 15, ink);
        a.oval(p - Vec{0, 2}, 13, 11, metal);
        a.rect(p.x - 9, p.y - 11, 6, 2, shine);
        a.rect(p.x - 8, p.y + 7, 11, 2, a.paint.dark);
        switch (b.level)
        {
        case 2:
            for (float s : {-1.f, 1.f})
            {
                a.line(p + Vec{s * 4, -8}, p + Vec{s * 14, 5}, 5, shine);
                a.line(p + Vec{s * 14, 5}, p + Vec{s * 6, 14}, 5, a.paint.dark);
            }
            break;
        case 3:
            a.rect(p.x - 8, p.y - 13, 14, 12, shine);
            a.line(p + Vec{-31.f * std::cos(23 * t), -14}, p + Vec{31.f * std::cos(23 * t), -16}, 4, bolt);
            break;
        case 4:
        case 7:
            for (int j = 0; j < 2; ++j)
            {
                Vec q = p + Vec{-7.f + 10 * j, 0};
                a.rect(q.x - 3, q.y - 19, 7, 17, shine);
                a.rect(q.x - 5, q.y - 21, 11, 4, shadow);
                a.rect(q.x - 2, q.y - 21, 5, 2, a.paint.lamp);
            }
            break;
        case 5:
            a.line(p, p + Vec{-16, -19}, 11, ink);
            a.line(p, p + Vec{-16, -19}, 7, shine);
            break;
        case 8:
        case 10:
            a.oval(p, 9, 9, ink);
            a.oval(p, 6, 6, a.paint.lamp);
            a.rect(p.x - 2, p.y - 3, 3, 3, Color::White);
            break;
        case 9:
            a.rect(p.x - 13, p.y - 3, 26, 10, ink);
            a.rect(p.x - 11, p.y - 3, 22, 2, amber);
            a.rect(p.x - 8, p.y + 6, 16, 1, shine);
            break;
        default:
            a.line(p, p + dir * 20.f, 5, shine);
            break;
        }
        a.rect(p.x - 3, p.y - 4, 6, 4, b.warning() > .55f ? Color::White : a.paint.lamp);
    }
}

void telegraph(Art &a, const StageBoss &b)
{
    const float glow = b.warning();
    if (glow <= 0.f)
        return;
    const Color beacon = alpha(b.critical() ? Color{255, 129, 97} : amber, .28f + .65f * glow);
    const bool gate = (b.level == 8 && b.volleys % 2 == 0) || (b.level == 10 && b.volleys % 3 == 0);
    if (gate)
    {
        const float open = b.level == 10 ? 39.f : 53.f;
        const float x = -20.f;
        for (float y = 18.f; y < 533.f; y += 23.f)
            if (std::abs(y - b.gapY()) > open)
                a.rect(x - 2, y - b.position.y - 2, 4, 4, beacon);
        for (float sign : {-1.f, 1.f})
        {
            const float y = b.gapY() - b.position.y + open * sign;
            a.line({-205, y}, {x, y}, 1, alpha({120, 243, 212}, glow * .68f));
        }
        return;
    }
    const Vec aim = b.target - b.position;
    if (b.rearPulse())
    {
        const Vec relay = Vec{16.f, b.target.y} - b.position;
        const Color magenta = alpha({255, 113, 174}, .5f + .5f * glow);
        a.oval(relay, 5 + 4 * glow, 26, shadow);
        for (float sign : {-1.f, 1.f})
        {
            a.line(relay + Vec{-4, sign * 23}, relay + Vec{8, sign * 16}, 3, magenta);
            a.line(relay + Vec{8, sign * 16}, relay + Vec{4, sign * 6}, 2, magenta);
        }
        for (float x = relay.x + 12; x < aim.x + 65.f; x += 23.f)
            a.line({x, relay.y}, {x + 8.f, relay.y}, 1.5f, alpha(magenta, .25f + .5f * glow));
        a.oval(relay, 3 + 3 * glow, 3 + 3 * glow, Color::White);
    }
    for (float s : {-1.f, 1.f})
    {
        a.rect(aim.x + s * 9 - 2, aim.y - 1, 5, 2, beacon);
        a.rect(aim.x - 1, aim.y + s * 9 - 2, 2, 5, beacon);
    }
}
} // namespace

void drawStageBoss(sf::RenderTarget &target, const Combat &combat)
{
    if (!combat.stageBoss() || combat.stageBoss()->health <= 0)
        return;
    const auto &b = *combat.stageBoss();
    Art a{{std::round(b.position.x), std::round(b.position.y)}, finishes[b.level - 2], {}};
    a.verts.reserve(5500);
    telegraph(a, b);
    if (b.hitFlash > .045f)
    {
        a.paint.dark = {115, 110, 125};
        a.paint.mid = {221, 224, 215};
        a.paint.light = Color::White;
    }
    switch (b.level)
    {
    case 2:
        cizalla(a, b);
        break;
    case 3:
        albatros(a, b);
        break;
    case 4:
        narval(a, b);
        break;
    case 5:
        taladro(a, b);
        break;
    case 6:
        anaconda(a, b);
        break;
    case 7:
        basilisco(a, b);
        break;
    case 8:
        prisma(a, b);
        break;
    case 9:
        arcangel(a, b);
        break;
    case 10:
        corona(a, b);
        break;
    }
    const float pulse = .5f + .5f * std::sin(b.age * (b.critical() ? 15.f : 5.f));
    a.oval({}, b.level == 10 ? 23 : 11, b.level == 10 ? 24 : 9, ink);
    const Color core = b.critical() ? Color{255, 104, 79} : a.paint.lamp;
    a.oval({}, b.level == 10 ? 17 : 8, b.level == 10 ? 18 : 6, core);
    a.rect(-3, -3, 4 + 3 * pulse, 4, b.hitFlash > .04f ? Color::White : Color{255, 237, 198});
    weapons(a, b);
    target.draw(a.verts.data(), a.verts.size(), sf::PrimitiveType::Triangles);
}
