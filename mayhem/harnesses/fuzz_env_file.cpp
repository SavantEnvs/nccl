// NCCL's config-file loader (src/misc/param.cc: setEnvFile). At first parameter read NCCL parses
// $NCCL_CONF_FILE (or ~/.nccl.conf) and /etc/nccl.conf line by line as `KEY=VALUE` into fixed
// 4096/1024-byte buffers and exports each pair with ncclOsSetEnv (src/os/linux.cc:
// setenv(name, value, 0)). The input is the file: it is written to a scratch file and passed to
// setEnvFile, the same call initEnvFunc() makes for each config path.
//
// State: setEnvFile's only side effect is the process environment. glibc's setenv() never frees
// the "NAME=VALUE" strings it allocates, so a fuzzer feeding it unique pairs grows RSS without
// bound (≈380 MB per 100k execs measured) and would end every long run in an out-of-memory
// report against the harness. The harness therefore mocks the environment (a libc mock, not a
// change to NCCL's code): setenv below keeps glibc's contract (EINVAL for a NULL/empty name or
// one containing '=', no overwrite when asked not to, the real environment counting as set) but
// stores into a map that is cleared after every input, so each input starts from the same
// environment and nothing leaks into the next one.
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>

#include "param.h"
#include "fuzz_common.h"

static std::map<std::string, std::string>* gSet;

extern "C" int setenv(const char* name, const char* value, int overwrite) {
  if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL) {
    errno = EINVAL;
    return -1;
  }
  if (gSet == NULL) gSet = new std::map<std::string, std::string>();
  if (!overwrite && (gSet->count(name) || getenv(name) != NULL)) return 0;
  (*gSet)[name] = value ? value : "";
  return 0;
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  fuzzScratchPath(".conf");
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const char* path = fuzzScratchPath(".conf");
  fuzzWriteFile(path, data, size);
  setEnvFile(path);
  if (gSet) gSet->clear();
  return 0;
}
