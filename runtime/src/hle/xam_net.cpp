// xam.xex networking (NetDll_*). Xbox Live is gone. Titles still need a working socket layer to host
// their own games (UE3 campaigns are listen servers for co-op), so by default sockets are emulated
// locally: they bind and accept options, sends are discarded and nothing is ever received, as on a
// console with no other machine on the network.
//
// With System Link on ([network] enabled in settings.toml, x360/system_link.h) the sockets are host
// sockets instead, the console has an Ethernet link and an address on the chosen adapter, and
// XNADDRs translate to the addresses peers are reached at. Each function below starts with that
// branch; the rest is the offline behaviour, unchanged.
#include <algorithm>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>

#include "x360/kernel.h"
#include "x360/system_link.h"

using namespace x360::kernel;

namespace {

constexpr int32_t kSocketError = -1;
constexpr uint32_t kWsaENetDown = 10050;
thread_local uint32_t t_wsa_error = 0;

// System Link's stack, when it started (the host starts it before the title runs).
x360::net::Stack* Link() {
  static x360::net::Stack* link = x360::net::Global().started() ? &x360::net::Global() : nullptr;
  return link;
}

// ANYX360_NET_TRACE: a line per XNet call (and, in the stack, per datagram).
bool NetTrace() {
  static const bool on = std::getenv("ANYX360_NET_TRACE") != nullptr;
  return on;
}
#define NET_TRACE(...) \
  do {                 \
    if (NetTrace()) KLOG_I(__VA_ARGS__); \
  } while (0)

// Socket calls on the host stack: its WSA error is the guest's.
int32_t Linked(int32_t result) {
  if (result < 0) t_wsa_error = x360::net::Stack::LastError();
  return result;
}

// sockaddr_in: be16 family, be16 port, be32 address, 8 bytes of zeros.
x360::net::Stack::Address LoadAddress(uint32_t name) {
  return {Load<uint32_t>(name + 4), Load<uint16_t>(name + 2)};
}
void StoreAddress(uint32_t name, uint32_t length_ptr, const x360::net::Stack::Address& address) {
  if (name) {
    std::memset(Ptr(name), 0, 16);
    Store<uint16_t>(name, 2);  // AF_INET
    Store<uint16_t>(name + 2, address.port);
    Store<uint32_t>(name + 4, address.ip);
  }
  if (length_ptr) Store<uint32_t>(length_ptr, 16);
}

// NetDll functions take the caller type as their first argument.
uint32_t NetDll_WSAStartup(uint32_t caller, uint32_t version, uint32_t data) {
  (void)caller;
  if (data) {
    std::memset(Ptr(data), 0, 0x190);
    Store<uint16_t>(data + 0, uint16_t(version));
    Store<uint16_t>(data + 2, 0x0202);
  }
  return 0;
}
uint32_t NetDll_WSACleanup(uint32_t caller) {
  (void)caller;
  return 0;
}
uint32_t NetDll_XNetStartup(uint32_t caller, uint32_t params) {
  (void)caller, (void)params;
  return 0;
}
uint32_t NetDll_XNetCleanup(uint32_t caller, uint32_t params) {
  (void)caller, (void)params;
  return 0;
}
uint32_t NetDll_WSAGetLastError() { return t_wsa_error; }
void NetDll_WSASetLastError(uint32_t error) { t_wsa_error = error; }
uint32_t NetDll_WSACreateEvent() {
  uint32_t handle = 0;
  uint32_t body = SystemAlloc(0x10);
  InitializeDispatcher(body, kEventNotification, 0);
  handle = CreateHandle(std::make_shared<DispatcherObject>(body));
  return handle;
}
// WSAResetEvent and WSACloseEvent return TRUE, or FALSE with the error for WSAGetLastError.
uint32_t NetDll_WSAResetEvent(uint32_t handle) {
  uint32_t body = ObjectFromHandle(handle);
  if (!body) {
    t_wsa_error = X_ERROR_INVALID_HANDLE;
    return 0;
  }
  ResetEvent(body);
  return 1;
}
uint32_t NetDll_WSACloseEvent(uint32_t handle) {
  if (!CloseHandle(handle)) {
    t_wsa_error = X_ERROR_INVALID_HANDLE;
    return 0;
  }
  return 1;
}

uint32_t NetDll_XNetGetEthernetLinkStatus(uint32_t caller) {
  (void)caller;
  if (Link()) return 0x01 | 0x02 | 0x08;  // active, 100 Mbps, full duplex
  return 0;  // no link
}

// XNADDR: ina, inaOnline, wPortOnline, abEnet[6], abOnline[20]. A loopback address and a fixed MAC,
// as Xenia reports (titles derive "random" ids from abEnet, so it must not be zero).
uint32_t NetDll_XNetGetTitleXnAddr(uint32_t caller, uint32_t addr) {
  (void)caller;
  if (x360::net::Stack* link = Link()) {
    // The adapter's address, and abEnet naming this PC's instance.
    if (addr) x360::net::WriteXnAddr(Ptr(addr), link->own_ip(), link->instance());
    return 0x02 | 0x04;  // XNET_GET_XNADDR_ETHERNET | XNET_GET_XNADDR_STATIC
  }
  if (std::getenv("ANYX360_NO_SOCKETS")) {  // debugging switch: no network address
    if (addr) std::memset(Ptr(addr), 0, 0x24);
    return 1;  // XNET_GET_XNADDR_NONE
  }
  if (addr) {
    std::memset(Ptr(addr), 0, 0x24);
    Store<uint32_t>(addr, 0x7F000001);
    std::memset(Ptr(addr + 10), 0xCC, 6);
  }
  return 4;  // XNET_GET_XNADDR_STATIC
}

// ---- local sockets ----------------------------------------------------------------------------

constexpr uint32_t kWsaEWouldBlock = 10035;
constexpr uint32_t kWsaENotSock = 10038;
constexpr uint32_t kWsaEConnRefused = 10061;

struct Socket {
  uint32_t type = 0;  // 1 stream, 2 datagram
  uint16_t port = 0;
};

std::mutex g_socket_mutex;
std::unordered_map<uint32_t, Socket> g_sockets;
uint32_t g_next_socket = 0x100;
uint16_t g_next_port = 49152;

int32_t Error(uint32_t code) {
  t_wsa_error = code;
  return kSocketError;
}

bool IsSocket(uint32_t s) {
  std::lock_guard lock(g_socket_mutex);
  return g_sockets.count(s) != 0;
}

int32_t NetDll_socket(uint32_t caller, uint32_t af, uint32_t type, uint32_t protocol) {
  (void)caller, (void)af, (void)protocol;
  if (x360::net::Stack* link = Link()) {
    int32_t s = Linked(link->Socket(af, type, protocol));
    NET_TRACE("system link: socket(%u, type %u, protocol %u) = %d", af, type, protocol, s);
    return s;
  }
  static const bool disabled = std::getenv("ANYX360_NO_SOCKETS") != nullptr;  // debugging switch
  if (disabled) return Error(10050);  // WSAENETDOWN
  std::lock_guard lock(g_socket_mutex);
  uint32_t s = g_next_socket;
  g_next_socket += 4;
  g_sockets[s] = Socket{type, 0};
  return int32_t(s);
}

int32_t NetDll_closesocket(uint32_t caller, uint32_t s) {
  (void)caller;
  if (x360::net::Stack* link = Link()) return Linked(link->Close(s));
  std::lock_guard lock(g_socket_mutex);
  return g_sockets.erase(s) ? 0 : Error(kWsaENotSock);
}

// sockaddr_in: be16 family, be16 port, be32 address.
int32_t NetDll_bind(uint32_t caller, uint32_t s, uint32_t name, uint32_t length) {
  (void)caller, (void)length;
  if (x360::net::Stack* link = Link()) return Linked(link->Bind(s, name ? LoadAddress(name) : x360::net::Stack::Address{}));
  std::lock_guard lock(g_socket_mutex);
  auto it = g_sockets.find(s);
  if (it == g_sockets.end()) return Error(kWsaENotSock);
  uint16_t port = name ? Load<uint16_t>(name + 2) : 0;
  it->second.port = port ? port : g_next_port++;
  return 0;
}

int32_t NetDll_connect(uint32_t caller, uint32_t s, uint32_t name, uint32_t length) {
  (void)caller, (void)name, (void)length;
  if (x360::net::Stack* link = Link()) {
    if (!name) return Error(10014);  // WSAEFAULT
    return Linked(link->Connect(s, LoadAddress(name)));
  }
  std::lock_guard lock(g_socket_mutex);
  auto it = g_sockets.find(s);
  if (it == g_sockets.end()) return Error(kWsaENotSock);
  return it->second.type == 2 ? 0 : Error(kWsaEConnRefused);  // nobody to connect to
}

int32_t NetDll_listen(uint32_t caller, uint32_t s, uint32_t backlog) {
  (void)caller, (void)backlog;
  if (x360::net::Stack* link = Link()) return Linked(link->Listen(s, int(backlog)));
  return IsSocket(s) ? 0 : Error(kWsaENotSock);
}

int32_t NetDll_accept(uint32_t caller, uint32_t s, uint32_t addr, uint32_t length) {
  (void)caller, (void)addr, (void)length;
  if (x360::net::Stack* link = Link()) {
    x360::net::Stack::Address peer;
    int32_t accepted = Linked(link->Accept(s, &peer));
    if (accepted >= 0) StoreAddress(addr, length, peer);
    return accepted;
  }
  return Error(IsSocket(s) ? kWsaEWouldBlock : kWsaENotSock);
}

int32_t NetDll_send(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags) {
  (void)caller, (void)buffer, (void)flags;
  if (x360::net::Stack* link = Link()) return Linked(link->SendTo(s, Ptr(buffer), length, flags, nullptr));
  return IsSocket(s) ? int32_t(length) : Error(kWsaENotSock);
}

int32_t NetDll_sendto(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags,
                      uint32_t to, uint32_t to_length) {
  (void)caller, (void)buffer, (void)flags, (void)to, (void)to_length;
  if (x360::net::Stack* link = Link()) {
    x360::net::Stack::Address address;
    if (to) address = LoadAddress(to);
    return Linked(link->SendTo(s, Ptr(buffer), length, flags, to ? &address : nullptr));
  }
  return IsSocket(s) ? int32_t(length) : Error(kWsaENotSock);
}

int32_t NetDll_recv(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags) {
  (void)caller, (void)buffer, (void)length, (void)flags;
  if (x360::net::Stack* link = Link()) return Linked(link->RecvFrom(s, Ptr(buffer), length, flags, nullptr));
  return Error(IsSocket(s) ? kWsaEWouldBlock : kWsaENotSock);
}

int32_t NetDll_recvfrom(uint32_t caller, uint32_t s, uint32_t buffer, uint32_t length, uint32_t flags,
                        uint32_t from, uint32_t from_length) {
  (void)caller, (void)buffer, (void)length, (void)flags, (void)from, (void)from_length;
  if (x360::net::Stack* link = Link()) {
    x360::net::Stack::Address address;
    int32_t received = Linked(link->RecvFrom(s, Ptr(buffer), length, flags, &address));
    // A datagram larger than the buffer still names its sender (WSAEMSGSIZE).
    if (received >= 0 || t_wsa_error == 10040) StoreAddress(from, from_length, address);
    return received;
  }
  return Error(IsSocket(s) ? kWsaEWouldBlock : kWsaENotSock);
}

// fd_set: be32 count, be32 sockets[64]. Nothing is ever readable; every socket is writable.
int32_t NetDll_select(uint32_t caller, uint32_t nfds, uint32_t readfds, uint32_t writefds,
                      uint32_t exceptfds, uint32_t timeout) {
  (void)caller, (void)nfds, (void)timeout;
  if (x360::net::Stack* link = Link()) {
    auto load = [](uint32_t set, std::vector<uint32_t>& out) {
      if (!set) return;
      uint32_t count = std::min<uint32_t>(Load<uint32_t>(set), 64);
      for (uint32_t i = 0; i < count; ++i) out.push_back(Load<uint32_t>(set + 4 + i * 4));
    };
    auto store = [](uint32_t set, const std::vector<uint32_t>& sockets) {
      if (!set) return;
      Store<uint32_t>(set, uint32_t(sockets.size()));
      for (size_t i = 0; i < sockets.size(); ++i) Store<uint32_t>(set + 4 + uint32_t(i) * 4, sockets[i]);
    };
    std::vector<uint32_t> sets[3];
    load(readfds, sets[0]), load(writefds, sets[1]), load(exceptfds, sets[2]);
    // timeval: be32 seconds, be32 microseconds; none waits for ever.
    int64_t timeout_us = timeout ? int64_t(Load<uint32_t>(timeout)) * 1000000 + Load<uint32_t>(timeout + 4) : -1;
    int32_t ready = Linked(link->Select(readfds ? &sets[0] : nullptr, writefds ? &sets[1] : nullptr,
                                        exceptfds ? &sets[2] : nullptr, timeout_us));
    if (ready >= 0) store(readfds, sets[0]), store(writefds, sets[1]), store(exceptfds, sets[2]);
    return ready;
  }
  if (readfds) Store<uint32_t>(readfds, 0);
  if (exceptfds) Store<uint32_t>(exceptfds, 0);
  return writefds ? int32_t(Load<uint32_t>(writefds)) : 0;
}

int32_t NetDll_setsockopt(uint32_t caller, uint32_t s, uint32_t level, uint32_t name, uint32_t value,
                          uint32_t length) {
  (void)caller, (void)level, (void)name, (void)value, (void)length;
  if (x360::net::Stack* link = Link()) {
    NET_TRACE("system link: setsockopt(%u, level %X, option %X, %u bytes: %08X)", s, level, name, length,
              value && length >= 4 ? Load<uint32_t>(value) : 0u);
    return Linked(link->SetOption(s, level, name, value ? Ptr(value) : nullptr, length));
  }
  return IsSocket(s) ? 0 : Error(kWsaENotSock);
}

int32_t NetDll_getsockopt(uint32_t caller, uint32_t s, uint32_t level, uint32_t name, uint32_t value,
                          uint32_t length_ptr) {
  (void)caller, (void)level, (void)name;
  if (x360::net::Stack* link = Link()) {
    uint32_t v = 0;
    int32_t result = Linked(link->GetOption(s, level, name, v));
    if (result == 0 && value && length_ptr && Load<uint32_t>(length_ptr) >= 4) {
      Store<uint32_t>(value, v);
      Store<uint32_t>(length_ptr, 4);
    }
    return result;
  }
  if (!IsSocket(s)) return Error(kWsaENotSock);
  if (value && length_ptr && Load<uint32_t>(length_ptr) >= 4) Store<uint32_t>(value, 0x10000);
  return 0;
}

int32_t NetDll_getsockname(uint32_t caller, uint32_t s, uint32_t name, uint32_t length_ptr) {
  (void)caller;
  if (x360::net::Stack* link = Link()) {
    x360::net::Stack::Address address;
    int32_t result = Linked(link->GetName(s, address));
    if (result == 0) StoreAddress(name, length_ptr, address);
    return result;
  }
  std::lock_guard lock(g_socket_mutex);
  auto it = g_sockets.find(s);
  if (it == g_sockets.end()) return Error(kWsaENotSock);
  if (name) {
    std::memset(Ptr(name), 0, 16);
    Store<uint16_t>(name, 2);  // AF_INET
    Store<uint16_t>(name + 2, it->second.port);
    Store<uint32_t>(name + 4, 0x7F000001);
  }
  if (length_ptr) Store<uint32_t>(length_ptr, 16);
  return 0;
}

int32_t NetDll_ioctlsocket(uint32_t caller, uint32_t s, uint32_t command, uint32_t argument) {
  (void)caller;
  if (x360::net::Stack* link = Link()) {
    uint32_t v = argument ? Load<uint32_t>(argument) : 0;
    int32_t result = Linked(link->IoControl(s, command, v));
    if (result == 0 && argument) Store<uint32_t>(argument, v);
    return result;
  }
  if (!IsSocket(s)) return Error(kWsaENotSock);
  if (command == 0x4004667F && argument) Store<uint32_t>(argument, 0);  // FIONREAD: nothing pending
  return 0;
}

uint32_t NetDll_XNetRandom(uint32_t caller, uint32_t buffer, uint32_t length) {
  (void)caller;
  static std::mt19937 rng{std::random_device{}()};
  for (uint32_t i = 0; i < length; ++i) Store<uint8_t>(buffer + i, uint8_t(rng()));
  return 0;
}

// Unlike most NetDll exports, inet_addr has no XNCALLER argument: the string is the first argument.
uint32_t NetDll_inet_addr(uint32_t cp) {
  unsigned a, b, c, d;
  if (!cp || std::sscanf(Ptr<const char>(cp), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0xFFFFFFFF;
  return (a << 24) | (b << 16) | (c << 8) | d;
}

int32_t NetDll_shutdown(uint32_t caller, uint32_t s, uint32_t how) {
  (void)caller, (void)how;
  if (x360::net::Stack* link = Link()) return Linked(link->Shutdown(s, int(how)));
  return IsSocket(s) ? 0 : Error(kWsaENotSock);
}

// No stream connection ever succeeds, so no socket has a peer.
int32_t NetDll_getpeername(uint32_t caller, uint32_t s, uint32_t name, uint32_t length_ptr) {
  (void)caller, (void)name, (void)length_ptr;
  if (x360::net::Stack* link = Link()) {
    x360::net::Stack::Address address;
    int32_t result = Linked(link->GetPeerName(s, address));
    if (result == 0) StoreAddress(name, length_ptr, address);
    return result;
  }
  return Error(IsSocket(s) ? 10057u : kWsaENotSock);  // WSAENOTCONN
}

// __WSAFDIsSet(socket, fd_set): the macro FD_ISSET (no XNCALLER argument).
int32_t NetDll___WSAFDIsSet(uint32_t s, uint32_t set) {
  uint32_t count = std::min<uint32_t>(Load<uint32_t>(set), 64);
  for (uint32_t i = 0; i < count; ++i)
    if (Load<uint32_t>(set + 4 + i * 4) == s) return 1;
  return 0;
}

// XNetInAddrToString(caller, in_addr, buffer, size): dotted quad.
uint32_t NetDll_XNetInAddrToString(uint32_t caller, uint32_t address, uint32_t buffer, uint32_t size) {
  (void)caller;
  char text[16];
  int length = std::snprintf(text, sizeof(text), "%u.%u.%u.%u", address >> 24, (address >> 16) & 0xFF,
                             (address >> 8) & 0xFF, address & 0xFF);
  if (!buffer || size <= uint32_t(length)) return 0x80070000u | 122;  // ERROR_INSUFFICIENT_BUFFER
  std::memcpy(Ptr(buffer), text, size_t(length) + 1);
  return 0;
}

// Machine IDs belong to Xbox Live addresses.
uint32_t NetDll_XNetXnAddrToMachineId() { return 1; }

uint32_t NetDll_XNetGetConnectStatus(uint32_t caller, uint32_t address) {
  (void)caller, (void)address;
  NET_TRACE("system link: XNetGetConnectStatus(%08X)", address);
  if (Link()) return 2;  // XNET_CONNECT_STATUS_CONNECTED: there is no key exchange to wait for
  return 0;  // XNET_CONNECT_STATUS_IDLE
}

uint32_t NetDll_ErrorStatus() { return 0x80070000u | kWsaENetDown; }

// ---- System Link: addresses and keys ----------------------------------------------------------

std::mutex g_link_mutex;
uint8_t g_registered_key[8] = {};  // the last session key registered, for XNetInAddrToXnAddr
std::set<uint64_t> g_logged_peers;

void LogPeerOnce(const char* what, const x360::net::Peer& peer, uint32_t guest_ip) {
  uint64_t key = uint64_t(peer.ip) << 8 | uint32_t(peer.instance);
  {
    std::lock_guard lock(g_link_mutex);
    if (!g_logged_peers.insert(key).second) return;
  }
  KLOG_I("system link: %s %s instance %d is %s for the game", what, x360::net::IpText(peer.ip).c_str(), peer.instance,
         x360::net::IpText(guest_ip).c_str());
}

// XNetXnAddrToInAddr(caller, XNADDR*, XNKID*, IN_ADDR*): the address the title reaches a peer at.
uint32_t NetDll_XNetXnAddrToInAddr(uint32_t caller, uint32_t xnaddr, uint32_t key_id, uint32_t in_addr) {
  (void)caller, (void)key_id;
  x360::net::Stack* link = Link();
  if (!link) return NetDll_ErrorStatus();
  if (!xnaddr || !in_addr) return 0x80070057;  // E_INVALIDARG
  x360::net::Peer peer = x360::net::ReadXnAddr(Ptr(xnaddr));
  uint32_t guest_ip = link->book().GuestAddress(peer);
  Store<uint32_t>(in_addr, guest_ip);
  NET_TRACE("system link: XNetXnAddrToInAddr(%s instance %d) = %08X", x360::net::IpText(peer.ip).c_str(), peer.instance,
            guest_ip);
  LogPeerOnce("XNADDR", peer, guest_ip);
  return 0;
}

// XNetInAddrToXnAddr(caller, IN_ADDR, XNADDR*, XNKID*): back to the peer's XNADDR (and the session).
uint32_t NetDll_XNetInAddrToXnAddr(uint32_t caller, uint32_t in_addr, uint32_t xnaddr, uint32_t key_id) {
  (void)caller;
  x360::net::Stack* link = Link();
  if (!link) return NetDll_ErrorStatus();
  x360::net::Peer peer = link->book().Resolve(in_addr);
  if (in_addr == link->own_guest_ip()) peer = {link->own_ip(), link->instance()};
  NET_TRACE("system link: XNetInAddrToXnAddr(%08X) = %s instance %d", in_addr, x360::net::IpText(peer.ip).c_str(),
            peer.instance);
  if (xnaddr) x360::net::WriteXnAddr(Ptr(xnaddr), peer.ip, peer.instance);
  if (key_id) {
    std::lock_guard lock(g_link_mutex);
    std::memcpy(Ptr(key_id), g_registered_key, 8);
  }
  return 0;
}

// XNetCreateKey(caller, XNKID*, XNKEY*): a system link session's ID and key exchange key.
uint32_t NetDll_XNetCreateKey(uint32_t caller, uint32_t key_id, uint32_t key) {
  (void)caller;
  if (!Link()) return 0;  // as the export's stub: nothing written
  NET_TRACE("system link: XNetCreateKey");
  static std::mt19937_64 rng{std::random_device{}()};
  std::lock_guard lock(g_link_mutex);
  if (key_id) x360::net::MakeSystemLinkKeyId(Ptr(key_id), rng());
  if (key)
    for (int i = 0; i < 16; i += 8) Store<uint64_t>(key + i, rng());
  return 0;
}

// A session's key (XNKID, XNKEY) is registered with the console's own network stack, which works
// without a network: hosts register their session's key, local games included. As in Xenia.
uint32_t NetDll_XNetRegisterKey(uint32_t caller, uint32_t key_id, uint32_t exchange_key) {
  (void)caller, (void)key_id, (void)exchange_key;
  NET_TRACE("system link: XNetRegisterKey(%016llX)", key_id ? (unsigned long long)Load<uint64_t>(key_id) : 0ull);
  if (Link() && key_id) {
    std::lock_guard lock(g_link_mutex);
    std::memcpy(g_registered_key, Ptr(key_id), 8);
  }
  return 0;
}
uint32_t NetDll_XNetUnregisterKey(uint32_t caller, uint32_t key_id) {
  (void)caller, (void)key_id;
  return 0;
}

// Connecting to a peer is the secure association's key exchange: nothing to do between AnyX360s.
uint32_t NetDll_XNetConnect(uint32_t caller, uint32_t address) {
  (void)caller, (void)address;
  NET_TRACE("system link: XNetConnect(%08X)", address);
  return Link() ? 0 : NetDll_ErrorStatus();
}
uint32_t NetDll_XNetUnregisterInAddr(uint32_t caller, uint32_t address) {
  (void)caller, (void)address;
  return Link() ? 0 : NetDll_ErrorStatus();
}
uint32_t NetDll_XNetSetSystemLinkPort(uint32_t caller, uint32_t port) {
  (void)caller;
  if (!Link()) return NetDll_ErrorStatus();
  KLOG_I("system link: the title's system link port is %u", port & 0xFFFF);
  return 0;
}

// QoS listening: a host registers the data its session's QoS probes answer with (by the session's
// key), in the console's own network stack, which works without a network. Black Ops II's local
// lobby stops with an error when it fails. Nothing ever probes it here.
uint32_t NetDll_XNetQosListen(uint32_t caller, uint32_t key_id, uint32_t data, uint32_t size, uint32_t bits_per_sec,
                              uint32_t flags) {
  (void)caller, (void)key_id, (void)data, (void)size, (void)bits_per_sec, (void)flags;
  NET_TRACE("system link: XNetQosListen(key %016llX, %u bytes, flags %X)",
            key_id ? (unsigned long long)Load<uint64_t>(key_id) : 0ull, size, flags);
  return 0;
}

// The QoS service lookup (Xbox Live's data centres) finds none: an XNQOS with no entries, as in
// Xenia (titles read it even when the lookup fails). XNetQosRelease frees it.
std::mutex g_qos_mutex;
std::set<uint32_t> g_qos_results;  // XNQOS blocks given out, for XNetQosRelease

uint32_t NetDll_XNetQosServiceLookup(uint32_t caller, uint32_t flags, uint32_t event, uint32_t qos_ptr) {
  (void)caller, (void)flags;
  if (qos_ptr) {
    uint32_t qos = SystemAlloc(0x20);
    std::memset(Ptr(qos), 0, 0x20);  // count, pending count, one empty XNQOSINFO
    Store<uint32_t>(qos_ptr, qos);
    std::lock_guard lock(g_qos_mutex);
    g_qos_results.insert(qos);
  }
  if (event) {
    if (uint32_t body = ObjectFromHandle(event)) SetEvent(body);
  }
  return 0;
}

// XNetQosLookup(caller, peers, XNADDR*[], XNKID*[], XNKEY*[], servers, IN_ADDR[], service IDs[],
// probes, bits per second, flags, event, XNQOS**): with System Link, every peer answers at once, a
// millisecond away (the data a host listens with isn't carried).
uint32_t NetDll_XNetQosLookup(uint32_t caller, uint32_t peers, uint32_t xnaddrs, uint32_t key_ids, uint32_t keys,
                              uint32_t servers, uint32_t server_addresses, uint32_t service_ids, uint32_t probes,
                              uint32_t bits_per_sec, uint32_t flags, uint32_t event, uint32_t qos_ptr) {
  (void)caller, (void)xnaddrs, (void)key_ids, (void)keys, (void)server_addresses, (void)service_ids,
      (void)bits_per_sec, (void)flags;
  if (!Link()) return NetDll_ErrorStatus();
  NET_TRACE("system link: XNetQosLookup(%u peers, %u servers, %u probes, flags %X)", peers, servers, probes, flags);
  uint32_t count = peers + servers;
  if (qos_ptr) {
    uint32_t size = std::max<uint32_t>(0x20, 8 + 0x18 * count);
    uint32_t qos = SystemAlloc(size);
    std::memset(Ptr(qos), 0, size);
    Store<uint32_t>(qos, count);  // cxnqos; cxnqosPending stays 0
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t info = qos + 8 + i * 0x18;
      Store<uint8_t>(info + 0, i < peers ? 0x03 : 0x01);  // complete (and contacted, for peers)
      Store<uint16_t>(info + 2, uint16_t(probes));
      Store<uint16_t>(info + 4, uint16_t(i < peers ? probes : 0));
      Store<uint16_t>(info + 12, 1);  // RTT min and median, ms
      Store<uint16_t>(info + 14, 1);
      Store<uint32_t>(info + 16, 100000000);  // up and down, bits per second
      Store<uint32_t>(info + 20, 100000000);
    }
    Store<uint32_t>(qos_ptr, qos);
    std::lock_guard lock(g_qos_mutex);
    g_qos_results.insert(qos);
  }
  if (event) {
    if (uint32_t body = ObjectFromHandle(event)) SetEvent(body);
  }
  return 0;
}

uint32_t NetDll_XNetQosRelease(uint32_t caller, uint32_t qos) {
  (void)caller;
  std::lock_guard lock(g_qos_mutex);
  if (!g_qos_results.erase(qos)) return 0x80070000u | kWsaENetDown;
  SystemFree(qos);
  return 0;
}

// The machine ID comes with an Xbox Live logon (X_ONLINE_E_LOGON_NOT_LOGGED_ON otherwise, as in
// Xenia).
uint32_t XNetLogonGetMachineID(uint32_t machine_id_ptr) {
  if (machine_id_ptr) Store<uint64_t>(machine_id_ptr, 0);
  return 0x80151802;
}

}  // namespace

X360_EXPORT(NetDll_WSAStartup, NetDll_WSAStartup)
X360_EXPORT(NetDll_WSACleanup, NetDll_WSACleanup)
X360_EXPORT(NetDll_XNetStartup, NetDll_XNetStartup)
X360_EXPORT(NetDll_XNetCleanup, NetDll_XNetCleanup)
X360_EXPORT(NetDll_WSAGetLastError, NetDll_WSAGetLastError)
X360_EXPORT(NetDll_WSASetLastError, NetDll_WSASetLastError)
X360_EXPORT(NetDll_WSACreateEvent, NetDll_WSACreateEvent)
X360_EXPORT(NetDll_WSAResetEvent, NetDll_WSAResetEvent)
X360_EXPORT(NetDll_WSACloseEvent, NetDll_WSACloseEvent)
X360_EXPORT(NetDll_XNetGetEthernetLinkStatus, NetDll_XNetGetEthernetLinkStatus)
X360_EXPORT(NetDll_XNetGetTitleXnAddr, NetDll_XNetGetTitleXnAddr)
X360_EXPORT(NetDll_XNetRandom, NetDll_XNetRandom)
X360_EXPORT(NetDll_inet_addr, NetDll_inet_addr)
X360_EXPORT(NetDll_socket, NetDll_socket)
X360_EXPORT(NetDll_closesocket, NetDll_closesocket)
X360_EXPORT(NetDll_bind, NetDll_bind)
X360_EXPORT(NetDll_connect, NetDll_connect)
X360_EXPORT(NetDll_listen, NetDll_listen)
X360_EXPORT(NetDll_accept, NetDll_accept)
X360_EXPORT(NetDll_send, NetDll_send)
X360_EXPORT(NetDll_sendto, NetDll_sendto)
X360_EXPORT(NetDll_recv, NetDll_recv)
X360_EXPORT(NetDll_recvfrom, NetDll_recvfrom)
X360_EXPORT(NetDll_select, NetDll_select)
X360_EXPORT(NetDll_setsockopt, NetDll_setsockopt)
X360_EXPORT(NetDll_getsockopt, NetDll_getsockopt)
X360_EXPORT(NetDll_getsockname, NetDll_getsockname)
X360_EXPORT(NetDll_ioctlsocket, NetDll_ioctlsocket)
X360_EXPORT(NetDll_shutdown, NetDll_shutdown)
X360_EXPORT(NetDll_getpeername, NetDll_getpeername)
X360_EXPORT(NetDll___WSAFDIsSet, NetDll___WSAFDIsSet)
X360_EXPORT(NetDll_XNetInAddrToString, NetDll_XNetInAddrToString)
X360_EXPORT(NetDll_XNetXnAddrToMachineId, NetDll_XNetXnAddrToMachineId)
X360_EXPORT(NetDll_XNetGetConnectStatus, NetDll_XNetGetConnectStatus)
X360_EXPORT(NetDll_XNetRegisterKey, NetDll_XNetRegisterKey)
X360_EXPORT(NetDll_XNetUnregisterKey, NetDll_XNetUnregisterKey)
X360_EXPORT(NetDll_XNetCreateKey, NetDll_XNetCreateKey)
X360_EXPORT(XNetLogonGetMachineID, XNetLogonGetMachineID)
X360_EXPORT(NetDll_XnpGetConfigStatus, NetDll_ErrorStatus)
// System Link's address translation and key exchange; without it, unavailable.
X360_EXPORT(NetDll_XNetConnect, NetDll_XNetConnect)
X360_EXPORT(NetDll_XNetInAddrToXnAddr, NetDll_XNetInAddrToXnAddr)
X360_EXPORT(NetDll_XNetXnAddrToInAddr, NetDll_XNetXnAddrToInAddr)
X360_EXPORT(NetDll_XNetUnregisterInAddr, NetDll_XNetUnregisterInAddr)
X360_EXPORT(NetDll_XNetSetSystemLinkPort, NetDll_XNetSetSystemLinkPort)
X360_EXPORT(NetDll_XNetQosLookup, NetDll_XNetQosLookup)
// XNet services (Xbox Live's): unavailable.
X360_EXPORT(NetDll_XNetDnsLookup, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetDnsRelease, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetServerToInAddr, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetQosListen, NetDll_XNetQosListen)
X360_EXPORT(NetDll_XNetQosServiceLookup, NetDll_XNetQosServiceLookup)
X360_EXPORT(NetDll_XNetQosRelease, NetDll_XNetQosRelease)
X360_EXPORT(NetDll_XNetQosGetListenStats, NetDll_ErrorStatus)
