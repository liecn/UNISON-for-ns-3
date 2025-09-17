/*
 * Minimal testbed: 1 client, 1 server, single flow (17 bytes) client->server
 */
#include <iostream>
#include <vector>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/qbb-helper.h"
#include <ns3/switch-node.h>
#include <ns3/rdma-driver.h>
#include <ns3/rdma.h>
#include "kv-lite-client-app.h"
#include "kv-lite-server-app.h"
// Include implementations so they link into this scratch target
#include "kv-lite-client-app.cc"
#include "kv-lite-server-app.cc"

using namespace ns3;
using namespace std;

static Ipv4Address node_id_to_ip(uint32_t id){
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}

static uint32_t ip_to_node_id(Ipv4Address ip){
    return (ip.Get() >> 8) & 0xffff;
}

int main(int argc, char *argv[])
{
    // Hardcode network and device config for this constrained testbed
    std::string linkDataRate = "10Gb/s";
    std::string linkDelay = "1us";
    uint32_t packetPayloadSize = 1500; // B
    bool enablePfc = true;
    bool enableQcn = true;
    double stopTimeSec = 10000;

    // Expose tunables via CLI
    uint32_t argMaxWindows = 16;    // default matches previous hardcoded value
    uint32_t argDataBytes = 102408; // default matches previous hardcoded value
    CommandLine cmd;
    cmd.AddValue("maxWindows", "Number of outstanding request sends per round (client window)", argMaxWindows);
    cmd.AddValue("dataBytes", "Server data response size in bytes (for post-handshake)", argDataBytes);
    cmd.Parse(argc, argv);

    // Apply device defaults
    Config::SetDefault("ns3::QbbNetDevice::PauseTime", UintegerValue(5));
    Config::SetDefault("ns3::QbbNetDevice::QbbEnabled", BooleanValue(enablePfc));
    Config::SetDefault("ns3::QbbNetDevice::QcnEnabled", BooleanValue(enableQcn));

    // Create hosts: server (0), client1 (1), client2 (2), client3 (3)
    NodeContainer hosts;
    hosts.Create(4);

    // Create switches: sw1 (server's TOR), sw2 (client1's TOR), sw3 (client2+client3's TOR), root
    NodeContainer switches;
    {
        Ptr<SwitchNode> sw1 = CreateObject<SwitchNode>();
        Ptr<SwitchNode> sw2 = CreateObject<SwitchNode>();
        Ptr<SwitchNode> sw3 = CreateObject<SwitchNode>();
        Ptr<SwitchNode> root = CreateObject<SwitchNode>();
        switches.Add(sw1);
        switches.Add(sw2);
        switches.Add(sw3);
        switches.Add(root);
    }

    // Install Internet stack on all nodes
    InternetStackHelper internet;
    internet.Install(hosts);
    internet.Install(switches);

    // Link helper
    QbbHelper qbb;
    qbb.SetDeviceAttribute("DataRate", StringValue(linkDataRate));
    qbb.SetChannelAttribute("Delay", StringValue(linkDelay));

    // Helper to track interface indices for RDMA forwarding entries
    auto ifIndexOf = [](Ptr<NetDevice> dev) {
        return DynamicCast<QbbNetDevice>(dev)->GetIfIndex();
    };

    // Addresses for hosts (primary IPs used by apps/RDMA)
    std::vector<Ipv4Address> hostIp(4);
    for (uint32_t i = 0; i < 4; i++) hostIp[i] = node_id_to_ip(i);

    Ipv4AddressHelper ipv4;
    uint32_t linkIdx = 0;
    auto assignSubnet = [&](const NetDeviceContainer &d) {
        char base[16];
        sprintf(base, "10.%u.%u.0", linkIdx / 254 + 1, linkIdx % 254 + 1);
        ipv4.SetBase(base, "255.255.255.0");
        ipv4.Assign(d);
        linkIdx++;
    };

    // Build links per requested topology
    // server (hosts[0]) -> sw1 (switches[0]) -> root (switches[3])
    NetDeviceContainer d_srv_sw1 = qbb.Install(hosts.Get(0), switches.Get(0));
    {
        Ptr<Ipv4> ipv4h = hosts.Get(0)->GetObject<Ipv4>();
        ipv4h->AddInterface(d_srv_sw1.Get(0));
        ipv4h->AddAddress(1, Ipv4InterfaceAddress(hostIp[0], Ipv4Mask(0xff000000)));
    }
    assignSubnet(d_srv_sw1);
    NetDeviceContainer d_sw1_root = qbb.Install(switches.Get(0), switches.Get(3));
    assignSubnet(d_sw1_root);
    // client1 (hosts[1]) -> sw2 (switches[1]) -> root
    NetDeviceContainer d_c1_sw2 = qbb.Install(hosts.Get(1), switches.Get(1));
    {
        Ptr<Ipv4> ipv4h = hosts.Get(1)->GetObject<Ipv4>();
        ipv4h->AddInterface(d_c1_sw2.Get(0));
        ipv4h->AddAddress(1, Ipv4InterfaceAddress(hostIp[1], Ipv4Mask(0xff000000)));
    }
    assignSubnet(d_c1_sw2);
    NetDeviceContainer d_sw2_root = qbb.Install(switches.Get(1), switches.Get(3));
    assignSubnet(d_sw2_root);
    // client2 (hosts[2]) and client3 (hosts[3]) -> sw3 (switches[2]) -> root
    NetDeviceContainer d_c2_sw3 = qbb.Install(hosts.Get(2), switches.Get(2));
    {
        Ptr<Ipv4> ipv4h = hosts.Get(2)->GetObject<Ipv4>();
        ipv4h->AddInterface(d_c2_sw3.Get(0));
        ipv4h->AddAddress(1, Ipv4InterfaceAddress(hostIp[2], Ipv4Mask(0xff000000)));
    }
    assignSubnet(d_c2_sw3);
    NetDeviceContainer d_c3_sw3 = qbb.Install(hosts.Get(3), switches.Get(2));
    {
        Ptr<Ipv4> ipv4h = hosts.Get(3)->GetObject<Ipv4>();
        ipv4h->AddInterface(d_c3_sw3.Get(0));
        ipv4h->AddAddress(1, Ipv4InterfaceAddress(hostIp[3], Ipv4Mask(0xff000000)));
    }
    assignSubnet(d_c3_sw3);
    NetDeviceContainer d_sw3_root = qbb.Install(switches.Get(2), switches.Get(3));
    assignSubnet(d_sw3_root);

    // Primary IPs already assigned above before subnet assignment

    // Install RDMA hardware and drivers on all nodes (hosts + switches)
    NodeContainer allNodes;
    allNodes.Add(hosts);
    allNodes.Add(switches);
    for (uint32_t i = 0; i < allNodes.GetN(); i++){
        Ptr<RdmaHw> rdmaHw = CreateObject<RdmaHw>();
        rdmaHw->SetAttribute("Mtu", UintegerValue(packetPayloadSize));
        rdmaHw->SetAttribute("CcMode", UintegerValue(1)); // DCQCN by default
        rdmaHw->SetAttribute("L2AckInterval", UintegerValue(1));
        rdmaHw->SetAttribute("L2BackToZero", BooleanValue(false));

        Ptr<RdmaDriver> rdma = CreateObject<RdmaDriver>();
        rdma->SetNode(allNodes.Get(i));
        rdma->SetRdmaHw(rdmaHw);
        allNodes.Get(i)->AggregateObject(rdma);
        rdma->Init();
    }

    // RDMA forwarding tables (exact-match) on hosts and switches
    // Get interface indices for each link endpoint
    uint8_t if_srv_to_sw1 = ifIndexOf(d_srv_sw1.Get(0));
    uint8_t if_sw1_to_srv = ifIndexOf(d_srv_sw1.Get(1));
    uint8_t if_sw1_to_root = ifIndexOf(d_sw1_root.Get(0));
    uint8_t if_root_to_sw1 = ifIndexOf(d_sw1_root.Get(1));

    uint8_t if_c1_to_sw2 = ifIndexOf(d_c1_sw2.Get(0));
    uint8_t if_sw2_to_c1 = ifIndexOf(d_c1_sw2.Get(1));
    uint8_t if_sw2_to_root = ifIndexOf(d_sw2_root.Get(0));
    uint8_t if_root_to_sw2 = ifIndexOf(d_sw2_root.Get(1));

    uint8_t if_c2_to_sw3 = ifIndexOf(d_c2_sw3.Get(0));
    uint8_t if_sw3_to_c2 = ifIndexOf(d_c2_sw3.Get(1));
    uint8_t if_c3_to_sw3 = ifIndexOf(d_c3_sw3.Get(0));
    uint8_t if_sw3_to_c3 = ifIndexOf(d_c3_sw3.Get(1));
    uint8_t if_sw3_to_root = ifIndexOf(d_sw3_root.Get(0));
    uint8_t if_root_to_sw3 = ifIndexOf(d_sw3_root.Get(1));

    // Hosts: send everything off-box via their single interface
    for (uint32_t i = 0; i < 4; i++){
        Ptr<Node> host = hosts.Get(i);
        uint8_t hostIf = (i == 0) ? if_srv_to_sw1 : (i == 1) ? if_c1_to_sw2 : (i == 2) ? if_c2_to_sw3 : if_c3_to_sw3;
        for (uint32_t j = 0; j < 4; j++){
            if (i == j) continue;
            host->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[j], hostIf);
        }
    }

    // Switch1: attached to server and root
    {
        Ptr<Node> sw1 = switches.Get(0);
        // To server
        sw1->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[0], if_sw1_to_srv);
        DynamicCast<SwitchNode>(sw1)->AddTableEntry(hostIp[0], if_sw1_to_srv);
        // To clients -> root
        for (uint32_t j = 1; j < 4; j++){
            sw1->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[j], if_sw1_to_root);
            DynamicCast<SwitchNode>(sw1)->AddTableEntry(hostIp[j], if_sw1_to_root);
        }
    }

    // Switch2: attached to client1 and root
    {
        Ptr<Node> sw2 = switches.Get(1);
        sw2->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[1], if_sw2_to_c1);
        DynamicCast<SwitchNode>(sw2)->AddTableEntry(hostIp[1], if_sw2_to_c1);
        // server, client2, client3 -> root
        for (uint32_t j : {0u, 2u, 3u}){
            sw2->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[j], if_sw2_to_root);
            DynamicCast<SwitchNode>(sw2)->AddTableEntry(hostIp[j], if_sw2_to_root);
        }
    }

    // Switch3: attached to client2, client3 and root
    {
        Ptr<Node> sw3 = switches.Get(2);
        sw3->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[2], if_sw3_to_c2);
        sw3->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[3], if_sw3_to_c3);
        DynamicCast<SwitchNode>(sw3)->AddTableEntry(hostIp[2], if_sw3_to_c2);
        DynamicCast<SwitchNode>(sw3)->AddTableEntry(hostIp[3], if_sw3_to_c3);
        // server, client1 -> root
        for (uint32_t j : {0u, 1u}){
            sw3->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[j], if_sw3_to_root);
            DynamicCast<SwitchNode>(sw3)->AddTableEntry(hostIp[j], if_sw3_to_root);
        }
    }

    // Root: decide by TOR
    {
        Ptr<Node> root = switches.Get(3);
        root->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[0], if_root_to_sw1);
        root->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[1], if_root_to_sw2);
        root->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[2], if_root_to_sw3);
        root->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(hostIp[3], if_root_to_sw3);
        DynamicCast<SwitchNode>(root)->AddTableEntry(hostIp[0], if_root_to_sw1);
        DynamicCast<SwitchNode>(root)->AddTableEntry(hostIp[1], if_root_to_sw2);
        DynamicCast<SwitchNode>(root)->AddTableEntry(hostIp[2], if_root_to_sw3);
        DynamicCast<SwitchNode>(root)->AddTableEntry(hostIp[3], if_root_to_sw3);
    }

    // Install server app on server host (hosts[0])
    {
        Ptr<KvLiteServerApp> srv = CreateObject<KvLiteServerApp>();
        srv->SetAttribute("DataBytes", UintegerValue(argDataBytes));
        hosts.Get(0)->AddApplication(srv);
        srv->SetStartTime(Seconds(0));
        srv->SetStopTime(Seconds(stopTimeSec));
    }

    // Install client apps on all three clients targeting server IP
    auto installClient = [&](Ptr<Node> node){
        Ptr<KvLiteClientApp> cli = CreateObject<KvLiteClientApp>();
        // Only tunable client knob is MaxWindows (WINDOW_SIZE)
        cli->SetAttribute("MaxWindows", UintegerValue(argMaxWindows));
        node->AddApplication(cli);
        cli->SetStartTime(Seconds(0));
        cli->SetStopTime(Seconds(stopTimeSec));
    };
    installClient(hosts.Get(1));
    installClient(hosts.Get(2));
    installClient(hosts.Get(3));

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Simulator::Stop(Seconds(stopTimeSec));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}


