// Settings panel (F2): display (window size, fullscreen, upscaling, FXAA, frame rate, render scale
// with a restart), mouse and keyboard, players' devices, the virtual Kinect (Kinect games), and
// frame pacing graphs. While it is open the game gets no keyboard and mouse input and the cursor is
// free.
//
// Everything that changes the window or the presenter is posted to the UI thread instead of running
// inside OnDraw (Xenia forbids surface changes during a paint).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include <thread>

#include "anyx360_version.h"
#include "frame_timing.h"
#include "gpu_internal.h"
#include "stats_overlay.h"
#include "third_party/imgui/imgui.h"
#include "x360/input.h"
#include "x360/kernel.h"
#include "x360/nui.h"
#include "x360/settings.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/window_win.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::gpu {

namespace {

using Upscale = DisplayConfig::Upscale;
using settings::Settings;

// ---- actions (UI thread; shared with ANYX360_SETTINGS_TEST) ------------------------------------

void SetResolution(uint32_t lines) {
  uint32_t width, height;
  settings::ResolutionSize(lines, width, height);
  settings::Update([&](Settings& s) { s.display.width = width, s.display.height = height; });
  ApplyWindowSize(width, height);
}

void SetUpscaleSetting(Upscale upscale) {
  settings::Update([&](Settings& s) { s.display.upscale = upscale; });
  SetUpscale(upscale);
}

void SetFxaaSetting(bool fxaa) {
  settings::Update([&](Settings& s) { s.display.fxaa = fxaa; });
  SetFxaa(fxaa);
}

void SetVsyncSetting(bool vsync) {
  settings::Update([&](Settings& s) { s.display.vsync = vsync; });
  SetVsync(vsync);
}

void SetDisplayLockSetting(bool lock) {
  settings::Update([&](Settings& s) { s.pacing.display_lock = lock; });
  SetDisplayLock(lock);
}

void SetFps(uint32_t fps) {
  settings::Update([&](Settings& s) { s.display.target_fps = fps; });
  SetTargetFps(fps);
}

void SetRenderScale(uint32_t scale) {
  settings::Update([&](Settings& s) { s.display.render_scale = scale; });
  KLOG_I("display: render scale %ux after a restart", scale);
}

void ShowStats(bool show) {
  SetStatsOverlayVisible(show);
  settings::Update([&](Settings& s) { s.show_stats = show; });
}

// The [kinect] settings: saved, and applied by the virtual sensor (it follows the settings).
void UpdateKinect(const std::function<void(settings::KinectSettings&)>& change) {
  settings::Update([&](Settings& s) { change(s.kinect); });
}

std::string g_restart_error;
std::string g_select_tab;  // test hook: tab to bring to the front

void RestartNow() { g_restart_error = Restart(); }

// ---- the panel --------------------------------------------------------------------------------

const char* const kDeviceLabels[] = {"Keyboard + mouse", "Controller (first free)", "Controller 1", "Controller 2",
                                     "Controller 3",     "Controller 4",            "None"};

int DeviceIndex(input::Device d) {
  if (d.kind == input::DeviceKind::kKeyboardMouse) return 0;
  if (d.kind == input::DeviceKind::kXInput) return d.xinput < 0 ? 1 : 2 + d.xinput;
  return 6;
}

input::Device DeviceAt(int index) {
  if (index == 0) return {input::DeviceKind::kKeyboardMouse, -1};
  if (index == 1) return {input::DeviceKind::kXInput, -1};
  if (index >= 2 && index <= 5) return {input::DeviceKind::kXInput, int8_t(index - 2)};
  return {};
}

// "(command line)" when this session's value comes from a flag and differs from the saved one.
void CommandLineNote(bool overridden) {
  if (!overridden) return;
  ImGui::SameLine();
  ImGui::TextDisabled("(command line)");
}

class SettingsPanel final : public xe::ui::ImGuiDialog {
 public:
  explicit SettingsPanel(xe::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    Settings s = settings::Get();
    Settings saved = settings::GetSaved();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    if (ImGui::Begin("AnyX360 settings (F2)###anyx360_settings", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
      if (ImGui::BeginTabBar("##tabs")) {
        auto tab_flags = [](const char* name) {
          if (g_select_tab != name) return ImGuiTabItemFlags_None;
          g_select_tab.clear();
          return ImGuiTabItemFlags_SetSelected;
        };
        if (ImGui::BeginTabItem("Display", nullptr, tab_flags("Display"))) {
          DrawDisplay(s, saved);
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls", nullptr, tab_flags("Controls"))) {
          DrawControls(s);
          ImGui::EndTabItem();
        }
        if (s.kinect.enabled && ImGui::BeginTabItem("Kinect", nullptr, tab_flags("Kinect"))) {
          DrawKinect(s);
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Players", nullptr, tab_flags("Players"))) {
          DrawPlayers(s);
          ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Performance", nullptr, tab_flags("Performance"))) {
          DrawPerformance();
          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }
      ImGui::Separator();
      ImGui::TextDisabled("AnyX360 " ANYX360_VERSION_FULL "   F1 performance overlay   F2/Esc close   F11/Alt+Enter "
                          "fullscreen");
    }
    ImGui::End();
    if (!open) PostToUIThread(CloseSettingsPanel);
  }

 private:
  void DrawDisplay(const Settings& s, const Settings& saved) {
    const DisplayConfig& live = GetDisplayConfig();
    ImGui::SeparatorText("Window");
    static const uint32_t kLines[] = {720, 1080, 1440, 2160};
    static const char* const kNames[] = {"1280x720 (720p)", "1920x1080 (1080p)", "2560x1440 (1440p)",
                                         "3840x2160 (4K)"};
    uint32_t lines = settings::ResolutionLines(s.display);
    int current = 0;
    for (int i = 0; i < 4; ++i)
      if (kLines[i] == lines) current = i;
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Resolution", &current, kNames, 4)) {
      uint32_t chosen = kLines[current];
      PostToUIThread([chosen] { SetResolution(chosen); });
    }
    CommandLineNote(lines != settings::ResolutionLines(saved.display));
    bool fullscreen = GameWindow()->IsFullscreen();
    if (ImGui::Checkbox("Fullscreen", &fullscreen)) PostToUIThread([fullscreen] { SetFullscreenMode(fullscreen); });
    if (fullscreen) {
      ImGui::SameLine();
      ImGui::TextDisabled("(the screen's resolution; the window size applies in windowed mode)");
    }

    ImGui::SeparatorText("Image");
    int upscale = int(s.display.upscale);
    ImGui::TextUnformatted("Upscaling");
    ImGui::SameLine(140);
    bool changed = ImGui::RadioButton("AMD FSR", &upscale, int(Upscale::kFsr));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("AMD CAS", &upscale, int(Upscale::kCas));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Bilinear", &upscale, int(Upscale::kBilinear));
    if (changed) PostToUIThread([upscale] { SetUpscaleSetting(Upscale(upscale)); });
    CommandLineNote(s.display.upscale != saved.display.upscale);
    bool fxaa = s.display.fxaa;
    if (ImGui::Checkbox("FXAA", &fxaa)) PostToUIThread([fxaa] { SetFxaaSetting(fxaa); });
    CommandLineNote(s.display.fxaa != saved.display.fxaa);
    ImGui::SameLine(140);
    bool vsync = s.display.vsync;
    if (ImGui::Checkbox("VSync", &vsync)) PostToUIThread([vsync] { SetVsyncSetting(vsync); });
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Waits for the display's refresh to show each frame: no tearing in fullscreen.");

    int fps = int(s.display.target_fps);
    ImGui::TextUnformatted("Frame rate");
    ImGui::SameLine(140);
    if (FixedFps()) {
      ImGui::TextDisabled("%u FPS (this game runs at this rate only)", FixedFps());
    } else {
      changed = ImGui::RadioButton("30 FPS", &fps, 30);
      ImGui::SameLine();
      changed |= ImGui::RadioButton("60 FPS", &fps, 60);
      if (changed) PostToUIThread([fps] { SetFps(uint32_t(fps)); });
      CommandLineNote(s.display.target_fps != saved.display.target_fps);
    }

    int scale = int(s.display.render_scale);
    ImGui::TextUnformatted("Render scale");
    ImGui::SameLine(140);
    changed = false;
    for (int i = 1; i <= 3; ++i) {
      char label[32];
      std::snprintf(label, sizeof(label), "%dx (%dx%d)", i, 1280 * i, 720 * i);
      changed |= ImGui::RadioButton(label, &scale, i);
      if (i < 3) ImGui::SameLine();
    }
    if (changed) PostToUIThread([scale] { SetRenderScale(uint32_t(scale)); });
    ImGui::TextDisabled("2x costs the GPU about four times as much as 1x, 3x about nine times.");
    if (uint32_t(scale) != live.render_scale) {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Applies after a restart (now %ux).", live.render_scale);
      ImGui::SameLine();
      if (ImGui::Button("Restart now")) PostToUIThread(RestartNow);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Saves the settings and starts the game again. Progress since the last checkpoint is lost.");
    }
    if (!g_restart_error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", g_restart_error.c_str());

    bool stats = IsStatsOverlayVisible();
    if (ImGui::Checkbox("Performance overlay (F1)", &stats)) PostToUIThread([stats] { ShowStats(stats); });
  }

  void DrawControls(const Settings& s) {
    settings::InputSettings in = s.input;
    bool changed = false;
    ImGui::SeparatorText("Mouse");
    ImGui::SetNextItemWidth(260);
    changed |= ImGui::SliderFloat("Sensitivity", &in.sensitivity, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(260);
    changed |= ImGui::SliderFloat("Aiming sensitivity", &in.aim_sensitivity, 0.1f, 2.0f, "%.2fx");
    changed |= ImGui::Checkbox("Invert vertical look", &in.invert_y);
    int smoothing = int(in.smoothing_ms);
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderInt("Smoothing", &smoothing, 4, 50, "%d ms")) {
      in.smoothing_ms = uint32_t(smoothing);
      changed = true;
    }
    ImGui::SetNextItemWidth(260);
    changed |= ImGui::SliderFloat("Anti dead zone", &in.anti_deadzone, 0.0f, 0.5f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Minimum stick deflection for any mouse movement, so slow movements still turn the camera.");
    if (ImGui::Button("Reset mouse settings")) {
      settings::InputSettings defaults;
      defaults.bindings = in.bindings;
      in = defaults;
      changed = true;
    }
    if (changed) settings::Update([in](Settings& st) { st.input = in; });

    ImGui::SeparatorText("Keys (settings.toml, [input.bindings])");
    if (ImGui::BeginTable("##bindings", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
      auto bindings = input::DescribeBindings();
      for (size_t i = 0; i < bindings.size(); i += 2) {
        ImGui::TableNextRow();
        for (size_t j = i; j < i + 2 && j < bindings.size(); ++j) {
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(bindings[j].first.c_str());
          ImGui::TableNextColumn();
          ImGui::TextDisabled("%s", bindings[j].second.c_str());
        }
      }
      ImGui::EndTable();
    }
    ImGui::TextDisabled("LUP/LDOWN/LLEFT/LRIGHT: left stick. Mouse: right stick.");
  }

  void DrawPlayers(const Settings& s) {
    uint32_t controllers = input::ConnectedControllers();
    ImGui::Text("Controllers connected: %s%s%s%s%s", controllers ? "" : "none", (controllers & 1) ? "1 " : "",
                (controllers & 2) ? "2 " : "", (controllers & 4) ? "3 " : "", (controllers & 8) ? "4" : "");
    if (ImGui::BeginTable("##players", 4, ImGuiTableFlags_SizingFixedFit)) {
      for (uint32_t user = 0; user < 4; ++user) {
        ImGui::PushID(int(user));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Player %u (%s)", user + 1, s.players[user].name.c_str());
        ImGui::TableNextColumn();
        int device = DeviceIndex(input::GetSlotDevice(user));
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("##device", &device, kDeviceLabels, IM_ARRAYSIZE(kDeviceLabels))) {
          input::Device chosen = DeviceAt(device);
          PostToUIThread([user, chosen] { input::SetSlotDevice(user, chosen); });
        }
        ImGui::TableNextColumn();
        int controller = input::ResolvedController(user);
        if (input::GetSlotDevice(user).kind == input::DeviceKind::kNone)
          ImGui::TextDisabled("-");
        else if (!input::IsConnected(user))
          ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "not connected");
        else if (controller >= 0)
          ImGui::Text("controller %d", controller + 1);
        else
          ImGui::Text("connected");
        ImGui::TableNextColumn();
        bool signed_in = kernel::IsUserSignedIn(user);
        if (user == 0) {
          ImGui::TextDisabled("signed in");
        } else if (signed_in) {
          if (ImGui::SmallButton("Sign out")) PostToUIThread([user] { kernel::SignOutUser(user); });
        } else if (input::IsConnected(user)) {
          if (ImGui::SmallButton("Sign in")) PostToUIThread([user] { kernel::SignInUser(user); });
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    static const char* const kSignIn[] = {"on_demand", "at_start", "manual"};
    static const char* const kSignInLabels[] = {"When the game asks", "At startup", "Only from this panel"};
    int policy = 0;
    for (int i = 0; i < 3; ++i)
      if (s.sign_in == kSignIn[i]) policy = i;
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Players 2-4 sign in", &policy, kSignInLabels, 3)) {
      std::string chosen = kSignIn[policy];
      settings::Update([chosen](Settings& st) { st.sign_in = chosen; });
    }
    ImGui::TextDisabled("Split screen: player 2 presses Start in the main menu to join (as \"not signed in\").");
    ImGui::TextDisabled("To join with a profile, sign in at the title screen: signing in later sends the game");
    ImGui::TextDisabled("back to the title screen. Playing alone with a controller: set player 1 to it.");
  }

  void DrawKinect(const Settings& s) {
    const settings::KinectSettings& k = s.kinect;
    const nui::Status status = nui::GetStatus();
    ImGui::SeparatorText("Sensor");
    ImGui::Text("Source: %s", status.source.empty() ? "not started yet" : status.source.c_str());
    const bool camera = status.source == "webcam" && status.camera_running;
    if (!status.message.empty() && !camera) {
      ImGui::SameLine();
      ImGui::TextWrapped("- %s", status.message.c_str());
    }
    if (camera) {
      ImGui::Text("Camera %ux%u at %.0f fps   latency %.0f ms", status.camera_width, status.camera_height,
                  status.camera_fps, status.latency_ms);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("From the bridge getting the camera's image to the game's frame (the camera's own\n"
                          "delay, before that, isn't measured).");
      ImGui::TextDisabled("  waiting %.0f + pose %.0f + mapping %.0f + images %.0f + to the game %.0f ms%s",
                          status.capture_ms, status.detect_ms, status.process_ms, status.render_ms,
                          status.transfer_ms, status.dropped_frames ? "" : ", no frames dropped");
      if (status.dropped_frames) {
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled(", %u camera frames dropped", status.dropped_frames);
      }
    }
    ImGui::Text("People in view: %u (%u tracked)", status.bodies, status.tracked);
    if (status.restarts) {
      ImGui::SameLine();
      ImGui::TextDisabled("   pose bridge restarted %u time%s", status.restarts, status.restarts == 1 ? "" : "s");
    }
    DrawSkeletons();

    ImGui::SeparatorText("Source");
    static const char* const kSources[] = {"webcam", "mouse", "none"};
    static const char* const kSourceLabels[] = {"Webcam", "Mouse (the right hand follows the cursor)", "None"};
    int source = 0;
    for (int i = 0; i < 3; ++i)
      if (k.source == kSources[i]) source = i;
    ImGui::SetNextItemWidth(300);
    if (ImGui::Combo("##source", &source, kSourceLabels, 3)) {
      std::string chosen = kSources[source];
      UpdateKinect([chosen](settings::KinectSettings& kk) { kk.source = chosen; });
    }
    if (k.source == "mouse")
      ImGui::TextDisabled("Left click: select (a swipe). Right click: back. Space: the Kinect guide gesture.");
    if (k.source != "webcam") return;

    ImGui::SeparatorText("Webcam");
    std::vector<std::string> cameras = nui::ListCameras();
    std::string current = k.camera < cameras.size() ? cameras[k.camera] : "Camera " + std::to_string(k.camera);
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("Camera", current.c_str())) {
      for (uint32_t i = 0; i < cameras.size(); ++i) {
        if (ImGui::Selectable(cameras[i].c_str(), i == k.camera))
          UpdateKinect([i](settings::KinectSettings& kk) { kk.camera = i; });
      }
      if (cameras.empty()) ImGui::TextDisabled("No webcam found");
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) nui::ListCameras(true);
    bool mirror = k.mirror;
    if (ImGui::Checkbox("My camera already mirrors its image", &mirror))
      UpdateKinect([mirror](settings::KinectSettings& kk) { kk.mirror = mirror; });
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Turn this on if raising your right hand moves the skeleton's left hand.");
    float fov = k.fov;
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderFloat("Field of view", &fov, 40.0f, 100.0f, "%.0f degrees"))
      UpdateKinect([fov](settings::KinectSettings& kk) { kk.fov = fov; });
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The webcam's horizontal field of view (most are 60-78 degrees). A wrong value makes\n"
                        "you look nearer or farther than you are.");
    float smoothing = k.smoothing;
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderFloat("Smoothing", &smoothing, 0.0f, 1.0f, "%.2f"))
      UpdateKinect([smoothing](settings::KinectSettings& kk) { kk.smoothing = smoothing; });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("More: steadier but later. Less: quicker but jittery.");
    int players = int(k.players);
    ImGui::TextUnformatted("Players");
    ImGui::SameLine(140);
    bool changed = ImGui::RadioButton("1", &players, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("2", &players, 2);
    if (changed) UpdateKinect([players](settings::KinectSettings& kk) { kk.players = uint32_t(players); });
    int model = k.model == "lite" ? 0 : 1;
    ImGui::TextUnformatted("Pose model");
    ImGui::SameLine(140);
    changed = ImGui::RadioButton("Fast", &model, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Accurate", &model, 1);
    if (changed) UpdateKinect([model](settings::KinectSettings& kk) { kk.model = model ? "full" : "lite"; });
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fast uses less CPU; Accurate tracks better.");
    // The webcam's tilt: a camera pitched down or rolled makes a standing player look like they're
    // leaning; the bridge measures it (from the player's body, then the floor) and corrects it.
    ImGui::TextUnformatted("Camera tilt");
    ImGui::SameLine(140);
    int tilt_mode = k.tilt_manual ? 1 : 0;
    changed = ImGui::RadioButton("Measured", &tilt_mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Set by hand", &tilt_mode, 1);
    if (changed) UpdateKinect([tilt_mode](settings::KinectSettings& kk) { kk.tilt_manual = tilt_mode == 1; });
    if (tilt_mode == 0) {
      static const char* const kTiltSources[] = {"measuring...", "from your body", "from the floor", "set by hand"};
      ImGui::SameLine();
      if (status.tilt_source == 0)
        ImGui::TextDisabled("  measuring: stand still, all of you in view");
      else
        ImGui::TextDisabled("  %.0f%s down, %.0f%s roll (%s)", status.tilt_pitch, "\xC2\xB0", status.tilt_roll,
                            "\xC2\xB0", kTiltSources[std::min<uint32_t>(status.tilt_source, 3)]);
    } else {
      float pitch = k.tilt_pitch, roll = k.tilt_roll;
      ImGui::SetNextItemWidth(300);
      if (ImGui::SliderFloat("Pitch (down)", &pitch, -35.0f, 35.0f, "%.0f degrees"))
        UpdateKinect([pitch](settings::KinectSettings& kk) { kk.tilt_pitch = pitch; });
      ImGui::SetNextItemWidth(300);
      if (ImGui::SliderFloat("Roll (clockwise)", &roll, -35.0f, 35.0f, "%.0f degrees"))
        UpdateKinect([roll](settings::KinectSettings& kk) { kk.tilt_roll = roll; });
    }
    if (status.floor_height > 0) ImGui::TextDisabled("Your webcam is %.2f m above the floor.", status.floor_height);
    if (ImGui::Button("Recentre on me")) nui::Calibrate();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Stand where you'll play: the game then sees you centred, at the Kinect's distance.");
    ImGui::SameLine();
    if (ImGui::Button("Restart camera")) nui::RestartSource();
    ImGui::TextDisabled("Stand 2-3 m from the webcam, in good light, with all of you in view.");
  }

  // What the game sees, mirrored like a Kinect's view: the camera's color image (when the source
  // has a camera) and the skeletons over it, where the virtual color camera sees them. Each joint
  // is drawn by its state: tracked green, inferred amber. A skeleton off the person in the image
  // means a wrong field of view or tilt.
  void DrawSkeletons() {
    const ImVec2 size(320, 240);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 end(o.x + size.x, o.y + size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    UpdatePreview();
    if (preview_)
      draw->AddImage(reinterpret_cast<ImTextureID>(preview_.get()), o, end);
    else
      draw->AddRectFilled(o, end, IM_COL32(18, 18, 26, 255));
    draw->AddRect(o, end, IM_COL32(90, 90, 110, 255));
    const float focal = nui::kColorFocal * size.x / nui::kColorWidth;
    auto project = [&](const float* j) {
      const float z = std::max(j[2], 0.2f);
      return ImVec2(o.x + size.x / 2 + j[0] * focal / z, o.y + size.y / 2 - j[1] * focal / z);
    };
    draw->PushClipRect(o, end, true);
    for (const nui::PreviewBody& body : nui::GetBodies()) {
      const ImU32 color = body.tracked ? IM_COL32(90, 220, 130, 255) : IM_COL32(150, 150, 160, 255);
      for (const auto& bone : nui::kSkeletonBones)
        draw->AddLine(project(body.joints[bone[0]]), project(body.joints[bone[1]]), color, 3.0f);
      draw->AddCircleFilled(project(body.joints[nui::kHead]), 7.0f, color);
      if (!body.tracked) continue;
      for (uint32_t j = 0; j < nui::kJointCount; ++j) {
        const ImU32 state = body.joint_state[j] == nui::kJointTracked ? IM_COL32(40, 160, 80, 255)
                            : body.joint_state[j] == nui::kJointInferred ? IM_COL32(240, 170, 40, 255)
                                                                          : IM_COL32(220, 60, 60, 255);
        draw->AddCircleFilled(project(body.joints[j]), 3.5f, state);
      }
    }
    draw->PopClipRect();
    ImGui::Dummy(size);
  }

  // The camera's image as a 320x240 texture, made again 10 times a second while the tab shows it.
  void UpdatePreview() {
    std::shared_ptr<const std::vector<uint8_t>> color = nui::GetPreviewColor();
    xe::ui::ImmediateDrawer* drawer = SharedImmediateDrawer();
    if (!color || !drawer || color->size() < size_t(nui::kColorWidth) * nui::kColorHeight * 4) {
      preview_.reset();
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (preview_ && now - preview_time_ < std::chrono::milliseconds(100)) return;
    preview_time_ = now;
    constexpr uint32_t kW = nui::kColorWidth / 2, kH = nui::kColorHeight / 2;
    std::vector<uint8_t> rgba(size_t(kW) * kH * 4);
    for (uint32_t y = 0; y < kH; ++y) {
      for (uint32_t x = 0; x < kW; ++x) {
        const uint8_t* p = color->data() + (size_t(y * 2) * nui::kColorWidth + x * 2) * 4;  // BGRA
        uint8_t* q = &rgba[(size_t(y) * kW + x) * 4];
        q[0] = p[2], q[1] = p[1], q[2] = p[0], q[3] = 0xFF;
      }
    }
    preview_ = drawer->CreateTexture(kW, kH, xe::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
  }

  std::unique_ptr<xe::ui::ImmediateTexture> preview_;
  std::chrono::steady_clock::time_point preview_time_;

  void DrawPerformance() {
    timing::Summary t = timing::Summarize(2.0);
    ImGui::Text("%.1f FPS  (target %u)   presented by the %s thread", t.vdswaps / 2.0, GetDisplayConfig().target_fps,
                t.presents_ui_thread > t.presents_gpu_thread ? "UI" : "GPU");
    ImGui::Text("Frame interval p50 %.2f  p99 %.2f  max %.1f ms", t.swap_p50_ms, t.swap_p99_ms, t.swap_max_ms);
    double display_hz;
    bool locked;
    DisplayLockState(display_hz, locked);
    ImGui::Text("Vblank %.0f Hz, late p99 %.0f us; display %.2f Hz, %s", t.vblanks / 2.0, t.vblank_late_p99_us,
                display_hz, locked ? "locked to it" : "not locked");
    bool lock = settings::Get().pacing.display_lock;
    if (ImGui::Checkbox("Lock the game's vblank to the display refresh", &lock))
      PostToUIThread([lock] { SetDisplayLockSetting(lock); });
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("With a 60 or 120 Hz display, every frame stays on screen equally long.");
    float values[240];
    size_t count = timing::RecentIntervals(timing::Event::kVdSwap, values, 240);
    ImGui::PlotLines("Frames (VdSwap)", values, int(count), 0, "0-50 ms", 0.0f, 50.0f, ImVec2(420, 70));
    count = timing::RecentIntervals(timing::Event::kPresentGpuThread, values, 240);
    ImGui::PlotLines("Presents", values, int(count), 0, "0-50 ms", 0.0f, 50.0f, ImVec2(420, 70));
  }
};

SettingsPanel* g_panel = nullptr;  // lives until exit; registered with the drawer while open
bool g_open = false;

void CenterCursor() {
  auto* window = static_cast<xe::ui::Win32Window*>(GameWindow());
  RECT client;
  if (!window || !GetClientRect(window->hwnd(), &client)) return;
  POINT center{client.right / 2, client.bottom / 2};
  ClientToScreen(window->hwnd(), &center);
  SetCursorPos(center.x, center.y);
}

void OpenSettingsPanel() {
  if (g_open || !g_panel) return;
  g_open = true;
  g_restart_error.clear();
  SharedImGuiDrawer()->AddDialog(g_panel);
  SharedImGuiDrawer()->SetContinuousRepaint(true);  // interactive: repaint at the UI rate
  bool was_captured = input::IsCursorCaptured();
  input::SetGameInputEnabled(false);
  if (was_captured) CenterCursor();
  KLOG_I("settings panel opened");
}

// ANYX360_SETTINGS_TEST="<seconds>:<setting>=<value>,..." applies settings as the panel does
// (resolution, fullscreen, upscale, fxaa, fps, scale, stats, panel=1/0, tab=<name>, signin=<player>, signout=<player>, restart,
// kinect_source=<webcam|mouse|none>, kinect_players=<1|2>, kinect_mirror=<0|1>, kinect_restart, kinect_calibrate).
void RunSettingsTest(std::string spec) {
  auto start = std::chrono::steady_clock::now();
  for (size_t pos = 0; pos < spec.size();) {
    size_t end = std::min(spec.find(',', pos), spec.size());
    std::string item = spec.substr(pos, end - pos);
    pos = end + 1;
    size_t colon = item.find(':');
    if (colon == std::string::npos) continue;
    std::string action = item.substr(colon + 1);
    size_t equals = action.find('=');
    std::string key = action.substr(0, equals);
    std::string value = equals == std::string::npos ? "" : action.substr(equals + 1);
    std::this_thread::sleep_until(start + std::chrono::milliseconds(int64_t(std::atof(item.c_str()) * 1000)));
    PostToUIThread([key, value] {
      KLOG_I("settings test: %s=%s", key.c_str(), value.c_str());
      uint32_t n = uint32_t(std::atoi(value.c_str()));
      if (key == "resolution") SetResolution(n);
      else if (key == "fullscreen") SetFullscreenMode(n != 0);
      else if (key == "upscale") SetUpscaleSetting(value == "cas" ? Upscale::kCas : value == "bilinear" ? Upscale::kBilinear : Upscale::kFsr);
      else if (key == "fxaa") SetFxaaSetting(n != 0);
      else if (key == "fps" && !FixedFps()) SetFps(n >= 60 ? 60 : 30);
      else if (key == "vsync") SetVsyncSetting(n != 0);
      else if (key == "lock") SetDisplayLockSetting(n != 0);
      else if (key == "scale") SetRenderScale(std::clamp(n, 1u, 3u));
      else if (key == "stats") ShowStats(n != 0);
      else if (key == "panel") n ? OpenSettingsPanel() : CloseSettingsPanel();
      else if (key == "restart") RestartNow();
      else if (key == "tab") g_select_tab = value;
      else if (key == "signin") kernel::SignInUser(n - 1);
      else if (key == "signout") kernel::SignOutUser(n - 1);
      else if (key == "kinect_source") UpdateKinect([value](settings::KinectSettings& k) { k.source = value; });
      else if (key == "kinect_players") UpdateKinect([n](settings::KinectSettings& k) { k.players = std::clamp(n, 1u, 2u); });
      else if (key == "kinect_mirror") UpdateKinect([n](settings::KinectSettings& k) { k.mirror = n != 0; });
      else if (key == "kinect_restart") nui::RestartSource();
      else if (key == "kinect_calibrate") nui::Calibrate();
    });
  }
}

}  // namespace

void StartSettingsPanel() {
  g_panel = new SettingsPanel(SharedImGuiDrawer());  // registers itself
  SharedImGuiDrawer()->RemoveDialog(g_panel);
  if (const char* spec = std::getenv("ANYX360_SETTINGS_TEST")) std::thread(RunSettingsTest, std::string(spec)).detach();
}

void ToggleSettingsPanel() {
  if (g_open)
    CloseSettingsPanel();
  else
    OpenSettingsPanel();
}

bool IsSettingsPanelOpen() { return g_open; }

void CloseSettingsPanel() {
  if (!g_open) return;
  g_open = false;
  SharedImGuiDrawer()->RemoveDialog(g_panel);
  SharedImGuiDrawer()->SetContinuousRepaint(false);
  input::SetGameInputEnabled(true);
  KLOG_I("settings panel closed");
}

}  // namespace x360::gpu
