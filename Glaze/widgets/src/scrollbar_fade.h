#pragma once
#include <cstdint>

// RULLNINGSLISTORNAS TIMER-ID — INTERN header, som `tooltip.h`: inget här är
// publikt API. Det publika är `Widget::setScrollbarVisibility` (widget.h);
// mekaniken bor i scrollbar_fade.cpp. Grindarna (scrollbar_gate) injicerar ett
// `Timer`-event med id:t, så det står i en header och inte TU-lokalt.

namespace glaze {

// Ramverkets timer-id:n ligger HÖGT (caret-blinket är 0x80000001, tooltipen
// 0x80000002, autoskrollen 0x80000003) — appens egna `Window::startTimer`-id
// ska ligga under.
inline constexpr std::uint32_t kScrollbarTimerId = 0x80000004;

} // namespace glaze
