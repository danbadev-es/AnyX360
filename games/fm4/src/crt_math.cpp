// Forza Motorsport 4: the C runtime's pow, run natively.
//
// The loading screens recompute a gamma ramp every frame with the CRT's pow (8230F7A0, through
// 8243F330 and 8287AE48): recompiled, those calls took most of the main thread, and the game,
// which loads a race in the time its frames leave over, never got to load the first one.
#include <cmath>

#include "x360/hle.h"

X360_GUEST_ORIGINAL(8230F7A0)

// double pow(double x, double y): x in f1, y in f2, the result in f1.
PPC_FUNC(sub_8230F7A0) {
  (void)base;
  ctx.f1.f64 = std::pow(ctx.f1.f64, ctx.f2.f64);
}
