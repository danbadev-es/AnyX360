# System Link

System Link is the Xbox 360's local-network play: consoles on one network find each other's games
and play together, with no online service. AnyX360 implements it between copies of AnyX360, so a
game's System Link mode works:

- between two (up to four) copies running **on one PC**;
- between PCs **on a LAN**;
- **over the internet** through a LAN-style VPN (ZeroTier, Radmin VPN, Tailscale), with no server
  of our own and nothing from Microsoft: no servers, code or keys.

It is **off by default**. With it off the game's sockets stay what they were before: emulated
locally, nothing goes to the network, no firewall prompt.

| Game | Status |
|---|---|
| Halo 3 | **Works on one PC**: host, find, join, custom game (Slayer on Last Resort) with two players. Campaign co-op over System Link is refused by the game's hard drive check (below). |
| Halo: Reach | **Hosts and finds**: each copy's roster lists the other's System Link party. Joining doesn't start yet (below). |
| Gears of War 3, Black Ops II | Their System Link modes not tried yet. Their campaigns play as before with System Link on (real sockets) and off. |
| Modern Warfare 3 | Not tried yet. |

## Turning it on

In the game's settings (F2 > Players > System Link), or in its `settings.toml` (in the game's
user folder, `%LOCALAPPDATA%\AnyX360\<game>\settings.toml`):

```toml
[network]
enabled = true
adapter = "auto"      # auto, loopback (this PC only), or an adapter: "Tailscale", "ZeroTier", "Radmin", an address
port_offset = 10000   # added to the game's ports; the same on every PC
instance = "auto"     # auto, or 0-3: each copy running on one PC has its own
peers = []            # addresses that also get the game's broadcasts (Tailscale)
```

The settings are read when the game starts (F2 offers "Restart now"). In the game, System Link is
where the console has it: in Halo 3's lobby, **Network: System Link** ("Interconexión" in
Spanish), then Host game or Find game.

`adapter`:
- `auto`: the adapter with the default route (the one Windows uses for the internet: usually the
  LAN's Wi-Fi or Ethernet). If a VPN is the default route, name the adapter instead.
- `loopback`: this PC only. Nothing leaves the PC and Windows Firewall never asks.
- a part of an adapter's name or description, case-insensitive (`"Tailscale"`, `"ZeroTier"`,
  `"Radmin"`, `"Ethernet 2"`), or one of its IPv4 addresses. If nothing matches, System Link stays
  off and the log lists the adapters.

## Playing

**Every player needs a profile of their own** (the launcher's profiles: each has its own XUID).
Games tell players apart by XUID: a player without a profile, and the profile carried over from
before profiles existed (XUID `E000000012345678`), have the same XUID on every copy of AnyX360,
and Halo: Reach then takes the other copy's party for its own and doesn't list it. The log warns
about it when System Link starts.

### Two copies on one PC

1. Give each copy its own AnyX360 folder, so each has its own profiles and settings: for example
   two folders started with `--user <folder>\<game>` (the profiles live next to the game's
   folder), or two Windows accounts. Each copy signs in with a different profile.
2. Turn System Link on in both, with `adapter = "loopback"` (no firewall prompt) or `"auto"`.
3. Start both. The first is instance 0, the second instance 1: its window title ends in
   "(System Link 2)".
4. One hosts, the other finds and joins.

### Two PCs on a LAN

1. On both PCs: `enabled = true`, `adapter = "auto"` (or the LAN adapter's name), the same
   `port_offset`.
2. Start the game on both. The first time, **Windows Firewall asks** whether the game may use the
   network: allow it on **private networks** (the LAN must be a private network in Windows'
   settings; on a public network Windows blocks the broadcasts). AnyX360 never changes firewall
   rules itself.
3. One hosts, the other finds the game in the System Link list and joins.

### Over the internet with a VPN

The PCs join one virtual network; the game then sees a LAN.

- **ZeroTier** or **Radmin VPN** carry broadcasts like a LAN: create a network, join it on both
  PCs, then `adapter = "ZeroTier"` (or `"Radmin"`) on both.
- **Tailscale** doesn't carry broadcasts, so the game's search can't find the host by itself: list
  the other PC's Tailscale address in `peers` (on both PCs, each the other's), and
  `adapter = "Tailscale"`:

  ```toml
  [network]
  enabled = true
  adapter = "Tailscale"
  peers = ["100.101.102.103"]
  ```

  A Tailscale machine name (MagicDNS) works too.
- Allow the game in Windows Firewall for the VPN's network (it's usually a private or an
  unidentified network: Windows asks once).
- Latency is the VPN's: a direct connection between the PCs is best (ZeroTier and Tailscale say
  "direct" when they have one, "relay" when they don't).

### Checking it

The game's log (`logs\<game>\last_run.log` in the AnyX360 folder, when started from the launcher)
has a line per step:

```
system link: on, adapter "Wi-Fi" 192.168.0.11/24 (broadcast 192.168.0.255), instance 0, guest port + 10000
system link: socket 256 (UDP): guest port 1000 is host port 11000
system link: peer 192.168.0.12 instance 0, first heard from its port 1001 on our port 1001
system link: XNADDR 192.168.0.12 instance 0 is 0.1.0.1 for the game
```

- No "peer" line: the other PC's packets don't arrive (firewall, a different `port_offset`, a
  public network, a VPN without broadcasts and no `peers`).
- `ANYX360_NET_TRACE=1` logs every XNet call and every datagram with its first bytes.

## Design

The runtime's `NetDll_*` exports (`runtime/src/hle/xam_net.cpp`) start with one branch: with
System Link on they call the stack in `runtime/src/system_link.cpp` (game-independent, unit-tested
in `tests/net/test_system_link.cpp`); otherwise they keep the offline emulation, unchanged.

**Sockets.** Each guest socket is a host Winsock socket. VDP sockets (protocol 254, the console's
"voice and data protocol") are UDP: their payload already starts with the game data's length, and
both ends are AnyX360, so it goes as it is. The secure-socket options (`0x5801`, `0x5802`) and the
console's own options are accepted and ignored; the standard ones (`SO_REUSEADDR`, buffers,
timeouts, linger, `TCP_NODELAY`) are passed on. `select`, `ioctlsocket` (`FIONBIO`, `FIONREAD`),
blocking and non-blocking calls behave as Winsock's. ICMP "port unreachable" errors are turned off
for UDP sockets, so a datagram to an instance that isn't running doesn't fail the next receive.

**Ports.** A guest port P is host port `P + port_offset + instance * 100` (wrapped into 1024-65535).
`port_offset` (default 10000) keeps the game off ports Windows or other software use (Halo binds
1000 and 1001; 3074 is often taken). A bind to port 0 gets a guest port of its own (40000-49999)
whose host port is free.

**The datagram header.** Every UDP datagram starts with 24 bytes: `AXSL`, a version, flags
(broadcast), the sender's instance, its guest port, its address as its XNADDR has it, a random ID
per process and a sequence number. The receiver strips it and reports the sender's guest port;
datagrams without it (anything else on the port) are dropped, and `select`/`FIONREAD` don't report
them. The sender's ID drops our own broadcasts (the console doesn't deliver them to their sender);
the sequence number drops a broadcast's second copy (sent both to the subnet and to this PC).

**Broadcasts.** A datagram to 255.255.255.255 (or the adapter's subnet broadcast) goes, for each
instance number 0-3, to that instance's host port on the adapter's broadcast address, on
127.0.0.1 (other copies on this PC) and on every `peers` address.

**Addresses.**
- `XNetGetTitleXnAddr` reports the adapter's IPv4 address in `ina`, and an `abEnet` (MAC) that
  names the instance: `02|instance<<4`, the address's 4 bytes, `'X'` (locally administered, so
  unique per PC and instance). The link status is "active, 100 Mbps, full duplex".
- The addresses the game sees for peers (`XNetXnAddrToInAddr`, a unicast datagram's sender,
  `accept`, `getpeername`) are **handles shaped like the console's secure addresses**, 0.1.x.y,
  one per peer and instance for as long as the process runs. A broadcast's sender is its plain
  address (the one in its XNADDR), as on the console, where broadcasts aren't secured. A real console's secure addresses are opaque
  0.x.x.x values too, and Halo 3 only connects to an address whose first byte is 0 (its observer,
  `823B7420`, refuses any other: "can't join, incompatible network configuration").
  `XNetInAddrToXnAddr` maps a handle back to the peer's XNADDR.
- On the `loopback` adapter each instance is a machine of its own to the games, 198.51.100.1-4 (a
  documentation range, never routed), so a datagram's sender is always the address in its XNADDR;
  everything goes to 127.0.0.1.
- When a peer's packets arrive from another address than the one it advertises (a VPN in between),
  replies follow the packets.

**Keys and connections.** There is no key exchange and no encryption: both ends are AnyX360.
`XNetCreateKey` makes a system link session ID (the type bits of its first byte clear) and a
random key; `XNetRegisterKey`, `XNetConnect`, `XNetUnregisterInAddr` succeed;
`XNetGetConnectStatus` says connected. `XNetQosLookup` answers every peer at once ("contacted",
1 ms, 100 Mbps) without the host's QoS data. Sessions made with `XSessionCreate` (Call of Duty,
Gears of War 3) carry the System Link XNADDR. Nothing contacts an online service: DNS lookups,
`XNetServerToInAddr` and the QoS service stay unavailable.

**The instance.** `instance = "auto"` takes the first number (0-3) whose named mutex
(`Local\AnyX360.SystemLink.<n>`) no other running copy holds, so copies on one PC never share
ports. Instances 1-3 add "(System Link n+1)" to the window's title.

## Games

### Halo 3

Tested with two copies on one PC (`adapter = "loopback"`), each with its own user folder and
profile:

1. Copy A: the lobby (Custom games or Campaign), **Network > System Link > Host game**: "this party
   is open to System Link".
2. Copy B: **Network > System Link > Find game**: the list shows A's game (leader, mode, map,
   players); A joins it: both lobbies show both players, "LinkA is your party leader".
3. A changes the mode to Custom games (B follows) and starts: **Slayer on Last Resort with both
   players**, each in their own window, the scoreboard listing both; it held for minutes of play.

The game binds UDP 1000 (VDP: the game itself) and UDP 1001 (broadcast search, join messages).
What it took: the link status and XNADDR, real sockets, and the secure-style addresses above.

- **Campaign co-op over System Link** shows "one or more players don't have an Xbox 360 hard
  drive" (as Theater does, even offline): the game's hard drive check fails on AnyX360, System Link
  or not. To be investigated with Theater.

### Halo: Reach

Tested the same way, each copy with its own profile:

1. Main menu > Custom (or any mode), **Y: Select network > System Link games**: "this party is
   open to System Link".
2. The other copy does the same: its roster gets a **System Link** section listing the first
   copy's party (mode, players), and the player's card offers "X: Join". Both copies list each
   other.
3. **Joining doesn't start**: Join (X, or A > Join player) does nothing, and the copy sends
   nothing and makes no XNet call; the game refuses before connecting. On the press the game
   thread enumerates downloadable content (`XamContentCreateEnumerator` type 2), hashes two
   0xD9AC-byte blocks (`XeCryptSha`) and checks privileges, then stops: a local compatibility
   check, not the network. Where to resume: the roster's join action, as Halo 3's join was found
   (Halo 3's observer `823F35E0`/`823B7420`, its join state machine `8243BCB8`).

What it took so far: the two players must have different XUIDs (above); with the same one each
copy accepted the other's search replies (`8232DE90` adds them to the results) but its roster
showed nothing. Broadcasts' senders are reported as plain addresses (changed in the same run). The
game binds UDP 1000 (VDP) and 1001 (broadcasts).

## Known issues and limits

- Two PCs and a VPN haven't been tested yet (only one PC): the design is meant for them, and the
  log shows each step.
- Copies on one PC with `adapter = "auto"` rely on Windows delivering a subnet broadcast to the
  PC's own sockets as well as unicast copies to 127.0.0.1; `loopback` is the sure way on one PC.
  Mixing copies on one PC with copies on other PCs works only through `auto`.
- `XNetQosLookup` doesn't carry the host's QoS data; a game that shows it (ping bars, a host's
  description) shows nothing there.
- TCP connections between copies on one PC all appear to come from instance 0 (no header on
  streams); the games tested use UDP only.
- Xbox LIVE, its matchmaking and its services are not part of this and never will be.
