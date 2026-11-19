// NCCL_GRAPH_FILE: ncclTopoCompute() (src/graph/search.cc) loads a user-supplied graph XML through
// ncclTopoGetXmlGraphFromFile() and imports its <graph>/<channel> nodes into the ring/tree graphs
// (ncclTopoGetGraphFromXml). The system the graphs refer to is a fixed 2-GPU system built at startup
// from mayhem/harnesses/topo_2gpu.h; it is only read per input. Graph and XML are fresh per input.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "graph.h"
#include "topo.h"
#include "xml.h"
#include "fuzz_common.h"
#include "topo_2gpu.h"

static const uint64_t kHostHash = 0x6e63636c66757a7aULL;
static struct ncclTopoSystem* gSystem = NULL;
static const char* gGraphFile = NULL;

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  // No NVML on a GPU-less host; see fuzz_topo_system.cpp.
  setenv("NCCL_IGNORE_DISABLED_P2P", "2", 1);
  const char* topoFile = fuzzScratchPath(".topo.xml");
  fuzzWriteFile(topoFile, (const uint8_t*)kTopo2Gpu, strlen(kTopo2Gpu));
  struct ncclXml* xml;
  struct ncclXmlNode* node;
  if (xmlAlloc(&xml, NCCL_TOPO_XML_MAX_NODES) != ncclSuccess ||
      ncclTopoGetXmlFromFile(topoFile, xml, 1) != ncclSuccess || xmlFindTag(xml, "cpu", &node) != ncclSuccess) {
    fprintf(stderr, "fuzz_graph_xml: cannot load the built-in topology\n");
    abort();
  }
  while (node != nullptr) {
    if (xmlSetAttrLong(node, "host_hash", kHostHash) != ncclSuccess ||
        xmlFindNextTag(xml, "cpu", node, &node) != ncclSuccess) abort();
  }
  if (ncclTopoGetSystemFromXml(xml, &gSystem, kHostHash) != ncclSuccess ||
      ncclTopoComputePaths(gSystem, NULL) != ncclSuccess || ncclTopoSearchInit(gSystem) != ncclSuccess) {
    fprintf(stderr, "fuzz_graph_xml: cannot build the built-in topology\n");
    abort();
  }
  free(xml);
  unlink(topoFile);

  // A second scratch path for the graph file (fuzzScratchPath caches one path per process).
  static char graphPath[512];
  snprintf(graphPath, sizeof(graphPath), "%s.graph.xml", topoFile);
  gGraphFile = graphPath;
  return 0;
}

// ncclTopoCompute() allocates the graph XML with NCCL_GRAPH_XML_MAX_NODES (65536) nodes of ~14 KB
// each, ~900 MB per call, which caps a fuzzer at a couple of inputs per second and trips the RSS
// limit. The harness calls the same two functions ncclTopoCompute() runs on NCCL_GRAPH_FILE
// (src/graph/search.cc: ncclTopoGetXmlGraphFromFile, then ncclTopoGetGraphFromXml for the graph
// being computed) with a 2048-node buffer instead. That only lowers the capacity at which the
// loader reports "too many nodes"; every graph a 2-GPU system can use fits well within it.
static const int kGraphXmlNodes = 2048;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzWriteFile(gGraphFile, data, size);
  struct ncclXml* xml;
  if (xmlAlloc(&xml, kGraphXmlNodes) != ncclSuccess) return 0;
  struct ncclTopoGraph* graph = (struct ncclTopoGraph*)calloc(1, sizeof(struct ncclTopoGraph));
  if (graph == NULL) { free(xml); return 0; }
  if (ncclTopoGetXmlGraphFromFile(gGraphFile, xml) == ncclSuccess) {
    // The ring graph (id 0) and the tree graph (id 1), set up as init.cc sets them up.
    graph->id = 0;
    graph->pattern = NCCL_TOPO_PATTERN_RING;
    graph->minChannels = 1;
    graph->maxChannels = MAXCHANNELS / 2;
    int nChannels = 0;
    ncclTopoGetGraphFromXml(xml->nodes, gSystem, graph, &nChannels);
    memset(graph, 0, sizeof(*graph));
    graph->id = 1;
    graph->pattern = NCCL_TOPO_PATTERN_BALANCED_TREE;
    ncclTopoGetGraphFromXml(xml->nodes, gSystem, graph, &nChannels);
  }
  free(graph);
  free(xml);
  return 0;
}
