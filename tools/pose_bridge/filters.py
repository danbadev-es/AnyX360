"""The One-Euro filter (Casiez, Roussel and Vogel, CHI 2012): little smoothing when a joint moves
fast (low latency), more when it holds still (no jitter)."""

import math

import numpy as np


def _alpha(cutoff, dt):
    tau = 1.0 / (2 * math.pi * cutoff)
    return 1.0 / (1.0 + tau / dt)


class OneEuro:
    """Filters arrays of a fixed shape (e.g. 20 joints x 3).

    min_cutoff may be an array (broadcast to the values: a cutoff per joint and axis). With
    axis_weights (one per column), the speed that opens the filter is each row's (a joint's) speed,
    its axes weighted, rather than each value's: a joint moving fast along one axis isn't held back
    along the others, and a noisy axis can count for less. lead (seconds) predicts that far along
    the filtered speed, faded in between PREDICT_FROM and PREDICT_FULL m/s so a joint at rest isn't
    pushed around by its noise.
    """

    PREDICT_FROM, PREDICT_FULL = 0.25, 0.75

    def __init__(self, min_cutoff=1.0, beta=0.5, derivative_cutoff=1.0, axis_weights=None, lead=0.0):
        self.min_cutoff = min_cutoff
        self.beta = beta
        self.derivative_cutoff = derivative_cutoff
        self.axis_weights = None if axis_weights is None else np.asarray(axis_weights, dtype=np.float64)
        self.lead = lead
        self.speed = None  # the last frame's filtered speeds (per row with axis_weights)
        self._value = None
        self._derivative = None
        self._time = None

    def reset(self):
        self._value = self._derivative = self._time = None

    def _speed(self):
        if self.axis_weights is not None:
            return np.linalg.norm(self._derivative * self.axis_weights, axis=-1, keepdims=True)
        return np.abs(self._derivative)

    def __call__(self, value, time):
        value = np.asarray(value, dtype=np.float64)
        if self._value is None or time <= self._time:
            self._value = value.copy()
            self._derivative = np.zeros_like(value)
            self._time = time
            self.speed = self._speed()
            return self._value.copy()
        dt = time - self._time
        self._time = time
        derivative = (value - self._value) / dt
        a_d = _alpha(self.derivative_cutoff, dt)
        self._derivative = a_d * derivative + (1 - a_d) * self._derivative
        speed = self.speed = self._speed()
        cutoff = self.min_cutoff + self.beta * speed
        tau = 1.0 / (2 * math.pi * cutoff)
        a = 1.0 / (1.0 + tau / dt)
        self._value = a * value + (1 - a) * self._value
        if not self.lead:
            return self._value.copy()
        fade = np.clip((speed - self.PREDICT_FROM) / (self.PREDICT_FULL - self.PREDICT_FROM), 0.0, 1.0)
        return self._value + self.lead * fade * self._derivative
