#pragma once
// 状态转换色带用 categorical 调色板 + 稳定着色哈希。
// 
//   - Okabe-Ito 色盲友好 10 色调色板（追加式，索引顺序即视觉语义）；
//   - FNV-1a 64 位哈希对 UTF-8 字节取模选色，跨会话/平台确定，
//     同一状态串永远同色，无需配置；10 桶允许碰撞。

#include <QColor>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nextpilot::plotscope::fastplot
{

inline constexpr std::array<std::array<std::uint8_t, 3>, 10> kStatePalette = {{
    {{0x00, 0x72, 0xB2}}, // blue
    {{0xD5, 0x5E, 0x00}}, // vermillion
    {{0x00, 0x9E, 0x73}}, // bluish green
    {{0xE6, 0x9F, 0x00}}, // orange
    {{0x56, 0xB4, 0xE9}}, // sky blue
    {{0xCC, 0x79, 0xA7}}, // reddish purple
    {{0xF0, 0xE4, 0x42}}, // yellow
    {{0x99, 0x99, 0x99}}, // grey
    {{0x17, 0xBE, 0xCF}}, // teal
    {{0xBC, 0xBD, 0x22}}, // olive
}};

// FNV-1a 64 位：offset basis 14695981039346656037，prime 1099511628211。
inline constexpr std::uint64_t fnv1a64(std::string_view bytes) noexcept
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : bytes)
    {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// 状态值的稳定调色板索引。
inline constexpr std::size_t stateColorIndex(std::string_view value) noexcept
{
    return static_cast<std::size_t>(fnv1a64(value) % kStatePalette.size());
}

// 状态值对应的调色板颜色（Qt 入口，哈希 UTF-8 编码）。
inline QColor stateColor(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    const auto &rgb = kStatePalette[stateColorIndex(
        std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())))];
    return {rgb[0], rgb[1], rgb[2]};
}

} // namespace nextpilot::plotscope::fastplot
