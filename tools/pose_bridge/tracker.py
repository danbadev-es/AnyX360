"""Stable IDs for the people in view: MediaPipe returns its poses in no particular order, the Kinect
keeps a tracking ID while a person stays."""

import numpy as np


class Tracker:
    """Matches each frame's poses to the people seen before by the distance between their hips and
    where each person's hips were heading (two players crossing keep their IDs)."""

    MAX_STEP = 0.6     # metres a hip may be from where it was expected and stay the same person
    KEEP_SECONDS = 1.0  # a person missing this long is gone (their ID isn't reused)
    PREDICT_SECONDS = 0.2  # how far ahead a person's velocity is trusted
    MAX_SPEED = 3.0  # m/s

    def __init__(self):
        self._tracks = []  # dicts: id, hip, velocity, seen
        self._next_id = 1

    def reset(self):
        self._tracks = []

    def _expected(self, track, time):
        return track["hip"] + track["velocity"] * min(time - track["seen"], self.PREDICT_SECONDS)

    def update(self, hips, time):
        """hips: one (3,) position per pose. Returns each pose's ID, in the same order."""
        hips = [np.asarray(h, dtype=np.float64) for h in hips]
        self._tracks = [t for t in self._tracks if time - t["seen"] <= self.KEEP_SECONDS]
        ids = [None] * len(hips)
        expected = [self._expected(track, time) for track in self._tracks]
        pairs = sorted(
            (float(np.linalg.norm(expected[t] - hip)), t, p)
            for t in range(len(self._tracks)) for p, hip in enumerate(hips))
        used_tracks = set()
        for distance, t, p in pairs:  # closest pairs first
            if distance > self.MAX_STEP or t in used_tracks or ids[p] is not None:
                continue
            used_tracks.add(t)
            track = self._tracks[t]
            ids[p] = track["id"]
            dt = time - track["seen"]
            if dt > 0:
                velocity = (hips[p] - track["hip"]) / dt
                speed = np.linalg.norm(velocity)
                if speed > self.MAX_SPEED:
                    velocity *= self.MAX_SPEED / speed
                track["velocity"] = 0.5 * track["velocity"] + 0.5 * velocity
            track["hip"] = hips[p]
            track["seen"] = time
        for p, hip in enumerate(hips):
            if ids[p] is None:
                ids[p] = self._next_id
                self._next_id += 1
                self._tracks.append({"id": ids[p], "hip": hip, "velocity": np.zeros(3), "seen": time})
        return ids

    def known(self):
        """The IDs of the people still remembered (seen within KEEP_SECONDS of the last update)."""
        return {t["id"] for t in self._tracks}

    def order(self, ids):
        """Indices that sort these IDs oldest first (the first player stays first)."""
        return sorted(range(len(ids)), key=lambda i: ids[i])
