// The example tuner plugin's config file (plugins/tuner/example/plugin.c, NCCL_TUNER_CONFIG_FILE):
// a CSV of collective,min,max,algo,proto,channels,nNodes,nRanks[,numPipeOps,regBuff] lines.
// The harness drives the plugin through its exported ncclTunerPlugin_v6 table the way NCCL core
// does (src/tuning/tuning.cc): init() loads the file, getCollInfo() is asked about every collective
// at a few sizes with a contiguous cost table cast to float**, finalize() frees the context.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tuner.h"
#include "fuzz_common.h"

extern const ncclTuner_v6_t ncclTunerPlugin_v6;

static void quietLogger(ncclDebugLogLevel level, unsigned long flags, const char* file, int line, const char* fmt, ...) {
  (void)level; (void)flags; (void)file; (void)line; (void)fmt;
}

static const char* gConfig;

int LLVMFuzzerInitialize(int* argc, char*** argv) {
  (void)argc; (void)argv;
  gConfig = fuzzScratchPath(".conf");
  setenv("NCCL_TUNER_CONFIG_FILE", gConfig, 1);
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzWriteFile(gConfig, data, size);
  void* ctx = NULL;
  if (ncclTunerPlugin_v6.init(&ctx, 0, 8, 2, quietLogger, NULL, NULL) != ncclSuccess || ctx == NULL) return 0;
  static const size_t sizes[] = {0, 4096, 65536, 1048576, (size_t)1 << 32};
  for (int coll = ncclFuncBroadcast; coll <= ncclFuncAllReduce; coll++) {
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
      float table[NCCL_NUM_ALGORITHMS][NCCL_NUM_PROTOCOLS];
      for (int a = 0; a < NCCL_NUM_ALGORITHMS; a++)
        for (int p = 0; p < NCCL_NUM_PROTOCOLS; p++) table[a][p] = 1.0f;
      int nChannels = 0;
      ncclTunerPlugin_v6.getCollInfo(ctx, (ncclFunc_t)coll, sizes[s], 1, (float**)table, NCCL_NUM_ALGORITHMS,
                                     NCCL_NUM_PROTOCOLS, (int)(s & 1), &nChannels);
    }
  }
  ncclTunerPlugin_v6.finalize(ctx);
  return 0;
}
