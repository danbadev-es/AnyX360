// Adding and removing games: long jobs run on a worker thread, reporting to the UI through an
// observer. A game is added from an archive (extracted into games\<id>\game) or from the folder where
// the player's copy already is (remembered and used in place); a Games on Demand copy is always
// written out to games\<id>\game. Which game it is comes from default.xex's title ID, matched
// against the catalog, and the copy must be complete.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/paths.h"
#include "core/strings.h"

struct CatalogGame;

namespace anyx360::launcher {

// What a job ends with.
struct Outcome {
  Message message;      // none: cancelled, or nothing to say
  std::wstring select;  // the game to select afterwards
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
  // The last call: the job has finished (the worker can be joined).
  virtual void OnDone(const Outcome& outcome) = 0;
};

class Job {
 public:
  explicit Job(JobObserver& observer) : observer_(observer) {}

  // From any thread: stops the running 7-Zip and the extraction loops; the job then cleans up.
  void Cancel();
  bool Cancelled() const { return cancel_; }

  void Report(Step step, int percent = -1) { observer_.OnProgress(step, percent); }
  // Extracting at `percent`, finishing at 100.
  void Progress(int percent) { Report(percent >= 100 ? Step::kFinishing : Step::kExtracting, percent); }
  void Found(const CatalogGame& game) { observer_.OnGame(game); }
  // True if the game isn't in the library yet or the player wants it replaced.
  bool ConfirmReplace(const CatalogGame& game);
  // The running extractor (a process handle, or nullptr when it has finished): Cancel ends it.
  void SetProcess(void* process);

 private:
  JobObserver& observer_;
  std::atomic<bool> cancel_{false};
  std::mutex mutex_;
  void* process_ = nullptr;
};

// Identifies the game in the archive (only default.xex is extracted for that), extracts it into
// games\.partial and moves the folder holding default.xex to games\<id>\game. A Games on Demand
// archive: only that title's folders are extracted, then the package is converted.
Outcome AddArchive(Job& job, const fs::path& archive);
// Identifies the copy in the player's folder and remembers it; it is played where it is. A Games
// on Demand copy is converted instead (written out to games\<id>\game).
Outcome AddFolder(Job& job, const fs::path& folder);
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
