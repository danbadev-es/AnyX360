// Skeletons and images from the webcam pose bridge (bridge.h, x360/nui_bridge.h): the shared memory
// channel, and the source that starts the bridge, watches it and reads its newest frame at each
// sensor tick.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "bridge.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::nui {

using namespace bridge;

namespace {

constexpr size_t kMappingSize = sizeof(Header) + size_t(kSlotCount) * sizeof(Slot);
constexpr uint32_t kExitIncompatible = 3;   // the bridge's exit code for a layout it can't speak
constexpr double kBackoff[] = {1, 2, 4, 8};  // seconds before the 2nd to 5th start
constexpr uint32_t kMaxStarts = 5;
constexpr double kHealthySeconds = 60;  // a run this long starts the count again

int64_t Qpc() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

uint32_t ModelIndex(const std::string& model) { return model == "lite" ? 0 : model == "heavy" ? 2 : 1; }

std::string Format(const char* format, auto... args) {
  char text[256];
  std::snprintf(text, sizeof(text), format, args...);
  return text;
}

}  // namespace

// ---- BridgeChannel ----

BridgeChannel::~BridgeChannel() {
  if (header_) UnmapViewOfFile(header_);
  if (mapping_) CloseHandle(mapping_);
  if (event_) CloseHandle(event_);
}

bool BridgeChannel::Create(const std::wstring& name) {
  name_ = name;
  mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, DWORD(uint64_t(kMappingSize) >> 32),
                                DWORD(kMappingSize), name.c_str());
  if (!mapping_ || GetLastError() == ERROR_ALREADY_EXISTS) return false;
  event_ = CreateEventW(nullptr, FALSE, FALSE, event_name().c_str());
  header_ = static_cast<Header*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kMappingSize));
  if (!event_ || !header_) return false;
  // A new mapping is zeroed: write the layout, nothing published yet.
  header_->magic = kMagic;
  header_->version_major = kVersionMajor;
  header_->version_minor = kVersionMinor;
  header_->header_size = sizeof(Header);
  header_->slot_size = sizeof(Slot);
  header_->slot_count = kSlotCount;
  header_->max_bodies = kMaxBodies;
  header_->latest_slot = kSlotCount;
  return true;
}

bool BridgeChannel::Open(const std::wstring& name) {
  name_ = name;
  mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
  event_ = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, event_name().c_str());
  if (!mapping_ || !event_) return false;
  header_ = static_cast<Header*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kMappingSize));
  return header_ != nullptr;
}

Slot* BridgeChannel::slot(uint32_t index) const {
  return reinterpret_cast<Slot*>(reinterpret_cast<uint8_t*>(header_) + sizeof(Header) + size_t(index) * sizeof(Slot));
}

BridgeChannel::Read BridgeChannel::ReadLatest(Slot& out, uint32_t& last_frame) const {
  const uint32_t index = header_->latest_slot;
  if (index >= kSlotCount) return Read::kNone;
  const Slot* s = slot(index);
  const uint32_t before = s->seq;
  if (before == 0 || (before & 1)) return Read::kNone;
  std::atomic_thread_fence(std::memory_order_acquire);
  if (s->frame_number == last_frame) return Read::kSame;
  std::memcpy(&out, const_cast<const Slot*>(s), sizeof(Slot));
  std::atomic_thread_fence(std::memory_order_acquire);
  if (s->seq != before || out.seq != before) return Read::kNone;  // overwritten while copying
  last_frame = out.frame_number;
  return Read::kNew;
}

void BridgeChannel::BeginWrite(Slot& s) {
  s.seq = s.seq | 1;
  std::atomic_thread_fence(std::memory_order_seq_cst);
}

void BridgeChannel::EndWrite(Slot& s, uint32_t index) {
  std::atomic_thread_fence(std::memory_order_seq_cst);
  s.seq = s.seq + 1;
  header_->latest_slot = index;
  SetEvent(event_);
}

// ---- the source ----

namespace {

class BridgeSource : public Source {
 public:
  BridgeSource(const settings::KinectSettings& settings, BridgeOptions options)
      : options_(std::move(options)), slot_(std::make_unique<Slot>()) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    frequency_ = double(frequency.QuadPart);
    // A name per source: a new one can start while the previous one is still stopping.
    static std::atomic<uint32_t> instances{0};
    if (options_.name.empty())
      options_.name = L"Local\\AnyX360-Nui-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                      std::to_wstring(instances++);
    if (!channel_.Create(options_.name)) {
      problem_ = "cannot create the pose bridge's shared memory";
      gave_up_ = true;
      KLOG_E("nui: %s (%lu)", problem_.c_str(), GetLastError());
      message_ = problem_;
      return;
    }
    Configure(settings);
    if (options_.launch) log_ = options_.log.empty() ? BridgeLogPath() : options_.log;
  }

  ~BridgeSource() override {
    if (Header* h = channel_.header()) h->quit = 1;
    if (process_.Started()) process_.Stop(1000);
  }

  void Configure(const settings::KinectSettings& settings) override {
    Header* h = channel_.header();
    if (!h) return;
    h->camera_index = settings.camera;
    h->mirror = settings.mirror;
    h->players = std::clamp<uint32_t>(settings.players, 1, 2);
    h->model = ModelIndex(settings.model);
    h->fov_deg = settings.fov;
    h->smoothing = settings.smoothing;
    h->camera_height_m = settings.camera_height;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    h->control_seq = h->control_seq + 1;
  }

  void Poll(double time, Snapshot& out) override {
    out = Snapshot{};
    if (!channel_.header()) return;
    Supervise(time);
    message_ = Describe();
    if (const uint32_t state = channel_.header()->state; state != logged_state_ && process_.Running()) {
      logged_state_ = state;
      KLOG_I("nui: pose bridge: %s", message_.c_str());
    }
    if (!Live()) {  // nobody in view; the newest frame is read again when the bridge recovers
      snapshot_ = Snapshot{};
      last_frame_ = 0;
      return;
    }
    if (channel_.ReadLatest(*slot_, last_frame_) == BridgeChannel::Read::kNew) Convert();
    out = snapshot_;
  }

  std::string Message() const override { return message_; }

  void Calibrate() override {
    if (Header* h = channel_.header()) h->calibrate = h->calibrate + 1;
  }

  void Restart() override {
    if (!options_.launch) return;
    process_.Kill();
    starts_ = 0;
    next_start_ = 0;
    gave_up_ = incompatible_ = false;
    problem_.clear();
    if (Header* h = channel_.header()) h->producer_version_major = h->producer_version_minor = 0;
    KLOG_I("nui: restarting the pose bridge");
  }

  void Stats(Status& status) const override {
    const Header* h = channel_.header();
    if (!h) return;
    status.camera_running = Live();
    status.camera_width = h->camera_width;
    status.camera_height = h->camera_height;
    status.camera_fps = h->camera_fps;
    status.inference_ms = h->inference_ms;
    status.latency_ms = float(latency_ms_);
    status.restarts = restarts_;
  }

 private:
  // Starts the bridge when it's due, restarts it if it exited or stopped responding.
  void Supervise(double time) {
    Header* h = channel_.header();
    if (h->producer_version_major && h->producer_version_major != kVersionMajor) incompatible_ = true;
    if (incompatible_) {
      if (process_.Started()) process_.Kill();
      gave_up_ = true;
      return;
    }
    stalled_ = Stale(time);
    if (!options_.launch || gave_up_) return;
    if (!process_.Started()) {
      if (time >= next_start_) StartBridge(time);
      return;
    }
    if (!process_.Running()) {
      const uint32_t code = process_.ExitCode();
      process_.Kill();
      if (code == kExitIncompatible) {
        incompatible_ = gave_up_ = true;
        KLOG_E("nui: the pose bridge can't speak protocol %u.%u", kVersionMajor, kVersionMinor);
        return;
      }
      Failed(time, Format("the pose bridge exited (code %u)", code));
      return;
    }
    if (stalled_) {
      process_.Kill();
      Failed(time, "the pose bridge stopped responding");
      return;
    }
    if (starts_ > 1 && h->state == kRunning && time - started_ > kHealthySeconds) starts_ = 1;
  }

  void StartBridge(double time) {
    const std::wstring command = FindBridgeCommand();
    if (command.empty()) {
      gave_up_ = true;
      problem_ = "the pose bridge is missing (pose_bridge\\pose_bridge.exe next to the game)";
      KLOG_W("nui: %s", problem_.c_str());
      return;
    }
    // Producer fields of a previous run.
    Header* h = channel_.header();
    h->state = kStarting;
    h->heartbeat_qpc = 0;
    h->latest_slot = kSlotCount;
    h->status[0] = 0;
    h->quit = 0;
    for (uint32_t i = 0; i < kSlotCount; ++i) channel_.slot(i)->seq = 0;
    last_frame_ = 0;
    std::error_code ec;
    if (starts_ == 0 && std::filesystem::exists(log_, ec)) {
      std::filesystem::path previous = log_;
      std::filesystem::rename(log_, previous.replace_filename("pose_bridge.previous.log"), ec);
    }
    if (launched_) ++restarts_;
    launched_ = true;
    ++starts_;
    started_ = time;
    const std::wstring arguments = L"--shm " + channel_.name() + L" --event " + channel_.event_name() +
                                   L" --parent " + std::to_wstring(GetCurrentProcessId());
    std::string error;
    if (!process_.Start(command, arguments, log_, error)) {
      Failed(time, error);
      return;
    }
    problem_.clear();
    KLOG_I("nui: pose bridge started (%ls), output in %s", command.c_str(), log_.string().c_str());
  }

  void Failed(double time, const std::string& reason) {
    if (starts_ >= kMaxStarts) {
      gave_up_ = true;
      problem_ = reason + "; giving up (see logs\\pose_bridge.log)";
    } else {
      next_start_ = time + kBackoff[starts_ - 1];
      problem_ = Format("%s; restarting in %.0f s", reason.c_str(), kBackoff[starts_ - 1]);
    }
    KLOG_W("nui: %s", problem_.c_str());
  }

  // No heartbeat for stall_seconds (or none at all since the start, for startup_seconds).
  bool Stale(double time) const {
    const int64_t beat = channel_.header()->heartbeat_qpc;
    if (!beat) return options_.launch && process_.Started() && time - started_ > options_.startup_seconds;
    return double(Qpc() - beat) / frequency_ > options_.stall_seconds;
  }

  bool Live() const {
    if (incompatible_ || stalled_ || channel_.header()->state != kRunning) return false;
    return !options_.launch || process_.Running();
  }

  void Convert() {
    const Slot& s = *slot_;
    Snapshot snapshot;
    snapshot.body_count = std::min<uint32_t>(s.body_count, kSkeletonCount);
    for (uint32_t i = 0; i < snapshot.body_count; ++i) {
      const bridge::Body& from = s.bodies[i];
      nui::Body& to = snapshot.bodies[i];
      to.id = from.id ? from.id : 1000 + i;
      std::memcpy(to.position, from.position, sizeof(to.position));
      for (uint32_t j = 0; j < kJointCount; ++j) {
        std::memcpy(to.joints[j], from.joints[j], sizeof(to.joints[j]));
        to.joint_state[j] = std::min<uint8_t>(from.joint_state[j], kJointTracked);
      }
      to.quality = from.quality;
    }
    if (s.flags & kSlotHasImages) {
      snapshot.color = std::make_shared<const std::vector<uint8_t>>(std::begin(s.color), std::end(s.color));
      snapshot.depth = std::make_shared<const std::vector<uint16_t>>(std::begin(s.depth), std::end(s.depth));
    }
    snapshot_ = std::move(snapshot);
    if (s.capture_qpc) {
      const double latency = double(Qpc() - s.capture_qpc) / frequency_ * 1000;
      latency_ms_ = latency_ms_ ? latency_ms_ * 0.9 + latency * 0.1 : latency;
    }
  }

  std::string Describe() const {
    const Header* h = channel_.header();
    if (incompatible_)
      return Format("the pose bridge speaks protocol %u.%u, the game %u.%u: reinstall the game",
                    h->producer_version_major, h->producer_version_minor, kVersionMajor, kVersionMinor);
    if (gave_up_) return problem_;
    if (options_.launch && !process_.Running()) return problem_.empty() ? "starting the pose bridge" : problem_;
    if (stalled_) return "the pose bridge stopped responding";
    char status[sizeof(h->status) + 1] = {};
    std::memcpy(status, const_cast<const char*>(h->status), sizeof(h->status));
    switch (h->state) {
      case kStarting:
        return *status ? status : "starting the camera";
      case kRunning:
        return Format("webcam %ux%u, %.0f fps, inference %.0f ms, latency %.0f ms", h->camera_width,
                      h->camera_height, h->camera_fps, h->inference_ms, latency_ms_);
      case kNoCamera:
        return *status ? status : "no webcam found";
      case kCameraDenied:
        return "Windows blocks the camera: Settings > Privacy & security > Camera > Let desktop apps access "
               "your camera";
      case kError:
        return *status ? status : "the pose bridge failed";
      default:
        return "the pose bridge stopped";
    }
  }

  BridgeOptions options_;
  BridgeChannel channel_;
  BridgeProcess process_;
  std::filesystem::path log_;
  std::unique_ptr<Slot> slot_;  // the copy of the newest frame (1.4 MB)
  double frequency_ = 1;
  uint32_t last_frame_ = 0;
  uint32_t logged_state_ = ~0u;
  Snapshot snapshot_;
  double latency_ms_ = 0;

  uint32_t starts_ = 0;    // since the last success or Restart (for the backoff)
  uint32_t restarts_ = 0;  // in all
  bool launched_ = false;
  double started_ = 0, next_start_ = 0;
  bool gave_up_ = false, incompatible_ = false, stalled_ = false;
  std::string problem_, message_ = "starting the pose bridge";
};

}  // namespace

std::unique_ptr<Source> MakeBridgeSource(const settings::KinectSettings& settings, BridgeOptions options) {
  return std::make_unique<BridgeSource>(settings, std::move(options));
}

}  // namespace x360::nui
