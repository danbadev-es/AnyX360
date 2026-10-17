"""Stable IDs for the people in view: MediaPipe returns its poses in no particular order, the Kinect
keeps a tracking ID while a person stays."""

import numpy as np


class Tracker:
    """Matches each frame's poses to the people seen before by the distance between their hips."""

    MAX_STEP = 0.6     # metres a hip may move between frames and stay the same person
    KEEP_SECONDS = 0.5  # a person missing this long is gone (their ID isn't reused)

    def __init__(self):
        self._tracks = []  # dicts: id, hip, seen
        self._next_id = 1

    def reset(self):
        self._tracks = []

    def update(self, hips, time):
        """hips: one (3,) position per pose. Returns each pose's ID, in the same order."""
        hips = [np.asarray(h, dtype=np.float64) for h in hips]
        self._tracks = [t for t in self._tracks if time - t["seen"] <= self.KEEP_SECONDS]
        ids = [None] * len(hips)
        pairs = sorted(
            (float(np.linalg.norm(track["hip"] - hip)), t, p)
            for t, track in enumerate(self._tracks) for p, hip in enumerate(hips))
        used_tracks = set()
        for distance, t, p in pairs:  # closest pairs first
            if distance > self.MAX_STEP or t in used_tracks or ids[p] is not None:
                continue
            used_tracks.add(t)
            ids[p] = self._tracks[t]["id"]
            self._tracks[t]["hip"] = hips[p]
            self._tracks[t]["seen"] = time
        for p, hip in enumerate(hips):
            if ids[p] is None:
                ids[p] = self._next_id
                self._next_id += 1
                self._tracks.append({"id": ids[p], "hip": hip, "seen": time})
        return ids

    def order(self, ids):
        """Indices that sort these IDs oldest first (the first player stays first)."""
        return sorted(range(len(ids)), key=lambda i: ids[i])
