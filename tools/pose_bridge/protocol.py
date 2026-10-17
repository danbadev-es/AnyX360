"""The pose bridge protocol (runtime/include/x360/nui_bridge.h), the bridge's side.

The game creates the shared memory and the frame event; the bridge opens them, checks the layout,
and publishes each frame into the slot after the newest one under a sequence counter (odd while
writing). run_tests.py checks every offset here against the header's static_asserts.
"""

import ctypes
import mmap
from ctypes import wintypes

import numpy as np

MAGIC = 0x49554E41
VERSION_MAJOR, VERSION_MINOR = 1, 0
SLOT_COUNT, MAX_BODIES, JOINTS = 3, 6, 20
COLOR_WIDTH, COLOR_HEIGHT = 640, 480
DEPTH_WIDTH, DEPTH_HEIGHT = 320, 240

STARTING, RUNNING, NO_CAMERA, CAMERA_DENIED, ERROR, STOPPED = range(6)
BODY_NONE, BODY_POSITION_ONLY, BODY_TRACKED = range(3)
SLOT_HAS_IMAGES = 1
EXIT_INCOMPATIBLE = 3  # the game knows this exit code: don't restart, the versions don't match


class Header(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32),
        ("version_major", ctypes.c_uint16),
        ("version_minor", ctypes.c_uint16),
        ("header_size", ctypes.c_uint32),
        ("slot_size", ctypes.c_uint32),
        ("slot_count", ctypes.c_uint32),
        ("max_bodies", ctypes.c_uint32),
        # Producer.
        ("state", ctypes.c_uint32),
        ("producer_pid", ctypes.c_uint32),
        ("heartbeat_qpc", ctypes.c_int64),
        ("latest_slot", ctypes.c_uint32),
        ("camera_fps", ctypes.c_float),
        ("inference_ms", ctypes.c_float),
        ("camera_width", ctypes.c_uint32),
        ("camera_height", ctypes.c_uint32),
        ("status", ctypes.c_char * 128),
        # Consumer (the game's settings).
        ("control_seq", ctypes.c_uint32),
        ("camera_index", ctypes.c_uint32),
        ("mirror", ctypes.c_uint32),
        ("players", ctypes.c_uint32),
        ("model", ctypes.c_uint32),
        ("fov_deg", ctypes.c_float),
        ("smoothing", ctypes.c_float),
        ("camera_height_m", ctypes.c_float),
        ("calibrate", ctypes.c_uint32),
        ("quit", ctypes.c_uint32),
        ("producer_version_major", ctypes.c_uint16),
        ("producer_version_minor", ctypes.c_uint16),
        ("reserved", ctypes.c_uint8 * (4096 - 232)),
    ]


class Body(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_uint32),
        ("state", ctypes.c_uint32),
        ("position", ctypes.c_float * 4),
        ("joints", (ctypes.c_float * 4) * JOINTS),
        ("joint_state", ctypes.c_uint8 * JOINTS),
        ("quality", ctypes.c_uint32),
    ]


class Slot(ctypes.Structure):
    _fields_ = [
        ("seq", ctypes.c_uint32),
        ("frame_number", ctypes.c_uint32),
        ("capture_qpc", ctypes.c_int64),
        ("publish_qpc", ctypes.c_int64),
        ("flags", ctypes.c_uint32),
        ("body_count", ctypes.c_uint32),
        ("floor_plane", ctypes.c_float * 4),
        ("bodies", Body * MAX_BODIES),
        ("_align", ctypes.c_uint8 * 48),  # color is 64-byte aligned
        ("color", ctypes.c_uint8 * (COLOR_WIDTH * COLOR_HEIGHT * 4)),
        ("depth", ctypes.c_uint16 * (DEPTH_WIDTH * DEPTH_HEIGHT)),
    ]


HEADER_SIZE = ctypes.sizeof(Header)
SLOT_SIZE = ctypes.sizeof(Slot)
MAPPING_SIZE = HEADER_SIZE + SLOT_COUNT * SLOT_SIZE

# The bodies of a slot as a numpy record array (written in one go).
BODY_DTYPE = np.dtype({
    "names": ["id", "state", "position", "joints", "joint_state", "quality"],
    "formats": ["<u4", "<u4", ("<f4", 4), ("<f4", (JOINTS, 4)), ("u1", JOINTS), "<u4"],
    "offsets": [Body.id.offset, Body.state.offset, Body.position.offset, Body.joints.offset,
                Body.joint_state.offset, Body.quality.offset],
    "itemsize": ctypes.sizeof(Body),
})

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_kernel32.OpenEventW.restype = wintypes.HANDLE
_kernel32.OpenEventW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
_kernel32.CreateEventW.restype = wintypes.HANDLE
_kernel32.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
_kernel32.SetEvent.argtypes = [wintypes.HANDLE]
_kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
_kernel32.GetCurrentProcessId.restype = wintypes.DWORD
_EVENT_MODIFY_STATE = 0x0002


def qpc():
    """QueryPerformanceCounter, the clock the game compares timestamps with."""
    value = ctypes.c_int64()
    _kernel32.QueryPerformanceCounter(ctypes.byref(value))
    return value.value


def qpc_frequency():
    value = ctypes.c_int64()
    _kernel32.QueryPerformanceFrequency(ctypes.byref(value))
    return value.value


class Channel:
    """The shared memory and the frame event."""

    def __init__(self, memory, event):
        self._memory = memory
        self._event = event
        self.header = Header.from_buffer(memory, 0)
        self.slots = [Slot.from_buffer(memory, HEADER_SIZE + i * SLOT_SIZE) for i in range(SLOT_COUNT)]
        self._bodies = [np.frombuffer(memory, BODY_DTYPE, MAX_BODIES, HEADER_SIZE + i * SLOT_SIZE + Slot.bodies.offset)
                        for i in range(SLOT_COUNT)]
        self._color = [np.frombuffer(memory, np.uint8, COLOR_WIDTH * COLOR_HEIGHT * 4,
                                     HEADER_SIZE + i * SLOT_SIZE + Slot.color.offset)
                       .reshape(COLOR_HEIGHT, COLOR_WIDTH, 4) for i in range(SLOT_COUNT)]
        self._depth = [np.frombuffer(memory, np.uint16, DEPTH_WIDTH * DEPTH_HEIGHT,
                                     HEADER_SIZE + i * SLOT_SIZE + Slot.depth.offset)
                       .reshape(DEPTH_HEIGHT, DEPTH_WIDTH) for i in range(SLOT_COUNT)]

    @classmethod
    def open(cls, name, event_name):
        """The game's channel. An unknown name gives a zeroed mapping, which check_layout refuses."""
        memory = mmap.mmap(-1, MAPPING_SIZE, tagname=name)
        event = _kernel32.OpenEventW(_EVENT_MODIFY_STATE, False, event_name) if event_name else None
        return cls(memory, event)

    @classmethod
    def create(cls, name, event_name=None):
        """A channel as the game creates it (self-test and unit tests)."""
        memory = mmap.mmap(-1, MAPPING_SIZE, tagname=name)
        event = _kernel32.CreateEventW(None, False, False, event_name) if event_name else None
        h = Header.from_buffer(memory, 0)
        h.magic, h.version_major, h.version_minor = MAGIC, VERSION_MAJOR, VERSION_MINOR
        h.header_size, h.slot_size, h.slot_count, h.max_bodies = HEADER_SIZE, SLOT_SIZE, SLOT_COUNT, MAX_BODIES
        h.latest_slot = SLOT_COUNT
        del h
        return cls(memory, event)

    def close(self):
        if self._event:
            _kernel32.CloseHandle(self._event)
            self._event = None
        # The views must go before the mapping closes.
        self.header = self.slots = self._bodies = self._color = self._depth = None
        self._memory.close()

    def check_layout(self):
        """None if this bridge speaks the game's protocol, else why not."""
        h = self.header
        if h.magic != MAGIC:
            return "no game channel at this name"
        if h.version_major != VERSION_MAJOR:
            return (f"the game speaks protocol {h.version_major}.{h.version_minor}, "
                    f"this bridge {VERSION_MAJOR}.{VERSION_MINOR}")
        if (h.header_size, h.slot_size, h.slot_count, h.max_bodies) != (HEADER_SIZE, SLOT_SIZE, SLOT_COUNT, MAX_BODIES):
            return "the game's shared memory layout differs from this bridge's"
        return None

    def hello(self):
        h = self.header
        h.producer_version_major, h.producer_version_minor = VERSION_MAJOR, VERSION_MINOR
        h.producer_pid = _kernel32.GetCurrentProcessId()

    def set_state(self, state, status=""):
        self.header.status = status.encode("utf-8")[:127]
        self.header.state = state

    def beat(self):
        self.header.heartbeat_qpc = qpc()

    def publish(self, frame_number, capture_qpc, bodies, color=None, depth=None, floor=(0.0, 1.0, 0.0, 0.0)):
        """bodies: dicts with id, state, position (3), joints (20x3), joint_state (20), quality."""
        h = self.header
        index = (h.latest_slot + 1) % SLOT_COUNT
        slot = self.slots[index]
        slot.seq = slot.seq | 1
        slot.frame_number = frame_number
        slot.capture_qpc = capture_qpc
        slot.flags = SLOT_HAS_IMAGES if color is not None and depth is not None else 0
        count = min(len(bodies), MAX_BODIES)
        slot.body_count = count
        for i, value in enumerate(floor):
            slot.floor_plane[i] = value
        records = self._bodies[index]
        records[:] = 0
        for i, body in enumerate(bodies[:count]):
            records["id"][i] = body["id"]
            records["state"][i] = body["state"]
            records["position"][i, :3] = body["position"]
            records["position"][i, 3] = 1.0
            records["joints"][i, :, :3] = body["joints"]
            records["joints"][i, :, 3] = 1.0
            records["joint_state"][i] = body["joint_state"]
            records["quality"][i] = body["quality"]
        if slot.flags:
            np.copyto(self._color[index], color)
            np.copyto(self._depth[index], depth)
        slot.publish_qpc = qpc()
        slot.seq = slot.seq + 1
        h.latest_slot = index
        if self._event:
            _kernel32.SetEvent(self._event)

    def read_latest(self):
        """The newest frame as the game reads it (tests): (frame_number, bodies, color, depth) or None."""
        index = self.header.latest_slot
        if index >= SLOT_COUNT:
            return None
        slot = self.slots[index]
        before = slot.seq
        if before == 0 or before & 1:
            return None
        frame = slot.frame_number
        bodies = self._bodies[index][:slot.body_count].copy()
        color = self._color[index].copy() if slot.flags & SLOT_HAS_IMAGES else None
        depth = self._depth[index].copy() if slot.flags & SLOT_HAS_IMAGES else None
        if slot.seq != before:
            return None
        return frame, bodies, color, depth

    def control(self):
        """The game's settings."""
        h = self.header
        return {
            "seq": h.control_seq, "camera": h.camera_index, "mirror": bool(h.mirror), "players": max(1, min(2, h.players or 2)),
            "model": ("lite", "full", "heavy")[min(h.model, 2)], "fov": h.fov_deg or 60.0,
            "smoothing": min(max(h.smoothing, 0.0), 1.0), "camera_height": h.camera_height_m or 0.8,
            "calibrate": h.calibrate, "quit": bool(h.quit),
        }
