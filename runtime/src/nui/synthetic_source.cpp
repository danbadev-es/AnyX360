// Scripted test poses for the virtual Kinect (ANYX360_NUI_SCRIPT), to drive a Kinect title without a
// camera: "seconds:action[/duration][@player],...", player 0 or 1 (default 0).
//
// "@<file>" reads the script from a file and keeps reading what is appended to it while the game
// runs; an appended action without "seconds:" happens when it's read (driving the game live).
//
// Actions:
//   enter, leave             the player walks into / out of view
//   menu_right, menu_left    that arm held out to the side at shoulder height (the menu pose)
//   hand_up, hand_down       the raised hand moves 12 cm (over the duration, default 0.3 s)
//   hand_y_<cm>              the raised hand moves to that height from the shoulder's (same)
//   swipe_left, swipe_right  the raised hand sweeps across the body and back (default 0.6 s)
//   lower                    both arms down
//   wave                     the right hand waves (default 2 s)
//   guide                    the left arm held at 45 degrees (the Kinect guide gesture)
//   arm_left_<az>_<el>,      that arm held straight at an azimuth and elevation in degrees (see
//   arm_right_<az>_<el>      ArmPose), raised from the side over the duration (default at once)
//   dance                    a looping routine of arm, hip and knee movement (default 30 s)
//   jump                     the whole body rises 30 cm and lands (default 0.6 s)
//   crouch                   drops 35 cm, knees bent, and stands up again (default 1.2 s)
//   lean_left, lean_right    the torso tilts 20 degrees to that side and back (default 1.5 s)
//   step_left, step_right    moves 45 cm to that side and stays there (default 0.4 s)
//   walk_in, walk_out        moves 50 cm towards / away from the sensor and stays (default 0.8 s)
//   hands_up, arms_out       both arms up over the head / out to the sides
//   flap                     both arms flap like wings, from the sides to above the shoulders,
//                            1.25 times a second (default 4 s)
//   wheel_<degrees>          both hands held forward at chest height, together, holding a steering
//                            wheel turned by that angle (positive: to the right, the left hand up
//                            and the right hand down); it turns there over the duration (default
//                            0.3 s) and is held until lower or another arm pose
//   boost                    the hands pulled back to the chest and pushed forward again, into
//                            the wheel pose (default 0.8 s)
//   lean_forward, lean_back  the torso pitches 20 degrees towards / away from the sensor and back
//                            (default 1.5 s)
//   idle                     stops any movement in progress
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "source.h"

namespace x360::nui {

namespace {

constexpr float kPi = 3.14159265f;

struct Event {
  double at = 0;
  std::string action;
  double duration = -1;  // the action's default
  int player = 0;
};

enum class Arm { kDown, kMenu, kGuide, kAngle };

struct Player {
  bool present = false;
  Arm right = Arm::kDown, left = Arm::kDown;
  float right_y = 0, left_y = 0;  // raised hand offset (m)
  float from_y = 0, to_y = 0;     // hand_up / hand_down in progress
  double move_start = -1, move_end = -1;
  bool move_left_hand = false;
  double swipe_start = -1, swipe_end = -1;
  float swipe_direction = 0;  // -1 towards the player's left, +1 towards their right
  bool swipe_left_hand = false;
  double wave_start = -1, wave_end = -1;
  double dance_start = -1, dance_end = -1;
  float azimuth[2] = {}, elevation[2] = {};  // kAngle, per arm (0 right, 1 left)
  double raise_start[2] = {}, raise_end[2] = {};
  // Whole-body moves (Kinect Adventures: jumping, ducking, leaning, stepping).
  float x = 0, from_x = 0, to_x = 0;  // sideways position (steps), metres
  double step_start = -1, step_end = -1;
  float z = 0, from_z = 0, to_z = 0;  // towards (-) or away from (+) the sensor
  double walk_start = -1, walk_end = -1;
  double jump_start = -1, jump_end = -1;
  double crouch_start = -1, crouch_end = -1;
  double lean_start = -1, lean_end = -1;
  float lean_direction = 0;  // -1 the player's left, +1 their right
  double flap_start = -1, flap_end = -1;
  // Driving (Kinect Joy Ride): a steering wheel held in both hands, pushes, leaning on the pitch.
  bool wheel = false;
  float wheel_from = 0, wheel_to = 0;  // degrees, positive to the right
  double wheel_start = -1, wheel_end = -1;
  double boost_start = -1, boost_end = -1;
  double pitch_start = -1, pitch_end = -1;
  float pitch_direction = 0;  // +1 towards the sensor, -1 away from it
};

// 0 -> 1 -> 0 over [start, end): eased in over the first fifth, held, eased out over the last.
float HoldProfile(double t, double start, double end) {
  if (t < start || t >= end) return 0;
  const float k = float((t - start) / std::max(end - start, 1e-3));
  auto ease = [](float v) { return 0.5f - 0.5f * std::cos(std::clamp(v, 0.0f, 1.0f) * kPi); };
  return k < 0.2f ? ease(k / 0.2f) : k > 0.8f ? ease((1.0f - k) / 0.2f) : 1.0f;
}

// A move from `from` to `to` over [start, end), eased; `to` after it.
float Glide(double t, double start, double end, float from, float to) {
  if (t >= end) return to;
  if (t < start) return from;
  const float k = float((t - start) / std::max(end - start, 1e-3));
  return from + (to - from) * (0.5f - 0.5f * std::cos(k * kPi));
}

// The steering wheel's angle at t (degrees), turning from wheel_from to wheel_to.
float WheelAngle(const Player& p, double t) { return Glide(t, p.wheel_start, p.wheel_end, p.wheel_from, p.wheel_to); }

void Set(Body& b, Joint j, float x, float y, float z) {
  b.joints[j][0] = x;
  b.joints[j][1] = y;
  b.joints[j][2] = z;
}

class SyntheticSource : public Source {
 public:
  SyntheticSource(const std::string& script, float camera_height) : camera_height_(camera_height) {
    if (!script.empty() && script[0] == '@') {
      file_ = script.substr(1);
      ReadFile(0);
    } else {
      Parse(script, 0, false);
    }
  }

  void Poll(double time, Snapshot& out) override {
    if (!file_.empty() && time >= next_read_) {
      next_read_ = time + 0.2;
      ReadFile(time);
    }
    // Each action starts at its scheduled time, whenever the poll that applies it comes.
    while (next_ < events_.size() && events_[next_].at <= time) {
      const Event& e = events_[next_++];
      Apply(e, e.at);
    }
    out.body_count = 0;
    for (int i = 0; i < 2; ++i) {
      if (!players_[i].present) continue;
      Body& body = out.bodies[out.body_count++];
      Pose(i, time, body);
    }
  }

  std::string Message() const override { return "scripted poses"; }

 private:
  // Items separated by commas or lines; without "seconds:" an item happens at `now` (live only).
  void Parse(const std::string& script, double now, bool live) {
    for (size_t start = 0; start < script.size();) {
      size_t end = script.find_first_of(",\r\n", start);
      if (end == std::string::npos) end = script.size();
      std::string item = script.substr(start, end - start);
      start = end + 1;
      while (!item.empty() && item.back() == ' ') item.pop_back();
      while (!item.empty() && item.front() == ' ') item.erase(0, 1);
      if (item.empty()) continue;
      Event e;
      size_t colon = item.find(':');
      if (colon == std::string::npos && !live) continue;
      e.at = colon == std::string::npos ? now : std::atof(item.substr(0, colon).c_str());
      std::string rest = colon == std::string::npos ? item : item.substr(colon + 1);
      if (size_t at = rest.find('@'); at != std::string::npos) {
        e.player = std::clamp(std::atoi(rest.substr(at + 1).c_str()), 0, 1);
        rest = rest.substr(0, at);
      }
      if (size_t slash = rest.find('/'); slash != std::string::npos) {
        e.duration = std::atof(rest.substr(slash + 1).c_str());
        rest = rest.substr(0, slash);
      }
      e.action = rest;
      events_.push_back(e);
    }
    std::stable_sort(events_.begin() + next_, events_.end(),
                     [](const Event& a, const Event& b) { return a.at < b.at; });
  }

  // What was appended to the script file since the last read (whole lines).
  void ReadFile(double now) {
    FILE* f = _wfopen(std::filesystem::path(file_).c_str(), L"rb");
    if (!f) return;
    std::string text;
    if (std::fseek(f, long(read_), SEEK_SET) == 0) {
      char buffer[4096];
      for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0;) text.append(buffer, n);
    }
    std::fclose(f);
    const size_t complete = text.find_last_of('\n');
    if (complete == std::string::npos) return;
    text.resize(complete + 1);
    read_ += complete + 1;
    Parse(text, now, true);
  }

  void Apply(const Event& e, double time) {
    Player& p = players_[e.player];
    auto duration = [&](double fallback) { return e.duration >= 0 ? e.duration : fallback; };
    std::fprintf(stderr, "[nui] script %.1fs: %s @%d\n", time, e.action.c_str(), e.player);
    if (e.action == "enter") {
      p = Player{};
      p.present = true;
    } else if (e.action == "leave") {
      p.present = false;
    } else if (e.action == "menu_right") {
      p.wheel = false;
      p.right = Arm::kMenu;
      p.right_y = 0;
    } else if (e.action == "menu_left") {
      p.wheel = false;
      p.left = Arm::kMenu;
      p.left_y = 0;
    } else if (float cm; e.action == "hand_up" || e.action == "hand_down" ||
                         std::sscanf(e.action.c_str(), "hand_y_%f", &cm) == 1) {
      p.move_left_hand = p.right != Arm::kMenu && p.left == Arm::kMenu;
      float& y = p.move_left_hand ? p.left_y : p.right_y;
      p.from_y = y;
      p.to_y = e.action == "hand_up" ? y + 0.12f : e.action == "hand_down" ? y - 0.12f : cm / 100;
      p.move_start = time;
      p.move_end = time + duration(0.3);
    } else if (e.action == "swipe_left" || e.action == "swipe_right") {
      p.swipe_left_hand = p.right != Arm::kMenu && p.left == Arm::kMenu;
      p.swipe_direction = e.action == "swipe_left" ? -1.0f : 1.0f;
      p.swipe_start = time;
      p.swipe_end = time + duration(0.6);
    } else if (e.action == "lower") {
      p.wheel = false;
      p.right = p.left = Arm::kDown;
    } else if (e.action == "wave") {
      p.wheel = false;
      p.right = Arm::kMenu;
      p.wave_start = time;
      p.wave_end = time + duration(2.0);
    } else if (e.action == "guide") {
      p.wheel = false;
      p.left = Arm::kGuide;
    } else if (float az, el; std::sscanf(e.action.c_str(), "arm_left_%f_%f", &az, &el) == 2 ||
                             std::sscanf(e.action.c_str(), "arm_right_%f_%f", &az, &el) == 2) {
      const int arm = e.action.rfind("arm_left_", 0) == 0 ? 1 : 0;
      p.wheel = false;
      (arm ? p.left : p.right) = Arm::kAngle;
      p.azimuth[arm] = az;
      p.elevation[arm] = el;
      p.raise_start[arm] = time;
      p.raise_end[arm] = time + std::max(duration(0.0), 0.0);
    } else if (e.action == "dance") {
      p.dance_start = time;
      p.dance_end = time + duration(30.0);
    } else if (e.action == "jump") {
      p.jump_start = time;
      p.jump_end = time + duration(0.6);
    } else if (e.action == "crouch") {
      p.crouch_start = time;
      p.crouch_end = time + duration(1.2);
    } else if (e.action == "lean_left" || e.action == "lean_right") {
      p.lean_direction = e.action == "lean_left" ? -1.0f : 1.0f;
      p.lean_start = time;
      p.lean_end = time + duration(1.5);
    } else if (e.action == "step_left" || e.action == "step_right") {
      p.from_x = Glide(time, p.step_start, p.step_end, p.from_x, p.to_x);
      p.to_x = p.from_x + (e.action == "step_left" ? -0.45f : 0.45f);
      p.step_start = time;
      p.step_end = time + duration(0.4);
    } else if (e.action == "walk_in" || e.action == "walk_out") {
      p.from_z = Glide(time, p.walk_start, p.walk_end, p.from_z, p.to_z);
      p.to_z = p.from_z + (e.action == "walk_in" ? -0.5f : 0.5f);
      p.walk_start = time;
      p.walk_end = time + duration(0.8);
    } else if (e.action == "hands_up" || e.action == "arms_out") {
      const float elevation = e.action == "hands_up" ? 80.0f : 0.0f;
      p.wheel = false;
      for (int arm = 0; arm < 2; ++arm) {
        (arm ? p.left : p.right) = Arm::kAngle;
        p.azimuth[arm] = e.action == "hands_up" ? 10.0f : 0.0f;
        p.elevation[arm] = elevation;
        p.raise_start[arm] = time;
        p.raise_end[arm] = time + std::max(duration(0.0), 0.0);
      }
    } else if (e.action == "flap") {
      p.flap_start = time;
      p.flap_end = time + duration(4.0);
    } else if (float degrees; std::sscanf(e.action.c_str(), "wheel_%f", &degrees) == 1) {
      p.wheel_from = p.wheel ? WheelAngle(p, time) : degrees;
      p.wheel_to = degrees;
      p.wheel = true;
      p.wheel_start = time;
      p.wheel_end = time + duration(0.3);
    } else if (e.action == "boost") {
      if (!p.wheel) p.wheel_from = p.wheel_to = 0;
      p.wheel = true;
      p.boost_start = time;
      p.boost_end = time + duration(0.8);
    } else if (e.action == "lean_forward" || e.action == "lean_back") {
      p.pitch_direction = e.action == "lean_forward" ? 1.0f : -1.0f;
      p.pitch_start = time;
      p.pitch_end = time + duration(1.5);
    } else if (e.action == "idle") {
      p.wave_end = p.dance_end = p.swipe_end = p.move_end = time;
      p.jump_end = p.crouch_end = p.lean_end = p.flap_end = time;
      p.boost_end = p.pitch_end = time;
    } else {
      std::fprintf(stderr, "[nui] script: unknown action %s\n", e.action.c_str());
    }
  }

  void Pose(int index, double t, Body& body) {
    Player& p = players_[index];
    float x0 = (index == 0 ? 0.0f : 0.6f) + Glide(t, p.step_start, p.step_end, p.from_x, p.to_x);
    float z0 = 2.3f + Glide(t, p.walk_start, p.walk_end, p.from_z, p.to_z);
    StandingPose(body, x0, z0, camera_height_);
    body.id = 1001 + index;

    // hand_up / hand_down in progress
    if (t < p.move_end) {
      float k = float(std::clamp((t - p.move_start) / std::max(p.move_end - p.move_start, 1e-3), 0.0, 1.0));
      (p.move_left_hand ? p.left_y : p.right_y) = p.from_y + (p.to_y - p.from_y) * k;
    } else if (p.move_end > 0) {
      (p.move_left_hand ? p.left_y : p.right_y) = p.to_y;
      p.move_end = -1;
    }

    // Swipe: the raised arm sweeps across the body in the first third of the duration (an
    // ease-in-out), stays there, and comes back in the last third.
    float sweep = 0;
    if (t >= p.swipe_start && t < p.swipe_end) {
      float k = float((t - p.swipe_start) / (p.swipe_end - p.swipe_start));
      sweep = k < 1.0f / 3 ? 0.5f - 0.5f * std::cos(k * 3 * kPi)
              : k < 2.0f / 3 ? 1.0f
                             : 0.5f + 0.5f * std::cos((k - 2.0f / 3) * 3 * kPi);
    }

    auto arm = [&](float side, Arm pose, float dy, bool swiping) {
      if (pose == Arm::kDown) return;
      float azimuth = 25.0f, elevation = std::asin(std::clamp(dy / 0.62f, -0.9f, 0.9f)) * 180 / kPi;
      if (pose == Arm::kGuide) azimuth = 0, elevation = -45;
      if (pose == Arm::kAngle) {
        const int i = side < 0 ? 1 : 0;
        const double span = p.raise_end[i] - p.raise_start[i];
        const float k = span > 0 ? float(std::clamp((t - p.raise_start[i]) / span, 0.0, 1.0)) : 1.0f;
        azimuth = p.azimuth[i] * k;
        elevation = -85.0f + (p.elevation[i] + 85.0f) * k;  // from hanging at the side
      }
      // A swipe towards the other side of the body crosses in front of it (to 155 degrees).
      if (swiping && pose == Arm::kMenu && p.swipe_direction == -side) azimuth += 130.0f * sweep;
      ArmPose(body, side, azimuth, elevation);
    };
    arm(1.0f, p.right, p.right_y, !p.swipe_left_hand);
    arm(-1.0f, p.left, p.left_y, p.swipe_left_hand);

    // Steering wheel: both hands 40 cm in front of the chest, on a wheel 34 cm across turned by its
    // angle; a boost pulls them back to the chest and pushes them out again.
    if (p.wheel) {
      float reach = 0.40f;
      if (t >= p.boost_start && t < p.boost_end) {
        const double span = std::max(p.boost_end - p.boost_start, 1e-3);
        const double a = p.boost_start, b = a + span * 0.4, c = a + span * 0.5, d = a + span * 0.8;
        reach = t < b ? Glide(t, a, b, 0.40f, 0.12f)
                : t < c ? 0.12f
                : t < d ? Glide(t, c, d, 0.12f, 0.52f)
                        : Glide(t, d, p.boost_end, 0.52f, 0.40f);
      }
      const float angle = WheelAngle(p, t) * kPi / 180, radius = 0.17f;
      const float* chest = body.joints[kShoulderCenter];
      const float cx = chest[0], cy = chest[1] - 0.12f, cz = chest[2] - reach;
      auto hold = [&](float side, Joint shoulder, Joint elbow, Joint wrist, Joint hand) {
        // side +1 the right hand (at +x), -1 the left; turning right lowers the right hand.
        const float hx = cx + side * radius * std::cos(angle), hy = cy - side * radius * std::sin(angle);
        const float* s = body.joints[shoulder];
        const float ex = (s[0] + hx) / 2 + side * 0.08f, ey = (s[1] + hy) / 2 - 0.12f, ez = (s[2] + cz) / 2 + 0.02f;
        Set(body, elbow, ex, ey, ez);
        Set(body, wrist, hx + (ex - hx) * 0.25f, hy + (ey - hy) * 0.25f, cz + (ez - cz) * 0.25f);
        Set(body, hand, hx, hy, cz);
      };
      hold(1.0f, kShoulderRight, kElbowRight, kWristRight, kHandRight);
      hold(-1.0f, kShoulderLeft, kElbowLeft, kWristLeft, kHandLeft);
    }

    // Flap: both arms swing in the body's plane between hanging (-70 degrees) and 20 degrees up.
    if (t >= p.flap_start && t < p.flap_end) {
      const float k = 0.5f - 0.5f * std::cos(float(t - p.flap_start) * 2 * kPi * 1.25f);
      ArmPose(body, 1.0f, 0, -70.0f + 90.0f * k);
      ArmPose(body, -1.0f, 0, -70.0f + 90.0f * k);
    }

    // Wave: the right hand swings sideways at 2 Hz.
    if (t >= p.wave_start && t < p.wave_end) {
      float s = 0.12f * std::sin(float(t - p.wave_start) * 2 * kPi * 2);
      body.joints[kHandRight][0] += s;
      body.joints[kWristRight][0] += s * 0.7f;
    }

    // Dance: arms swing up and down out of phase, hips sway, knees bend on the beat (120 BPM).
    if (t >= p.dance_start && t < p.dance_end) {
      float beat = float(t - p.dance_start) * 2 * kPi * 2.0f;
      float up = 0.5f + 0.5f * std::sin(beat);
      float sway = 0.08f * std::sin(beat / 2);
      for (uint32_t j = 0; j < kJointCount; ++j) body.joints[j][0] += sway;
      for (Joint j : {kElbowRight, kWristRight, kHandRight}) body.joints[j][1] += up * (j == kElbowRight ? 0.3f : 0.6f);
      for (Joint j : {kElbowLeft, kWristLeft, kHandLeft})
        body.joints[j][1] += (1 - up) * (j == kElbowLeft ? 0.3f : 0.6f);
      float bend = 0.06f * (0.5f + 0.5f * std::sin(beat * 2));
      for (uint32_t j = 0; j < kJointCount; ++j)
        if (j != kAnkleLeft && j != kAnkleRight && j != kFootLeft && j != kFootRight) body.joints[j][1] -= bend;
    }

    auto is_foot = [](uint32_t j) { return j == kAnkleLeft || j == kAnkleRight || j == kFootLeft || j == kFootRight; };
    auto is_leg = [&](uint32_t j) { return is_foot(j) || j == kKneeLeft || j == kKneeRight; };

    // Crouch: everything above the knees drops 35 cm, the knees bend forward and the hips go back.
    if (float c = HoldProfile(t, p.crouch_start, p.crouch_end); c > 0) {
      for (uint32_t j = 0; j < kJointCount; ++j) {
        if (is_foot(j)) continue;
        if (j == kKneeLeft || j == kKneeRight) {
          body.joints[j][1] -= 0.15f * c;
          body.joints[j][2] -= 0.18f * c;
        } else {
          body.joints[j][1] -= 0.35f * c;
          body.joints[j][2] += 0.10f * c;
        }
      }
    }

    // Lean: the torso, head and arms tilt 20 degrees sideways about the hips.
    if (float l = HoldProfile(t, p.lean_start, p.lean_end); l > 0) {
      const float angle = 20.0f * kPi / 180 * l * p.lean_direction;
      const float hx = body.joints[kHipCenter][0], hy = body.joints[kHipCenter][1];
      for (uint32_t j = 0; j < kJointCount; ++j) {
        if (is_leg(j) || j == kHipCenter || j == kHipLeft || j == kHipRight) continue;
        const float dx = body.joints[j][0] - hx, dy = body.joints[j][1] - hy;
        body.joints[j][0] = hx + dx * std::cos(angle) + dy * std::sin(angle);
        body.joints[j][1] = hy - dx * std::sin(angle) + dy * std::cos(angle);
      }
    }

    // Lean forward / back: the torso, head and arms pitch 20 degrees about the hips.
    if (float q = HoldProfile(t, p.pitch_start, p.pitch_end); q > 0) {
      const float angle = 20.0f * kPi / 180 * q * p.pitch_direction;
      const float hy = body.joints[kHipCenter][1], hz = body.joints[kHipCenter][2];
      for (uint32_t j = 0; j < kJointCount; ++j) {
        if (is_leg(j) || j == kHipCenter || j == kHipLeft || j == kHipRight) continue;
        const float dy = body.joints[j][1] - hy, dz = body.joints[j][2] - hz;
        body.joints[j][2] = hz + dz * std::cos(angle) - dy * std::sin(angle);
        body.joints[j][1] = hy + dz * std::sin(angle) + dy * std::cos(angle);
      }
    }

    // Jump: the whole body rises 30 cm and comes down.
    if (t >= p.jump_start && t < p.jump_end) {
      const float h = 0.30f * std::sin(float((t - p.jump_start) / (p.jump_end - p.jump_start)) * kPi);
      for (uint32_t j = 0; j < kJointCount; ++j) body.joints[j][1] += h;
    }

    std::memcpy(body.position, body.joints[kSpine], sizeof(body.position));
  }

  float camera_height_;
  std::vector<Event> events_;
  size_t next_ = 0;
  Player players_[2];
  std::string file_;  // a live script
  size_t read_ = 0;   // bytes of it read
  double next_read_ = 0;
};

}  // namespace

void StandingPose(Body& body, float x, float z, float camera_height) {
  // An adult about 1.75 m tall, arms down, facing the sensor; y measured from the sensor's height.
  const float floor = -camera_height;
  const float hip = floor + 0.95f;
  struct {
    Joint joint;
    float x, y, z;
  } const kJoints[] = {
      {kHipCenter, 0, 0, 0},           {kSpine, 0, 0.22f, 0.02f},          {kShoulderCenter, 0, 0.50f, 0},
      {kHead, 0, 0.68f, 0},            {kShoulderLeft, -0.18f, 0.46f, 0},  {kElbowLeft, -0.22f, 0.20f, 0},
      {kWristLeft, -0.24f, -0.02f, 0}, {kHandLeft, -0.25f, -0.10f, -0.02f}, {kShoulderRight, 0.18f, 0.46f, 0},
      {kElbowRight, 0.22f, 0.20f, 0},  {kWristRight, 0.24f, -0.02f, 0},   {kHandRight, 0.25f, -0.10f, -0.02f},
      {kHipLeft, -0.10f, -0.04f, 0},   {kKneeLeft, -0.11f, -0.50f, -0.02f}, {kAnkleLeft, -0.12f, -0.90f, 0},
      {kFootLeft, -0.12f, -0.95f, -0.10f}, {kHipRight, 0.10f, -0.04f, 0}, {kKneeRight, 0.11f, -0.50f, -0.02f},
      {kAnkleRight, 0.12f, -0.90f, 0}, {kFootRight, 0.12f, -0.95f, -0.10f},
  };
  for (const auto& j : kJoints) Set(body, j.joint, x + j.x, hip + j.y, z + j.z);
  std::fill(std::begin(body.joint_state), std::end(body.joint_state), uint8_t(kJointTracked));
  body.quality = 0;
  std::memcpy(body.position, body.joints[kSpine], sizeof(body.position));
}

void ArmPose(Body& body, float side, float azimuth, float elevation) {
  const float a = azimuth * kPi / 180, e = elevation * kPi / 180;
  const float dir[3] = {side * std::cos(e) * std::cos(a), std::sin(e), -std::cos(e) * std::sin(a)};
  const bool right = side > 0;
  const float* s = body.joints[right ? kShoulderRight : kShoulderLeft];
  auto at = [&](Joint j, float length) {
    Set(body, j, s[0] + dir[0] * length, s[1] + dir[1] * length, s[2] + dir[2] * length);
  };
  at(right ? kElbowRight : kElbowLeft, 0.30f);
  at(right ? kWristRight : kWristLeft, 0.55f);
  at(right ? kHandRight : kHandLeft, 0.63f);
}

std::unique_ptr<Source> MakeSyntheticSource(const std::string& script, float camera_height) {
  return std::make_unique<SyntheticSource>(script, camera_height);
}

}  // namespace x360::nui
