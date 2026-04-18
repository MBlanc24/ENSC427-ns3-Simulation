#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PROJECT");

int main(int argc, char *argv[])
{
    /* ==========================================================
       PROCESS 1: SIMULATION PARAMETERIZATION
       Define and parse command-line arguments to allow for dynamic
       testing of different TCP variants, packet loss rates, and
       one-way propagation delays without modifying the source code.
       ========================================================== */
    std::string tcpType = "cubic";
    double loss = 0.0;
    std::string satDelay = "25ms";

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpType", "TCP variant: cubic or bbr", tcpType);
    cmd.AddValue("loss", "Packet loss rate", loss);
    cmd.AddValue("satDelay", "Satellite one-way delay", satDelay);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

    /* ==========================================================
       PROCESS 2: PROTOCOL STACK SELECTION
       Configure the global TCP socket factory based on the requested
       variant. This script supports side-by-side performance
       benchmarking for TCP CUBIC and TCP BBR.
       ========================================================== */
    if (tcpType == "cubic")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           TypeIdValue(TcpCubic::GetTypeId()));
    }
    else if (tcpType == "bbr")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           TypeIdValue(TcpBbr::GetTypeId()));
    }
    else
    {
        NS_FATAL_ERROR("Invalid tcpType. Use --tcpType=cubic or --tcpType=bbr");
    }

    NS_LOG_INFO("Creating Topology");

    /* ==========================================================
       PROCESS 3: NETWORK TOPOLOGY GENERATION
       Construct a 4-node network consisting of a source, receiver,
       and two intermediate routers. High-speed ground links connect
       the hosts to the routers, while a 100Mbps satellite link
       acts as the core bottleneck with a variable delay.
       ========================================================== */
    NodeContainer nodes;
    nodes.Create(4);

    NodeContainer senderRouter(nodes.Get(0), nodes.Get(1));
    NodeContainer routerRouter(nodes.Get(1), nodes.Get(2));
    NodeContainer routerReceiver(nodes.Get(2), nodes.Get(3));

    PointToPointHelper gndLink;
    gndLink.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    gndLink.SetChannelAttribute("Delay", StringValue("2ms"));

    PointToPointHelper satLink;
    satLink.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    satLink.SetChannelAttribute("Delay", StringValue(satDelay));

    NetDeviceContainer d1 = gndLink.Install(senderRouter);
    NetDeviceContainer d2 = satLink.Install(routerRouter);
    NetDeviceContainer d3 = gndLink.Install(routerReceiver);

    /* ==========================================================
       PROCESS 4: ADDRESS ASSIGNMENT & ROUTING
       Install the Internet stack and assign IPv4 addresses across
       three distinct subnets. Global routing tables are populated
       to enable end-to-end communication.
       ========================================================== */
    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer i1 = address.Assign(d1);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer i2 = address.Assign(d2);

    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer i3 = address.Assign(d3);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    /* ==========================================================
       PROCESS 5: TRAFFIC FLOW & APPLICATION SETUP
       Deploy a BulkSendApplication to provide an continuous source of
       data, saturating the bottleneck. A PacketSink is utilized
       on the receiver to track the total received data volume.
       ========================================================== */
    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), 8080));

    ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(60.0));

    BulkSendHelper source("ns3::TcpSocketFactory",
                          InetSocketAddress(i3.GetAddress(1), 8080));

    source.SetAttribute("MaxBytes", UintegerValue(0));

    ApplicationContainer sourceApp = source.Install(nodes.Get(0));
    sourceApp.Start(Seconds(1.0));
    sourceApp.Stop(Seconds(60.0));

    /* ==========================================================
       PROCESS 6: ATMOSPHERIC FADING (ERROR MODEL)
       A RateErrorModel is integrated on the satellite link to
       simulate non-congestion-related packet loss based on the
       dynamic command-line input.
       ========================================================== */
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
    em->SetAttribute("ErrorRate", DoubleValue(loss));

    d2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    /* ==========================================================
       PROCESS 7: EXECUTION & THROUGHPUT ANALYSIS
       Execute the simulation and perform post-run calculations.
       Throughput is calculated by dividing total bits received
       by the actual active transmission duration (59.0s).
       ========================================================== */
    Simulator::Run();

    Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink>(sinkApp.Get(0));
    uint64_t totalBytes = sinkPtr->GetTotalRx();
    double throughputMbps = (totalBytes * 8.0) / (59.0 * 1000000.0);

    std::cout << "TCP Type: " << tcpType << std::endl;
    std::cout << "Total Bytes Received: " << totalBytes << std::endl;
    std::cout << "Average Throughput: " << throughputMbps << " Mbps" << std::endl;

    Simulator::Destroy();
    return 0;
}