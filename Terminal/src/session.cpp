// Sessionen — trådarna, inkorgen, taket och rivningsordningen. Kontraktet
// står i session.h.

#include "glaze_terminal/session.h"

#include <algorithm>
#include <chrono>

namespace glaze::term {
namespace {

constexpr std::size_t kReadChunk = 64 * 1024;   // en läsning — under halva taket
constexpr int         kPollMs    = 10;          // gruppens livstecken under fristen

} // namespace

Session::Session() = default;

Session::~Session() { kill(); }

bool Session::start(const StartInfo& info)
{
    if (started_) {
        error_ = "sessionen har redan startats — en ny körning är en ny session";
        return false;
    }
    if (!pty_.start(info, error_))
        return false;
    started_ = true;
    reader_  = std::thread([this] { readLoop(); });
    writer_  = std::thread([this] { writeLoop(); });
    waiter_  = std::thread([this] { waitLoop(); });
    return true;
}

bool Session::isRunning() const
{
    std::lock_guard<std::mutex> l(m_);
    return started_ && !exited_ && !killed_;
}

int Session::exitCode() const
{
    std::lock_guard<std::mutex> l(m_);
    return exitCode_;
}

void Session::notice(std::string s)
{
    std::lock_guard<std::mutex> l(m_);
    notices_.push_back(std::move(s));
}

std::vector<std::string> Session::takeNotices()
{
    std::lock_guard<std::mutex> l(m_);
    std::vector<std::string> ut;
    ut.swap(notices_);
    return ut;
}

std::size_t Session::inboxHighWater() const
{
    std::lock_guard<std::mutex> l(m_);
    return highWater_;
}

// ---------------------------------------------------------------- läsaren

void Session::readLoop()
{
    std::vector<char> buf(kReadChunk);
    for (;;) {
        const std::ptrdiff_t n = pty_.read(buf.data(), buf.size());
        if (n <= 0)
            break;
        {
            std::unique_lock<std::mutex> l(m_);
            // TAKET: väntar tills ägaren tömt inkorgen till hälften — utom
            // under rivningen, då allt SLÄNGS (session.h, rivningens första steg).
            if (!closing_ && inbox_.size() + static_cast<std::size_t>(n) > kInboxMax)
                inboxCv_.wait(l, [&] { return closing_ || inbox_.size() <= kInboxMax / 2; });
            if (closing_)
                continue;
            inbox_.append(buf.data(), static_cast<std::size_t>(n));
            highWater_ = std::max(highWater_, inbox_.size());
        }
        if (!pending_.exchange(true) && onOutput)
            onOutput();
    }
    {
        std::lock_guard<std::mutex> l(m_);
        readerDone_ = true;
    }
    finishExit();
}

// SLUTET: `onExit` först när processen avslutats
// OCH läsaren nått EOF — då ligger programmets sista utdata redan i inkorgen,
// och ägaren levererar den före sin egen slut-händelse. Den av läsaren och
// väntaren som blir klar sist anropar, en gång.
void Session::finishExit()
{
    bool nu = false;
    {
        std::lock_guard<std::mutex> l(m_);
        nu            = exited_ && readerDone_ && !exitSignalled_;
        exitSignalled_ = exitSignalled_ || nu;
    }
    if (nu && onExit)
        onExit();
}

std::string Session::take(bool& more)
{
    std::string ut;
    {
        std::lock_guard<std::mutex> l(m_);
        const std::size_t n = std::min(kTakeBudget, inbox_.size());
        ut.assign(inbox_, 0, n);
        inbox_.erase(0, n);
        more = !inbox_.empty();
        if (!more)
            pending_ = false;   // under låset: en läsare som lägger till efter oss postar igen
    }
    inboxCv_.notify_all();
    return ut;
}

// -------------------------------------------------------------- skrivaren

void Session::write(std::string_view bytes)
{
    if (bytes.empty())
        return;
    bool kapad = false;
    {
        std::lock_guard<std::mutex> l(m_);
        if (!started_ || closing_ || exited_)
            return;
        const std::size_t plats = kOutboxMax - std::min(kOutboxMax, outbox_.size());
        if (bytes.size() > plats) {
            bytes = bytes.substr(0, plats);
            kapad = !outboxCut_;
            outboxCut_ = true;
        }
        outbox_.append(bytes);
    }
    if (kapad)
        notice("programmet läser inte sin indata — det som inte rymdes i kön (" +
               std::to_string(kOutboxMax >> 20) + " MiB) kapades");
    outboxCv_.notify_one();
}

void Session::writeLoop()
{
    for (;;) {
        std::string ut;
        {
            std::unique_lock<std::mutex> l(m_);
            outboxCv_.wait(l, [&] { return closing_ || !outbox_.empty(); });
            if (closing_)
                return;
            ut.swap(outbox_);
            outboxCut_ = false;
        }
        if (!pty_.write(ut.data(), ut.size()))
            return;
    }
}

void Session::resize(int columns, int rows)
{
    if (started_)
        pty_.resize(columns, rows);
}

// --------------------------------------------------------------- väntaren

void Session::waitLoop()
{
    std::string anm;
    const int   kod = pty_.wait(anm);
    {
        std::lock_guard<std::mutex> l(m_);
        exitCode_ = kod;
        exited_   = true;
        if (!anm.empty())
            notices_.push_back(std::move(anm));
    }
    exitCv_.notify_all();
    // Windows: utdatan tar slut först när pseudokonsolen stängts. Här, i
    // väntartråden, aldrig i läsaren — som tömmer under tiden.
    pty_.closeConsole();
    finishExit();
}

// --------------------------------------------------------------- rivningen

void Session::kill()
{
    if (!started_)
        return;
    {
        std::lock_guard<std::mutex> l(m_);
        if (killed_)
            return;
        killed_  = true;
        closing_ = true;   // 1. läsaren släpper taket och slänger
    }
    inboxCv_.notify_all();
    outboxCv_.notify_all();

    // 2. processkedjan — och på POSIX hela GRUPPEN: huvudprocessens
    // slut är inte gruppens, så livstecknet är gruppens, och SIGKILL går ut
    // också efter skörden.
    pty_.terminate(false);
    {
        const auto                   gräns = std::chrono::steady_clock::now() +
                                           std::chrono::milliseconds(kGraceMs);
        std::unique_lock<std::mutex> l(m_);
        while (!(exited_ && !pty_.alive()) && std::chrono::steady_clock::now() < gräns)
            exitCv_.wait_for(l, std::chrono::milliseconds(kPollMs));
        const bool kvar = !exited_ || pty_.alive();
        l.unlock();
        if (kvar)
            pty_.terminate(true);
    }
    pty_.closeConsole();   // 3. Windows — på den ägande tråden
    pty_.wakeAll();        // 4. säkringen
    for (std::thread* t : {&reader_, &writer_, &waiter_})   // 5.
        if (t->joinable())
            t->join();
}

} // namespace glaze::term
