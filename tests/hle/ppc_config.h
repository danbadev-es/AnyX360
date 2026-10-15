// Stand-in for a game's generated ppc_config.h, used by the HLE tests. No optimisation flags are
// set, so PPCContext has every register (the most conservative layout).
#pragma once

#ifndef PPC_CONFIG_H_INCLUDED
#define PPC_CONFIG_H_INCLUDED

#define PPC_IMAGE_BASE 0x82000000ull
#define PPC_IMAGE_SIZE 0x01000000ull
#define PPC_CODE_BASE 0x82010000ull
#define PPC_CODE_SIZE 0x00800000ull

#endif
