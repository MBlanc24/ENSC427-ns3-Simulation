#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PROJECT");

/* ==========================================================
   PROCESS 1: GLOBAL TRACING & METRIC COLLECTION
   Define global variables and callback functions to hook into
   the ns-3 trace source system. This captures dynamic Round-Trip
   Time (RTT) data. A 5-second offset is utilized to filter out
   initial "slow-start" transients, ensuring the calculated
   average reflects the steady-state performance of the network.
   ========================================================== */
double rttSum = 0.0;
int rttCount = 0;

static void
RttTracer(Time oldRtt, Time newRtt)
{
    std::cout << Simulator::Now().GetSeconds()
              << "," << newRtt.GetMilliSeconds() << std::endl;

    if (Simulator::Now().GetSeconds() > 5.0)
    {
        double rttMs = newRtt.GetMilliSeconds();
        rttSum += rttMs;
        rttCount++;
    }
}

static void
ConnectRttTrace()
{
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT",
        MakeCallback(&RttTracer));
}

int main(int argc, char *argv[])
{
    /* ==========================================================
       PROCESS 2: SIMULATION SETUP & PARAMETERIZATION
       Initialize core parameters and parse dynamic command-line
       arguments. High-capacity TCP buffers are set by default to
       accommodate the high Bandwidth-Delay Product (BDP) typical
       of satellite links.
       ========================================================== */
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    std::string tcpType = "cubic";
    std::string queueSize = "100p";
    double loss = 0.0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpType", "TCP variant: cubic or bbr", tcpType);
    cmd.AddValue("loss", "Packet loss rate", loss);
    cmd.AddValue("queueSize", "Queue size, e.g. 20p, 50p, 100p", queueSize);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

    /* ==========================================================
       PROCESS 3: TRANSPORT LAYER PROTOCOL SELECTION
       Dynamically map the user-defined terminal arguments to the
       internal ns-3 TCP socket factories, allowing seamless
       switching between TCP CUBIC and TCP BBR during testing.
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
       PROCESS 4: NETWORK TOPOLOGY & PHYSICAL LAYER EMULATION
       Construct a 4-node dumbbell topology representing a LEO
       satellite relay system. Terrestrial ground links are configured
       with high bandwidth and low delay, while the core bottleneck
       link simulates a high-delay satellite connection with a
       strictly parameterized DropTail queue.
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
    satLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    satLink.SetChannelAttribute("Delay", StringValue("100ms"));
    satLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(queueSize)));

    NetDeviceContainer d1 = gndLink.Install(senderRouter);
    NetDeviceContainer d2 = satLink.Install(routerRouter);
    NetDeviceContainer d3 = gndLink.Install(routerReceiver);

    /* ==========================================================
       PROCESS 5: NETWORK LAYER (IP STACK & ROUTING)
       Deploy the IPv4 protocol stack across all simulation nodes
       and assign distinct network subnets to the physical links.
       Global routing tables are then populated to ensure end-to-end
       packet delivery.
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
       PROCESS 6: APPLICATION LAYER (TRAFFIC GENERATION)
       Install a PacketSink on the destination node to capture
       incoming data. A BulkSendApplication is deployed on the
       source node to generate a continuous, aggressive data stream
       designed to fully saturate the bottleneck link.
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
       PROCESS 7: CHANNEL IMPAIRMENT (ERROR MODELING)
       If a packet loss rate is specified via the command line,
       a RateErrorModel is attached strictly to the receiving
       interface of the satellite link. This simulates environmental
       wireless fading independently of standard queue congestion.
       ========================================================== */
    if (loss > 0.0)
    {
        Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
        em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
        em->SetAttribute("ErrorRate", DoubleValue(loss));

        d2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    /* ==========================================================
       PROCESS 8: SIMULATION EXECUTION & RESULTS ANALYSIS
       Schedule the RTT tracing hook to activate shortly after
       traffic generation begins. The digital timeline is executed,
       and upon completion, the steady-state latency metrics are
       computed and output to the console.
       ========================================================== */
    Simulator::Schedule(Seconds(1.001), &ConnectRttTrace);

    Simulator::Run();

    double avgRtt = rttSum / rttCount;
    std::cout << "Average RTT: " << avgRtt << " ms" << std::endl;

    // Optional: Uncomment for Throughput extraction
    // Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink>(sinkApp.Get(0));
    // uint64_t totalBytes = sinkPtr->GetTotalRx();
    // double throughputMbps = (totalBytes * 8.0) / (59.0 * 1000000.0);
    // std::cout << "TCP Type: " << tcpType << std::endl;
    // std::cout << "Total Bytes Received: " << totalBytes << std::endl;
    // std::cout << "Average Throughput: " << throughputMbps << " Mbps" << std::endl;

    Simulator::Destroy();
    return 0;
}