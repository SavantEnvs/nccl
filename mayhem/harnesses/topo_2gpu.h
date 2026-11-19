// A small single-node topology (2 GPUs behind one PCIe switch, NVLink between them, one NIC),
// in the format ncclTopoDumpXmlToFile() writes. The graph harness builds its fixed system from it.
#pragma once
static const char kTopo2Gpu[] =
    "<system version=\"1\">\n"
    "  <cpu numaid=\"0\" affinity=\"ffffffff\" arch=\"x86_64\" vendor=\"GenuineIntel\" familyid=\"6\" modelid=\"85\">\n"
    "    <pci busid=\"0000:17:00.0\" class=\"0x060400\" vendor=\"0x10b5\" device=\"0x8747\" subsystem_vendor=\"0x10b5\" subsystem_device=\"0x8747\" link_speed=\"16.0 GT/s PCIe\" link_width=\"16\">\n"
    "      <pci busid=\"0000:18:00.0\" class=\"0x030200\" vendor=\"0x10de\" device=\"0x20b0\" subsystem_vendor=\"0x10de\" subsystem_device=\"0x134f\" link_speed=\"16.0 GT/s PCIe\" link_width=\"16\">\n"
    "        <gpu dev=\"0\" sm=\"80\" rank=\"0\" gdr=\"1\">\n"
    "          <nvlink target=\"0000:19:00.0\" count=\"12\" tclass=\"0x030200\"/>\n"
    "        </gpu>\n"
    "      </pci>\n"
    "      <pci busid=\"0000:19:00.0\" class=\"0x030200\" vendor=\"0x10de\" device=\"0x20b0\" subsystem_vendor=\"0x10de\" subsystem_device=\"0x134f\" link_speed=\"16.0 GT/s PCIe\" link_width=\"16\">\n"
    "        <gpu dev=\"1\" sm=\"80\" rank=\"1\" gdr=\"1\">\n"
    "          <nvlink target=\"0000:18:00.0\" count=\"12\" tclass=\"0x030200\"/>\n"
    "        </gpu>\n"
    "      </pci>\n"
    "      <pci busid=\"0000:1a:00.0\" class=\"0x020000\" vendor=\"0x15b3\" device=\"0x101b\" subsystem_vendor=\"0x15b3\" subsystem_device=\"0x0007\" link_speed=\"16.0 GT/s PCIe\" link_width=\"16\">\n"
    "        <nic>\n"
    "          <net name=\"mlx5_0\" dev=\"0\" speed=\"200000\" port=\"1\" latency=\"0.000000\" guid=\"0x1\" maxconn=\"131072\" gdr=\"1\" rail=\"0\" plane=\"0\"/>\n"
    "        </nic>\n"
    "      </pci>\n"
    "    </pci>\n"
    "  </cpu>\n"
    "</system>\n";
