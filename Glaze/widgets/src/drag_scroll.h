#pragma once
#include <cstdint>

// AUTOSKROLLENS TIMER-ID — INTERN
// header, som `tooltip.h`: inget här är publikt API. Det publika är kroken
// `Widget::dragScroll` (widget.h); mekaniken bor i drag_drop.cpp, där
// sessionen startar fönstrets timer när något under pekaren kan rulla och
// stoppar den vid första ticket utan rullning och vid sessionens slut.
// Grindarna (tree_gate) injicerar ett `Timer`-event med id:t, så det står
// i en header och inte TU-lokalt.

namespace glaze {

// Ramverkets timer-id:n ligger HÖGT (caret-blinket är 0x80000001, tooltipen
// 0x80000002 i tooltip.h) — appens egna `Window::startTimer`-id ska ligga
// under.
inline constexpr std::uint32_t kDragScrollTimerId = 0x80000003;

} // namespace glaze
