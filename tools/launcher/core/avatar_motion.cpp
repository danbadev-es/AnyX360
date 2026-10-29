// The launcher's avatar in motion (see avatar_motion.h).
#include "core/avatar_motion.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <vector>

namespace anyx360::launcher::avatar {

namespace {

constexpr float kDeg = 3.14159265358979f / 180;
constexpr double kTau = 6.283185307179586;
constexpr Vec3 kX{1, 0, 0}, kY{0, 1, 0}, kZ{0, 0, 1};

Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
Vec3 Mirror(Vec3 v) { return {-v.x, v.y, v.z}; }

// Turns the joint by `q` after its own rotation, in its parent's frame.
void Turn(Pose& pose, int joint, Quat q) { pose.local[size_t(joint)] = q * pose.local[size_t(joint)]; }

// Turned `yaw` about y (+: to the avatar's left), after `pitch` about x (+: the top forward) and
// `roll` about z (+: the top to the avatar's right). Radians.
Quat YawPitchRoll(float yaw, float pitch, float roll) {
  return AxisAngle(kY, yaw) * AxisAngle(kX, pitch) * AxisAngle(kZ, roll);
}

// 0 to 1, smooth to the second derivative.
float Smoother(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return float(t * t * t * (t * (t * 6 - 15) + 10));
}
// 0 until a, up to 1 by b, 1 until c, down to 0 by d.
float Envelope(double t, double a, double b, double c, double d) {
  if (t <= a || t >= d) return 0;
  if (t < b) return Smoother((t - a) / (b - a));
  if (t <= c) return 1;
  return 1 - Smoother((t - c) / (d - c));
}
// 0 at a, 1 at peak, 0 at b.
float Bump(double t, double a, double peak, double b) { return Envelope(t, a, peak, peak, b); }
float Sine(double t, double period, double phase) { return float(std::sin(kTau * t / period + phase)); }

// Parents before children.
const std::vector<int>& Order() {
  static const std::vector<int> order = [] {
    std::vector<int> out;
    std::bitset<kJoints> done;
    while (out.size() < size_t(kJoints)) {
      for (int j = 0; j < kJoints; ++j) {
        const int parent = Parents()[size_t(j)];
        if (!done[size_t(j)] && (parent < 0 || done[size_t(parent)])) {
          out.push_back(j);
          done.set(size_t(j));
        }
      }
    }
    return out;
  }();
  return order;
}

// The idle's looks around: when (in the loop) and where, in degrees.
struct Glance {
  double start, in, hold, out;
  float yaw, pitch, roll;
};
constexpr Glance kGlances[] = {
    {4.6, 0.85, 2.3, 0.95, 13, 2, 2.5},   // to its left (on Home, the games), a little down
    {12.6, 0.8, 1.9, 0.9, -10, -3, -1.5},  // to its right, a little up
    {19.0, 0.6, 0.9, 0.7, 5, 7, 0},        // a glance down
};
// Blinks, each 0.16 s: half closed, closed, half closed. Two close together once; one before each
// look around.
constexpr double kBlinks[] = {1.7, 4.5, 8.3, 10.6, 10.95, 12.5, 16.4, 18.95, 22.3};
constexpr double kBlinkLength = 0.16;

void ApplyWave(double t, Pose& p, float viewer_yaw) {
  if (t <= 0 || t >= kWaveLength) return;
  // Each part of the arm on its own time: the forearm folds up as the arm rises (a little ahead of
  // it, so the hand never hangs from a raised arm), the palm turns to the front on the way up; and
  // the other way down.
  const float shoulder = GestureWeight(Gesture::kWave, t);
  const float elbow = Envelope(t, 0.0, 0.4, 2.42, 3.12);
  const float twist = Envelope(t, 0.04, 0.5, 2.4, 3.05);
  const float body = Envelope(t, 0.0, 0.8, 2.3, kWaveLength);
  // Three swings of the forearm, 0.6 s each, once the hand is up; the hand a little behind.
  const float waving = Envelope(t, 0.52, 0.78, 2.22, 2.5);
  const double phase = kTau * (t - 0.62) / 0.6;
  const float swing = waving * float(std::sin(phase));
  const float flick = waving * float(std::sin(phase - 1.1));
  // The right arm (in the bind pose along -x, the palm down): out to the side a little under the
  // shoulder (the collar raises it 7 more) and 18 degrees forward, the forearm up beside the head
  // and swinging outwards from upright (never towards the head), the palm to the front.
  // On the way up and down the arm passes more in front of the body than out to the side (the hand
  // doesn't reach far out).
  const float in_front = 55 * (Bump(t, 0.0, 0.25, 0.62) + Bump(t, 2.48, 2.85, kWaveLength));
  const Quat upper = AxisAngle(kY, (18 + in_front) * kDeg) * AxisAngle(kZ, (26 - 2 * swing) * kDeg);
  const Quat forearm = AxisAngle(kZ, -(93 + 10 * swing) * kDeg);
  const Quat palm = AxisAngle(kX, -90 * kDeg);
  const Quat hand = AxisAngle(kY, 9 * flick * kDeg);
  // While the arm is low the forearm folds forward (as an elbow bends), turning up beside the head as
  // the arm rises: the hand comes up in front instead of out to the side.
  const Quat fold = Slerp(AxisAngle(kY, 80 * kDeg), forearm, shoulder);
  p.local[kUpperArmR] = Slerp(p.local[kUpperArmR], upper, shoulder);
  p.local[kElbowR] = Slerp(p.local[kElbowR], fold, elbow);
  p.local[kForearmR] = Slerp(p.local[kForearmR], palm, twist);
  p.local[kHandR] = Slerp(p.local[kHandR], hand, twist);
  Turn(p, kCollarR, AxisAngle(kZ, -7 * kDeg * shoulder));  // the shoulder rises with the arm
  // The body leans a little away from the raised arm; the head tilts towards it, turns back to the
  // viewer and nods as the hand comes up; a smile.
  Turn(p, kSpine, AxisAngle(kZ, -1.2f * kDeg * body));
  Turn(p, kChest, AxisAngle(kZ, -0.8f * kDeg * body));
  const float nod = Bump(t, 0.25, 0.5, 0.95);
  const float to_viewer = -0.6f * std::clamp(viewer_yaw, -0.5f, 0.5f) * body;  // at most 17 degrees
  Turn(p, kNeck, YawPitchRoll(0.35f * to_viewer, 2.5f * kDeg * nod, 0.8f * kDeg * body));
  Turn(p, kHead, YawPitchRoll(0.65f * to_viewer, 5.5f * kDeg * nod, 2.4f * kDeg * body));
  if (body > 0.25f) p.mouth = kMouthSmile;
}

void ApplyNod(double t, Pose& p) {
  if (t <= 0 || t >= kNodLength) return;
  // Down, then back up a little past level.
  const float down = GestureWeight(Gesture::kNod, t), up = Bump(t, 0.45, 0.65, kNodLength);
  const float pitch = (9 * down - 2 * up) * kDeg;
  Turn(p, kNeck, AxisAngle(kX, 0.35f * pitch));
  Turn(p, kHead, AxisAngle(kX, 0.65f * pitch));
  Turn(p, kChest, AxisAngle(kX, 1.2f * kDeg * down));
}

}  // namespace

float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }

Quat AxisAngle(Vec3 axis, float radians) {
  const float length = Length(axis);
  if (length <= 0) return {};
  const float s = std::sin(radians / 2) / length;
  return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians / 2)};
}

Quat operator*(Quat a, Quat b) {
  return {a.w * b.x + b.w * a.x + a.y * b.z - a.z * b.y, a.w * b.y + b.w * a.y + a.z * b.x - a.x * b.z,
          a.w * b.z + b.w * a.z + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat Conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

Vec3 Rotate(Quat q, Vec3 v) {
  const Vec3 u{q.x, q.y, q.z};
  return v + Cross(u, Cross(u, v) + v * q.w) * 2;
}

Quat Slerp(Quat a, Quat b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  float d = Dot(a, b);
  if (d < 0) b = {-b.x, -b.y, -b.z, -b.w}, d = -d;
  float wa = 1 - t, wb = t;
  if (d < 0.9995f) {
    const float angle = std::acos(d), s = std::sin(angle);
    wa = std::sin((1 - t) * angle) / s;
    wb = std::sin(t * angle) / s;
  }
  Quat q{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
  const float n = std::sqrt(Dot(q, q));
  return {q.x / n, q.y / n, q.z / n, q.w / n};
}

float Angle(Quat q) { return 2 * std::atan2(Length({q.x, q.y, q.z}), std::abs(q.w)); }  // exact for small ones too
float AngleBetween(Quat a, Quat b) { return Angle(Conjugate(a) * b); }

const std::array<int8_t, kJoints>& Parents() {
  // From the vertices' blends (docs/AVATARS.md): the root (0) and the pelvis (4); the spine (10, with
  // 1 at the belly), the chest (18, with 5 at the upper back and 14 at the collar of jackets), the
  // neck (24) and the head (19); each leg's thigh (7, 9), shin (13, 17, with 6 and 8 for boots), foot
  // (11, 15) and toes (21, 23), and 2 and 3 at the hips; each arm's collar (12, 16), upper arm (27,
  // 30), its twist (26, 29), the forearm from the elbow (32, 35), its twist by the wrist (31, 34),
  // the hand (33, 36) and five fingers of three joints. Joints no part was seen using follow the
  // nearest that one would expect.
  static const std::array<int8_t, kJoints> parents = [] {
    std::array<int8_t, kJoints> p{};
    const int8_t list[kJoints] = {
        -1, 10, 4,  4,  0,  18, 13, 4,  17, 4,   // 0-9
        4,  13, 18, 7,  18, 17, 18, 9,  10, 24,  // 10-19
        19, 11, 19, 15, 18, 18, 27, 12, 18, 30,  // 20-29
        16, 32, 26, 31, 35, 29, 34, 33, 33, 33,  // 30-39
        33, 33, 33, 33, 36, 36, 36, 36, 36, 36,  // 40-49
        36, 37, 38, 39, 40, 43, 44, 45, 46, 47,  // 50-59
        50, 51, 52, 53, 54, 55, 56, 57, 58, 59,  // 60-69
        60, 0};                                   // 70-71
    std::copy(std::begin(list), std::end(list), p.begin());
    return p;
  }();
  return parents;
}

Skeleton DefaultSkeleton() {
  Skeleton s;
  auto both = [&](int left, int right, Vec3 at) {
    s.pivot[size_t(left)] = at;
    s.pivot[size_t(right)] = Mirror(at);
  };
  s.pivot[0] = {0, 0.70f, 0};
  s.pivot[1] = {0, 0.851f, -0.022f};
  s.pivot[4] = {0, 0.709f, -0.031f};
  s.pivot[5] = {0, 1.019f, -0.019f};
  s.pivot[10] = {0, 0.804f, 0.021f};
  s.pivot[14] = {0, 1.131f, -0.018f};
  s.pivot[18] = {0, 0.883f, 0.004f};
  s.pivot[19] = {0, 1.125f, -0.040f};
  s.pivot[24] = {0, 1.110f, -0.016f};
  both(2, 3, {0.071f, 0.634f, 0.006f});
  both(7, 9, {0.068f, 0.638f, 0.001f});
  both(13, 17, {0.087f, 0.385f, -0.013f});
  both(6, 8, {0.096f, 0.148f, -0.045f});
  both(11, 15, {0.096f, 0.108f, -0.044f});
  both(21, 23, {0.093f, 0.049f, 0.058f});
  both(12, 16, {0.085f, 1.033f, -0.020f});
  both(27, 30, {0.120f, 1.052f, -0.030f});
  both(26, 29, {0.265f, 1.035f, -0.028f});
  both(32, 35, {0.345f, 1.036f, -0.020f});
  both(31, 34, {0.430f, 1.030f, -0.028f});
  both(33, 36, {0.508f, 1.022f, -0.024f});
  // The fingers (index, middle, ring, little, thumb), three joints each.
  both(37, 44, {0.608f, 1.009f, 0.009f});
  both(38, 45, {0.616f, 1.012f, -0.016f});
  both(39, 46, {0.611f, 1.006f, -0.042f});
  both(40, 47, {0.601f, 1.002f, -0.068f});
  both(43, 50, {0.541f, 1.004f, 0.009f});
  both(51, 56, {0.649f, 1.003f, 0.010f});
  both(52, 57, {0.653f, 0.996f, -0.017f});
  both(53, 58, {0.643f, 1.004f, -0.042f});
  both(54, 59, {0.626f, 0.995f, -0.067f});
  both(55, 60, {0.566f, 1.001f, 0.028f});
  both(61, 66, {0.677f, 1.005f, 0.008f});
  both(62, 67, {0.682f, 1.005f, -0.017f});
  both(63, 68, {0.673f, 1.002f, -0.043f});
  both(64, 69, {0.652f, 1.000f, -0.067f});
  both(65, 70, {0.597f, 0.979f, 0.040f});
  // Those no part uses: where their parent turns.
  for (int j : {20, 22, 25, 28, 41, 42, 48, 49, 71}) s.pivot[size_t(j)] = s.pivot[size_t(Parents()[size_t(j)])];
  return s;
}

Skeleton EstimateSkeleton(std::span<const SkinnedVertex> body, std::span<const SkinnedVertex> all, const Skeleton& fallback) {
  Skeleton s = fallback;
  std::array<std::bitset<kJoints>, kJoints> ancestors;
  for (int j = 0; j < kJoints; ++j)
    for (int p = Parents()[size_t(j)]; p >= 0; p = Parents()[size_t(p)]) ancestors[size_t(j)].set(size_t(p));
  // Each joint's pivot: the middle of its vertices that also follow its ancestors, by how much they
  // follow both.
  std::array<double, kJoints> sum{};
  std::array<std::array<double, 3>, kJoints> at{};
  for (const SkinnedVertex& v : body) {
    for (int k = 0; k < 4; ++k) {
      const int j = v.joints[k];
      if (!v.weights[k] || j >= kJoints) continue;
      double up = 0;
      for (int m = 0; m < 4; ++m)
        if (m != k && v.weights[m] && v.joints[m] < kJoints && ancestors[size_t(j)][v.joints[m]]) up += v.weights[m];
      const double w = std::min(double(v.weights[k]), up) / 255;
      if (w <= 0) continue;
      sum[size_t(j)] += w;
      at[size_t(j)][0] += w * v.position.x, at[size_t(j)][1] += w * v.position.y, at[size_t(j)][2] += w * v.position.z;
    }
  }
  for (int j = 1; j < kJoints; ++j)
    if (sum[size_t(j)] >= 0.25)
      s.pivot[size_t(j)] = {float(at[size_t(j)][0] / sum[size_t(j)]), float(at[size_t(j)][1] / sum[size_t(j)]),
                            float(at[size_t(j)][2] / sum[size_t(j)])};
  // The upper arms as the relaxed pose has turned them since 0.24.0: where the arm starts.
  for (int side = 0; side < 2; ++side) {
    const int joint = side ? kUpperArmR : kUpperArmL;
    double y = 0, z = 0, n = 0;
    float inner = 1e9f;
    for (const SkinnedVertex& v : all)
      for (int k = 0; k < 4; ++k)
        if (v.joints[k] == joint && v.weights[k] > 128) {
          inner = std::min(inner, std::abs(v.position.x));
          y += v.position.y, z += v.position.z, ++n;
        }
    if (n >= 8) s.pivot[size_t(joint)] = {side ? -inner : inner, float(y / n), float(z / n)};
  }
  return s;
}

Pose RestPose() {
  Pose p;
  // Each arm turned down about z, then forward about x (the left one is along +x).
  p.local[kUpperArmL] = AxisAngle(kX, -7 * kDeg) * AxisAngle(kZ, -74 * kDeg);
  p.local[kUpperArmR] = AxisAngle(kX, -7 * kDeg) * AxisAngle(kZ, 74 * kDeg);
  return p;
}

void Solve(const Skeleton& skeleton, const Pose& pose, Transforms& out) {
  std::array<Quat, kJoints> turned;  // each joint's rotation in the model
  std::array<Vec3, kJoints> pivot;   // where its pivot went
  const Vec3 feet = (skeleton.pivot[kFootL] + skeleton.pivot[kFootR]) * 0.5f;
  for (int j : Order()) {
    const size_t i = size_t(j);
    const int parent = Parents()[i];
    if (parent < 0) {
      turned[i] = pose.sway * pose.local[i];
      pivot[i] = feet + Rotate(pose.sway, skeleton.pivot[i] - feet);
    } else {
      const size_t p = size_t(parent);
      // The feet stay flat on the floor whatever the legs do.
      turned[i] = (j == kFootL || j == kFootR) ? pose.local[i] : turned[p] * pose.local[i];
      pivot[i] = pivot[p] + Rotate(turned[p], skeleton.pivot[i] - skeleton.pivot[p]);
    }
    out[i].rotation = turned[i];
    out[i].translation = pivot[i] - Rotate(turned[i], skeleton.pivot[i]);
  }
}

void DualQuaternion(const JointTransform& transform, float real[4], float dual[4]) {
  const Quat q = transform.rotation;
  const Vec3 t = transform.translation, v{q.x, q.y, q.z};
  const Vec3 d = (t * q.w + Cross(t, v)) * 0.5f;  // (t, 0) * q / 2
  real[0] = q.x, real[1] = q.y, real[2] = q.z, real[3] = q.w;
  dual[0] = d.x, dual[1] = d.y, dual[2] = d.z, dual[3] = -0.5f * Dot(t, v);
}

Vec3 Skin(const Transforms& transforms, const SkinnedVertex& vertex) {
  // The joints' dual quaternions blended by weight (each in the same hemisphere as the first),
  // normalized, applied.
  float r[4] = {}, d[4] = {};
  Quat first;
  bool any = false;
  for (int k = 0; k < 4; ++k) {
    if (!vertex.weights[k]) continue;
    float real[4], dual[4];
    DualQuaternion(transforms[std::min<size_t>(vertex.joints[k], kJoints - 1)], real, dual);
    const Quat q{real[0], real[1], real[2], real[3]};
    if (!any) first = q, any = true;
    const float w = (Dot(q, first) < 0 ? -1.0f : 1.0f) * vertex.weights[k] / 255.0f;
    for (int c = 0; c < 4; ++c) r[c] += w * real[c], d[c] += w * dual[c];
  }
  if (!any) return vertex.position;
  const float n = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
  if (n < 1e-6f) return vertex.position;
  const Vec3 rv{r[0] / n, r[1] / n, r[2] / n}, dv{d[0] / n, d[1] / n, d[2] / n};
  const float rw = r[3] / n, dw = d[3] / n;
  const Vec3 p = vertex.position;
  const Vec3 rotated = p + Cross(rv, Cross(rv, p) + p * rw) * 2;
  return rotated + (dv * rw - rv * dw + Cross(rv, dv)) * 2;
}

double GestureLength(Gesture gesture) {
  switch (gesture) {
    case Gesture::kWave: return kWaveLength;
    case Gesture::kNod: return kNodLength;
    default: return 0;
  }
}

void SampleIdle(double time, const Style& style, Pose& p) {
  p = RestPose();
  double t = std::fmod(time, kIdleLoop);
  if (t < 0) t += kIdleLoop;
  const float a = style.amount;
  // Every period divides the loop's 24 s, so it repeats exactly.
  // Breathing, every 4 s: in a little quicker than out.
  const double breath_phase = kTau * t / 4;
  const float breath = float(std::sin(breath_phase + 0.35 * std::sin(breath_phase)));
  // The weight from foot to foot every 12 s (resting a while on each), with a slower drift; a sway
  // forward and back. The whole body leans about the ankles (the feet stay flat).
  const float side = float(0.8 * std::tanh(1.5 * std::sin(kTau * t / 12 + 0.6)) / std::tanh(1.5)) + 0.2f * Sine(t, 24, 1.9);
  const float fore = 0.7f * Sine(t, 8, 0.4) + 0.3f * Sine(t, 4.8, 2.0);
  p.sway = AxisAngle(kZ, -0.6f * kDeg * a * side) * AxisAngle(kX, 0.35f * kDeg * a * fore);
  // The spine curves back over the feet (the hips go with the lean, the shoulders less), the chest
  // rises with the breath.
  Turn(p, kSpine, AxisAngle(kX, -0.3f * kDeg * a * breath) * AxisAngle(kZ, 0.7f * kDeg * a * side));
  Turn(p, kChest, AxisAngle(kX, (-0.8f * breath - 0.15f * fore) * kDeg * a) * AxisAngle(kZ, 0.3f * kDeg * a * side));
  Turn(p, kCollarL, AxisAngle(kZ, 0.9f * kDeg * a * breath));
  Turn(p, kCollarR, AxisAngle(kZ, -0.9f * kDeg * a * breath));
  // The arms hang straight down whatever the lean, swing a little (each a slow pendulum of its own)
  // and open with the breath; the elbows soft.
  const float swing_l = 0.7f * Sine(t, 6, 0.3) + 0.3f * Sine(t, 24.0 / 7, 1.2);
  const float swing_r = 0.7f * Sine(t, 6, 2.2) + 0.3f * Sine(t, 24.0 / 7, 3.7);
  const float hang = -0.4f * side;
  Turn(p, kUpperArmL, AxisAngle(kX, 1.2f * kDeg * a * swing_l) * AxisAngle(kZ, (0.4f * breath + hang) * kDeg * a));
  Turn(p, kUpperArmR, AxisAngle(kX, 1.2f * kDeg * a * swing_r) * AxisAngle(kZ, (-0.4f * breath + hang) * kDeg * a));
  p.local[kElbowL] = AxisAngle(kY, -(5 + 1.5f * a * Sine(t, 6, 1.2)) * kDeg);
  p.local[kElbowR] = AxisAngle(kY, (5 + 1.5f * a * Sine(t, 6, 3.1)) * kDeg);
  // The head: a slow drift, now and then a look around; the breath and the lean kept off it (it
  // stays level). The neck takes a third.
  float yaw = (1.2f * Sine(t, 24, 0.4) + 0.5f * Sine(t, 4.8, 2.2)) * a;
  float pitch = 0.7f * Sine(t, 12, 1.3) * a, roll = 0.5f * Sine(t, 8, 0.2) * a;
  if (style.look_around) {
    for (const Glance& g : kGlances) {
      const float e = Envelope(t, g.start, g.start + g.in, g.start + g.in + g.hold, g.start + g.in + g.hold + g.out);
      yaw += g.yaw * e, pitch += g.pitch * e, roll += g.roll * e;
    }
  }
  Turn(p, kNeck, YawPitchRoll(0.35f * yaw * kDeg, (0.35f * pitch + 0.6f * a * breath) * kDeg, (0.35f * roll - 0.2f * a * side) * kDeg));
  Turn(p, kHead, YawPitchRoll(0.65f * yaw * kDeg, (0.65f * pitch + 0.4f * a * breath - 0.2f * a * fore) * kDeg,
                              (0.65f * roll - 0.2f * a * side) * kDeg));
  for (double b : kBlinks) {
    const double since = t - b;
    if (since >= 0 && since < kBlinkLength) p.eyes = since < 0.04 || since >= 0.12 ? kEyesHalf : kEyesClosed;
  }
}

float GestureWeight(Gesture gesture, double time) {
  switch (gesture) {
    case Gesture::kWave: return Envelope(time, 0.0, 0.62, 2.48, kWaveLength);
    case Gesture::kNod: return Bump(time, 0.0, 0.28, 0.6);
    default: return 0;
  }
}

void ApplyGesture(Gesture gesture, double time, Pose& pose, float viewer_yaw) {
  if (gesture == Gesture::kWave) ApplyWave(time, pose, viewer_yaw);
  if (gesture == Gesture::kNod) ApplyNod(time, pose);
}

void Motion::Restart(double now) {
  start_ = now;
  current_ = next_ = {};
}

void Motion::Play(Gesture gesture, double at) {
  if (gesture == Gesture::kNone) return;
  // Those over by then are forgotten.
  if (current_.gesture != Gesture::kNone && current_.End() <= at) current_ = next_, next_ = {};
  if (current_.gesture != Gesture::kNone && current_.End() <= at) current_ = {};
  if (current_.gesture == Gesture::kNone) {
    current_ = {gesture, at};
    return;
  }
  if (current_.gesture == gesture || next_.gesture == gesture) return;
  if (gesture == Gesture::kNod) return;  // a wave is playing or follows
  next_ = {gesture, std::max(at, current_.End())};
}

void Motion::Sample(double now, Pose& pose) const {
  SampleIdle(now - start_, style_, pose);
  for (const Playing* g : {&current_, &next_})
    if (g->gesture != Gesture::kNone && now > g->at && now < g->End()) ApplyGesture(g->gesture, now - g->at, pose, viewer_yaw_);
}

bool Motion::Gesturing(double now) const {
  for (const Playing* g : {&current_, &next_})
    if (g->gesture != Gesture::kNone && now >= g->at - 0.05 && now < g->End()) return true;
  return false;
}

}  // namespace anyx360::launcher::avatar
