// Known-answer checks for the libnccl code the fuzz targets reach, run by mayhem/test.sh against a
// libnccl_static.a built with upstream's normal flags. Each check prints PASS:/FAIL: and asserts
// parsed values (node counts, path types, channel contents, selection masks, list fields), so a
// patch that turns a parser into a no-op fails here.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "graph.h"
#include "topo.h"
#include "xml.h"
#include "utils.h"
#include "config/algorithm_parser.h"
#include "topo_2gpu.h"

static int failures = 0;
#define CHECK(name, cond)                                   \
  do {                                                      \
    if (cond) printf("PASS: %s\n", name);                   \
    else { printf("FAIL: %s\n", name); failures++; }        \
  } while (0)

static const uint64_t kHostHash = 0x1234;

static const char* writeTmp(const char* content, char* path, size_t len) {
  const char* dir = getenv("TMPDIR");
  snprintf(path, len, "%s/nccl-ka-XXXXXX", (dir && dir[0]) ? dir : "/tmp");
  int fd = mkstemp(path);
  if (fd < 0) { perror("mkstemp"); exit(2); }
  if (write(fd, content, strlen(content)) != (ssize_t)strlen(content)) { perror("write"); exit(2); }
  close(fd);
  return path;
}

static const char kGraph[] =
    "<graphs version=\"1\">\n"
    "  <graph id=\"0\" pattern=\"4\" crossnic=\"0\" nchannels=\"2\" speedintra=\"20\" speedinter=\"20\" "
    "latencyinter=\"0\" typeintra=\"NVL\" typeinter=\"PIX\" samechannels=\"1\">\n"
    "    <channel><gpu dev=\"0\"/><gpu dev=\"1\"/></channel>\n"
    "    <channel><gpu dev=\"1\"/><gpu dev=\"0\"/></channel>\n"
    "  </graph>\n"
    "</graphs>\n";

int main() {
  setenv("NCCL_IGNORE_DISABLED_P2P", "2", 1);
  char topoPath[512], graphPath[512];

  // Topology XML -> system -> paths
  struct ncclXml* xml;
  struct ncclTopoSystem* system = NULL;
  struct ncclXmlNode* node;
  xmlAlloc(&xml, NCCL_TOPO_XML_MAX_NODES);
  CHECK("topo: load xml", ncclTopoGetXmlFromFile(writeTmp(kTopo2Gpu, topoPath, sizeof(topoPath)), xml, 1) == ncclSuccess);
  CHECK("topo: xml node count", xml->maxIndex == 12);
  int nGpuTags = 0;
  xmlFindTag(xml, "gpu", &node);
  while (node) { nGpuTags++; xmlFindNextTag(xml, "gpu", node, &node); }
  CHECK("topo: two <gpu> tags", nGpuTags == 2);
  xmlFindTag(xml, "net", &node);
  const char* netName = NULL;
  if (node) xmlGetAttrStr(node, "name", &netName);
  CHECK("topo: net name attribute", netName && strcmp(netName, "mlx5_0") == 0);
  xmlFindTag(xml, "cpu", &node);
  while (node) { xmlSetAttrLong(node, "host_hash", kHostHash); xmlFindNextTag(xml, "cpu", node, &node); }
  CHECK("topo: build system", ncclTopoGetSystemFromXml(xml, &system, kHostHash) == ncclSuccess);
  CHECK("topo: 2 GPUs", system && system->nodes[GPU].count == 2);
  CHECK("topo: 1 NIC", system && system->nodes[NET].count == 1);
  CHECK("topo: 1 CPU", system && system->nodes[CPU].count == 1);
  CHECK("topo: compute paths", system && ncclTopoComputePaths(system, NULL) == ncclSuccess);
  CHECK("topo: GPU0->GPU1 is NVLink",
        system && system->nodes[GPU].nodes[0].paths[GPU][1].type == PATH_NVL);
  CHECK("topo: GPU0->NET is a PCIe-switch path",
        system && system->nodes[GPU].nodes[0].paths[NET][0].type == PATH_PIX);
  CHECK("topo: search init", system && ncclTopoSearchInit(system) == ncclSuccess);
  free(xml);
  unlink(topoPath);

  // Graph XML -> ring graph
  struct ncclXml* gxml;
  xmlAlloc(&gxml, 1024);
  CHECK("graph: load xml", ncclTopoGetXmlGraphFromFile(writeTmp(kGraph, graphPath, sizeof(graphPath)), gxml) == ncclSuccess);
  struct ncclTopoGraph* graph = (struct ncclTopoGraph*)calloc(1, sizeof(*graph));
  int nChannels = 0;
  graph->id = 0;
  CHECK("graph: import", system && ncclTopoGetGraphFromXml(gxml->nodes, system, graph, &nChannels) == ncclSuccess);
  CHECK("graph: 2 channels", nChannels == 2 && graph->nChannels == 2);
  CHECK("graph: channel order", graph->intra[0] == 0 && graph->intra[1] == 1 && graph->intra[2] == 1 && graph->intra[3] == 0);
  CHECK("graph: link types", graph->typeIntra == PATH_NVL && graph->typeInter == PATH_PIX);
  CHECK("graph: bandwidth", graph->bwIntra == 20.0f && graph->bwInter == 20.0f);
  memset(graph, 0, sizeof(*graph));
  graph->id = 1;
  nChannels = 0;
  CHECK("graph: other id ignored", ncclTopoGetGraphFromXml(gxml->nodes, system, graph, &nChannels) == ncclSuccess &&
                                       nChannels == 0 && graph->nChannels == 0);
  free(graph);
  free(gxml);
  unlink(graphPath);
  if (system) ncclTopoFree(system);

  // Algorithm selection strings
  uint64_t ring = 0, tree = 0, both = 0, none = 1, notRing = 0;
  CHECK("alg: ring", ncclAlgParse("ring", &ring) == ncclSuccess && ring != 0);
  CHECK("alg: tree", ncclAlgParse("Tree", &tree) == ncclSuccess && tree != 0 && (tree & ring) == 0);
  CHECK("alg: union", ncclAlgParse("ring, tree", &both) == ncclSuccess && both == (ring | tree));
  CHECK("alg: negation", ncclAlgParse("^ring", &notRing) == ncclSuccess && (notRing & ring) == 0 && (notRing & tree) == tree);
  CHECK("alg: empty is automatic", ncclAlgParse("", &none) == ncclSuccess && none == 0);
  uint64_t bad = 0;
  CHECK("alg: unknown tag rejected", ncclAlgParse("bogus", &bad) == ncclInvalidArgument);

  // Interface lists
  struct netIf ifs[4];
  int n = parseStringList("mlx5_0:1,eth:2:3:4,,ib", ifs, 4);
  CHECK("iflist: count", n == 3);
  CHECK("iflist: fields", n == 3 && strcmp(ifs[0].prefix, "mlx5_0") == 0 && ifs[0].port == 1 && ifs[0].rail == -1 &&
                              ifs[1].port == 2 && ifs[1].rail == 3 && ifs[1].plane == 4 && strcmp(ifs[2].prefix, "ib") == 0);
  int id = -2;
  CHECK("iflist: prefix match", matchIfList("eth0", 2, ifs, n, false, &id) && id == 1);
  CHECK("iflist: exact mismatch", !matchIfList("eth0", 2, ifs, n, true, &id) && id == -1);
  CHECK("iflist: port mismatch", !matchIfList("mlx5_0", 2, ifs, n, false, NULL));

  printf("Known-answer results: %d failed\n", failures);
  return failures ? 1 : 0;
}
