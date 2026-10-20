// Adding and removing games: long jobs run on a worker thread, reporting to the UI through an
// observer. A game is added from whatever the player has, recognised by content: an archive in any
// format 7-Zip opens (extracted here, with what it holds recognised in turn: a disc image, a
// package, another archive), a disc image, a Games on Demand or Xbox Live Arcade package (written
// out into files), or the folder where the disc's files already are (remembered and used in
// place). Which game it is comes from default.xex's title ID (or a package's), matched against the
// catalog, and the copy must be complete.
#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/paths.h"
#include "core/strings.h"

struct CatalogGame;

namespace anyx360::launcher {

// What a job ends with.
struct Outcome {
  Message message;      // none: cancelled, or nothing to say
  std::wstring select;  // the game to select afterwards
  std::vector<Message> notes;  // said after the message (what was left out)
};

enum class Step {
  kChecking,    // finding out what the copy is
  kExtracting,  // with a percent
  kFinishing,   // moving the copy into place, reading its icon and content
  kRemoving,
};

// What a job reports. Called on the job's worker thread.
class JobObserver {
 public:
  virtual ~JobObserver() = default;
  // `percent` is -1 when it isn't known.
  virtual void OnProgress(Step step, int percent) = 0;
  // The game the job turned out to be.
  virtual void OnGame(const CatalogGame& game) = 0;
  // Whether `game`, already in the library, is replaced. Blocks the worker until the player answers.
  virtual bool AskReplace(const CatalogGame& game) = 0;
  // The password of `archive` (`wrong`: the last one didn't open it), or none if the player gave up.
  // Blocks the worker until the player answers.
  virtual std::optional<std::wstring> AskPassword(const fs::path& archive, bool wrong) = 0;
  // The last call: the job has finished (the worker can be joined).
  virtual void OnDone(const Outcome& outcome) = 0;
};

class Job {
 public:
  explicit Job(JobObserver& observer) : observer_(observer) {}

  // From any thread: stops the running 7-Zip and the extraction loops; the job then cleans up.
  void Cancel();
  bool Cancelled() const { return cancel_; }

  // The percent never goes back within a job (an extraction retried with another password, a step
  // that can't count): the bar fills once.
  void Report(Step step, int percent = -1) {
    percent_ = std::max(percent_, percent);
    observer_.OnProgress(step, percent_);
  }
  // Extracting at `percent`, finishing at 100.
  void Progress(int percent) { Report(percent >= 100 ? Step::kFinishing : Step::kExtracting, percent); }
  void Found(const CatalogGame& game) { observer_.OnGame(game); }
  // True if the game isn't in the library yet or the player wants it replaced (asked once a job).
  bool ConfirmReplace(const CatalogGame& game);
  // The archives' password, once the player has given one.
  const std::optional<std::wstring>& Password() const { return password_; }
  // Asks the player; false if they gave up.
  bool AskPassword(const fs::path& archive, bool wrong);
  // The running extractor (a process handle, or nullptr when it has finished): Cancel ends it.
  void SetProcess(void* process);

 private:
  JobObserver& observer_;
  std::atomic<bool> cancel_{false};
  std::mutex mutex_;
  void* process_ = nullptr;
  const CatalogGame* confirmed_ = nullptr;
  std::optional<std::wstring> password_;
  int percent_ = -1;  // the highest reported (Report runs on the job's thread)
};

// Recognises `source` (a file or a folder) and adds the game in it.
Outcome AddGame(Job& job, const fs::path& source);
// Forgets a game. Only games\<id> is deleted: a folder of the player's is never in it.
Outcome RemoveGame(Job& job, const CatalogGame& game);

// Runs one job at a time on its own thread.
class Worker {
 public:
  ~Worker() { Join(); }
  // Starts `work` (after an earlier job, if any, has been joined). The observer hears everything,
  // the outcome last (a cancelled job without a message ends with "Cancelled").
  void Start(JobObserver& observer, std::function<Outcome(Job&)> work);
  void Cancel();
  // Waits for the job's thread (call it after OnDone, or before exiting).
  void Join();

 private:
  std::unique_ptr<Job> job_;
  std::thread thread_;
};

}  // namespace anyx360::launcher
