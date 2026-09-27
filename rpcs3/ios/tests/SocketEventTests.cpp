// Dispatch and connect_followup are extracted verbatim from production.
// Socket ownership and the PPU scheduler are adapters; no guest code runs here.
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <vector>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using s32 = int32_t;
using u32 = uint32_t;
#define ensure(value) do { if (!(value)) { std::fprintf(stderr, "failed: %s (line %d)\n", #value, __LINE__); std::abort(); } } while (false)

template <typename E> struct bs_t
{
    u32 bits = 0;
    bs_t() = default;
    bs_t(E value) : bits(1u << static_cast<u32>(value)) {}
    explicit operator bool() const { return bits != 0; }
    bool operator&(E value) const { return (bits & (1u << static_cast<u32>(value))) != 0; }
    void operator+=(E value) { bits |= 1u << static_cast<u32>(value); }
};
template <typename E> struct atomic_events
{
    std::atomic<u32> bits{};
    bool test_and_reset(E value) { const u32 mask = 1u << static_cast<u32>(value); return (bits.fetch_and(~mask) & mask) != 0; }
    void store(bs_t<E> value) { bits.store(value.bits); }
    void add(E value) { bits.fetch_or(1u << static_cast<u32>(value)); }
};
constexpr int SYS_NET_SOCK_STREAM = 1, SYS_NET_SOCK_DGRAM = 2;
struct network_context { std::atomic<u32> num_polls{}; } context;
struct fxo_adapter { template<typename T> T& get() { return context; } } fxo;
auto* g_fxo = &fxo;

class lv2_socket
{
public:
    enum class poll_t { read, write, error };
    std::mutex mutex;
    atomic_events<poll_t> events;
    std::vector<std::pair<int, std::function<bool(bs_t<poll_t>)>>> queue;
    int type = SYS_NET_SOCK_STREAM;
    int so_rcvtimeo = 0, so_sendtimeo = 0;
    void handle_events(const pollfd&, bool = false);
    void enqueue(poll_t interest, std::function<bool(bs_t<poll_t>)> callback)
    {
        events.add(interest);
        queue.emplace_back(0, std::move(callback));
        context.num_polls++;
    }
};
#include "SocketEvents.inc"

// PS3 ECONNREFUSED is 61. Keep translation observable instead of treating
// a wake-up as a successful connection.
s32 convert_error(bool, int error) { ensure(error == ECONNREFUSED); return 61; }
class lv2_socket_native : public lv2_socket
{
public:
    int native_socket = -1;
    s32 connect_followup();
};
#include "SocketConnect.inc"

void dispatch_cases()
{
    using event = lv2_socket::poll_t;
    unsigned cases = 0;
    // All combinations of terminal/readiness notifications and subscribed
    // events. Each callback must run once, and only subscribed bits may fire.
    for (int mask = 0; mask < 16; ++mask)
    {
        const short returned = static_cast<short>((mask & 1 ? POLLIN : 0) | (mask & 2 ? POLLOUT : 0) |
                                                  (mask & 4 ? POLLERR : 0) | (mask & 8 ? POLLHUP : 0));
        for (int subscribed = 1; subscribed < 8; ++subscribed)
        {
            lv2_socket socket;
            unsigned observed = 0;
            for (unsigned bit = 0; bit < 3; ++bit)
            {
                if (!(subscribed & (1 << bit))) continue;
                const auto interest = static_cast<event>(bit);
                socket.enqueue(interest, [&, interest, bit](bs_t<event> ready)
                {
                    if (!(ready & interest)) return false;
                    ensure(!(observed & (1 << bit)));
                    observed |= 1 << bit;
                    return true;
                });
            }
            socket.handle_events({-1, 0, returned});
            const unsigned expected = subscribed & ((mask & 13 ? 1 : 0) | (mask & 14 ? 2 : 0) | (mask & 4 ? 4 : 0));
            ensure(observed == expected);
            ensure(context.num_polls == socket.queue.size());
            socket.handle_events({-1, 0, returned});
            ensure(observed == expected);
            context.num_polls = 0;
            ++cases;
        }
    }
    // A callback that would block again must retain its queue accounting and
    // re-arm normally; a later terminal event must complete it.
    lv2_socket socket;
    unsigned calls = 0;
    socket.enqueue(event::write, [&](bs_t<event> ready)
    {
        ensure(ready & event::write);
        if (++calls == 1) { socket.events.add(event::write); return false; }
        return true;
    });
    socket.handle_events({-1, POLLOUT, POLLOUT});
    ensure(calls == 1 && context.num_polls == 1);
    socket.handle_events({-1, POLLOUT, POLLHUP});
    ensure(calls == 2 && context.num_polls == 0 && socket.queue.empty());
    std::printf("%u socket dispatch cases plus rearm/accounting passed\n", cases);
}

void loopback_connect(bool listening, bool asynchronous)
{
    using event = lv2_socket::poll_t;
    const int server = ::socket(AF_INET, SOCK_STREAM, 0);
    ensure(server >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ensure(::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t length = sizeof(address);
    ensure(::getsockname(server, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    if (listening) ensure(::listen(server, 1) == 0);
    // Darwin does not immediately refuse a port while a non-listening socket
    // remains bound to it. Release the chosen port for the refused case.
    if (!listening) ::close(server);
    lv2_socket_native socket;
    socket.native_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    ensure(socket.native_socket >= 0);
    ensure(::fcntl(socket.native_socket, F_SETFL, O_NONBLOCK) == 0);
    const int connected = ::connect(socket.native_socket, reinterpret_cast<sockaddr*>(&address), length);
    ensure(connected == 0 || errno == EINPROGRESS);
    bool awake = false;
    s32 result = 999;
    socket.enqueue(event::write, [&](bs_t<event> ready)
    {
        if (!(ready & event::write)) return false;
        const s32 completed = socket.connect_followup();
        // Blocking calls return -errno; asynchronous completion caches +errno.
        result = asynchronous ? -completed : completed;
        awake = true;
        return true;
    });
    pollfd descriptor{socket.native_socket, POLLOUT, 0};
    ensure(::poll(&descriptor, 1, 1000) == 1);
    socket.handle_events(descriptor);
    ensure(awake);
    ensure(result == (listening ? 0 : asynchronous ? 61 : -61));
    ensure(context.num_polls == 0 && socket.queue.empty());
    std::printf("loopback %s %s: revents=0x%x, result=%d, waiter completed\n",
        listening ? "accepted" : "refused", asynchronous ? "async" : "blocking", descriptor.revents, result);
    ::close(socket.native_socket);
    if (listening) ::close(server);
}

int main(int argc, char**)
{
    if (argc > 1)
        for (bool listening : {false, true})
            for (bool asynchronous : {false, true}) loopback_connect(listening, asynchronous);
    dispatch_cases();
}
