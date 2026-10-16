"""Summarises an ANYX360_FRAMETIMES CSV (time_ms,event,payload).

    python tools/scripts/frametimes.py frametimes.csv [--from SECONDS] [--to SECONDS]

Prints, per event kind, the rate and the interval distribution, plus vblank lateness and the share
of presents done by the UI thread. Presents with payload 0 are repaints of an old frame.

With host_vblank events (the display's refreshes), also estimates how many refreshes each new frame
stayed on screen, assuming a frame is shown from the first refresh after its present: a steady
60 FPS on a 120 Hz screen is all 2s, and 1s/3s are judder.
"""
import argparse
import csv
import statistics


def percentile(values, p):
    if not values:
        return 0.0
    values = sorted(values)
    return values[min(len(values) - 1, int(p * (len(values) - 1) + 0.5))]


def describe(name, times):
    intervals = [b - a for a, b in zip(times, times[1:])]
    if not intervals:
        print(f'{name:12} {len(times)} events')
        return
    span = (times[-1] - times[0]) / 1000.0
    print(f'{name:12} {len(times) / span if span else 0:7.2f}/s  mean {statistics.fmean(intervals):6.2f}  '
          f'sd {statistics.pstdev(intervals):5.2f}  p50 {percentile(intervals, .5):6.2f}  '
          f'p99 {percentile(intervals, .99):6.2f}  max {max(intervals):6.1f} ms  '
          f'>20ms {sum(i > 20 for i in intervals)}  >34ms {sum(i > 34 for i in intervals)}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('csv')
    parser.add_argument('--from', dest='start', type=float, default=0.0)
    parser.add_argument('--to', dest='end', type=float, default=float('inf'))
    args = parser.parse_args()

    events = {}
    late = []
    repaints = 0
    with open(args.csv, newline='') as f:
        for row in csv.DictReader(f):
            t = float(row['time_ms'])
            if not args.start * 1000 <= t <= args.end * 1000:
                continue
            kind, payload = row['event'], int(row['payload'])
            if kind == 'vblank':
                late.append(payload)
            if kind.startswith('present'):
                if payload == 0:
                    repaints += 1
                    continue
                events.setdefault('present', []).append(t)
            events.setdefault(kind, []).append(t)

    for kind in ('vblank', 'host_vblank', 'vdswap', 'gpuswap', 'present', 'present_gpu', 'present_ui'):
        if kind in events:
            describe(kind, events[kind])
    if late:
        print(f'vblank late  p50 {percentile(late, .5)} us  p99 {percentile(late, .99)} us  max {max(late)} us')
    print(f'repaints     {repaints}')

    host = events.get('host_vblank', [])
    presents = events.get('present', [])
    if len(host) > 2 and len(presents) > 2:
        import bisect
        slots = []
        for t in presents:
            i = bisect.bisect_right(host, t)
            if i < len(host):
                slots.append(i)
        held = {}
        for a, b in zip(slots, slots[1:]):
            held[b - a] = held.get(b - a, 0) + 1
        total = sum(held.values())
        period = statistics.median([b - a for a, b in zip(host, host[1:])])
        print(f'display      {1000 / period:.2f} Hz; refreshes each frame stayed on screen: ' +
              ', '.join(f'{k}: {v} ({100 * v / total:.1f}%)' for k, v in sorted(held.items())))
        # Where in the refresh interval presents land (0 = right after a refresh, 1 = just before).
        phases = []
        for t in presents:
            i = bisect.bisect_right(host, t)
            if 0 < i < len(host):
                phases.append((t - host[i - 1]) / (host[i] - host[i - 1]))
        if phases:
            print(f'present phase in the refresh: p5 {percentile(phases, .05):.2f} p50 {percentile(phases, .5):.2f} '
                  f'p95 {percentile(phases, .95):.2f}')


if __name__ == '__main__':
    main()
