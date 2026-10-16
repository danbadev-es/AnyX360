#include "host_vblank.h"

#include <atomic>
#include <cmath>
#include <thread>

#include "frame_timing.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace x360::gpu::host_vblank {

namespace {

using Microsoft::WRL::ComPtr;

std::atomic<int64_t> g_time{0};
std::atomic<double> g_period{0};
std::atomic<bool> g_locked{false};

int64_t QpcNow() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

ComPtr<IDXGIOutput> OutputForMonitor(IDXGIFactory1* factory, HMONITOR monitor) {
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT a = 0; SUCCEEDED(factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf())); ++a) {
    ComPtr<IDXGIOutput> output;
    for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf())); ++o) {
      DXGI_OUTPUT_DESC desc;
      if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) return output;
    }
  }
  return nullptr;
}

void ThreadBody(HWND hwnd) {
  SetThreadDescription(GetCurrentThread(), L"Display vblank");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);  // timestamps right after the wait
  ComPtr<IDXGIFactory1> factory;
  ComPtr<IDXGIOutput> output;
  HMONITOR monitor = nullptr;
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  // Phase-locked loop on the measured vblanks: `predicted` is the expected time of the next one.
  double predicted = 0, period = 0;
  int good = 0;
  int64_t next_monitor_check = 0;
  for (;;) {
    int64_t now = QpcNow();
    if (now >= next_monitor_check) {
      next_monitor_check = now + frequency.QuadPart;  // once a second: the window may have moved
      HMONITOR current = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
      if (current != monitor || !output) {
        if (!factory || !factory->IsCurrent()) CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
        output = factory ? OutputForMonitor(factory.Get(), current) : nullptr;
        monitor = current;
        predicted = period = 0;
        good = 0;
        g_locked = false;
        if (output) {
          DXGI_OUTPUT_DESC desc;
          output->GetDesc(&desc);
          KLOG_I("display vblank: tracking %ls", desc.DeviceName);
        }
      }
    }
    if (!output || FAILED(output->WaitForVBlank())) {
      output.Reset();
      g_locked = false;
      Sleep(100);
      continue;
    }
    double t = double(QpcNow());
    timing::Record(timing::Event::kHostVblank);
    if (period == 0) {  // first two vblanks: initial period
      if (predicted != 0) period = t - predicted;
      predicted = t;
      if (period > 0) predicted += period;
      continue;
    }
    // Skip whole periods if vblanks were missed (the thread was late).
    double error = t - predicted;
    double skipped = std::floor(error / period + 0.5);
    if (skipped > 0 && skipped < 8) {
      predicted += skipped * period;
      error = t - predicted;
    }
    if (std::abs(error) > period * 0.25) {  // lost: start again
      predicted = t + period;
      good = 0;
      g_locked = false;
      continue;
    }
    predicted += error * 0.1;
    period += error * 0.01;
    g_time = int64_t(predicted);
    g_period = period;
    if (++good > 120) g_locked = true;
    predicted += period;
  }
}

}  // namespace

void Start(void* hwnd) {
  std::thread(ThreadBody, static_cast<HWND>(hwnd)).detach();
}

bool Estimate(int64_t& time, double& period) {
  if (!g_locked.load()) return false;
  time = g_time.load();
  period = g_period.load();
  return period > 0;
}

}  // namespace x360::gpu::host_vblank
