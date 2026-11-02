// System Link (x360/system_link.h): port mapping, the datagram header, XNADDRs, the address book,
// adapter choice and the [network] settings; then two stacks in this process on the loopback
// address, as two instances on one PC: a broadcast from one reaches the other, the reply comes back
// to the right instance, and datagrams that aren't AnyX360's are dropped. Then the synchronous
// XMsgStartIORequest's last error, which a System Link join depends on.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "x360/kernel.h"
#include "x360/settings.h"
#include "x360/system_link.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

namespace fs = std::filesystem;
using namespace x360::net;

// The kernel's loader fills the function table from this generated array; tests have no game.
PPCFuncMapping PPCFuncMappings[] = {{0, nullptr}};
PPC_EXTERN_FUNC(__imp__XMsgStartIORequest);

static int failures = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    if (cond) {                                                     \
      std::printf("PASS %s\n", #cond);                              \
    } else {                                                        \
      std::printf("FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                   \
    }                                                               \
  } while (0)

namespace {

void PureHelpers() {
  CHECK(ParseIp("192.168.0.11") == 0xC0A8000Bu);
  CHECK(ParseIp("255.255.255.255") == 0xFFFFFFFFu);
  CHECK(!ParseIp("256.1.1.1"));
  CHECK(!ParseIp("1.2.3"));
  CHECK(!ParseIp("1.2.3.4.5"));
  CHECK(!ParseIp("tailscale"));
  CHECK(IpText(0x7F000001) == "127.0.0.1");

  // Ports: guest + offset + instance * 100, wrapped into 1024-65535.
  CHECK(HostPort(1000, 0, 10000) == 11000);
  CHECK(HostPort(1000, 1, 10000) == 11100);
  CHECK(HostPort(3074, 3, 0) == 3374);
  CHECK(HostPort(65000, 0, 10000) == 65000 + 10000 - 64512);
  CHECK(HostPort(65535, 3, 60000) >= 1024);

  // The header round-trips; anything else isn't ours.
  Header h;
  h.flags = kFlagBroadcast;
  h.instance = 2;
  h.guest_port = 1001;
  h.advertised_ip = 0xC0A8000B;
  h.sender_id = 0xDEADBEEF;
  h.sequence = 77;
  uint8_t packet[kHeaderSize + 4] = {};
  EncodeHeader(h, packet);
  auto back = DecodeHeader(packet, sizeof(packet));
  CHECK(back && back->flags == kFlagBroadcast && back->instance == 2 && back->guest_port == 1001);
  CHECK(back && back->advertised_ip == 0xC0A8000B && back->sender_id == 0xDEADBEEF && back->sequence == 77);
  CHECK(!DecodeHeader(packet, kHeaderSize - 1));
  packet[6] = 4;  // no instance 4
  CHECK(!DecodeHeader(packet, sizeof(packet)));
  packet[6] = 2, packet[0] = 'B';
  CHECK(!DecodeHeader(packet, sizeof(packet)));

  // XNADDRs name the address and the instance (big-endian, as the guest has them).
  uint8_t xnaddr[kXnAddrSize];
  for (int k = 0; k < kMaxInstances; ++k) {
    WriteXnAddr(xnaddr, 0x0A000005, k);
    Peer peer = ReadXnAddr(xnaddr);
    CHECK(peer.ip == 0x0A000005 && peer.instance == k);
    CHECK((xnaddr[10] & 0x01) == 0 && (xnaddr[10] & 0x02) == 0x02);  // unicast, locally administered
  }
  CHECK(xnaddr[0] == 10 && xnaddr[3] == 5 && xnaddr[4] == 0);  // ina, then inaOnline 0
  // The offline XNADDR (127.0.0.1, MAC CC:CC:..) is an instance 0.
  std::memset(xnaddr, 0, sizeof(xnaddr));
  xnaddr[0] = 127, xnaddr[3] = 1;
  std::memset(xnaddr + 10, 0xCC, 6);
  CHECK(ReadXnAddr(xnaddr) == (Peer{0x7F000001, 0}));

  uint8_t id[8];
  MakeSystemLinkKeyId(id, 0xFFFFFFFFFFFFFFFFull);
  CHECK((id[0] & 0xF0) == 0);  // XNET_XNKID_SYSTEM_LINK
  MakeSystemLinkKeyId(id, 0);
  CHECK(id[7] == 1);  // never all zeros
}

void Book() {
  AddressBook book;
  uint32_t z = book.GuestAddress({0xC0A80002, 0});
  uint32_t a = book.GuestAddress({0xC0A80002, 1});
  uint32_t b = book.GuestAddress({0xC0A80002, 2});
  uint32_t c = book.GuestAddress({0xC0A80003, 1});
  // Handles shaped like secure addresses: the first byte 0 (Halo 3 refuses any other).
  CHECK(AddressBook::IsHandle(z) && AddressBook::IsHandle(a) && AddressBook::IsHandle(b) && AddressBook::IsHandle(c));
  CHECK((z >> 24) == 0 && z != 0);
  CHECK(z != a && a != b && a != c && b != c);
  CHECK(book.GuestAddress({0xC0A80002, 1}) == a);  // stable
  CHECK(book.Resolve(a) == (Peer{0xC0A80002, 1}));
  CHECK(book.Resolve(z) == (Peer{0xC0A80002, 0}));
  CHECK(book.Resolve(0xC0A80009) == (Peer{0xC0A80009, 0}));  // a raw address: an instance 0
  CHECK(book.Resolve(LoopbackAddress(2)) == (Peer{LoopbackAddress(2), 2}));
  book.SetRoute(0xC0A80002, 0x64400001);  // its packets come over a VPN
  CHECK(book.Route(0xC0A80002) == 0x64400001);
  CHECK(book.Advertised(0x64400001) == 0xC0A80002);
  CHECK(book.Route(0xC0A80003) == 0xC0A80003);
  book.SetRoute(0xC0A80002, 0xC0A80002);
  CHECK(book.Route(0xC0A80002) == 0xC0A80002);
}

void Adapters() {
  std::vector<Adapter> list = {
      {"Ethernet 2", "VirtualBox Host-Only", 0xC0A83801, 0xFFFFFF00, false, false, 25},
      {"Wi-Fi", "Intel Wireless", 0xC0A8000B, 0xFFFFFF00, true, false, 35},
      {"Tailscale", "Tailscale Tunnel", 0x64650203, 0xFFC00000, false, false, 5},
      {"ZeroTier One [abc]", "ZeroTier Virtual Port", 0x0A930001, 0xFFFFFF00, false, false, 1},
      {"Loopback", "Software Loopback Interface 1", 0x7F000001, 0xFF000000, false, true, 75},
  };
  CHECK(ChooseAdapter(list, "auto")->name == "Wi-Fi");  // the one with the default route
  CHECK(ChooseAdapter(list, "tailscale")->ip == 0x64650203);
  CHECK(ChooseAdapter(list, "ZEROTIER")->ip == 0x0A930001);
  CHECK(ChooseAdapter(list, "virtual port")->ip == 0x0A930001);  // by description
  CHECK(ChooseAdapter(list, "192.168.56.1")->name == "Ethernet 2");
  CHECK(ChooseAdapter(list, "loopback")->loopback);
  CHECK(!ChooseAdapter(list, "Radmin"));
  CHECK(!ChooseAdapter(list, "10.0.0.9"));
  list[1].gateway = false;  // no default route: the first adapter up
  CHECK(ChooseAdapter(list, "auto")->name == "Ethernet 2");
  CHECK(ChooseAdapter({}, "auto")->loopback);
  CHECK(BroadcastAddress(list[1]) == 0xC0A800FF);
  CHECK(BroadcastAddress(list[2]) == 0x647FFFFF);
  Adapter single{"VPN", "", 0x0A000002, 0xFFFFFFFF};
  CHECK(BroadcastAddress(single) == 0xFFFFFFFF);
  CHECK(!ListAdapters().empty() || true);  // the host's own: whatever it has
}

void Settings(const fs::path& dir) {
  using namespace x360::settings;
  fs::path file = dir / "settings.toml";
  std::ofstream(file) << "[network]\nenabled = true\nadapter = \"ZeroTier\"\nport_offset = 2000\ninstance = 1\n"
                         "peers = [\"100.64.0.2\", \"pc2\"]\n";
  Load(file);
  x360::net::Config n = Get().network;
  CHECK(n.enabled && n.adapter == "ZeroTier" && n.port_offset == 2000 && n.instance == 1);
  CHECK(n.peers.size() == 2 && n.peers[0] == "100.64.0.2" && n.peers[1] == "pc2");
  std::ofstream(file, std::ios::trunc) << "[network]\ninstance = \"auto\"\n";
  Load(file);
  CHECK(Get().network.instance == -1 && !Get().network.enabled && Get().network.port_offset == 10000);
  std::ofstream(file, std::ios::trunc) << "[display]\nfps = 30\n";
  Load(file);
  CHECK(Get().network == x360::net::Config{});  // off by default
  // The file it writes reads back the same.
  Update([](x360::settings::Settings& s) {
    s.network.enabled = true;
    s.network.adapter = "Radmin VPN";
    s.network.instance = 3;
    s.network.peers = {"10.1.2.3"};
  });
  SaveNow();
  Load(file);
  n = Get().network;
  CHECK(n.enabled && n.adapter == "Radmin VPN" && n.instance == 3 && n.peers == std::vector<std::string>{"10.1.2.3"});
}

void TwoInstances() {
  Config config;
  config.enabled = true;
  config.adapter = "loopback";
  config.port_offset = 23000;
  config.instance = 0;
  Stack a, b;
  CHECK(a.Start(config, [](const std::string& line) { std::printf("  a: %s\n", line.c_str()); }));
  config.instance = 1;
  CHECK(b.Start(config, [](const std::string& line) { std::printf("  b: %s\n", line.c_str()); }));
  CHECK(a.instance() == 0 && b.instance() == 1);

  int32_t sa = a.Socket(2, 2, 254);  // VDP
  int32_t sb = b.Socket(2, 2, 17);
  CHECK(sa >= 0x100 && sb >= 0x100);
  CHECK(a.Bind(uint32_t(sa), {0, 1000}) == 0);
  CHECK(b.Bind(uint32_t(sb), {0, 1000}) == 0);  // same guest port, other host port
  Stack::Address name;
  CHECK(b.GetName(uint32_t(sb), name) == 0 && name.port == 1000);

  // Nothing yet: a non-blocking receive would block.
  uint32_t on = 1;
  CHECK(b.IoControl(uint32_t(sb), 0x8004667E, on) == 0);
  uint8_t buffer[256];
  Stack::Address from;
  CHECK(b.RecvFrom(uint32_t(sb), buffer, sizeof(buffer), 0, &from) == -1 && Stack::LastError() == WSAEWOULDBLOCK);

  // a broadcasts on its port: b hears it from instance 0's address and port.
  const char hello[] = "who hosts?";
  Stack::Address broadcast{0xFFFFFFFF, 1000};
  CHECK(a.SendTo(uint32_t(sa), reinterpret_cast<const uint8_t*>(hello), sizeof(hello), 0, &broadcast) == sizeof(hello));
  std::vector<uint32_t> readable = {uint32_t(sb)};
  CHECK(b.Select(&readable, nullptr, nullptr, 2000000) == 1 && readable.size() == 1);
  uint32_t pending = 0;
  CHECK(b.IoControl(uint32_t(sb), 0x4004667F, pending) == 0 && pending == sizeof(hello));
  int32_t n = b.RecvFrom(uint32_t(sb), buffer, sizeof(buffer), 0, &from);
  CHECK(n == sizeof(hello) && !std::memcmp(buffer, hello, sizeof(hello)));
  // A broadcast comes from a's plain address (the one in its XNADDR), and its port.
  CHECK(from.ip == kLoopbackAdvertised && from.port == 1000);
  CHECK(b.book().Resolve(from.ip) == (Peer{kLoopbackAdvertised, 0}));

  // b answers to that address: a hears it from b's handle, and answers back to it.
  const char reply[] = "me";
  CHECK(b.SendTo(uint32_t(sb), reinterpret_cast<const uint8_t*>(reply), sizeof(reply), 0, &from) == sizeof(reply));
  readable = {uint32_t(sa)};
  CHECK(a.Select(&readable, nullptr, nullptr, 2000000) == 1);
  Stack::Address from_b;
  CHECK(a.RecvFrom(uint32_t(sa), buffer, sizeof(buffer), 0, &from_b) == sizeof(reply));
  CHECK(AddressBook::IsHandle(from_b.ip) && from_b.port == 1000);
  CHECK(a.book().Resolve(from_b.ip) == (Peer{LoopbackAddress(1), 1}));  // the address in its XNADDR
  CHECK(a.own_ip() == kLoopbackAdvertised && b.own_ip() == LoopbackAddress(1));
  CHECK(AddressBook::IsHandle(b.own_guest_ip()));
  CHECK(a.SendTo(uint32_t(sa), reinterpret_cast<const uint8_t*>(hello), 4, 0, &from_b) == 4);
  readable = {uint32_t(sb)};
  CHECK(b.Select(&readable, nullptr, nullptr, 2000000) == 1);
  CHECK(b.RecvFrom(uint32_t(sb), buffer, sizeof(buffer), 0, &from) == 4);

  // A datagram that isn't AnyX360's is dropped: the socket isn't readable.
  SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(HostPort(1000, 1, 23000));
  to.sin_addr.s_addr = htonl(0x7F000001);
  sendto(raw, "junk", 4, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
  closesocket(raw);
  Sleep(50);
  readable = {uint32_t(sb)};
  CHECK(b.Select(&readable, nullptr, nullptr, 100000) == 0 && readable.empty());

  // A process started with inherited handles (a title's switch to another of its executables) doesn't
  // keep a closed socket's port: Black Ops II couldn't bind its port again, and restarted for ever.
  {
    int32_t first = a.Socket(2, 2, 17);
    CHECK(a.Bind(uint32_t(first), {0, 1234}) == 0);
    wchar_t command[] = L"cmd.exe /c ping -n 4 127.0.0.1";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    CHECK(CreateProcessW(nullptr, command, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child));
    CHECK(a.Close(uint32_t(first)) == 0);
    int32_t again = a.Socket(2, 2, 17);
    CHECK(a.Bind(uint32_t(again), {0, 1234}) == 0);
    a.Close(uint32_t(again));
    TerminateProcess(child.hProcess, 0);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
  }

  // A port 0 bind gets a guest port of its own; a too small buffer is WSAEMSGSIZE with the sender.
  int32_t sc = a.Socket(2, 2, 17);
  CHECK(a.SendTo(uint32_t(sc), reinterpret_cast<const uint8_t*>(hello), sizeof(hello), 0, &broadcast) == sizeof(hello));
  CHECK(a.GetName(uint32_t(sc), name) == 0 && name.port >= 40000 && name.port < 50000);
  readable = {uint32_t(sb)};
  CHECK(b.Select(&readable, nullptr, nullptr, 2000000) == 1);
  CHECK(b.RecvFrom(uint32_t(sb), buffer, 3, 0, &from) == -1 && Stack::LastError() == WSAEMSGSIZE);
  CHECK(from.port == name.port);

  CHECK(a.Close(uint32_t(sa)) == 0 && a.Close(uint32_t(sc)) == 0 && b.Close(uint32_t(sb)) == 0);
  CHECK(a.Close(uint32_t(sa)) == -1 && Stack::LastError() == WSAENOTSOCK);
  a.Stop();
  b.Stop();

  config.enabled = false;
  Stack off;
  CHECK(!off.Start(config, nullptr));
}

// A synchronous XMsgStartIORequest (no overlapped) leaves its result as the calling thread's last
// error (KTHREAD+0x160): XAM's wrappers read GetLastError() after it returns, and Modern Warfare 3's
// XSessionCreate failed a System Link join on a stale one. Runs on a guest thread of its own.
void SynchronousXMsgLastError() {
  CHECK(x360::kernel::InitMemory());
  uint8_t* base = x360::kernel::g_base;
  uint32_t stale_kept = 0, after_success = 0xFFFFFFFF, after_failure = 0;
  auto thread = x360::kernel::CreateHostDrivenThread("xmsg test", 0, [&] {
    const uint32_t kthread = x360::kernel::CurrentThread()->kthread;
    alignas(64) PPCContext c{};
    x360::kernel::Store<uint32_t>(kthread + 0x160, 0x12);  // stale: ERROR_NO_MORE_FILES
    stale_kept = x360::kernel::Load<uint32_t>(kthread + 0x160);
    c.r3.u64 = 0xFB, c.r4.u64 = 0x000B0014, c.r5.u64 = 0, c.r6.u64 = 0, c.r7.u64 = 0;  // XSessionStart
    __imp__XMsgStartIORequest(c, base);
    after_success = x360::kernel::Load<uint32_t>(kthread + 0x160);
    c.r3.u64 = 0xFB, c.r4.u64 = 0x000BFFFF, c.r5.u64 = 0, c.r6.u64 = 0, c.r7.u64 = 0;  // unknown: fails
    __imp__XMsgStartIORequest(c, base);
    after_failure = x360::kernel::Load<uint32_t>(kthread + 0x160);
  });
  CHECK(thread != nullptr);
  if (thread) WaitForSingleObject(static_cast<HANDLE>(thread->host), INFINITE);
  CHECK(stale_kept == 0x12);
  CHECK(after_success == 0);
  CHECK(after_failure == 1627);  // ERROR_FUNCTION_FAILED
}

}  // namespace

int main() {
  wchar_t temp[MAX_PATH];
  GetTempPathW(MAX_PATH, temp);
  fs::path dir = fs::path(temp) / ("anyx360_test_system_link_" + std::to_string(GetCurrentProcessId()));
  fs::create_directories(dir);
  PureHelpers();
  Book();
  Adapters();
  Settings(dir);
  TwoInstances();
  SynchronousXMsgLastError();
  std::error_code ec;
  fs::remove_all(dir, ec);
  std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
  return failures ? 1 : 0;
}
