// Scripted test poses for the virtual Kinect (ANYX360_NUI_SCRIPT), to drive a Kinect title without a
// camera: "seconds:action[/duration][@player],...", player 0 or 1 (default 0).
//
// Actions:
//   enter, leave             the player walks into / out of view
//   menu_right, menu_left    that arm held out to the side at shoulder height (the menu pose)
//   hand_up, hand_down       the raised hand moves 12 cm (over the duration, default 0.3 s)
//   swipe_left, swipe_right  the raised hand sweeps across the body and back (default 0.6 s)
//   lower                    both arms down
//   wave                     the right hand waves (default 2 s)
//   guide                    the left arm held at 45 degrees (the Kinect guide gesture)
//   dance                    a looping routine of arm, hip and knee movement (default 30 s)
//   idle                     stops any movement in progress
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

enum class Arm { kDown, kMenu, kGuide };

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
};

void Set(Body& b, Joint j, float x, float y, float z) {
  b.joints[j][0] = x;
  b.joints[j][1] = y;
  b.joints[j][2] = z;
}

class SyntheticSource : public Source {
 public:
  SyntheticSource(const std::string& script, float camera_height) : camera_height_(camera_height) {
    for (size_t start = 0; start < script.size();) {
      size_t end = script.find(',', start);
      if (end == std::string::npos) end = script.size();
      std::string item = script.substr(start, end - start);
      start = end + 1;
      Event e;
      size_t colon = item.find(':');
      if (colon == std::string::npos) continue;
      e.at = std::atof(item.substr(0, colon).c_str());
      std::string rest = item.substr(colon + 1);
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
    std::stable_sort(events_.begin(), events_.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
  }

  void Poll(double time, Snapshot& out) override {
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
      p.right = Arm::kMenu;
      p.right_y = 0;
    } else if (e.action == "menu_left") {
      p.left = Arm::kMenu;
      p.left_y = 0;
    } else if (e.action == "hand_up" || e.action == "hand_down") {
      p.move_left_hand = p.right != Arm::kMenu && p.left == Arm::kMenu;
      float& y = p.move_left_hand ? p.left_y : p.right_y;
      p.from_y = y;
      p.to_y = y + (e.action == "hand_up" ? 0.12f : -0.12f);
      p.move_start = time;
      p.move_end = time + duration(0.3);
    } else if (e.action == "swipe_left" || e.action == "swipe_right") {
      p.swipe_left_hand = p.right != Arm::kMenu && p.left == Arm::kMenu;
      p.swipe_direction = e.action == "swipe_left" ? -1.0f : 1.0f;
      p.swipe_start = time;
      p.swipe_end = time + duration(0.6);
    } else if (e.action == "lower") {
      p.right = p.left = Arm::kDown;
    } else if (e.action == "wave") {
      p.right = Arm::kMenu;
      p.wave_start = time;
      p.wave_end = time + duration(2.0);
    } else if (e.action == "guide") {
      p.left = Arm::kGuide;
    } else if (e.action == "dance") {
      p.dance_start = time;
      p.dance_end = time + duration(30.0);
    } else if (e.action == "idle") {
      p.wave_end = p.dance_end = p.swipe_end = p.move_end = time;
    } else {
      std::fprintf(stderr, "[nui] script: unknown action %s\n", e.action.c_str());
    }
  }

  void Pose(int index, double t, Body& body) {
    Player& p = players_[index];
    float x0 = index == 0 ? 0.0f : 0.6f;
    StandingPose(body, x0, 2.3f, camera_height_);
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

    // Arms as straight lines from the shoulder: `azimuth` from pointing out to the side (0) through
    // pointing forward at the sensor (90 degrees) to across the body; `elevation` up from horizontal.
    // side: +1 the player's right, -1 their left.
    auto arm = [&](float side, Arm pose, float dy, bool swiping, Joint shoulder, Joint elbow, Joint wrist,
                   Joint hand) {
      if (pose == Arm::kDown) return;
      float azimuth = 25.0f, elevation = std::asin(std::clamp(dy / 0.62f, -0.9f, 0.9f)) * 180 / kPi;
      if (pose == Arm::kGuide) azimuth = 0, elevation = -45;
      // A swipe towards the other side of the body crosses in front of it (to 155 degrees).
      if (swiping && pose == Arm::kMenu && p.swipe_direction == -side) azimuth += 130.0f * sweep;
      const float a = azimuth * kPi / 180, e = elevation * kPi / 180;
      const float dir[3] = {side * std::cos(e) * std::cos(a), std::sin(e), -std::cos(e) * std::sin(a)};
      const float* s = body.joints[shoulder];
      auto at = [&](Joint j, float length) {
        Set(body, j, s[0] + dir[0] * length, s[1] + dir[1] * length, s[2] + dir[2] * length);
      };
      at(elbow, 0.30f);
      at(wrist, 0.55f);
      at(hand, 0.63f);
    };
    arm(1.0f, p.right, p.right_y, !p.swipe_left_hand, kShoulderRight, kElbowRight, kWristRight, kHandRight);
    arm(-1.0f, p.left, p.left_y, p.swipe_left_hand, kShoulderLeft, kElbowLeft, kWristLeft, kHandLeft);

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

    std::memcpy(body.position, body.joints[kSpine], sizeof(body.position));
  }

  float camera_height_;
  std::vector<Event> events_;
  size_t next_ = 0;
  Player players_[2];
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

std::unique_ptr<Source> MakeSyntheticSource(const std::string& script, float camera_height) {
  return std::make_unique<SyntheticSource>(script, camera_height);
}

}  // namespace x360::nui
