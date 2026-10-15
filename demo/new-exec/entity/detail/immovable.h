#pragma once


struct immovable
{
    constexpr immovable() = default;
    constexpr immovable(immovable&&) = delete;
    constexpr ~immovable() = default;
};