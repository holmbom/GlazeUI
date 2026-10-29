// glaze_net — namnuppslagningen. Kontraktet står i dns.h; OS-anropet bor i
// sys_win.cpp/sys_posix.cpp (`resolveBlocking`).

#include "glaze_net/dns.h"

#include "detached.h"

namespace glaze::net {

Result<std::vector<Address>> resolve(std::string_view host, std::stop_token stop,
                                     std::chrono::milliseconds timeout)
{
    std::string namn(host);
    if (namn.size() >= 2 && namn.front() == '[' && namn.back() == ']')
        namn = namn.substr(1, namn.size() - 2);
    if (namn.empty())
        return Error{Code::InvalidArgument, "namnuppslagning: tomt värdnamn"};

    // En numerisk adress är redan svaret — ingen tråd, inget OS-anrop.
    if (auto adress = Address::parse(namn))
        return std::vector<Address>{*adress};

    const std::string vad = "namnuppslagning av \"" + namn + "\"";
    if (stop.stop_requested())
        return sys::avbrutet(vad);
    if (auto init = sys::startup(); !init)
        return sys::med(vad, init.failure());

    return sys::runDetached<std::vector<Address>>(
        [namn] { return sys::resolveBlocking(namn); }, sys::deadlineFrom(timeout), stop,
        vad);
}

} // namespace glaze::net
