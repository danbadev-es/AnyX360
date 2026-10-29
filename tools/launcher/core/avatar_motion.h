// The launcher's avatar in motion (docs/AVATARS.md): an idle loop (breathing, the weight shifting,
// the arms swaying, a look around now and then, blinks) and gestures over it (a wave hello, a nod),
// made here on the avatar's skeleton. The console's own animations (the pack's "Animation ..."
// entries) are in a format nobody has decoded, for a skeleton the avatar library in each game
// builds; these are AnyX360's own.
//
// The skeleton is the Xbox avatar's (72 joints, the same numbering in every part): each vertex says
// which four joints move it and how much. The parts are modelled in a T pose (the bind pose); a pose
// here is each joint's rotation in its parent's frame from there, turned about the joint's pivot,
// and the viewer (ui/avatar_view.cpp) skins the parts with it (dual quaternions). The avatar faces
// +z with y up, so its left is +x.
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace anyx360::launcher::avatar {

inline constexpr int kJoints = 72;

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Length(Vec3 v);

// A rotation (unit quaternion).
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};
Quat AxisAngle(Vec3 axis, float radians);
Quat operator*(Quat a, Quat b);  // b first, then a
Quat Conjugate(Quat q);
Vec3 Rotate(Quat q, Vec3 v);
Quat Slerp(Quat a, Quat b, float t);  // the shorter way
float Angle(Quat q);                  // how far it turns, 0 to pi
float AngleBetween(Quat a, Quat b);

// The joints the motion moves ("L": the avatar's left, +x).
enum Joint : int {
  kRoot = 0,
  kPelvis = 4,
  kSpine = 10,
  kChest = 18,
  kNeck = 24,
  kHead = 19,
  kCollarL = 12,
  kCollarR = 16,
  kUpperArmL = 27,
  kUpperArmR = 30,
  kElbowL = 32,
  kElbowR = 35,
  kForearmL = 31,  // the forearm's twist, by the wrist
  kForearmR = 34,
  kHandL = 33,
  kHandR = 36,
  kThighL = 7,
  kThighR = 9,
  kShinL = 13,
  kShinR = 17,
  kFootL = 11,
  kFootR = 15,
};

// Each joint's parent (-1: the root), as the vertices' blends show them.
const std::array<int8_t, kJoints>& Parents();

// Where each joint turns, in the bind pose.
struct Skeleton {
  std::array<Vec3, kJoints> pivot{};
};
// The default male avatar's, measured from its skinning.
Skeleton DefaultSkeleton();

// A vertex of a part, in the bind pose: its position and its four joints and weights (0-255).
struct SkinnedVertex {
  Vec3 position;
  uint8_t joints[4] = {};
  uint8_t weights[4] = {};
};
// The avatar's own pivots: each joint's where its vertices in `body` (the body part's) blend with
// its ancestors'; the upper arms' where the arm starts in `all` (the innermost of the vertices
// mostly on the upper arm, at their middle height and depth, as the relaxed pose of 0.24.0). Joints
// without such vertices keep `fallback`'s.
Skeleton EstimateSkeleton(std::span<const SkinnedVertex> body, std::span<const SkinnedVertex> all,
                          const Skeleton& fallback = DefaultSkeleton());

// A pose: each joint's rotation in its parent's frame (identity: the bind pose); the whole body's
// sway, a lean about the point between the ankles (the feet stay flat on the floor); and the face's
// expression, as frames of its textures (eyes: 0 open, 7 half closed, 4 closed; mouth: 0 as it is,
// 4 a smile).
struct Pose {
  std::array<Quat, kJoints> local{};
  Quat sway{};
  int eyes = 0, mouth = 0;
};
inline constexpr int kEyesOpen = 0, kEyesHalf = 7, kEyesClosed = 4, kMouthRest = 0, kMouthSmile = 4;

// Standing relaxed, still: the bind pose with the arms lowered 74 degrees and 7 forward (the viewer's
// pose since 0.24.0; pictures and gamerpics keep it).
Pose RestPose();

// Each joint's transform for skinning: a point of the bind pose on that joint goes to
// rotation * point + translation.
struct JointTransform {
  Quat rotation;
  Vec3 translation;
};
using Transforms = std::array<JointTransform, kJoints>;
void Solve(const Skeleton& skeleton, const Pose& pose, Transforms& out);
// A joint's transform as a dual quaternion (x, y, z, w each): `real` the rotation, `dual` half the
// translation times it.
void DualQuaternion(const JointTransform& transform, float real[4], float dual[4]);
// Where a bind-pose point goes, moved by its joints: their dual quaternions blended by weight and
// normalized (the viewer's shader does the same), so a vertex shared by two joints turns about
// their pivot instead of collapsing towards it.
Vec3 Skin(const Transforms& transforms, const SkinnedVertex& vertex);

// How much it moves: 1 on Home; the avatar editor's is calmer (less of everything, no looking away
// from the player editing it).
struct Style {
  float amount = 1;
  bool look_around = true;
};

enum class Gesture { kNone, kWave, kNod };
inline constexpr double kIdleLoop = 24;      // seconds: the idle repeats exactly
inline constexpr double kWaveLength = 3.2;   // raising the right arm, three waves, lowering it
inline constexpr double kNodLength = 0.9;
double GestureLength(Gesture gesture);

// The idle at `time` seconds (any: it loops every kIdleLoop), from the rest pose.
void SampleIdle(double time, const Style& style, Pose& pose);
// How much of the gesture shows `time` seconds after it started: 0 before and after it, 1 at its
// height (for the wave, how far the arm is raised).
float GestureWeight(Gesture gesture, double time);
// The gesture `time` seconds in, over the idle in `pose`. `viewer_yaw`: how far the avatar is turned
// from the viewer (radians, + to its left): the head turns back towards them as it waves.
void ApplyGesture(Gesture gesture, double time, Pose& pose, float viewer_yaw = 0);

// One avatar's animation: the idle's clock and the gesture playing (and one to play after it).
class Motion {
 public:
  void SetStyle(const Style& style) { style_ = style; }
  void SetViewerYaw(float yaw) { viewer_yaw_ = yaw; }
  // The idle starts again from its beginning; no gesture.
  void Restart(double now);
  // Plays `gesture` from `at` (seconds, the clock Sample is given). A wave asked for during another
  // gesture follows it; a nod asked for during a wave is dropped, as is one gesture asked for twice.
  void Play(Gesture gesture, double at);
  // The pose at `now`.
  void Sample(double now, Pose& pose) const;
  // A gesture is playing at `now` (or is about to): it moves faster than the idle.
  bool Gesturing(double now) const;

 private:
  struct Playing {
    Gesture gesture = Gesture::kNone;
    double at = 0;
    double End() const { return at + GestureLength(gesture); }
  };
  Style style_;
  float viewer_yaw_ = 0;
  double start_ = 0;
  Playing current_, next_;
};

}  // namespace anyx360::launcher::avatar
