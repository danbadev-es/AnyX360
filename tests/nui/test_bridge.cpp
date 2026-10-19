// The pose bridge protocol, the game's side: the layout and settings it writes, a fake producer's
// frames turned into snapshots, the seqlock under a producer that never pauses, an incompatible
// bridge, a stalled heartbeat, and the process supervision. No camera is opened.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "../../runtime/src/nui/bridge.h"
#include "x360/hle.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

PPCFuncMapping PPCFuncMappings[] = {{0, nullptr}};

namespace nui = x360::nui;
namespace bridge = x360::nui::bridge;

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

int64_t Qpc() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

std::wstring ChannelName(const wchar_t* tag) {
  return L"Local\\AnyX360-Nui-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + tag;
}

bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// The producer's side, as tools/pose_bridge does it.
struct FakeBridge {
  nui::BridgeChannel channel;
  uint32_t frame = 0;

  bool Open(const std::wstring& name) {
    if (!channel.Open(name)) return false;
    bridge::Header* h = channel.header();
    h->producer_version_major = bridge::kVersionMajor;
    h->producer_version_minor = bridge::kVersionMinor;
    h->camera_width = 640;
    h->camera_height = 480;
    h->camera_fps = 30;
    h->state = bridge::kRunning;
    Beat();
    return true;
  }
  void Beat() { channel.header()->heartbeat_qpc = Qpc(); }

  // One person whose joints are all at (frame, 1, 2); images filled with the frame number.
  void Publish(bool images) {
    ++frame;
    channel.Publish([&](bridge::Slot& s) {
      s.frame_number = frame;
      s.capture_qpc = Qpc();
      s.body_count = 1;
      bridge::Body& b = s.bodies[0];
      b.id = 7;
      b.state = bridge::kBodyTracked;
      for (uint32_t j = 0; j < bridge::kJoints; ++j) {
        b.joints[j][0] = float(frame);
        b.joints[j][1] = 1;
        b.joints[j][2] = 2;
        b.joint_state[j] = 2;
      }
      b.position[0] = float(frame);
      s.flags = images ? bridge::kSlotHasImages : 0;
      if (images) {
        std::memset(s.color, int(frame & 0xFF), sizeof(s.color));
        std::fill(std::begin(s.depth), std::end(s.depth), uint16_t(frame));
      }
    });
  }
};

}  // namespace

int main() {
  x360::settings::KinectSettings settings;
  settings.camera = 1;
  settings.mirror = true;
  settings.players = 1;
  settings.fov = 70;
  settings.smoothing = 0.25f;
  settings.camera_height = 0.9f;
  settings.model = "lite";

  {  // The game creates the channel with its layout and settings; a bridge's frames become snapshots.
    nui::BridgeOptions options;
    options.name = ChannelName(L"frames");
    options.launch = false;
    auto source = nui::MakeBridgeSource(settings, options);
    FakeBridge producer;
    CHECK(producer.channel.Open(options.name));
    const bridge::Header* h = producer.channel.header();
    CHECK(h->magic == bridge::kMagic);
    CHECK(h->version_major == bridge::kVersionMajor);
    CHECK(h->header_size == sizeof(bridge::Header));
    CHECK(h->slot_size == sizeof(bridge::Slot));
    CHECK(h->slot_count == 3 && h->max_bodies == 6);
    CHECK(h->latest_slot == bridge::kSlotCount);  // nothing published
    CHECK(h->camera_index == 1 && h->mirror == 1 && h->players == 1 && h->model == 0);
    CHECK(h->fov_deg == 70.0f && h->smoothing == 0.25f && h->camera_height_m == 0.9f);
    CHECK(h->control_seq == 1);
    settings.players = 2;
    source->Configure(settings);
    CHECK(h->players == 2 && h->control_seq == 2);

    nui::Snapshot s;
    source->Poll(0, s);
    CHECK(s.body_count == 0);  // no producer running yet
    CHECK(Contains(source->Message(), "starting"));

    CHECK(producer.Open(options.name));
    source->Poll(0.1, s);
    CHECK(s.body_count == 0);  // running, nothing published
    producer.Publish(true);
    source->Poll(0.2, s);
    CHECK(s.body_count == 1);
    CHECK(s.bodies[0].id == 7);
    CHECK(s.bodies[0].joints[nui::kHandRight][0] == 1.0f && s.bodies[0].joints[nui::kHead][2] == 2.0f);
    CHECK(s.bodies[0].joint_state[nui::kHead] == nui::kJointTracked);
    CHECK(s.color && s.color->size() == 640 * 480 * 4 && (*s.color)[1000] == 1);
    CHECK(s.depth && s.depth->size() == 320 * 240 && (*s.depth)[500] == 1);
    CHECK(Contains(source->Message(), "webcam 640x480"));
    producer.Publish(false);
    source->Poll(0.3, s);
    CHECK(s.bodies[0].position[0] == 2.0f && !s.color);
    source->Poll(0.4, s);  // nothing new: the same frame again
    CHECK(s.body_count == 1 && s.bodies[0].position[0] == 2.0f);

    // A stalled heartbeat (2 s) takes the players away; a new beat brings them back.
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    source->Poll(2.6, s);
    CHECK(s.body_count == 0);
    CHECK(Contains(source->Message(), "stopped responding"));
    producer.Beat();
    source->Poll(2.7, s);
    CHECK(s.body_count == 1);

    // A bridge still starting (loading its model) gets the startup grace even after its first beat.
    producer.channel.header()->state = bridge::kStarting;
    std::this_thread::sleep_for(std::chrono::milliseconds(2200));
    source->Poll(5.0, s);
    CHECK(!Contains(source->Message(), "stopped responding"));
    producer.channel.header()->state = bridge::kRunning;
    producer.Beat();

    // Windows' camera privacy setting.
    producer.channel.header()->state = bridge::kCameraDenied;
    source->Poll(2.8, s);
    CHECK(s.body_count == 0 && Contains(source->Message(), "Privacy"));
  }

  {  // The seqlock: a producer that never pauses, a reader that never sees half a frame.
    nui::BridgeOptions options;
    options.name = ChannelName(L"seqlock");
    options.launch = false;
    auto source = nui::MakeBridgeSource(settings, options);
    FakeBridge producer;
    CHECK(producer.Open(options.name));
    nui::BridgeChannel reader;
    CHECK(reader.Open(options.name));
    constexpr uint32_t kFrames = 10000;
    std::atomic<bool> done{false};
    std::thread writer([&] {
      for (uint32_t i = 0; i < kFrames; ++i) producer.Publish(true);
      done = true;
    });
    auto copy = std::make_unique<bridge::Slot>();
    uint32_t last = 0, good = 0, retries = 0, broken = 0, backwards = 0;
    while (!done) {
      const uint32_t previous = last;
      switch (reader.ReadLatest(*copy, last)) {
        case nui::BridgeChannel::Read::kNew: {
          ++good;
          const uint32_t f = copy->frame_number;
          if (f <= previous) ++backwards;
          const uint8_t c = uint8_t(f & 0xFF);
          if (copy->bodies[0].joints[0][0] != float(f) || copy->bodies[0].joints[19][0] != float(f) ||
              copy->color[0] != c || copy->color[sizeof(copy->color) / 2] != c ||
              copy->color[sizeof(copy->color) - 1] != c || copy->depth[0] != uint16_t(f) ||
              copy->depth[std::size(copy->depth) - 1] != uint16_t(f))
            ++broken;
          break;
        }
        case nui::BridgeChannel::Read::kNone:
          ++retries;
          break;
        default:
          break;
      }
    }
    writer.join();
    std::printf("  %u frames read, %u retries\n", good, retries);
    CHECK(producer.frame == kFrames);
    CHECK(good > 100);
    CHECK(broken == 0);
    CHECK(backwards == 0);
  }

  {  // A bridge with another protocol version is refused for good.
    nui::BridgeOptions options;
    options.name = ChannelName(L"version");
    options.launch = false;
    auto source = nui::MakeBridgeSource(settings, options);
    FakeBridge producer;
    CHECK(producer.Open(options.name));
    producer.channel.header()->producer_version_major = bridge::kVersionMajor + 1;
    producer.Publish(false);
    nui::Snapshot s;
    source->Poll(0, s);
    CHECK(s.body_count == 0);
    CHECK(Contains(source->Message(), ("protocol " + std::to_string(bridge::kVersionMajor + 1) + ".").c_str()));
    producer.channel.header()->producer_version_major = bridge::kVersionMajor;
    source->Poll(0.1, s);
    CHECK(s.body_count == 0);
  }

  {  // Supervision: a bridge that exits is restarted after the backoff; the source ends a running one.
    const auto dir = std::filesystem::temp_directory_path() / ("anyx360_test_bridge_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    _wputenv_s(L"ANYX360_POSE_BRIDGE", L"cmd.exe /d /c exit 5 &rem");
    CHECK(nui::FindBridgeCommand() == L"cmd.exe /d /c exit 5 &rem");
    nui::BridgeOptions options;
    options.name = ChannelName(L"process");
    options.log = dir / "pose_bridge.log";
    auto source = nui::MakeBridgeSource(settings, options);
    nui::Snapshot s;
    source->Poll(0, s);  // starts it
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    source->Poll(0.5, s);
    std::printf("  %s\n", source->Message().c_str());
    CHECK(Contains(source->Message(), "exited (code 5)"));
    CHECK(Contains(source->Message(), "restarting in 1 s"));
    source->Poll(1.0, s);  // not yet
    CHECK(Contains(source->Message(), "restarting in 1 s"));
    source->Poll(1.6, s);  // second start
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    source->Poll(1.7, s);
    std::printf("  %s\n", source->Message().c_str());
    CHECK(Contains(source->Message(), "restarting in 2 s"));
    source.reset();

    nui::BridgeProcess process;
    std::string error;
    CHECK(process.Start(L"cmd.exe /d /c ping -n 30 127.0.0.1", L"", dir / "ping.log", error));
    CHECK(process.Running());
    process.Stop(100);
    CHECK(!process.Started());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }

  std::printf("\n%s\n", failures ? "FAILED" : "OK");
  return failures ? 1 : 0;
}
