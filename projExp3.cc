#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PROJECT");

/* ==========================================================
   PROCESS 1: TCP VARIANT RESOLUTION HELPER
   This helper function maps terminal string arguments to the
   appropriate ns-3 TCP TypeIds. This is required for multi-flow
   experiments to explicitly define different congestion control
   algorithms for competing nodes.
   ========================================================== */
static TypeId
GetTcpTypeId(const std::string &tcpType)
{
    if (tcpType == "cubic")
    {
        return TcpCubic::GetTypeId();
    }
    else if (tcpType == "bbr")
    {
        return TcpBbr::GetTypeId();
    }
    else
    {
        NS_FATAL_ERROR("Invalid TCP type. Use cubic or bbr");
    }
}

int main(int argc, char *argv[])
{
    /* ==========================================================
       PROCESS 2: SIMULATION PARAMETERIZATION
       Initialize core parameters and parse dynamic command-line
       arguments. This script is uniquely configured to accept two
       separate TCP variants to evaluate cross-protocol fairness
       over a shared satellite link.
       ========================================================== */
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    std::string tcpType1 = "cubic";
    std::string tcpType2 = "bbr";
    std::string queueSize = "100p";
    double loss = 0.0;
    std::string satDelay = "25ms";

    CommandLine cmd(__FILE__);
    cmd.AddValue("loss", "Packet loss rate", loss);
    cmd.AddValue("satDelay", "Satellite one-way delay", satDelay);
    cmd.AddValue("queueSize", "Queue size, e.g. 20p, 50p, 100p", queueSize);
    cmd.AddValue("tcpType1", "TCP variant for sender 1: cubic or bbr", tcpType1);
    cmd.AddValue("tcpType2", "TCP variant for sender 2: cubic or bbr", tcpType2);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

    NS_LOG_INFO("Creating Topology");

    /* ==========================================================
       PROCESS 3: MULTI-FLOW TOPOLOGY CONSTRUCTION
       Construct an extended 5-node dumbbell topology consisting of
       two independent senders, two central routers, and one receiver.
       Both senders merge at the first router and share the 100Mbps
       satellite bottleneck link.
       ========================================================== */
    NodeContainer nodes;
    nodes.Create(5);

    NodeContainer senderRouter1(nodes.Get(0), nodes.Get(2));
    NodeContainer senderRouter2(nodes.Get(1), nodes.Get(2));
    NodeContainer routerRouter(nodes.Get(2), nodes.Get(3));
    NodeContainer routerReceiver(nodes.Get(3), nodes.Get(4));

    PointToPointHelper gndLink;
    gndLink.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    gndLink.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper satLink;
    satLink.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    satLink.SetChannelAttribute("Delay", StringValue(satDelay));
    satLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(queueSize)));

    NetDeviceContainer d1a = gndLink.Install(senderRouter1);
    NetDeviceContainer d1b = gndLink.Install(senderRouter2);
    NetDeviceContainer d2 = satLink.Install(routerRouter);
    NetDeviceContainer d3 = gndLink.Install(routerReceiver);

    /* ==========================================================
       PROCESS 4: PROTOCOL STACK & PER-NODE CUSTOMIZATION
       Install the standard Internet stack, then explicitly override
       the TCP L4 Protocol socket type for Node 0 and Node 1.
       Finally, assign IPv4 addresses and populate global routing.
       ========================================================== */
    InternetStackHelper stack;
    stack.Install(nodes);

    TypeId tid1 = GetTcpTypeId(tcpType1);
    TypeId tid2 = GetTcpTypeId(tcpType2);
    Config::Set("/NodeList/0/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tid1));
    Config::Set("/NodeList/1/$ns3::TcpL4Protocol/SocketType", TypeIdValue(tid2));

    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i1a = address.Assign(d1a);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i1b = address.Assign(d1b);

    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i2 = address.Assign(d2);

    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i3 = address.Assign(d3);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    /* ==========================================================
       PROCESS 5: TRAFFIC GENERATION & SINK DEPLOYMENT
       Deploy two separate PacketSinks on the receiver using distinct
       ports (8080 and 8081). Deploy two BulkSendApplications on the
       senders. Sender 2 is deliberately staggered (1.1s) to observe
       dynamic bandwidth sharing behavior.
       ========================================================== */
    PacketSinkHelper sink1("ns3::TcpSocketFactory",
                           InetSocketAddress(Ipv4Address::GetAny(), 8080));
    ApplicationContainer sinkApp1 = sink1.Install(nodes.Get(4));
    sinkApp1.Start(Seconds(0.0));
    sinkApp1.Stop(Seconds(60.0));

    PacketSinkHelper sink2("ns3::TcpSocketFactory",
                           InetSocketAddress(Ipv4Address::GetAny(), 8081));
    ApplicationContainer sinkApp2 = sink2.Install(nodes.Get(4));
    sinkApp2.Start(Seconds(0.0));
    sinkApp2.Stop(Seconds(60.0));

    BulkSendHelper source1("ns3::TcpSocketFactory",
                           InetSocketAddress(i3.GetAddress(1), 8080));
    source1.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer sourceApp1 = source1.Install(nodes.Get(0));
    sourceApp1.Start(Seconds(1.0));
    sourceApp1.Stop(Seconds(60.0));

    BulkSendHelper source2("ns3::TcpSocketFactory",
                           InetSocketAddress(i3.GetAddress(1), 8081));
    source2.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer sourceApp2 = source2.Install(nodes.Get(1));
    sourceApp2.Start(Seconds(1.1));
    sourceApp2.Stop(Seconds(60.0));

    /* ==========================================================
       PROCESS 6: CHANNEL IMPAIRMENT (ERROR MODELING)
       Attach a RateErrorModel to the receiving interface of the
       satellite link to inject artificial, non-congestive packet
       drops based on user input.
       ========================================================== */
    if (loss > 0.0)
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        em->SetAttribute("ErrorRate", DoubleValue(loss));

        d2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    /* ==========================================================
       PROCESS 7: EXECUTION & FAIRNESS EVALUATION
       Execute the digital timeline. Once complete, extract the
       total bytes received for both distinct flows to compute their
       respective average throughputs. Finally, calculate Jain's
       Fairness Index to quantify network equity.
       ========================================================== */
    Simulator::Run();

    Ptr<PacketSink> sinkPtr1 = DynamicCast<PacketSink>(sinkApp1.Get(0));
    Ptr<PacketSink> sinkPtr2 = DynamicCast<PacketSink>(sinkApp2.Get(0));

    uint64_t totalBytes1 = sinkPtr1->GetTotalRx();
    uint64_t totalBytes2 = sinkPtr2->GetTotalRx();

    double throughput1 = (totalBytes1 * 8.0) / (59.0 * 1000000.0);
    double throughput2 = (totalBytes2 * 8.0) / (59.0 * 1000000.0);

    std::cout << "Flow 1 TCP Type: " << tcpType1 << std::endl;
    std::cout << "Flow 1 Throughput: " << throughput1 << " Mbps" << std::endl
              << std::endl;

    std::cout << "Flow 2 TCP Type: " << tcpType2 << std::endl;
    std::cout << "Flow 2 Throughput: " << throughput2 << " Mbps" << std::endl
              << std::endl;

    double fairness = ((throughput1 + throughput2) * (throughput1 + throughput2)) /
                      (2 * (throughput1 * throughput1 + throughput2 * throughput2));

    std::cout << "Jain Fairness Index: " << fairness << std::endl;

    Simulator::Destroy();
    return 0;
}