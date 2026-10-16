// xam.xex networking (NetDll_*). Xbox Live is gone and system link is not implemented, so the
// network reports "no link": startup succeeds, sockets fail with WSAENETDOWN.
#include <random>

#include "x360/kernel.h"

using namespace x360::kernel;

namespace {

constexpr int32_t kSocketError = -1;
constexpr uint32_t kWsaENetDown = 10050;
thread_local uint32_t t_wsa_error = 0;

int32_t Fail() {
  t_wsa_error = kWsaENetDown;
  return kSocketError;
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

uint32_t NetDll_XNetGetEthernetLinkStatus(uint32_t caller) {
  (void)caller;
  return 0;  // no link
}

uint32_t NetDll_XNetGetTitleXnAddr(uint32_t caller, uint32_t addr) {
  (void)caller;
  if (addr) std::memset(Ptr(addr), 0, 0x24);
  return 1;  // XNET_GET_XNADDR_NONE
}

uint32_t NetDll_XNetRandom(uint32_t caller, uint32_t buffer, uint32_t length) {
  (void)caller;
  static std::mt19937 rng{std::random_device{}()};
  for (uint32_t i = 0; i < length; ++i) Store<uint8_t>(buffer + i, uint8_t(rng()));
  return 0;
}

uint32_t NetDll_inet_addr(uint32_t caller, uint32_t cp) {
  (void)caller;
  unsigned a, b, c, d;
  if (!cp || std::sscanf(Ptr<const char>(cp), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0xFFFFFFFF;
  return (a << 24) | (b << 16) | (c << 8) | d;
}

int32_t NetDll_Fail() { return Fail(); }
uint32_t NetDll_ErrorStatus() { return 0x80070000u | kWsaENetDown; }

}  // namespace

X360_EXPORT(NetDll_WSAStartup, NetDll_WSAStartup)
X360_EXPORT(NetDll_WSACleanup, NetDll_WSACleanup)
X360_EXPORT(NetDll_XNetStartup, NetDll_XNetStartup)
X360_EXPORT(NetDll_XNetCleanup, NetDll_XNetCleanup)
X360_EXPORT(NetDll_WSAGetLastError, NetDll_WSAGetLastError)
X360_EXPORT(NetDll_WSASetLastError, NetDll_WSASetLastError)
X360_EXPORT(NetDll_WSACreateEvent, NetDll_WSACreateEvent)
X360_EXPORT(NetDll_XNetGetEthernetLinkStatus, NetDll_XNetGetEthernetLinkStatus)
X360_EXPORT(NetDll_XNetGetTitleXnAddr, NetDll_XNetGetTitleXnAddr)
X360_EXPORT(NetDll_XNetRandom, NetDll_XNetRandom)
X360_EXPORT(NetDll_inet_addr, NetDll_inet_addr)
// Socket API: everything fails with WSAENETDOWN.
X360_EXPORT(NetDll_socket, NetDll_Fail)
X360_EXPORT(NetDll_closesocket, NetDll_Fail)
X360_EXPORT(NetDll_bind, NetDll_Fail)
X360_EXPORT(NetDll_connect, NetDll_Fail)
X360_EXPORT(NetDll_listen, NetDll_Fail)
X360_EXPORT(NetDll_accept, NetDll_Fail)
X360_EXPORT(NetDll_send, NetDll_Fail)
X360_EXPORT(NetDll_sendto, NetDll_Fail)
X360_EXPORT(NetDll_recv, NetDll_Fail)
X360_EXPORT(NetDll_recvfrom, NetDll_Fail)
X360_EXPORT(NetDll_select, NetDll_Fail)
X360_EXPORT(NetDll_setsockopt, NetDll_Fail)
X360_EXPORT(NetDll_getsockopt, NetDll_Fail)
X360_EXPORT(NetDll_getsockname, NetDll_Fail)
X360_EXPORT(NetDll_ioctlsocket, NetDll_Fail)
// XNet services: unavailable.
X360_EXPORT(NetDll_XNetConnect, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetDnsLookup, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetDnsRelease, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetInAddrToXnAddr, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetXnAddrToInAddr, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetServerToInAddr, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetRegisterKey, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetUnregisterKey, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetUnregisterInAddr, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetQosListen, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetQosLookup, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetQosRelease, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetQosGetListenStats, NetDll_ErrorStatus)
X360_EXPORT(NetDll_XNetSetSystemLinkPort, NetDll_ErrorStatus)
