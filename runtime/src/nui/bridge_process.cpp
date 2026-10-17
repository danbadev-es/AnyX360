// The pose bridge process (bridge.h): found next to the game or in the developer build, started
// without a window in a kill-on-close job object, its output appended to logs\pose_bridge.log.
#include <cstdlib>
#include <system_error>
#include <vector>

#include "bridge.h"
#include "x360/kernel.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace x360::nui {

namespace fs = std::filesystem;

namespace {

fs::path ExeDir() {
  wchar_t path[MAX_PATH * 2];
  DWORD length = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  return fs::path(std::wstring(path, length)).parent_path();
}

std::wstring Quote(const fs::path& path) { return L"\"" + path.wstring() + L"\""; }

std::string LastError() {
  return std::system_category().message(int(GetLastError()));
}

}  // namespace

std::wstring FindBridgeCommand() {
  if (const wchar_t* command = _wgetenv(L"ANYX360_POSE_BRIDGE"); command && *command) return command;
  std::error_code ec;
  const fs::path exe_dir = ExeDir();
  if (fs::path bundled = exe_dir / "pose_bridge" / "pose_bridge.exe"; fs::exists(bundled, ec)) return Quote(bundled);
  const fs::path dev = fs::path("pose_bridge") / "dist" / "pose_bridge" / "pose_bridge.exe";
  for (fs::path dir = exe_dir; !dir.empty(); dir = dir.parent_path()) {
    for (const fs::path& candidate : {dir / dev, dir / "build" / dev})
      if (fs::exists(candidate, ec)) return Quote(candidate);
    if (dir == dir.parent_path()) break;
  }
  return {};
}

fs::path BridgeLogPath() {
  std::error_code ec;
  const fs::path exe_dir = ExeDir();
  fs::path dir = exe_dir.parent_path() / "logs";
  if (exe_dir.filename() != "bin" || !fs::is_directory(dir, ec)) {
    const fs::path& user = kernel::TitlePaths().user_root;
    dir = (user.empty() ? fs::temp_directory_path(ec) : user) / "logs";
  }
  fs::create_directories(dir, ec);
  return dir / "pose_bridge.log";
}

bool BridgeProcess::Start(const std::wstring& command, const std::wstring& arguments, const fs::path& log,
                          std::string& error) {
  Kill();
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE output = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
    error = "cannot open " + log.string() + ": " + LastError();
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    return false;
  }

  // Only the log and NUL are inherited, not the game's other handles.
  HANDLE inherited[] = {input, output};
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<uint8_t> attributes(size);
  auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  InitializeProcThreadAttributeList(list, 1, 0, &size);
  UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr,
                            nullptr);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input;
  startup.StartupInfo.hStdOutput = output;
  startup.StartupInfo.hStdError = output;
  startup.lpAttributeList = list;

  job_ = CreateJobObjectW(nullptr, nullptr);
  if (job_) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
  }
  std::wstring command_line = command + L" " + arguments;
  PROCESS_INFORMATION info{};
  const BOOL created = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                                      CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                      nullptr, &startup.StartupInfo, &info);
  if (!created) error = "cannot start the pose bridge: " + LastError();
  DeleteProcThreadAttributeList(list);
  CloseHandle(output);
  CloseHandle(input);
  if (!created) {
    Kill();
    return false;
  }
  if (job_ && !AssignProcessToJobObject(job_, info.hProcess)) KLOG_W("nui: pose bridge not in a job: %s", LastError().c_str());
  ResumeThread(info.hThread);
  CloseHandle(info.hThread);
  process_ = info.hProcess;
  return true;
}

bool BridgeProcess::Running() const {
  return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

uint32_t BridgeProcess::ExitCode() const {
  DWORD code = 0;
  if (process_) GetExitCodeProcess(process_, &code);
  return code;
}

void BridgeProcess::Stop(uint32_t timeout_ms) {
  if (process_) WaitForSingleObject(process_, timeout_ms);
  Kill();
}

void BridgeProcess::Kill() {
  if (job_) {
    TerminateJobObject(job_, 1);
    CloseHandle(job_);
    job_ = nullptr;
  }
  if (process_) {
    if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) TerminateProcess(process_, 1);
    CloseHandle(process_);
    process_ = nullptr;
  }
}

}  // namespace x360::nui
