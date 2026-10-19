"""Feet on the ground held still.

Feet bearing weight do not move, but the pose model's feet jitter and slide (their depth most of all);
a Kinect's planted feet are steady and a step shows at once.
"""

import numpy as np

import kinect_mapping as km


class FootLock:
    """A foot whose ankle stays still (filtered speed below HOLD_SPEED) is held where it stopped,
    until it moves off: faster than RELEASE_SPEED or farther than RELEASE_DISTANCE from there
    (depth counting for half, it's the noisiest). The limits are above what the model's noise
    reaches at rest and below what the first tenth of a second of a step does. While held, the
    point creeps towards the measured one (SETTLE per frame), so it settles on where the foot
    really is rather than on one noisy frame. On release the foot rejoins the measured one over a
    few frames instead of jumping."""

    HOLD_SPEED = 0.3  # m/s
    RELEASE_SPEED = 1.0
    RELEASE_DISTANCE = 0.06  # metres
    SETTLE = 0.05
    REJOIN = 0.5  # of the remaining gap closed each frame after a release
    _WEIGHTS = np.array([1.0, 1.0, 0.5])
    _FEET = ((km.ANKLE_LEFT, km.FOOT_LEFT), (km.ANKLE_RIGHT, km.FOOT_RIGHT))

    def __init__(self):
        self._locked = {}  # ankle -> (2, 3) the ankle's and foot's held positions
        self._gap = {}  # ankle -> (2, 3) what's left to close after a release

    def __call__(self, joints, speeds):
        """joints: (20, 3) filtered; speeds: (20,) their filtered speeds. Returns the joints."""
        joints = np.array(joints, dtype=np.float64)
        for ankle, foot in self._FEET:
            pair = [ankle, foot]
            held = self._locked.get(ankle)
            if held is not None:
                moved = np.linalg.norm((joints[ankle] - held[0]) * self._WEIGHTS)
                if moved > self.RELEASE_DISTANCE or speeds[ankle] > self.RELEASE_SPEED:
                    del self._locked[ankle]
                    self._gap[ankle] = held - joints[pair]
                else:
                    held += (joints[pair] - held) * self.SETTLE
                    joints[pair] = held
                    continue
            gap = self._gap.get(ankle)
            if gap is not None:
                gap = gap * self.REJOIN
                if np.abs(gap).max() < 0.002:
                    del self._gap[ankle]
                else:
                    self._gap[ankle] = gap
                    joints[pair] += gap
            if speeds[ankle] < self.HOLD_SPEED and ankle not in self._gap:
                self._locked[ankle] = joints[pair].copy()
        return joints
