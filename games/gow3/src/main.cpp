// Gears of War 3 host executable.
//
// Phase 2 milestone: link the complete recompiled game against the HLE layer. Referencing the
// function table and the entry point forces every recompiled function and every kernel import to
// resolve. Running the game (loader, threads, GPU...) is Phase 3+.
#include <cstdio>
#include <cstring>

#include "ppc_recomp_shared.h"
#include "x360/hle.h"
#include "x360/memory.h"

int main(int argc, char** argv) {
  size_t functions = 0;
  for (const PPCFuncMapping* m = PPCFuncMappings; m->host != nullptr; ++m) ++functions;

  std::printf("AnyX360 - Gears of War 3\n");
  std::printf("recompiled functions: %zu\n", functions);
  std::printf("image: 0x%llX..0x%llX, code: 0x%llX..0x%llX\n", PPC_IMAGE_BASE, PPC_IMAGE_BASE + PPC_IMAGE_SIZE,
              PPC_CODE_BASE, PPC_CODE_BASE + PPC_CODE_SIZE);

  if (argc > 1 && std::strcmp(argv[1], "--run") == 0) {
    // Not functional yet: the image, imports and function table are not loaded.
    x360::GuestMemory memory;
    x360::hle::set_guest_base(memory.base());
    alignas(64) static PPCContext ctx{};
    _xstart(ctx, memory.base());  // entry point 0x82CE8EB0
  }
  return 0;
}
