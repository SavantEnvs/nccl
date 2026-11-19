// NCCL's selector strings from the environment: NCCL_ALGO-style algorithm selections
// (ncclAlgParse, src/config/algorithm_parser.cc, over the registry in algorithm_registry.cc) and
// interface/HCA lists such as NCCL_SOCKET_IFNAME / NCCL_IB_HCA ("[^][=]prefix[:port[:rail[:plane]]],...",
// parseStringList + matchIfList, src/misc/utils.cc). The input is the string (NUL-terminated copy).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "utils.h"
#include "config/algorithm_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  char* str = (char*)malloc(size + 1);
  if (str == NULL) return 0;
  memcpy(str, data, size);
  str[size] = '\0';

  uint64_t mask = 0;
  ncclAlgParse(str, &mask);

  // Same handling as the IB/socket device filters: optional '^' (exclude) and '=' (exact match).
  const char* list = str;
  bool searchNot = list[0] == '^';
  if (searchNot) list++;
  bool searchExact = list[0] == '=';
  if (searchExact) list++;
  struct netIf userIfs[32];
  int nUserIfs = parseStringList(list, userIfs, 32);
  static const char* const names[] = {"mlx5_0", "mlx5_1", "eth0", "ib0", "lo", "bond0"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    int id;
    bool m = matchIfList(names[i], (int)(i & 1) + 1, userIfs, nUserIfs, searchExact, &id);
    (void)(m ^ searchNot);
  }
  free(str);
  return 0;
}
