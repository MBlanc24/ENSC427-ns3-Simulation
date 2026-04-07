#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

//  Network Topology
//
// Sender ---- Router ---- Router ---- Receiver
//                 ↑ satellite link ↑
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PROJECT");

//variables to compute rtt avg
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

int
main(int argc, char* argv[])
{

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

    //TCP CUBIC
    if (tcpType == "cubic") {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
            TypeIdValue(TcpCubic::GetTypeId()));
    }

    //TCP BBR
    else if (tcpType == "bbr") {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
            TypeIdValue(TcpBbr::GetTypeId()));
    }

    else {
        NS_FATAL_ERROR("Invalid tcpType. Use --tcpType=cubic or --tcpType=bbr");
    }

	NS_LOG_INFO("Creating Topology"); //added

    //create nodes
    NodeContainer nodes;
    nodes.Create(4);

    //define specific names for each node container
    NodeContainer senderRouter(nodes.Get(0), nodes.Get(1));
    NodeContainer routerRouter(nodes.Get(1), nodes.Get(2));
    NodeContainer routerReceiver(nodes.Get(2), nodes.Get(3));

    // Ground links - used for sender to router; router to reciever
    PointToPointHelper gndLink;
    gndLink.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
    gndLink.SetChannelAttribute ("Delay", StringValue ("2ms"));

    // Satellite link - used for router to router
    PointToPointHelper satLink;
    satLink.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
    satLink.SetChannelAttribute ("Delay", StringValue ("100ms"));
    //DropTail queue
    satLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize(queueSize)));

    //installing links for the routers
    NetDeviceContainer d1 = gndLink.Install(senderRouter);
    NetDeviceContainer d2 = satLink.Install(routerRouter);
    NetDeviceContainer d3 = gndLink.Install(routerReceiver);

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
    

    //receiver tcp socket
    PacketSinkHelper sink("ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), 8080));

    ApplicationContainer sinkApp = sink.Install(nodes.Get(3));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(60.0));

    //sender tcp socket
    BulkSendHelper source("ns3::TcpSocketFactory",
        InetSocketAddress(i3.GetAddress(1), 8080));

    source.SetAttribute("MaxBytes", UintegerValue(0));

    ApplicationContainer sourceApp = source.Install(nodes.Get(0));
    sourceApp.Start(Seconds(1.0));
    sourceApp.Stop(Seconds(60.0));


    //RateErrorModel simulates wireless loss
    if (loss > 0.0) {
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
    em->SetAttribute("ErrorRate", DoubleValue(loss)); //DoubleValue() defines losses

    //packet loss only on one side of the satellite link receiver
    d2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    

    Simulator::Schedule(Seconds(1.001), &ConnectRttTrace);

    Simulator::Run();

    //plot results for average rtt
    double avgRtt = rttSum / rttCount;
    std::cout << "Average RTT: " << avgRtt << " ms" << std::endl;

    //plot results for throughput v packetloss:
    // Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink>(sinkApp.Get(0));
    // uint64_t totalBytes = sinkPtr->GetTotalRx();
    // double throughputMbps = (totalBytes * 8.0) / (59.0 * 1000000.0);

    // std::cout << "TCP Type: " << tcpType << std::endl;
    // std::cout << "Total Bytes Received: " << totalBytes << std::endl;
    // std::cout << "Average Throughput: " << throughputMbps << " Mbps" << std::endl;

    Simulator::Destroy();
    return 0;
}


