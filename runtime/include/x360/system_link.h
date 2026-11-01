// System Link: the console's local-network play between AnyX360 instances, on one PC, on a LAN or
// over a LAN-style VPN (see docs/SYSTEMLINK.md). Game-independent: xam_net.cpp converts the guest's
// structures and calls this.
//
// The guest's sockets become host Winsock sockets. Every UDP datagram carries a small header
// (Header) naming the sending instance, its address and the guest port it sent from, so that:
// - several instances on one PC can each have their own host ports (guest port + port_offset +
//   instance * kInstanceStride) and still be told apart;
// - the address a guest sees for a peer is stable (AddressBook): the peer's own IPv4 address for an
//   instance 0, a stand-in address (192.0.2.x, a range never routed) for the other instances, as a
//   console's secure addresses are opaque too. On the "loopback" adapter each instance is a machine
//   of its own instead, 198.51.100.1-4 (never routed either), and every datagram goes to 127.0.0.1:
//   a datagram's sender is then the address in its XNADDR, which Halo 3 checks before it lets a
//   peer join;
// - a datagram the guest sends to the broadcast address goes to every instance's port on the
//   chosen adapter's broadcast address, on the loopback address and on the configured peers.
// There is no key exchange or encryption: both ends are AnyX360.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace x360::net {

constexpr int kMaxInstances = 4;
constexpr uint32_t kInstanceStride = 100;
// What each instance is to the guests on the "loopback" adapter: 198.51.100.1-4 (TEST-NET-2, never
// a real address), each a machine of its own.
constexpr uint32_t kLoopbackAdvertised = 0xC6336401;  // 198.51.100.1
constexpr uint32_t LoopbackAddress(int instance) { return kLoopbackAdvertised + uint32_t(instance); }

// [network] in settings.toml.
struct Config {
  bool enabled = false;
  // "auto" (the adapter with the default route), "loopback" (this PC only: 127.0.0.1, no firewall
  // prompt), part of an adapter's name or description ("Tailscale", "ZeroTier", "Radmin") or one of
  // its IPv4 addresses.
  std::string adapter = "auto";
  // Added to every guest port, the same on every PC (ports below 1024 and 3074 are often taken).
  uint32_t port_offset = 10000;
  // This PC's instance, 0-3; -1 picks the first one not running.
  int instance = -1;
  // Addresses that also get every broadcast (VPNs that don't carry broadcasts, like Tailscale).
  std::vector<std::string> peers;
  bool operator==(const Config&) const = default;
};

// ---- pure helpers (unit-tested) ----------------------------------------------------------------

std::string IpText(uint32_t ip);
std::optional<uint32_t> ParseIp(std::string_view text);

// The host port of a guest port for an instance (wraps past 65535 into 1024 and up).
uint16_t HostPort(uint16_t guest_port, int instance, uint32_t port_offset);

// The datagram header: 'A' 'X' 'S' 'L', version, flags, instance, reserved, be16 guest port,
// be16 reserved, be32 advertised address, be32 sender ID, be32 sequence.
constexpr size_t kHeaderSize = 24;
constexpr uint8_t kHeaderVersion = 1;
constexpr uint8_t kFlagBroadcast = 0x01;
struct Header {
  uint8_t flags = 0;
  uint8_t instance = 0;
  uint16_t guest_port = 0;
  uint32_t advertised_ip = 0;  // the sender's address as its XNADDR has it
  uint32_t sender_id = 0;      // random per process: drops our own broadcasts
  uint32_t sequence = 0;       // per sender: drops a broadcast's second copy
};
void EncodeHeader(const Header& header, uint8_t* out);
std::optional<Header> DecodeHeader(const uint8_t* data, size_t size);

// XNADDR as the guest lays it out (0x24 bytes, big-endian): ina, inaOnline, wPortOnline, abEnet[6],
// abOnline[20]. abEnet names the instance: 02|instance<<4, the address's 4 bytes, 'X'.
constexpr size_t kXnAddrSize = 0x24;
void WriteXnAddr(uint8_t* out, uint32_t ip, int instance);
// The peer an XNADDR names: its ina, and the instance from abEnet (0 when it isn't ours).
struct Peer {
  uint32_t ip = 0;
  int instance = 0;
  bool operator==(const Peer&) const = default;
};
Peer ReadXnAddr(const uint8_t* in);

// XNKID for a system link session: random, with the type bits of its first byte clear.
void MakeSystemLinkKeyId(uint8_t* id8, uint64_t random);

// The addresses the guest sees, both ways.
class AddressBook {
 public:
  // An instance 0 is seen at its own address; the others get a stand-in, 192.0.2.1-254.
  uint32_t GuestAddress(const Peer& peer);
  // The peer behind a guest address: a stand-in's, else instance 0 at that address.
  Peer Resolve(uint32_t guest_ip) const;
  // Where a peer's packets come from when that isn't the address it advertises (a VPN, loopback).
  void SetRoute(uint32_t advertised, uint32_t seen);
  uint32_t Route(uint32_t advertised) const;
  // The advertised address a host address routes for, else itself.
  uint32_t Advertised(uint32_t seen) const;
  static bool IsStandIn(uint32_t ip) { return (ip & 0xFFFFFF00u) == 0xC0000200u; }

 private:
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, uint32_t> stand_ins_;  // (ip << 8 | instance) -> stand-in
  std::unordered_map<uint32_t, Peer> peers_;          // stand-in -> peer
  std::unordered_map<uint32_t, uint32_t> routes_;     // advertised -> seen
  uint32_t next_ = 1;
};

// Host adapters (GetAdaptersAddresses) and the one a setting chooses.
struct Adapter {
  std::string name;         // friendly name ("Wi-Fi", "Tailscale")
  std::string description;  // the driver's ("ZeroTier Virtual Port")
  uint32_t ip = 0;
  uint32_t mask = 0;
  bool gateway = false;     // has a default gateway
  bool loopback = false;
  uint32_t metric = 0;      // the IPv4 route metric: the lowest one with a gateway has the default route
};
std::vector<Adapter> ListAdapters();
// "auto": the first adapter with a gateway, else the first one up that isn't loopback, else
// loopback. "loopback": 127.0.0.1. Otherwise an address, or a case-insensitive part of the name or
// description. Nothing when the setting names no adapter.
std::optional<Adapter> ChooseAdapter(const std::vector<Adapter>& adapters, std::string_view setting);
uint32_t BroadcastAddress(const Adapter& adapter);

// ---- the socket layer --------------------------------------------------------------------------

// Socket handles are the guest's (0x100, 0x104...). Every call returns what Winsock's would (-1
// for an error) and sets LastError() to the WSA error, which the guest's Winsock shares.
class Stack {
 public:
  using Logger = std::function<void(const std::string&)>;

  // Picks the adapter and the instance (a named mutex per instance number), starts Winsock. False
  // when System Link is off or no adapter matches.
  bool Start(const Config& config, Logger logger);
  void Stop();
  bool started() const { return started_; }
  int instance() const { return instance_; }
  const Adapter& adapter() const { return adapter_; }
  // This instance's address in XNADDRs (the adapter's; 198.51.100.1-4 on loopback), and as
  // the guest sees it.
  uint32_t own_ip() const { return advertised_; }
  uint32_t own_guest_ip() { return book_.GuestAddress({advertised_, instance_}); }
  AddressBook& book() { return book_; }

  static uint32_t LastError();
  static void SetLastError(uint32_t error);

  struct Address {
    uint32_t ip = 0;
    uint16_t port = 0;
  };

  int32_t Socket(uint32_t af, uint32_t type, uint32_t protocol);
  int32_t Close(uint32_t s);
  int32_t Bind(uint32_t s, const Address& name);
  int32_t Connect(uint32_t s, const Address& name);
  int32_t Listen(uint32_t s, int backlog);
  int32_t Accept(uint32_t s, Address* peer);
  int32_t Shutdown(uint32_t s, int how);
  int32_t SendTo(uint32_t s, const uint8_t* data, uint32_t length, uint32_t flags, const Address* to);
  int32_t RecvFrom(uint32_t s, uint8_t* data, uint32_t length, uint32_t flags, Address* from);
  // level/name are the guest's (they match Winsock's); values are host-order integers here.
  int32_t SetOption(uint32_t s, uint32_t level, uint32_t name, const uint8_t* value_be, uint32_t length);
  int32_t GetOption(uint32_t s, uint32_t level, uint32_t name, uint32_t& value);
  int32_t GetName(uint32_t s, Address& name);
  int32_t GetPeerName(uint32_t s, Address& name);
  int32_t IoControl(uint32_t s, uint32_t command, uint32_t& argument);
  // Sets keep the sockets that are ready. timeout_us < 0: wait for ever.
  int32_t Select(std::vector<uint32_t>* read, std::vector<uint32_t>* write, std::vector<uint32_t>* except,
                 int64_t timeout_us);
  bool IsSocket(uint32_t s);

 private:
  struct Sock;
  struct Received;
  void Log(const std::string& line);
  std::shared_ptr<Sock> Find(uint32_t s);
  int32_t ImplicitBind(Sock& sock);
  int32_t BindHost(Sock& sock, uint16_t guest_port);
  int32_t SendDatagram(Sock& sock, const uint8_t* data, uint32_t length, uint32_t flags, const Address& to);
  // Receives one of ours (dropping the rest): payload into data, the sender's guest address.
  int32_t ReceiveDatagram(Sock& sock, uint8_t* data, uint32_t length, uint32_t flags, Address* from);
  // Drops pending datagrams that aren't for the guest; true when one for it is left.
  bool DrainForeign(Sock& sock);
  bool Admit(Sock& sock, const Header& header, uint32_t seen_ip);
  void NotePeer(uint32_t advertised, uint32_t seen, int instance, uint16_t guest_port, uint16_t local_port);

  Config config_;
  Logger logger_;
  Adapter adapter_;
  bool loopback_ = false;
  bool trace_ = false;  // ANYX360_NET_TRACE: a line per datagram
  uint32_t advertised_ = 0;
  bool started_ = false;
  int instance_ = 0;
  void* instance_mutex_ = nullptr;
  uint32_t sender_id_ = 0;
  uint32_t broadcast_ip_ = 0;
  std::vector<uint32_t> peer_ips_;
  AddressBook book_;

  std::mutex mutex_;
  std::unordered_map<uint32_t, std::shared_ptr<Sock>> sockets_;
  uint32_t next_handle_ = 0x100;
  uint16_t next_ephemeral_ = 40000;
  uint32_t sequence_ = 0;
  std::vector<uint64_t> known_peers_;  // (advertised << 8 | instance), logged once
};

// The process's stack (started by the host when [network] enabled is set).
Stack& Global();
// " (System Link 2)" for instances 1-3, so that windows on one PC can be told apart; else empty.
std::string WindowTitleSuffix();

}  // namespace x360::net
