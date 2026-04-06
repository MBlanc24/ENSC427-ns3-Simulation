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

int
main(int argc, char* argv[])
{

    std::string tcpType = "cubic";

    CommandLine cmd(__FILE__);
    cmd.AddValue("tcpType", "TCP variant: cubic or bbr", tcpType);
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
    satLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

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
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(1e-5));

    //packet loss only on one side of the satellite link reciever
    d2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));




    Simulator::Run();
    Simulator::Destroy();
    return 0;
}


