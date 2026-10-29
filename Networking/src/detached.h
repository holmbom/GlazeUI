#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>

#include "sys.h"

// INTERN — ett OAVBRYTBART blockerande OS-anrop (getaddrinfo, IcmpSendEcho)
// körs i en egen, FRIKOPPLAD tråd, och anroparen väntar på det med tak och
// stopp. Tak och avbrott verkar direkt för anroparen; OS-anropet får löpa
// klart i bakgrunden och dess svar kastas.
//
// FRIKOPPLAD med flit (`detach`), och det är säkert därför att tråden BARA
// rör sitt eget delade tillstånd (shared_ptr) och anropets egna kopior —
// aldrig anroparens stack, aldrig paketets globala. Att joina den hade
// betytt att avbrytningen väntat ut OS:ets egen tidsgräns, vilket är exakt
// det formen finns för att slippa. Priset: en tråd per anrop (mikrosekunder
// mot ett DNS-svars millisekunder).

namespace glaze::net::sys {

template <class T>
Result<T> runDetached(std::function<Result<T>()> anrop, Deadline deadline,
                      const std::stop_token& stop, const std::string& vad)
{
    struct Delat {
        std::mutex                  m;
        std::condition_variable_any cv;
        std::optional<Result<T>>    svar;
    };
    auto delat = std::make_shared<Delat>();
    try {
        std::thread([delat, anrop = std::move(anrop)] {
            Result<T> r = anrop();
            std::lock_guard<std::mutex> lås(delat->m);
            delat->svar.emplace(std::move(r));
            delat->cv.notify_all();   // under låset — `delat` lever ändå (shared_ptr)
        }).detach();
    } catch (const std::system_error& e) {
        return Error{Code::Failed, vad + ": kunde inte starta en tråd (" + e.what() + ")"};
    }

    std::unique_lock<std::mutex> lås(delat->m);
    const auto klart = [&] { return delat->svar.has_value(); };
    // `wait_until(…, time_point::max())` räknas om till systemklockan i
    // libstdc++ och kan svämma över — utan tak används formen utan tid.
    const bool fick = deadline == Deadline::max()
                          ? delat->cv.wait(lås, stop, klart)
                          : delat->cv.wait_until(lås, stop, deadline, klart);
    if (fick) {
        Result<T> r = std::move(*delat->svar);
        if (!r)
            return med(vad, r.failure());
        return r;
    }
    if (stop.stop_requested())
        return avbrutet(vad);
    return Error{Code::Timeout, vad + ": inget svar inom tidsgränsen"};
}

} // namespace glaze::net::sys
