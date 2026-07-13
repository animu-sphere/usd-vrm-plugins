// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

namespace vrmContainer
{

// A non-owning, immutable view. The caller keeps the backing storage alive.
class ByteView
{
public:
    constexpr ByteView() noexcept = default;
    constexpr ByteView(const std::byte* data, std::size_t size) noexcept
        : _data(data), _size(size)
    {
    }

    constexpr const std::byte* data() const noexcept { return _data; }
    constexpr std::size_t size() const noexcept { return _size; }
    constexpr bool empty() const noexcept { return _size == 0; }
    constexpr bool valid() const noexcept { return _data != nullptr || _size == 0; }

private:
    const std::byte* _data = nullptr;
    std::size_t _size = 0;
};

} // namespace vrmContainer
