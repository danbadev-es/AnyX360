// The game's side of the pose bridge (x360/nui_bridge.h): the shared memory, the bridge process and
// the source that turns its frames into the virtual sensor's snapshots.
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "source.h"
#include "x360/nui_bridge.h"
#include "x360/settings.h"

namespace x360::nui {

// The shared memory and the frame event, created by the game (or opened by a producer in tests).
class BridgeChannel {
 public:
  BridgeChannel() = default;
  ~BridgeChannel();
  BridgeChannel(const BridgeChannel&) = delete;
  BridgeChannel& operator=(const BridgeChannel&) = delete;

  // Creates "<name>" and "<name>-frame" and writes the layout fields. False if they exist already.
  bool Create(const std::wstring& name);
  // Opens an existing channel (the producer's side).
  bool Open(const std::wstring& name);

  const std::wstring& name() const { return name_; }
  std::wstring event_name() const { return name_ + L"-frame"; }
  bridge::Header* header() const { return header_; }
  bridge::Slot* slot(uint32_t index) const;
  void* event() const { return event_; }

  // The consumer's read of the newest slot. kNew: `out` holds a frame newer than `last_frame`
  // (updated); kSame: nothing new; kNone: nothing published yet, or the slot changed while copying.
  enum class Read { kNone, kSame, kNew };
  Read ReadLatest(bridge::Slot& out, uint32_t& last_frame) const;

  // The producer's write of the next slot (tests): fill() writes the contents, seq is handled here.
  template <typename Fill>
  void Publish(Fill&& fill) {
    const uint32_t index = (header_->latest_slot + 1) % bridge::kSlotCount;
    bridge::Slot* s = slot(index);
    BeginWrite(*s);
    fill(*s);
    EndWrite(*s, index);
  }

 private:
  void BeginWrite(bridge::Slot& s);
  void EndWrite(bridge::Slot& s, uint32_t index);

  std::wstring name_;
  void* mapping_ = nullptr;
  void* event_ = nullptr;
  bridge::Header* header_ = nullptr;
};

// The bridge executable's command line: ANYX360_POSE_BRIDGE (a command prefix, e.g. a venv's
// python.exe -I main.py), else pose_bridge\pose_bridge.exe next to the game, else the developer
// build (build\pose_bridge\dist\pose_bridge\pose_bridge.exe above the game). Empty if none.
std::wstring FindBridgeCommand();

// Where the bridge's output goes: the distribution's logs\ folder (next to bin\), else the user
// folder's logs\.
std::filesystem::path BridgeLogPath();

// The bridge process, in a job object that ends it (and anything it starts) when the game exits.
class BridgeProcess {
 public:
  BridgeProcess() = default;
  ~BridgeProcess() { Kill(); }
  BridgeProcess(const BridgeProcess&) = delete;
  BridgeProcess& operator=(const BridgeProcess&) = delete;

  // Starts `command` + `arguments` without a window, its output appended to `log`.
  bool Start(const std::wstring& command, const std::wstring& arguments, const std::filesystem::path& log,
             std::string& error);
  bool Started() const { return process_ != nullptr; }
  bool Running() const;
  uint32_t ExitCode() const;
  // Waits up to `timeout_ms` for it to exit (after the game asked it to quit), then ends it.
  void Stop(uint32_t timeout_ms);
  void Kill();

 private:
  void* job_ = nullptr;
  void* process_ = nullptr;
};

struct BridgeOptions {
  std::wstring name;    // the channel name (default: Local\AnyX360-Nui-<pid>-<n>)
  bool launch = true;   // false: a producer is already there (tests)
  std::filesystem::path log;      // the bridge's output (default: BridgeLogPath())
  // A running bridge whose heartbeat stops this long is restarted. Timed from when the game polled
  // again after standing still itself (a laptop on battery stalls every process for seconds).
  double stall_seconds = 5.0;
  double startup_seconds = 45.0;  // time to load the model and open the camera
};

// Skeletons and images from the webcam pose bridge. The bridge starts with the source and is
// restarted with backoff (1, 2, 4, 8 s; 5 attempts) if it exits or stops responding; the source
// ends it on destruction (the job object ends it if the game dies).
std::unique_ptr<Source> MakeBridgeSource(const settings::KinectSettings& settings, BridgeOptions options = {});

}  // namespace x360::nui
