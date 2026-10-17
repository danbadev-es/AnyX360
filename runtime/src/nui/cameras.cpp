// The webcams Windows has, by Media Foundation, in the order OpenCV's Media Foundation backend (the
// pose bridge) numbers them. Listing them doesn't open them.
#include <mutex>

#include "x360/kernel.h"
#include "x360/nui.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>

namespace x360::nui {

namespace {

std::string Utf8(const wchar_t* text, uint32_t length) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, int(length), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(std::max(size, 0)), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, int(length), out.data(), size, nullptr, nullptr);
  return out;
}

std::vector<std::string> Enumerate() {
  std::vector<std::string> names;
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMFAttributes* attributes = nullptr;
  if (SUCCEEDED(MFCreateAttributes(&attributes, 1)) &&
      SUCCEEDED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFEnumDeviceSources(attributes, &devices, &count))) {
      for (UINT32 i = 0; i < count; ++i) {
        wchar_t* name = nullptr;
        UINT32 length = 0;
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length))) {
          names.push_back(Utf8(name, length));
          CoTaskMemFree(name);
        } else {
          names.push_back("Camera " + std::to_string(i));
        }
        devices[i]->Release();
      }
      CoTaskMemFree(devices);
    }
  }
  if (attributes) attributes->Release();
  if (SUCCEEDED(com)) CoUninitialize();
  return names;
}

}  // namespace

std::vector<std::string> ListCameras(bool refresh) {
  static std::mutex mutex;
  static std::vector<std::string> cameras;
  static bool listed = false;
  std::lock_guard lock(mutex);
  if (!listed || refresh) {
    cameras = Enumerate();
    listed = true;
    KLOG_I("nui: %zu webcam(s)", cameras.size());
  }
  return cameras;
}

}  // namespace x360::nui
