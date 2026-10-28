// Exercise production packet IO and accept/reconnect using real host sockets.
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <system_error>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usz = size_t;
static void check(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "%s\n", message); std::abort(); }
}
namespace fmt
{
	template <class... T> [[noreturn]] void throw_exception(const char* text, T...)
	{
		std::fprintf(stderr, "fatal exception: %s\n", text);
		std::abort();
	}
}
struct logger
{
	template <class... T> void error(const char*, T...) {}
	template <class... T> void warning(const char*, T...) {}
	template <class... T> void trace(const char*, T...) {}
	template <class... T> void success(const char*, T...) {}
} GDB;
enum class thread_state { running, aborting };
static std::atomic_bool aborting = false;
namespace thread_ctrl
{
	thread_state state() { return aborting ? thread_state::aborting : thread_state::running; }
	void wait_for(u64) { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
	void wait_until(u64*, u64 delay) { wait_for(delay); }
}
u64 get_system_time() { return 0; }
static std::atomic_int sockets_closed = 0;
int closesocket(int socket) { ++sockets_closed; return ::close(socket); }
void set_nonblocking(int socket) { check(fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK) == 0, "nonblocking setup"); }
bool check_errno_again() { return errno == EAGAIN || errno == EWOULDBLOCK; }
struct emulator
{
	bool IsRunning() const { return false; }
	void Pause() { check(false, "unexpected emulation lifecycle mutation"); }
} Emu;
struct gdb_cmd { std::string cmd, data; u8 checksum = 0; };
u8 hex_to_u8(std::string_view text)
{
	u8 value = 0;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
	if (result.ec != std::errc{}) fmt::throw_exception("bad hex");
	return value;
}
#include "GDBClassUnderTest.inc"
#define PROCESS_CMD(cmds, handler) if (cmd.cmd == cmds) { if (!handler(cmd)) break; else continue; }
#include "GDBMethodsUnderTest.inc"

struct socket_pair
{
	int fd[2];
	socket_pair() { check(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0, "socketpair"); set_nonblocking(fd[0]); }
	~socket_pair() { ::close(fd[0]); ::close(fd[1]); }
	void input(std::string_view bytes)
	{
		if (!bytes.empty()) check(::send(fd[1], bytes.data(), bytes.size(), 0) == static_cast<ssize_t>(bytes.size()), "fixture write");
		check(::shutdown(fd[1], SHUT_WR) == 0, "fixture EOF");
	}
	std::string output()
	{
		char bytes[1024];
		const auto count = recv(fd[1], bytes, sizeof(bytes), MSG_DONTWAIT);
		return count > 0 ? std::string(bytes, count) : "";
	}
};
static int cases = 0;
static std::string packet(std::string_view payload)
{
	std::string encoded;
	u8 sum = 0;
	for (unsigned char c : payload)
	{
		if (c == '#' || c == '$' || c == '}') { encoded += '}'; sum += '}'; c ^= 0x20; }
		encoded += static_cast<char>(c);
		sum += c;
	}
	constexpr char hex[] = "0123456789abcdef";
	return "$" + encoded + "#" + hex[sum >> 4] + hex[sum & 15];
}
static void read_case(std::string_view wire, bool valid, std::string_view cmd = "", std::string_view data = "", std::string_view ack = "")
{
	socket_pair sockets;
	sockets.input(wire);
	gdb_thread reader;
	reader.client_socket = sockets.fd[0];
	gdb_cmd parsed;
	check(reader.read_cmd(parsed) == valid, "packet completion result");
	if (valid) check(parsed.cmd == cmd && parsed.data == data, "decoded packet changed");
	check(sockets.output() == ack, "ACK emitted after EOF or checksum handling changed");
	++cases;
}
static void reconnect_case()
{
	gdb_thread server;
	server.server_socket = socket(AF_INET, SOCK_STREAM, 0);
	check(server.server_socket >= 0, "listen socket");
	set_nonblocking(server.server_socket);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	check(bind(server.server_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind loopback");
	check(listen(server.server_socket, 1) == 0, "listen");
	socklen_t length = sizeof(address);
	check(getsockname(server.server_socket, reinterpret_cast<sockaddr*>(&address), &length) == 0, "socket address");
	sockets_closed = 0;
	std::thread worker([&] { server(); });
	// Empty connection, truncated packet and valid packet on three successive clients.
	for (const std::string& input : {std::string{}, std::string{"$qSupp"}, std::string{"$qSupported#37+"}})
	{
		const int client = socket(AF_INET, SOCK_STREAM, 0);
		check(client >= 0, "client socket");
		check(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "reconnect");
		if (!input.empty()) check(::send(client, input.data(), input.size(), 0) == static_cast<ssize_t>(input.size()), "client input");
		shutdown(client, SHUT_WR);
		char buffer[1024];
		std::string output;
		for (ssize_t n; (n = recv(client, buffer, sizeof(buffer), 0)) > 0;) output.append(buffer, n);
		check(output == (input.size() > 10 ? "+$PacketSize=1200#f3" : ""), "reconnected protocol reply");
		::close(client);
		++cases;
	}
	aborting = true;
	worker.join();
	aborting = false;
	check(server.client_socket == -1 && sockets_closed == 3, "client descriptors leaked");
	::close(server.server_socket);
}
int main()
{
	alarm(15); // A disconnect must never turn into a retry/spin hang.
	std::signal(SIGPIPE, SIG_IGN);
	read_case("", false); // Reproduces the reported client-connect/EOF fatal.
	read_case("$qSupported#37", true, "qSupported", "", "+");
	read_case("+$qC#b4", true, "qC", "", "+");
	read_case(std::string(1, '\x03'), true, std::string(1, '\x03'), "", "+");
	read_case("$m0,4#fd", true, "m", "0,4", "+");
	read_case(packet("X0,3:#$}"), true, "X", "0,3:#$}", "+");
	read_case("$qC#00$qC#b4", true, "qC", "", "-+");
	read_case("$qC#00", false, "", "", "-");
	read_case("!", false);
	read_case("$qC#zz", false);
	const auto encoded = packet("X0,3:#$}");
	for (size_t length = 1; length < encoded.size(); ++length) read_case(encoded.substr(0, length), false);
	for (const std::string& acknowledgement : {std::string{}, std::string{"+"}, std::string{"-+"}, std::string{"?"}})
	{
		socket_pair sockets;
		sockets.input(acknowledgement);
		gdb_thread writer;
		writer.client_socket = sockets.fd[0];
		check(writer.send_cmd_ack("OK") == (acknowledgement == "+" || acknowledgement == "-+"), "response ACK EOF/retry");
		++cases;
	}
	{
		socket_pair sockets;
		sockets.input("");
		gdb_thread reader;
		reader.client_socket = sockets.fd[0];
		check(!reader.wait_with_interrupts(), "continue-wait EOF");
		++cases;
	}
	{
		socket_pair sockets;
		sockets.input(std::string(1, '\x03'));
		gdb_thread reader;
		reader.client_socket = sockets.fd[0];
		check(reader.wait_with_interrupts() && reader.paused, "interrupt behavior changed");
		++cases;
	}
	{
		gdb_thread reader;
		reader.client_socket = -1;
		gdb_cmd cmd;
		check(!reader.read_cmd(cmd), "socket error must end session");
		aborting = true;
		check(!reader.read_cmd(cmd), "shutdown must end session");
		check(!reader.wait_with_interrupts(), "shutdown must end interrupt wait");
		aborting = false;
		cases += 3;
	}
	{
		socket_pair sockets;
		gdb_thread reader;
		reader.client_socket = sockets.fd[0];
		std::thread delayed([&] {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			sockets.input("$qC#b4");
		});
		gdb_cmd cmd;
		check(reader.read_cmd(cmd) && cmd.cmd == "qC", "would-block retry lost packet");
		delayed.join();
		check(sockets.output() == "+", "delayed packet ACK");
		++cases;
	}
	{
		socket_pair sockets;
		gdb_thread reader;
		reader.client_socket = sockets.fd[0];
		std::thread shutdown([&] {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			aborting = true;
		});
		gdb_cmd cmd;
		check(!reader.read_cmd(cmd), "shutdown did not end would-block read");
		shutdown.join();
		aborting = false;
		check(sockets.output().empty(), "shutdown emitted ACK");
		++cases;
	}
	reconnect_case();
	std::printf("GDB disconnect tests: %d cases passed\n", cases);
}
