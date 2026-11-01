// System Link over host sockets: see x360/system_link.h and docs/SYSTEMLINK.md.
#include "x360/system_link.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mstcpip.h>
#include <windows.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <random>
#include <thread>

namespace x360::net {

namespace {

constexpr uint32_t kLoopback = 0x7F000001;
constexpr uint32_t kGuestBroadcast = 0xFFFFFFFF;
constexpr uint32_t kTypeStream = 1, kTypeDatagram = 2;
constexpr uint32_t kProtoTcp = 6, kProtoUdp = 17, kProtoVdp = 254;
constexpr size_t kMaxDatagram = 65536;

thread_local uint32_t t_error = 0;

uint16_t LoadBe16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t LoadBe32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void StoreBe16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }
void StoreBe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}

std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = char(std::tolower(uint8_t(c)));
  return out;
}

std::string Narrow(const wchar_t* text) {
  if (!text) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string out(size_t(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
  return out;
}

sockaddr_in HostAddress(uint32_t ip, uint16_t port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(ip);
  return a;
}

// The start of a datagram, for ANYX360_NET_TRACE.
std::string Hex(const uint8_t* data, size_t size) {
  std::string out = ":";
  char byte[4];
  for (size_t i = 0; i < size && i < 64; ++i) {
    std::snprintf(byte, sizeof(byte), " %02X", data[i]);
    out += byte;
  }
  return size > 64 ? out + " ..." : out;
}

int32_t Fail(uint32_t error) {
  t_error = error;
  return -1;
}
int32_t FailHost() { return Fail(uint32_t(WSAGetLastError())); }

}  // namespace

// ---- pure helpers ------------------------------------------------------------------------------

std::string IpText(uint32_t ip) {
  char text[16];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
  return text;
}

std::optional<uint32_t> ParseIp(std::string_view text) {
  uint32_t ip = 0;
  int parts = 0;
  size_t i = 0;
  while (parts < 4) {
    if (i >= text.size() || !std::isdigit(uint8_t(text[i]))) return std::nullopt;
    uint32_t part = 0;
    size_t digits = 0;
    while (i < text.size() && std::isdigit(uint8_t(text[i]))) {
      part = part * 10 + uint32_t(text[i++] - '0');
      if (++digits > 3 || part > 255) return std::nullopt;
    }
    ip = ip << 8 | part;
    if (++parts < 4) {
      if (i >= text.size() || text[i] != '.') return std::nullopt;
      ++i;
    }
  }
  if (i != text.size()) return std::nullopt;
  return ip;
}

uint16_t HostPort(uint16_t guest_port, int instance, uint32_t port_offset) {
  uint32_t port = uint32_t(guest_port) + port_offset + uint32_t(std::clamp(instance, 0, kMaxInstances - 1)) * kInstanceStride;
  while (port > 65535) port -= 65535 - 1023;  // into 1024..65535
  return uint16_t(port);
}

void EncodeHeader(const Header& h, uint8_t* out) {
  out[0] = 'A', out[1] = 'X', out[2] = 'S', out[3] = 'L';
  out[4] = kHeaderVersion;
  out[5] = h.flags;
  out[6] = h.instance;
  out[7] = 0;
  StoreBe16(out + 8, h.guest_port);
  StoreBe16(out + 10, 0);
  StoreBe32(out + 12, h.advertised_ip);
  StoreBe32(out + 16, h.sender_id);
  StoreBe32(out + 20, h.sequence);
}

std::optional<Header> DecodeHeader(const uint8_t* data, size_t size) {
  if (size < kHeaderSize || std::memcmp(data, "AXSL", 4) || data[4] != kHeaderVersion) return std::nullopt;
  if (data[6] >= kMaxInstances) return std::nullopt;
  Header h;
  h.flags = data[5];
  h.instance = data[6];
  h.guest_port = LoadBe16(data + 8);
  h.advertised_ip = LoadBe32(data + 12);
  h.sender_id = LoadBe32(data + 16);
  h.sequence = LoadBe32(data + 20);
  return h;
}

void WriteXnAddr(uint8_t* out, uint32_t ip, int instance) {
  std::memset(out, 0, kXnAddrSize);
  StoreBe32(out + 0, ip);  // ina; inaOnline and wPortOnline stay 0 (no online service)
  uint8_t* enet = out + 10;
  enet[0] = uint8_t(0x02 | (std::clamp(instance, 0, kMaxInstances - 1) << 4));  // locally administered
  StoreBe32(enet + 1, ip);
  enet[5] = 'X';
}

Peer ReadXnAddr(const uint8_t* in) {
  Peer peer;
  peer.ip = LoadBe32(in);
  const uint8_t* enet = in + 10;
  if ((enet[0] & 0x0F) == 0x02 && (enet[0] >> 4) < kMaxInstances && enet[5] == 'X' && LoadBe32(enet + 1) == peer.ip)
    peer.instance = enet[0] >> 4;
  return peer;
}

void MakeSystemLinkKeyId(uint8_t* id8, uint64_t random) {
  for (int i = 0; i < 8; ++i) id8[i] = uint8_t(random >> (56 - 8 * i));
  id8[0] &= 0x0F;  // XNET_XNKID_SYSTEM_LINK: the type bits (0xF0) clear
  if (!(id8[0] | id8[1] | id8[2] | id8[3] | id8[4] | id8[5] | id8[6] | id8[7])) id8[7] = 1;
}

uint32_t AddressBook::GuestAddress(const Peer& peer) {
  std::lock_guard lock(mutex_);
  uint64_t key = uint64_t(peer.ip) << 8 | uint32_t(peer.instance);
  if (auto it = handles_.find(key); it != handles_.end()) return it->second;
  uint32_t handle = 0x00010000u | (next_++ & 0xFFFF);  // 0.1.0.1, 0.1.0.2...
  handles_[key] = handle;
  peers_[handle] = peer;
  return handle;
}

Peer AddressBook::Resolve(uint32_t guest_ip) const {
  std::lock_guard lock(mutex_);
  if (auto it = peers_.find(guest_ip); it != peers_.end()) return it->second;
  for (int k = 1; k < kMaxInstances; ++k)
    if (guest_ip == LoopbackAddress(k)) return {guest_ip, k};
  return {guest_ip, 0};
}

void AddressBook::SetRoute(uint32_t advertised, uint32_t seen) {
  std::lock_guard lock(mutex_);
  if (advertised == seen) routes_.erase(advertised);
  else routes_[advertised] = seen;
}

uint32_t AddressBook::Route(uint32_t advertised) const {
  std::lock_guard lock(mutex_);
  auto it = routes_.find(advertised);
  return it == routes_.end() ? advertised : it->second;
}

uint32_t AddressBook::Advertised(uint32_t seen) const {
  std::lock_guard lock(mutex_);
  for (const auto& [advertised, route] : routes_)
    if (route == seen) return advertised;
  return seen;
}

std::vector<Adapter> ListAdapters() {
  std::vector<Adapter> out;
  ULONG size = 32 * 1024;
  std::vector<uint8_t> buffer;
  ULONG result = ERROR_BUFFER_OVERFLOW;
  for (int attempt = 0; attempt < 4 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
    buffer.resize(size);
    result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST,
                                  nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
  }
  if (result != NO_ERROR) return out;
  for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
      Adapter adapter;
      adapter.name = Narrow(a->FriendlyName);
      adapter.description = Narrow(a->Description);
      adapter.ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
      uint32_t prefix = std::min<uint32_t>(u->OnLinkPrefixLength, 32);
      adapter.mask = prefix ? 0xFFFFFFFFu << (32 - prefix) : 0;
      adapter.gateway = a->FirstGatewayAddress != nullptr;
      adapter.loopback = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
      adapter.metric = a->Ipv4Metric;
      out.push_back(adapter);
    }
  }
  return out;
}

std::optional<Adapter> ChooseAdapter(const std::vector<Adapter>& adapters, std::string_view setting) {
  std::string wanted = Lower(setting);
  Adapter loopback;
  loopback.name = "loopback";
  loopback.ip = kLoopback;
  loopback.mask = 0xFF000000u;
  loopback.loopback = true;
  if (wanted == "loopback") return loopback;
  if (wanted.empty() || wanted == "auto") {
    const Adapter* best = nullptr;
    for (const Adapter& a : adapters)
      if (!a.loopback && a.gateway && (!best || a.metric < best->metric)) best = &a;
    for (const Adapter& a : adapters)
      if (!best && !a.loopback && (a.ip >> 16) != 0xA9FE) best = &a;  // not link-local (169.254)
    if (best) return *best;
    return loopback;
  }
  if (auto ip = ParseIp(wanted)) {
    for (const Adapter& a : adapters)
      if (a.ip == *ip) return a;
    return std::nullopt;
  }
  for (const Adapter& a : adapters)
    if (!a.loopback && (Lower(a.name).find(wanted) != std::string::npos ||
                        Lower(a.description).find(wanted) != std::string::npos))
      return a;
  return std::nullopt;
}

uint32_t BroadcastAddress(const Adapter& adapter) {
  if (adapter.loopback) return kLoopback;
  // A /32 or /31 (some VPN adapters) has no broadcast address of its own: the limited one.
  if (adapter.mask == 0 || ~adapter.mask <= 1) return kGuestBroadcast;
  return adapter.ip | ~adapter.mask;
}

// ---- the socket layer --------------------------------------------------------------------------

struct Stack::Sock {
  uint32_t handle = 0;
  SOCKET host = INVALID_SOCKET;
  bool udp = true;
  uint16_t guest_port = 0;
  uint16_t host_port = 0;
  bool bound = false;
  bool nonblocking = false;
  bool connected = false;
  Address peer;  // a connected datagram socket's peer, as the guest named it
  std::mutex recent_mutex;
  uint64_t recent[64] = {};  // broadcasts received: sender << 32 | sequence
  size_t recent_next = 0;
};

uint32_t Stack::LastError() { return t_error; }
void Stack::SetLastError(uint32_t error) { t_error = error; }

void Stack::Log(const std::string& line) {
  if (logger_) logger_(line);
}

bool Stack::Start(const Config& config, Logger logger) {
  config_ = config;
  logger_ = std::move(logger);
  if (!config.enabled) return false;
  WSADATA data;
  if (int error = WSAStartup(MAKEWORD(2, 2), &data)) {
    Log("system link: WSAStartup failed (" + std::to_string(error) + ")");
    return false;
  }
  std::vector<Adapter> adapters = ListAdapters();
  std::optional<Adapter> chosen = ChooseAdapter(adapters, config.adapter);
  if (!chosen) {
    std::string names;
    for (const Adapter& a : adapters) names += "\n  " + a.name + " (" + a.description + ") " + IpText(a.ip);
    Log("system link: no adapter matches \"" + config.adapter + "\"; System Link is off. Adapters:" + names);
    return false;
  }
  adapter_ = *chosen;
  loopback_ = adapter_.loopback;
  advertised_ = adapter_.ip;
  broadcast_ip_ = BroadcastAddress(adapter_);

  // The instance: the first number no other AnyX360 on this PC holds (a named mutex each).
  int first = config.instance >= 0 ? std::min(config.instance, kMaxInstances - 1) : 0;
  int last = config.instance >= 0 ? first : kMaxInstances - 1;
  instance_ = -1;
  for (int k = first; k <= last; ++k) {
    std::wstring name = L"Local\\AnyX360.SystemLink." + std::to_wstring(k);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, name.c_str());
    bool taken = mutex && GetLastError() == ERROR_ALREADY_EXISTS;
    if (mutex && (!taken || config.instance >= 0)) {
      instance_mutex_ = mutex;
      instance_ = k;
      if (taken) Log("system link: instance " + std::to_string(k) + " is set but another AnyX360 has it too");
      break;
    }
    if (mutex) CloseHandle(mutex);
  }
  if (instance_ < 0) {
    Log("system link: instances 0-3 are all running on this PC; System Link is off");
    return false;
  }
  // On the loopback adapter each instance is a machine of its own to the guests, 198.51.100.1-4:
  // a datagram's sender is then the address in its XNADDR, as on a LAN (Halo 3 refuses a join
  // whose sender isn't).
  if (loopback_) advertised_ = LoopbackAddress(instance_);

  std::random_device rd;
  do sender_id_ = rd();
  while (!sender_id_);
  for (const std::string& text : config.peers) {
    if (auto ip = ParseIp(text)) {
      peer_ips_.push_back(*ip);
      continue;
    }
    addrinfo hints{}, *found = nullptr;
    hints.ai_family = AF_INET;
    if (!getaddrinfo(text.c_str(), nullptr, &hints, &found) && found) {
      peer_ips_.push_back(ntohl(reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr));
      freeaddrinfo(found);
    } else {
      Log("system link: peer \"" + text + "\" not found");
    }
  }
  started_ = true;
  trace_ = std::getenv("ANYX360_NET_TRACE") != nullptr;
  std::string line = "system link: on, adapter \"" + adapter_.name + "\" " + IpText(adapter_.ip);
  if (loopback_) {
    line += " (" + IpText(advertised_) + " to the game)";
  } else {
    int prefix = 0;
    for (uint32_t m = adapter_.mask; m & 0x80000000u; m <<= 1) ++prefix;
    line += "/" + std::to_string(prefix) + " (broadcast " + IpText(broadcast_ip_) + ")";
  }
  line += ", instance " + std::to_string(instance_) + ", guest port + " +
          std::to_string(config.port_offset + uint32_t(instance_) * kInstanceStride);
  for (uint32_t ip : peer_ips_) line += ", peer " + IpText(ip);
  Log(line);
  return true;
}

void Stack::Stop() {
  std::vector<std::shared_ptr<Sock>> open;
  {
    std::lock_guard lock(mutex_);
    for (auto& [handle, sock] : sockets_) open.push_back(sock);
    sockets_.clear();
  }
  for (auto& sock : open) closesocket(sock->host);
  if (instance_mutex_) CloseHandle(instance_mutex_);
  instance_mutex_ = nullptr;
  if (started_) WSACleanup();
  started_ = false;
}

std::shared_ptr<Stack::Sock> Stack::Find(uint32_t s) {
  std::lock_guard lock(mutex_);
  auto it = sockets_.find(s);
  return it == sockets_.end() ? nullptr : it->second;
}

bool Stack::IsSocket(uint32_t s) { return Find(s) != nullptr; }

int32_t Stack::Socket(uint32_t af, uint32_t type, uint32_t protocol) {
  if (af != AF_INET) return Fail(WSAEAFNOSUPPORT);
  bool udp;
  if (type == kTypeDatagram && (protocol == 0 || protocol == kProtoUdp || protocol == kProtoVdp)) udp = true;
  else if (type == kTypeStream && (protocol == 0 || protocol == kProtoTcp)) udp = false;
  else return Fail(WSAEPROTONOSUPPORT);
  // VDP (the console's voice and data protocol) is UDP whose payload starts with the game data's
  // length: both ends are AnyX360, so it goes as it is.
  SOCKET host = ::socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP);
  if (host == INVALID_SOCKET) return FailHost();
  if (udp) {
    // A datagram to a port nobody listens on (another instance's) must not fail the next receive.
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(host, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);
    BOOL on = TRUE;  // broadcasts are sent for the guest's, whatever its own option
    setsockopt(host, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
  }
  auto sock = std::make_shared<Sock>();
  sock->host = host;
  sock->udp = udp;
  std::lock_guard lock(mutex_);
  sock->handle = next_handle_;
  next_handle_ += 4;
  sockets_[sock->handle] = sock;
  return int32_t(sock->handle);
}

int32_t Stack::Close(uint32_t s) {
  std::shared_ptr<Sock> sock;
  {
    std::lock_guard lock(mutex_);
    auto it = sockets_.find(s);
    if (it == sockets_.end()) return Fail(WSAENOTSOCK);
    sock = it->second;
    sockets_.erase(it);
  }
  closesocket(sock->host);
  return 0;
}

int32_t Stack::BindHost(Sock& sock, uint16_t guest_port) {
  uint32_t local = loopback_ ? kLoopback : INADDR_ANY;
  if (guest_port) {
    uint16_t host_port = HostPort(guest_port, instance_, config_.port_offset);
    sockaddr_in a = HostAddress(local, host_port);
    if (bind(sock.host, reinterpret_cast<sockaddr*>(&a), sizeof(a))) {
      int error = WSAGetLastError();
      Log("system link: socket " + std::to_string(sock.handle) + ": guest port " + std::to_string(guest_port) +
          " (host " + std::to_string(host_port) + ") can't be bound (" + std::to_string(error) + ")");
      return Fail(uint32_t(error));
    }
    sock.guest_port = guest_port, sock.host_port = host_port, sock.bound = true;
    Log("system link: socket " + std::to_string(sock.handle) + (sock.udp ? " (UDP)" : " (TCP)") + ": guest port " +
        std::to_string(guest_port) + " is host port " + std::to_string(host_port));
    return 0;
  }
  // Port 0: a guest port of our own whose host port is free, so that peers can reach it.
  for (int attempt = 0; attempt < 200; ++attempt) {
    uint16_t candidate;
    {
      std::lock_guard lock(mutex_);
      candidate = next_ephemeral_++;
      if (next_ephemeral_ >= 50000) next_ephemeral_ = 40000;
    }
    uint16_t host_port = HostPort(candidate, instance_, config_.port_offset);
    sockaddr_in a = HostAddress(local, host_port);
    if (!bind(sock.host, reinterpret_cast<sockaddr*>(&a), sizeof(a))) {
      sock.guest_port = candidate, sock.host_port = host_port, sock.bound = true;
      return 0;
    }
    int error = WSAGetLastError();
    if (error != WSAEADDRINUSE && error != WSAEACCES) return Fail(uint32_t(error));
  }
  return Fail(WSAEADDRINUSE);
}

int32_t Stack::ImplicitBind(Sock& sock) { return sock.bound ? 0 : BindHost(sock, 0); }

int32_t Stack::Bind(uint32_t s, const Address& name) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (sock->bound) return Fail(WSAEINVAL);
  return BindHost(*sock, name.port);
}

// The host address and port of a guest address.
static void Target(AddressBook& book, bool loopback, uint32_t own_ip, int own_instance, uint32_t port_offset,
                   const Stack::Address& to, uint32_t& host_ip, uint16_t& host_port) {
  Peer peer = book.Resolve(to.ip);
  // The guest's own loopback address (or none): this instance.
  if (to.ip == 0 || (to.ip >> 24) == 127) peer = {own_ip, own_instance};
  // On the loopback adapter every instance is on this PC.
  host_ip = loopback ? kLoopback : book.Route(peer.ip);
  host_port = HostPort(to.port, peer.instance, port_offset);
}

int32_t Stack::Connect(uint32_t s, const Address& name) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (sock->udp && ImplicitBind(*sock)) return -1;
  uint32_t host_ip;
  uint16_t host_port;
  Target(book_, loopback_, advertised_, instance_, config_.port_offset, name, host_ip, host_port);
  sockaddr_in a = HostAddress(host_ip, host_port);
  if (connect(sock->host, reinterpret_cast<sockaddr*>(&a), sizeof(a))) {
    int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK)
      Log("system link: socket " + std::to_string(s) + ": connecting to " + IpText(host_ip) + ":" +
          std::to_string(host_port) + " failed (" + std::to_string(error) + ")");
    return Fail(uint32_t(error));
  }
  sock->connected = true;
  sock->peer = name;
  if (!sock->udp) {
    Log("system link: socket " + std::to_string(s) + " (TCP) connected to " + IpText(host_ip) + ":" +
        std::to_string(host_port));
    sockaddr_in local{};
    int length = sizeof(local);
    getsockname(sock->host, reinterpret_cast<sockaddr*>(&local), &length);
    if (!sock->bound) sock->host_port = ntohs(local.sin_port), sock->guest_port = sock->host_port, sock->bound = true;
  }
  return 0;
}

int32_t Stack::Listen(uint32_t s, int backlog) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (!sock->bound && BindHost(*sock, 0)) return -1;
  return listen(sock->host, backlog) ? FailHost() : 0;
}

int32_t Stack::Accept(uint32_t s, Address* peer) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  sockaddr_in from{};
  int length = sizeof(from);
  SOCKET host = accept(sock->host, reinterpret_cast<sockaddr*>(&from), &length);
  if (host == INVALID_SOCKET) return FailHost();
  auto accepted = std::make_shared<Sock>();
  accepted->host = host;
  accepted->udp = false;
  accepted->bound = true;
  accepted->guest_port = sock->guest_port, accepted->host_port = sock->host_port;
  accepted->connected = true;
  uint32_t seen = ntohl(from.sin_addr.s_addr);
  accepted->peer = {book_.GuestAddress({book_.Advertised(seen), 0}), ntohs(from.sin_port)};
  if (peer) *peer = accepted->peer;
  Log("system link: socket " + std::to_string(s) + " (TCP) accepted " + IpText(seen) + ":" +
      std::to_string(ntohs(from.sin_port)));
  std::lock_guard lock(mutex_);
  accepted->handle = next_handle_;
  next_handle_ += 4;
  sockets_[accepted->handle] = accepted;
  return int32_t(accepted->handle);
}

int32_t Stack::Shutdown(uint32_t s, int how) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  return shutdown(sock->host, how) ? FailHost() : 0;
}

int32_t Stack::SendDatagram(Sock& sock, const uint8_t* data, uint32_t length, uint32_t flags, const Address& to) {
  if (length > kMaxDatagram - kHeaderSize) return Fail(WSAEMSGSIZE);
  thread_local std::vector<uint8_t> packet;
  packet.resize(kHeaderSize + length);
  Header header;
  header.instance = uint8_t(instance_);
  header.guest_port = sock.guest_port;
  header.advertised_ip = advertised_;
  header.sender_id = sender_id_;
  const bool broadcast = to.ip == kGuestBroadcast || (!loopback_ && to.ip == broadcast_ip_);
  if (broadcast) {
    header.flags |= kFlagBroadcast;
    std::lock_guard lock(mutex_);
    header.sequence = ++sequence_;
  }
  EncodeHeader(header, packet.data());
  if (length) std::memcpy(packet.data() + kHeaderSize, data, length);
  int host_flags = int(flags & MSG_DONTROUTE);

  auto send_one = [&](uint32_t ip, uint16_t port) {
    sockaddr_in a = HostAddress(ip, port);
    return sendto(sock.host, reinterpret_cast<const char*>(packet.data()), int(packet.size()), host_flags,
                  reinterpret_cast<sockaddr*>(&a), sizeof(a)) >= 0;
  };
  if (!broadcast) {
    uint32_t host_ip;
    uint16_t host_port;
    Target(book_, loopback_, advertised_, instance_, config_.port_offset, to, host_ip, host_port);
    bool ok = send_one(host_ip, host_port);
    if (trace_)
      Log("system link: socket " + std::to_string(sock.handle) + " port " + std::to_string(sock.guest_port) + " sent " +
          std::to_string(length) + " bytes to " + IpText(to.ip) + ":" + std::to_string(to.port) + " (host " +
          IpText(host_ip) + ":" + std::to_string(host_port) + (ok ? ")" : ", failed)") + Hex(data, length));
    if (!ok) return FailHost();
    return int32_t(length);
  }
  if (trace_)
    Log("system link: socket " + std::to_string(sock.handle) + " port " + std::to_string(sock.guest_port) +
        " broadcast " + std::to_string(length) + " bytes to port " + std::to_string(to.port) + Hex(data, length));
  // Every instance's port: on the adapter's broadcast address, on this PC (the copy a broadcast
  // may also bring is dropped by its sequence number) and on each configured peer.
  bool sent = false;
  int error = 0;
  for (int k = 0; k < kMaxInstances; ++k) {
    uint16_t port = HostPort(to.port, k, config_.port_offset);
    auto count = [&](bool ok) {
      sent |= ok;
      if (!ok) error = WSAGetLastError();
    };
    if (!loopback_) count(send_one(broadcast_ip_, port));
    if (k != instance_) count(send_one(kLoopback, port));
    for (uint32_t peer : peer_ips_) count(send_one(peer, port));
  }
  if (!sent) return Fail(uint32_t(error ? error : WSAENETUNREACH));
  return int32_t(length);
}

int32_t Stack::SendTo(uint32_t s, const uint8_t* data, uint32_t length, uint32_t flags, const Address* to) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (!sock->udp) {
    int sent = send(sock->host, reinterpret_cast<const char*>(data), int(length), int(flags & (MSG_OOB | MSG_DONTROUTE)));
    return sent < 0 ? FailHost() : sent;
  }
  if (ImplicitBind(*sock)) return -1;
  if (!to) {
    if (!sock->connected) return Fail(WSAENOTCONN);
    to = &sock->peer;
  }
  return SendDatagram(*sock, data, length, flags, *to);
}

bool Stack::Admit(Sock& sock, const Header& header, uint32_t seen_ip) {
  if (header.sender_id == sender_id_) return false;  // our own broadcast
  if (header.flags & kFlagBroadcast) {
    uint64_t key = uint64_t(header.sender_id) << 32 | header.sequence;
    std::lock_guard lock(sock.recent_mutex);
    for (uint64_t recent : sock.recent)
      if (recent == key) return false;  // its second copy
    sock.recent[sock.recent_next++ % std::size(sock.recent)] = key;
  }
  uint32_t advertised = header.advertised_ip ? header.advertised_ip : seen_ip;
  if (advertised != seen_ip && book_.Route(advertised) != seen_ip) book_.SetRoute(advertised, seen_ip);
  NotePeer(advertised, seen_ip, header.instance, header.guest_port, sock.guest_port);
  return true;
}

void Stack::NotePeer(uint32_t advertised, uint32_t seen, int instance, uint16_t guest_port, uint16_t local_port) {
  uint64_t key = uint64_t(advertised) << 8 | uint32_t(instance);
  {
    std::lock_guard lock(mutex_);
    if (std::find(known_peers_.begin(), known_peers_.end(), key) != known_peers_.end()) return;
    known_peers_.push_back(key);
  }
  std::string line = "system link: peer " + IpText(advertised) + " instance " + std::to_string(instance);
  if (seen != advertised) line += " (its packets come from " + IpText(seen) + ")";
  line += ", first heard from its port " + std::to_string(guest_port) + " on our port " + std::to_string(local_port);
  Log(line);
}

int32_t Stack::ReceiveDatagram(Sock& sock, uint8_t* data, uint32_t length, uint32_t flags, Address* from) {
  thread_local std::vector<uint8_t> packet(kMaxDatagram);
  const bool peek = flags & MSG_PEEK;
  for (;;) {
    sockaddr_in source{};
    int source_length = sizeof(source);
    int received = recvfrom(sock.host, reinterpret_cast<char*>(packet.data()), int(packet.size()), peek ? MSG_PEEK : 0,
                            reinterpret_cast<sockaddr*>(&source), &source_length);
    if (received < 0) {
      int error = WSAGetLastError();
      if (error == WSAECONNRESET || error == WSAEMSGSIZE) continue;
      return Fail(uint32_t(error));
    }
    uint32_t seen = ntohl(source.sin_addr.s_addr);
    std::optional<Header> header = DecodeHeader(packet.data(), size_t(received));
    bool ours = header && (peek ? header->sender_id != sender_id_ : Admit(sock, *header, seen));
    if (!ours) {
      if (peek) recvfrom(sock.host, reinterpret_cast<char*>(packet.data()), int(packet.size()), 0, nullptr, nullptr);
      continue;
    }
    uint32_t payload = uint32_t(received) - uint32_t(kHeaderSize);
    uint32_t copied = std::min(payload, length);
    if (copied) std::memcpy(data, packet.data() + kHeaderSize, copied);
    if (from) {
      // A broadcast isn't secured on the console: its sender is the plain address its XNADDR has
      // (Halo: Reach drops a search reply from any other). The rest come from a secure address.
      uint32_t advertised = header->advertised_ip ? header->advertised_ip : seen;
      from->ip = (header->flags & kFlagBroadcast) ? advertised : book_.GuestAddress({advertised, header->instance});
      from->port = header->guest_port;
    }
    if (trace_)
      Log("system link: socket " + std::to_string(sock.handle) + " port " + std::to_string(sock.guest_port) +
          " received " + std::to_string(payload) + " bytes from " + IpText(seen) + " instance " +
          std::to_string(header->instance) + " port " + std::to_string(header->guest_port) +
          ((header->flags & kFlagBroadcast) ? " (broadcast)" : "") + Hex(packet.data() + kHeaderSize, payload));
    if (copied < payload) return Fail(WSAEMSGSIZE);
    return int32_t(copied);
  }
}

bool Stack::DrainForeign(Sock& sock) {
  thread_local std::vector<uint8_t> packet(kMaxDatagram);
  for (;;) {
    u_long available = 0;
    if (ioctlsocket(sock.host, FIONREAD, &available) || !available) return false;
    if (!sock.udp) return true;
    int received = recv(sock.host, reinterpret_cast<char*>(packet.data()), int(packet.size()), MSG_PEEK);
    if (received < 0) {
      int error = WSAGetLastError();
      if (error == WSAECONNRESET || error == WSAEMSGSIZE) {
        recv(sock.host, reinterpret_cast<char*>(packet.data()), int(packet.size()), 0);
        continue;
      }
      return error != WSAEWOULDBLOCK;  // let the guest's receive see the error
    }
    std::optional<Header> header = DecodeHeader(packet.data(), size_t(received));
    if (header && header->sender_id != sender_id_) {
      if (!(header->flags & kFlagBroadcast)) return true;
      uint64_t key = uint64_t(header->sender_id) << 32 | header->sequence;
      std::lock_guard lock(sock.recent_mutex);
      if (std::find(std::begin(sock.recent), std::end(sock.recent), key) == std::end(sock.recent)) return true;
    }
    recv(sock.host, reinterpret_cast<char*>(packet.data()), int(packet.size()), 0);  // not the guest's
  }
}

int32_t Stack::RecvFrom(uint32_t s, uint8_t* data, uint32_t length, uint32_t flags, Address* from) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (!sock->udp) {
    int received = recv(sock->host, reinterpret_cast<char*>(data), int(length), int(flags & (MSG_PEEK | MSG_OOB)));
    if (received < 0) return FailHost();
    if (from) *from = sock->peer;
    return received;
  }
  if (!sock->bound) return Fail(WSAEINVAL);
  return ReceiveDatagram(*sock, data, length, flags & MSG_PEEK, from);
}

int32_t Stack::SetOption(uint32_t s, uint32_t level, uint32_t name, const uint8_t* value, uint32_t length) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  auto integer = [&]() -> int {
    if (!value || !length) return 0;
    return length >= 4 ? int(LoadBe32(value)) : length >= 2 ? int(LoadBe16(value)) : int(value[0]);
  };
  int host_level = 0, host_name = 0;
  if (level == 0xFFFF) {
    switch (name) {
      case 0x0004:  // SO_REUSEADDR
      case 0x0008:  // SO_KEEPALIVE
      case 0x1001:  // SO_SNDBUF
      case 0x1002:  // SO_RCVBUF
      case 0x1005:  // SO_SNDTIMEO (ms)
      case 0x1006:  // SO_RCVTIMEO
        host_level = SOL_SOCKET, host_name = int(name);
        break;
      case 0xFFFFFFFB:  // SO_EXCLUSIVEADDRUSE
        host_level = SOL_SOCKET, host_name = SO_EXCLUSIVEADDRUSE;
        break;
      case 0x0080: {  // SO_LINGER {be16 on, be16 seconds}
        if (!value || length < 4) return Fail(WSAEFAULT);
        linger l{u_short(LoadBe16(value)), u_short(LoadBe16(value + 2))};
        return setsockopt(sock->host, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&l), sizeof(l)) ? FailHost() : 0;
      }
      case 0xFFFFFF7F: {  // SO_DONTLINGER
        BOOL on = integer() != 0;
        return setsockopt(sock->host, SOL_SOCKET, SO_DONTLINGER, reinterpret_cast<const char*>(&on), sizeof(on)) ? FailHost()
                                                                                                             : 0;
      }
      default:
        // SO_BROADCAST (always on for the host socket), 0x5801/0x5802 (the console's insecure
        // sockets: nothing is encrypted here) and the console's own options.
        return 0;
    }
  } else if (level == kProtoTcp && name == 0x0001) {  // TCP_NODELAY
    host_level = IPPROTO_TCP, host_name = TCP_NODELAY;
  } else {
    return 0;
  }
  int v = integer();
  return setsockopt(sock->host, host_level, host_name, reinterpret_cast<const char*>(&v), sizeof(v)) ? FailHost() : 0;
}

int32_t Stack::GetOption(uint32_t s, uint32_t level, uint32_t name, uint32_t& value) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  value = 0;
  if (level == 0xFFFF && (name == 0x1001 || name == 0x1002 || name == 0x1007 || name == 0x1008 || name == 0x0004)) {
    int v = 0, size = sizeof(v);
    if (getsockopt(sock->host, SOL_SOCKET, int(name), reinterpret_cast<char*>(&v), &size)) return FailHost();
    value = uint32_t(v);
    return 0;
  }
  if (level == 0xFFFF && name == 0x0020) value = 1;  // SO_BROADCAST
  return 0;
}

int32_t Stack::GetName(uint32_t s, Address& name) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (!sock->bound) return Fail(WSAEINVAL);
  name = {own_guest_ip(), sock->guest_port};
  return 0;
}

int32_t Stack::GetPeerName(uint32_t s, Address& name) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (!sock->connected) return Fail(WSAENOTCONN);
  name = sock->peer;
  return 0;
}

int32_t Stack::IoControl(uint32_t s, uint32_t command, uint32_t& argument) {
  auto sock = Find(s);
  if (!sock) return Fail(WSAENOTSOCK);
  if (command == 0x8004667E) {  // FIONBIO
    u_long on = argument != 0;
    if (ioctlsocket(sock->host, FIONBIO, &on)) return FailHost();
    sock->nonblocking = on != 0;
    return 0;
  }
  if (command == 0x4004667F) {  // FIONREAD: the next datagram's size (the stream's bytes)
    argument = 0;
    if (!DrainForeign(*sock)) return 0;
    if (!sock->udp) {
      u_long available = 0;
      ioctlsocket(sock->host, FIONREAD, &available);
      argument = uint32_t(available);
      return 0;
    }
    thread_local std::vector<uint8_t> packet(kMaxDatagram);
    int received = recv(sock->host, reinterpret_cast<char*>(packet.data()), int(packet.size()), MSG_PEEK);
    if (received >= int(kHeaderSize)) argument = uint32_t(received) - uint32_t(kHeaderSize);
    return 0;
  }
  return 0;
}

int32_t Stack::Select(std::vector<uint32_t>* read, std::vector<uint32_t>* write, std::vector<uint32_t>* except,
                      int64_t timeout_us) {
  std::vector<uint32_t>* sets[3] = {read, write, except};
  std::vector<std::shared_ptr<Sock>> socks[3];
  size_t total = 0;
  for (int i = 0; i < 3; ++i) {
    if (!sets[i]) continue;
    for (uint32_t s : *sets[i]) {
      auto sock = Find(s);
      if (!sock) return Fail(WSAENOTSOCK);
      socks[i].push_back(sock);
    }
    total += socks[i].size();
  }
  using Clock = std::chrono::steady_clock;
  const auto deadline = timeout_us < 0 ? Clock::time_point::max() : Clock::now() + std::chrono::microseconds(timeout_us);
  if (!total) {  // Winsock refuses empty sets: a plain wait
    if (timeout_us < 0) return Fail(WSAEINVAL);
    std::this_thread::sleep_until(deadline);
    return 0;
  }
  for (;;) {
    fd_set host_sets[3];
    for (int i = 0; i < 3; ++i) {
      FD_ZERO(&host_sets[i]);
      for (auto& sock : socks[i]) FD_SET(sock->host, &host_sets[i]);
    }
    timeval tv{}, *ptv = nullptr;
    if (timeout_us >= 0) {
      auto left = std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count());
      tv.tv_sec = long(left / 1000000), tv.tv_usec = long(left % 1000000);
      ptv = &tv;
    }
    int n = ::select(0, socks[0].empty() ? nullptr : &host_sets[0], socks[1].empty() ? nullptr : &host_sets[1],
                     socks[2].empty() ? nullptr : &host_sets[2], ptv);
    if (n < 0) return FailHost();
    std::vector<uint32_t> ready[3];
    for (int i = 0; i < 3; ++i)
      for (auto& sock : socks[i])
        if (FD_ISSET(sock->host, &host_sets[i]) && (i != 0 || DrainForeign(*sock))) ready[i].push_back(sock->handle);
    size_t count = ready[0].size() + ready[1].size() + ready[2].size();
    // Only foreign datagrams (dropped): wait again for what's left of the timeout.
    if (count || n == 0 || Clock::now() >= deadline) {
      for (int i = 0; i < 3; ++i)
        if (sets[i]) *sets[i] = std::move(ready[i]);
      return int32_t(count);
    }
  }
}

Stack& Global() {
  static Stack stack;
  return stack;
}

std::string WindowTitleSuffix() {
  Stack& stack = Global();
  if (!stack.started() || stack.instance() == 0) return {};
  return " (System Link " + std::to_string(stack.instance() + 1) + ")";
}

}  // namespace x360::net
