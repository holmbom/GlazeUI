#pragma once

// Handens index i paketets par: 0 är vänster, 1 höger — som `FrameInfo`s fält.

#include <cstddef>

#include <glaze_openxr/session.h>

namespace glaze::openxr::detail {

inline std::size_t handIndex(Hand hand) noexcept { return hand == Hand::Left ? 0 : 1; }

} // namespace glaze::openxr::detail
