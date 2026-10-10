#pragma once
// Storage-only PCM placement. 0: independent ordinary allocation;
// 1: 64-byte alignment plus Track*64; 2: alignment-only diagnostic control.
// Historical M21.3 environments retain their exact policy through this alias.
#ifndef P4SDM_PCM_PLACEMENT
#ifdef P4SDM_M213_LAYOUT
#define P4SDM_PCM_PLACEMENT P4SDM_M213_LAYOUT
#else
#define P4SDM_PCM_PLACEMENT 0
#endif
#endif
#if defined(P4SDM_M213_LAYOUT) && P4SDM_PCM_PLACEMENT != P4SDM_M213_LAYOUT
#error Conflicting PCM placement policies
#endif
static_assert(P4SDM_PCM_PLACEMENT >= 0 && P4SDM_PCM_PLACEMENT <= 2,
              "Unsupported PCM placement policy");
