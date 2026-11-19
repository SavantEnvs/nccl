// NCCL_TOPO_FILE end to end: the same sequence ncclTopoGetSystem() (src/graph/topo.cc) runs on a
// user-supplied topology file -- load the XML, rewrite every <cpu host_hash>, add a <system> node
// when the file had none -- then build the topology (ncclTopoGetSystemFromXml), compute the paths
// and initialise the search, as init.cc does next. comm is NULL: paths.cc supports that (it skips
// the per-peer transport checks that need a live communicator).
#include <stdint.h>
#include <stdlib.h>

#include "core.h"
#include "graph.h"
#include "topo.h"
#include "xml.h"
#include "fuzz_common.h"

static const uint64_t kHostHash = 0x6e63636c66757a7aULL;

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  // There is no NVML on a GPU-less host: tell the P2P check not to ask it (a user-facing param,
  // src/graph/paths.cc), so path computation runs on the XML alone.
  setenv("NCCL_IGNORE_DISABLED_P2P", "2", 1);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const char* filename = fuzzScratchPath(".xml");
  fuzzWriteFile(filename, data, size);

  struct ncclXml* xml;
  if (xmlAlloc(&xml, NCCL_TOPO_XML_MAX_NODES) != ncclSuccess) return 0;
  struct ncclTopoSystem* system = NULL;
  struct ncclXmlNode* node;
  if (ncclTopoGetXmlFromFile(filename, xml, 1) != ncclSuccess) goto out;
  if (xmlFindTag(xml, "cpu", &node) != ncclSuccess) goto out;
  while (node != nullptr) {
    if (xmlSetAttrLong(node, "host_hash", kHostHash) != ncclSuccess) goto out;
    if (xmlFindNextTag(xml, "cpu", node, &node) != ncclSuccess) goto out;
  }
  if (xml->maxIndex == 0) {
    struct ncclXmlNode* top;
    if (xmlAddNode(xml, NULL, "system", &top) != ncclSuccess) goto out;
    if (xmlSetAttrInt(top, "version", NCCL_TOPO_XML_VERSION) != ncclSuccess) goto out;
  }
  if (ncclTopoGetSystemFromXml(xml, &system, kHostHash) != ncclSuccess) goto out;
  if (ncclTopoComputePaths(system, NULL) != ncclSuccess) goto out;
  ncclTopoSearchInit(system);
out:
  if (system) ncclTopoFree(system);
  free(xml);
  return 0;
}
