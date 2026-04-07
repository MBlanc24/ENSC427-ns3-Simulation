#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

//  Network Topology
// Sender1 ----\
//              Router ---- Router ---- Receiver
// Sender2 ----/
//

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PROJECT");

//variables to compute rtt avg
double rttSum = 0.0;
int rttCount = 0;

//only use following function when doing cubic vs bbr
static TypeId
GetTcpTypeId(const std::string& tcpType)
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

int
main(int argc, char* argv[])
{

    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(1 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(1 << 20));

    //used for comparing cubic vs bbr
    std::string tcpType1 = "cubic";
    std::string tcpType2 = "bbr";

    //std::string tcpType = "cubic";
    std::string queueSize = "100p";
    double loss = 0.0;

    CommandLine cmd(__FILE__);
    //cmd.AddValue("tcpType", "TCP variant: cubic or bbr", tcpType);
    cmd.AddValue("loss", "Packet loss rate", loss);
    cmd.AddValue("queueSize", "Queue size, e.g. 20p, 50p, 100p", queueSize);

    cmd.AddValue("tcpType1", "TCP variant for sender 1: cubic or bbr", tcpType1);
    cmd.AddValue("tcpType2", "TCP variant for sender 2: cubic or bbr", tcpType2);

    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

    //remove if/else when doing cubic vs bbr
    //TCP CUBIC
    // if (tcpType == "cubic") {
    //     Config::SetDefault("ns3::TcpL4Protocol::SocketType",
    //         TypeIdValue(TcpCubic::GetTypeId()));
    // }

    // //TCP BBR
    // else if (tcpType == "bbr") {
    //     Config::SetDefault("ns3::TcpL4Protocol::SocketType",
    //         TypeIdValue(TcpBbr::GetTypeId()));
    // }

    // else {
    //     NS_FATAL_ERROR("Invalid tcpType. Use --tcpType=cubic or --tcpType=bbr");
    // }

	NS_LOG_INFO("Creating Topology"); //added

    //create nodes
    NodeContainer nodes;
    nodes.Create(5);

    //define specific names for each node container
    NodeContainer senderRouter1(nodes.Get(0), nodes.Get(2));
    NodeContainer senderRouter2(nodes.Get(1), nodes.Get(2));

    NodeContainer routerRouter(nodes.Get(2), nodes.Get(3));
    NodeContainer routerReceiver(nodes.Get(3), nodes.Get(4));

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
    NetDeviceContainer d1a = gndLink.Install(senderRouter1);
    NetDeviceContainer d1b = gndLink.Install(senderRouter2);

    NetDeviceContainer d2 = satLink.Install(routerRouter);
    NetDeviceContainer d3 = gndLink.Install(routerReceiver);

    InternetStackHelper stack;
    stack.Install(nodes);

    //following lines only for cubic vs bbr
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


    //receiver tcp socket 1
    PacketSinkHelper sink1("ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), 8080));
    ApplicationContainer sinkApp1 = sink1.Install(nodes.Get(4));
    sinkApp1.Start(Seconds(0.0));
    sinkApp1.Stop(Seconds(60.0));

    //receiver tcp socket 2
    PacketSinkHelper sink2("ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), 8081));
    ApplicationContainer sinkApp2 = sink2.Install(nodes.Get(4));
    sinkApp2.Start(Seconds(0.0));
    sinkApp2.Stop(Seconds(60.0)); 

    //sender tcp socket1
    BulkSendHelper source1("ns3::TcpSocketFactory",
        InetSocketAddress(i3.GetAddress(1), 8080));
    source1.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer sourceApp1 = source1.Install(nodes.Get(0));
    sourceApp1.Start(Seconds(1.0));
    sourceApp1.Stop(Seconds(60.0));

    //sender tcp socket2
    BulkSendHelper source2("ns3::TcpSocketFactory",
        InetSocketAddress(i3.GetAddress(1), 8081));
    source2.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer sourceApp2 = source2.Install(nodes.Get(1));
    sourceApp2.Start(Seconds(1.1));
    sourceApp2.Stop(Seconds(60.0));


    //RateErrorModel simulates wireless loss
    if (loss > 0.0) {
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
    em->SetAttribute("ErrorRate", DoubleValue(loss)); //DoubleValue() defines losses

    //packet loss only on one side of the satellite link receiver
    d2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }


    Simulator::Run();

    Ptr<PacketSink> sinkPtr1 = DynamicCast<PacketSink>(sinkApp1.Get(0));
    Ptr<PacketSink> sinkPtr2 = DynamicCast<PacketSink>(sinkApp2.Get(0));

    uint64_t totalBytes1 = sinkPtr1->GetTotalRx();
    uint64_t totalBytes2 = sinkPtr2->GetTotalRx();

    double throughput1 = (totalBytes1 * 8.0) / (59.0 * 1000000.0);
    double throughput2 = (totalBytes2 * 8.0) / (59.0 * 1000000.0);

    std::cout << "Flow 1 TCP Type: " << tcpType1 << std::endl;
    std::cout << "Flow 1 Throughput: " << throughput1 << " Mbps" << std::endl << std::endl;

    std::cout << "Flow 2 TCP Type: " << tcpType2 << std::endl;
    std::cout << "Flow 2 Throughput: " << throughput2 << " Mbps" << std::endl << std::endl;

    double fairness = ((throughput1 + throughput2) * (throughput1 + throughput2)) /
    (2 * (throughput1 * throughput1 + throughput2 * throughput2));

    std::cout << "Jain Fairness Index: " << fairness << std::endl;

    

    Simulator::Destroy();
    return 0;
}


