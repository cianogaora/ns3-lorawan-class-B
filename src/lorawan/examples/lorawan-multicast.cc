//
// Created by cian on 28/01/23.
//
#include <ns3/log.h>
#include <ns3/core-module.h>
#include <ns3/network-module.h>
#include <ns3/ipv4-address.h>
#include <ns3/lorawan-module.h>
#include <ns3/propagation-loss-model.h>
#include <ns3/propagation-delay-model.h>
#include <ns3/mobility-module.h>
#include <ns3/applications-module.h>
#include <ns3/simulator.h>
#include <ns3/single-model-spectrum-channel.h>
#include <ns3/constant-position-mobility-model.h>
#include <ns3/node.h>
#include <ns3/packet.h>

#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"

#include <iostream>

using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("LORAWAN_CLASS_B_DEMO");

void
ReceivePacket (Ptr<Socket> socket)
{
    NS_LOG_LOGIC("receive packet called");

    Ptr<Packet> packet;
    uint64_t bytes = 0;
    while ((packet = socket->Recv ()))
    {
        bytes += packet->GetSize ();
    }

    NS_LOG_LOGIC("SOCKET received " << bytes << " bytes");
}

Ptr<Socket>
SetupPacketReceive (Ptr<Node> node)
{
    NS_LOG_LOGIC("setup packet receive called");
    TypeId tid = TypeId::LookupByName ("ns3::PacketSocketFactory");
    Ptr<Socket> sink = Socket::CreateSocket (node, tid);
    sink->Bind ();
    sink->SetRecvCallback (MakeCallback (&ReceivePacket));
    return sink;
}

int main (int argc, char *argv[])
{
    NS_LOG_INFO ("Creating the channel...");
    LogComponentEnableAll (LOG_PREFIX_FUNC);
    LogComponentEnableAll (LOG_PREFIX_NODE);
    LogComponentEnableAll (LOG_PREFIX_TIME);
//    NS_LOG_DEBUG("testing logging");
    LogComponentEnable("LoRaWANEndDeviceApplication",LOG_LEVEL_ALL);
    LogComponentEnable("LoRaWANGatewayApplication",LOG_LEVEL_ALL);

//    LogComponentEnable ("LoRaWANMac", LOG_LEVEL_ALL);
//    LogComponentEnable ("LoRaWANNetDevice", LOG_LEVEL_ALL);

    uint32_t ucNodes = 1;
    uint132_t mcNodes = 1;
    uint32_t nNodes = 2;
    uint8_t  dr = 0;
    double runtime = 200.0;
//    bool m_dsConfirmedData = false;
//    uint32_t m_dsPacketSize = 21;
//    bool m_dsDataGenerate = false;
//    double m_dsDataExpMean = -1;

    CommandLine cmd;
    cmd.AddValue("nNodes", "Number of nodes to add to simulation", nNodes);
    cmd.AddValue("dr", "Data rate to be used (up and down, a and b)", dr);
    cmd.Parse (argc, argv);

    dr &= (0b111); //this is a bit of a hack, changes inputed int to same as equivalent uint for small values

    NS_LOG_LOGIC("Start of Simulation!");
    NodeContainer unicastEndDeviceNodes;
    NodeContainer multicastEndDeviceNodes;
    NodeContainer gatewayNodes;
    NodeContainer allNodes;

    unicastEndDeviceNodes.Create (ucNodes);
    multicastEndDeviceNodes.Create(mcNodes);
    gatewayNodes.Create (1);

    allNodes.Add (unicastEndDeviceNodes);
    allNodes.Add (gatewayNodes);
    allNodes.Add(multicastEndDeviceNodes);


    double m_discRadius = 6100.0;
    MobilityHelper edMobility;
    edMobility.SetPositionAllocator ("ns3::UniformDiscPositionAllocator",
                                     "X", DoubleValue (0.0),
                                     "Y", DoubleValue (0.0),
                                     "rho", DoubleValue (m_discRadius));
    edMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    edMobility.Install (endDeviceNodes);


    // the gateway is placed at 0,0,0
    MobilityHelper gwMobility;
    Ptr<ListPositionAllocator> nodePositionList = CreateObject<ListPositionAllocator> ();
    nodePositionList->Add (Vector (0.0, 0.0, 0.0));  // gateway
    gwMobility.SetPositionAllocator (nodePositionList);
    gwMobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    gwMobility.Install (gatewayNodes);


    LoRaWANHelper lorawanHelper;
    lorawanHelper.SetNbRep(1); // no retransmissions
    NetDeviceContainer lorawanEDDevices = lorawanHelper.Install (endDeviceNodes);

    lorawanHelper.SetDeviceType (LORAWAN_DT_GATEWAY);
    NetDeviceContainer lorawanGWDevices = lorawanHelper.Install (gatewayNodes);

//    lorawanHelper.EnableLogComponents(ns3::LOG_LEVEL_DEBUG);

    PacketSocketHelper packetSocket;
    packetSocket.Install (endDeviceNodes);
    packetSocket.Install (gatewayNodes);


    // install end device application on nodes
    LoRaWANEndDeviceHelper enddevicehelper;
    enddevicehelper.SetAttribute ("DataRateIndex", UintegerValue (dr));
    enddevicehelper.SetAttribute ("ClassBDataRateIndex", UintegerValue (dr));
    enddevicehelper.SetAttribute ("IsClassB", BooleanValue(true));
    enddevicehelper.SetAttribute("IsMulticast", BooleanValue(true));

    ApplicationContainer enddeviceApps = enddevicehelper.Install (endDeviceNodes);

    // install gw application on gateways
    LoRaWANGatewayHelper gatewayhelper;
    gatewayhelper.SetAttribute ("DefaultClassBDataRateIndex", UintegerValue (dr));
    ApplicationContainer gatewayApps = gatewayhelper.Install (gatewayNodes);

    gatewayApps.Start (Seconds (0.0));
    gatewayApps.Stop (Seconds (runtime));


//    Ptr<LoRaWANNetworkServer> lorawanNSPtr = LoRaWANNetworkServer::getLoRaWANNetworkServerPointer ();
//    NS_ASSERT (lorawanNSPtr);
//    if (lorawanNSPtr) {
//        lorawanNSPtr->SetAttribute ("PacketSize", UintegerValue (m_dsPacketSize));
//        lorawanNSPtr->SetAttribute ("GenerateDataDown", BooleanValue (m_dsDataGenerate));
//        lorawanNSPtr->SetAttribute ("ConfirmedDataDown", BooleanValue (m_dsConfirmedData));
//        std::stringstream downstreamiatss;
//        downstreamiatss << "ns3::ExponentialRandomVariable[Mean=" << m_dsDataExpMean << "]";
//        lorawanNSPtr->SetAttribute ("DownstreamIAT", StringValue (downstreamiatss.str ()));
//    }

    enddeviceApps.Start (Seconds (0.0));
    enddeviceApps.Stop (Seconds (runtime));

    Ptr<Socket> recvSink = SetupPacketReceive (gatewayNodes.Get (0));

    Simulator::Stop (Seconds (runtime));
    Simulator::Run ();

    std::cout << "RESULTS START HERE" << std::endl;
    std::cout << nNodes << std::endl;
    Simulator::Destroy ();
    return 0;
}
