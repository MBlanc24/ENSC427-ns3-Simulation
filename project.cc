

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
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    Time::SetResolution(Time::NS);

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
    satLink.SetChannelAttribute ("Delay", StringValue ("300ms"));

    //installing links for the routers
    NetDeviceContainer d1 = gndLink.Install(senderRouter);
    NetDeviceContainer d2 = satLink.Install(routerRouter);
    NetDeviceContainer d3 = gndLink.Install(routerReceiver);

    InternetStackHelper stack;
    stack.Install(nodes);

    Simulator::Run();
    Simulator::Destroy();
    return 0;
}


