"""The One-Euro filter (Casiez, Roussel and Vogel, CHI 2012): little smoothing when a joint moves
fast (low latency), more when it holds still (no jitter)."""

import math

import numpy as np


def _alpha(cutoff, dt):
    tau = 1.0 / (2 * math.pi * cutoff)
    return 1.0 / (1.0 + tau / dt)


class OneEuro:
    """Filters arrays of a fixed shape (e.g. 20 joints x 3)."""

    def __init__(self, min_cutoff=1.0, beta=0.5, derivative_cutoff=1.0):
        self.min_cutoff = min_cutoff
        self.beta = beta
        self.derivative_cutoff = derivative_cutoff
        self._value = None
        self._derivative = None
        self._time = None

    @classmethod
    def for_smoothing(cls, smoothing):
        """The [kinect] smoothing setting: 0 raw, 0.5 default, 1 heavy. None for raw."""
        if smoothing <= 0.01:
            return None
        # 0.5 -> 1.5 Hz minimum cutoff; 1 -> 0.4 Hz; small values approach 5 Hz.
        min_cutoff = 5.0 * (0.08 ** smoothing)
        return cls(min_cutoff=min_cutoff, beta=0.6 * (1.0 - smoothing) + 0.1)

    def reset(self):
        self._value = self._derivative = self._time = None

    def __call__(self, value, time):
        value = np.asarray(value, dtype=np.float64)
        if self._value is None or time <= self._time:
            self._value = value.copy()
            self._derivative = np.zeros_like(value)
            self._time = time
            return self._value.copy()
        dt = time - self._time
        self._time = time
        derivative = (value - self._value) / dt
        a_d = _alpha(self.derivative_cutoff, dt)
        self._derivative = a_d * derivative + (1 - a_d) * self._derivative
        cutoff = self.min_cutoff + self.beta * np.abs(self._derivative)
        tau = 1.0 / (2 * math.pi * cutoff)
        a = 1.0 / (1.0 + tau / dt)
        self._value = a * value + (1 - a) * self._value
        return self._value.copy()
