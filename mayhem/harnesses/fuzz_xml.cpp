// Ported from the OSS-Fuzz / mayhemheroes nccl harness (Copyright 2023 Google LLC, Apache-2.0).
// Fuzzes the NCCL topology XML loader (NCCL_TOPO_FILE) through ncclTopoGetXmlFromFile.
// Changes from the original: the xml buffer is allocated the way ncclTopoGetSystem() does it
// (xmlAlloc with NCCL_TOPO_XML_MAX_NODES; the original ncclCalloc(&xml, 1) left maxNodes = 0, so
// the loader refused the first node), and the scratch file lives in $TMPDIR, not a fixed /tmp path.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "graph.h"
#include "topo.h"
#include "xml.h"
#include "fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const char* filename = fuzzScratchPath(".xml");
  fuzzWriteFile(filename, data, size);

  struct ncclXml* xml;
  if (xmlAlloc(&xml, NCCL_TOPO_XML_MAX_NODES) != ncclSuccess) return 0;
  ncclTopoGetXmlFromFile(filename, xml, 1);
  free(xml);
  return 0;
}
