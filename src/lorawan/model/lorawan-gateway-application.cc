/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 IDLab-imec
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Floris Van den Abeele <floris.vandenabeele@ugent.be>
 */
#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/packet-socket-address.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
#include "ns3/node-list.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "lorawan.h"
#include "lorawan-net-device.h"
#include "lorawan-gateway-application.h"
#include "lorawan-frame-header.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/string.h"
#include "ns3/pointer.h"

#include "aes.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("LoRaWANGatewayApplication");

NS_OBJECT_ENSURE_REGISTERED (LoRaWANGatewayApplication);

Ptr<LoRaWANNetworkServer> LoRaWANNetworkServer::m_ptr = NULL;

LoRaWANNetworkServer::LoRaWANNetworkServer () : m_endDevices(), m_pktSize(0), m_generateDataDown(false), m_confirmedData(false), m_endDevicesPopulated(false), m_downstreamIATRandomVariable(nullptr), m_nrRW1Sent(0), m_nrRW2Sent(0), m_nrRW1Missed(0), m_nrRW2Missed(0), m_ClassBpktSize(30), m_ClassBdownstreamIATRandomVariable(nullptr), m_beaconTimer(), m_gateways(), m_generateClassBDataDown(true), m_ClassBBeaconChannelIndex(0), m_ClassBBeaconDataRateIndex(0) {}

TypeId
LoRaWANNetworkServer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LoRaWANNetworkServer")
    .SetParent<Application> ()
    .SetGroupName("LoRaWAN")
    .AddConstructor<LoRaWANNetworkServer> ()
    .AddAttribute ("PacketSize", "The size of DS packets sent to end devices",
                   UintegerValue (21),
                   MakeUintegerAccessor (&LoRaWANNetworkServer::m_pktSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("GenerateDataDown",
                   "Generate DS packets for sending to end devices. Note that DS Acks will be send regardless of this boolean.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&LoRaWANNetworkServer::m_generateDataDown),
                   MakeBooleanChecker ())
    .AddAttribute ("ConfirmedDataDown",
                   "Send Downstream data as Confirmed Data DOWN MAC packets."
                   "False means Unconfirmed data down packets are sent.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&LoRaWANNetworkServer::GetConfirmedDataDown,
                                        &LoRaWANNetworkServer::SetConfirmedDataDown),
                   MakeBooleanChecker ())
    .AddAttribute ("DownstreamIAT", "A RandomVariableStream used to pick the time between subsequent Class A DS transmissions to an end device.",
                   StringValue ("ns3::ExponentialRandomVariable[Mean=10]"),
                   MakePointerAccessor (&LoRaWANNetworkServer::m_downstreamIATRandomVariable),
                   MakePointerChecker <RandomVariableStream>())
    .AddAttribute ("ClassBDownstreamIAT", "A RandomVariableStream used to pick the time between subsequent Class B DS transmissions to an end device.",
                   StringValue ("ns3::ExponentialRandomVariable[Mean=10]"),
                   MakePointerAccessor (&LoRaWANNetworkServer::m_ClassBdownstreamIATRandomVariable),
                   MakePointerChecker <RandomVariableStream>())
    .AddTraceSource ("nrRW1Sent",
                     "The number of times that a DS packet was sent in RW1 by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_nrRW1Sent),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("nrRW2Sent",
                     "The number of times that a DS packet was sent in RW2 by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_nrRW2Sent),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("nrRW1Missed",
                     "The number of times RW1 was missed for all end devics served by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_nrRW1Missed),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("nrRW2Missed",
                     "The number of times RW2 was missed for all end devics served by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_nrRW2Missed),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("DSMsgGenerated",
                     "A DS msg for an end device has been generated by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_dsMsgGeneratedTrace),
                     "ns3::TracedValueCallback::LoRaWANDSMessageTracedCallback")
    .AddTraceSource ("DSMsgTransmitted",
                     "A DS msg has been transmitted by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_dsMsgTransmittedTrace),
                     "ns3::TracedValueCallback::LoRaWANDSMessageTransmittedTracedCallback")
    .AddTraceSource ("DSMsgAckd",
                     "DS msg has been acknowledged by end device.",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_dsMsgAckdTrace),
                     "ns3::TracedValueCallback::LoRaWANDSMessageTracedCallback")
    .AddTraceSource ("DSMsgDropped",
                     "DS msg has been dropped by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_dsMsgDroppedTrace),
                     "ns3::TracedValueCallback::LoRaWANDSMessageTracedCallback")
    .AddTraceSource ("USMsgReceived",
                     "An US msg has been received by this network server",
                     MakeTraceSourceAccessor (&LoRaWANNetworkServer::m_usMsgReceivedTrace),
                     "ns3::TracedValueCallback::LoRaWANDSMessageTracedCallback")
  ;
  return tid;
}

void
LoRaWANNetworkServer::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  std::cout << "testing!" << std::endl;
  //begin broadcasting of beacons
  if(m_generateClassBDataDown){
      Time t;
      if(m_scheduleFromUnixTime){
        uint64_t t_gps = getGPSTime();
        uint64_t t_until_next_beacon = 128 - (t_gps%128); // get number of seconds till next beacon time
        t = Seconds(t_until_next_beacon);
      }
      else{
        t = Seconds (128);
      }
      m_beaconTimer = Simulator::Schedule (t, &LoRaWANNetworkServer::ClassBSendBeacon, this);
      NS_LOG_DEBUG (this << " Class B beacon " << "scheduled at " << t);  
  }  

  Object::DoInitialize ();
}

void
LoRaWANNetworkServer::PopulateEndDevices (void)
{
  // only populate end devices once:
  if (m_endDevicesPopulated)
    return;

  // Populate m_endDevices based on ns3::NodeList
  for (NodeList::Iterator it = NodeList::Begin (); it != NodeList::End (); ++it)
  {
    Ptr<Node> nodePtr(*it);
    Address devAddr = nodePtr->GetDevice (0)->GetAddress();
    if (Ipv4Address::IsMatchingType (devAddr)) {
      Ipv4Address ipv4DevAddr = Ipv4Address::ConvertFrom (devAddr);
      if (ipv4DevAddr.IsEqual (Ipv4Address(0xffffffff))) { // gateway?
        continue;
      }

      // Construct LoRaWANEndDeviceInfoNS object
      LoRaWANEndDeviceInfoNS info = InitEndDeviceInfo (ipv4DevAddr);
      uint32_t key = ipv4DevAddr.Get ();
      m_endDevices[key] = info; // store object
    } else {
      NS_LOG_ERROR (this << " Unable to allocate device address");
      continue;
    }
  }
  m_endDevicesPopulated = true;
}

void
LoRaWANNetworkServer::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  Object::DoDispose ();
}

LoRaWANEndDeviceInfoNS
LoRaWANNetworkServer::InitEndDeviceInfo (Ipv4Address ipv4DevAddr)
{
  uint32_t key = ipv4DevAddr.Get ();

  // Construct LoRaWANEndDeviceInfoNS object
  LoRaWANEndDeviceInfoNS info;
  info.m_deviceAddress = ipv4DevAddr;
  info.m_rx1DROffset = 0; // default
  info.m_setAck = false;

  info.m_ClassBPingSlots = 4; //TODO: added by Joe, static for now.

  std::ostringstream addrOss;
  ipv4DevAddr.Print (addrOss);
  std::string addrStr = addrOss.str();

  std::cout << "address of node is: " << addrStr << std::endl; 

  if (m_generateDataDown) {
    Time t = Seconds (this->m_downstreamIATRandomVariable->GetValue ());
    info.m_downstreamTimer = Simulator::Schedule (t, &LoRaWANNetworkServer::DSTimerExpired, this, key);
    NS_LOG_DEBUG (this << " DS Traffic Timer for node " << ipv4DevAddr << " scheduled at " << t);
  }

  if (m_generateClassBDataDown) {
    //begin pseudo-random generation of Class B downlink traffic
    Time t = Seconds (this->m_ClassBdownstreamIATRandomVariable->GetValue ());
    info.m_ClassBdownstreamTimer = Simulator::Schedule (t, &LoRaWANNetworkServer::ClassBDSTimerExpired, this, key);
    NS_LOG_DEBUG (this << " Class B DS Traffic Timer for node " << ipv4DevAddr << " scheduled at " << t);
  }

  return info;
}

Ptr<LoRaWANNetworkServer>
LoRaWANNetworkServer::getLoRaWANNetworkServerPointer ()
{
  if (!LoRaWANNetworkServer::m_ptr) {
    LoRaWANNetworkServer::m_ptr = CreateObject<LoRaWANNetworkServer> ();
    LoRaWANNetworkServer::m_ptr->Initialize ();
  }

  return LoRaWANNetworkServer::m_ptr;
}

void
LoRaWANNetworkServer::HandleUSPacket (Ptr<LoRaWANGatewayApplication> lastGW, Address from, Ptr<Packet> packet)
{
  NS_LOG_FUNCTION(this);

  // if lastGW has never been seen before, add it to vector of GWs. Get it to send beacons later if we're using Class B.
  if( m_gateways.find(lastGW) == m_gateways.end() ){
    m_gateways.insert(lastGW);
  }

  // PacketSocketAddress fromAddress = PacketSocketAddress::ConvertFrom (from);

  // Decode Frame header
  LoRaWANFrameHeader frmHdr;
  frmHdr.setSerializeFramePort (true); // Assume that frame Header contains Frame Port so set this to true so that RemoveHeader will deserialize the FPort
  packet->RemoveHeader (frmHdr);

  // Find end device meta data:
  Ipv4Address deviceAddr = frmHdr.getDevAddr ();
  //NS_LOG_INFO(this << "Received packet from device addr = " << deviceAddr);
  uint32_t key = deviceAddr.Get ();
  auto it = m_endDevices.find (key);
  if (it == m_endDevices.end ()) { // not found, so create a new struct and insert it (note this should have already happened in DoInitialize()):
    NS_LOG_WARN (this << " end device with address = " << deviceAddr << " not found in m_endDevices, allocating");

    LoRaWANEndDeviceInfoNS info = InitEndDeviceInfo (deviceAddr);
    m_endDevices[key] = info;
    it = m_endDevices.find (key);
  }

  // Always update number of received upstream packets:
  it->second.m_nUSPackets += 1;

  // Always update last seen GWs:
  if ((Simulator::Now () - it->second.m_lastSeen) > Seconds(1.0)) { // assume a new upstream transmission, so clear the vector of seenGWs
    it->second.m_lastGWs.clear ();
  }
  it->second.m_lastGWs.push_back (lastGW);

  // Check for duplicate.
  // Depending on the frame counter and received time, we can classify the US Packet as:
  // i) The first time the NS sees the US Packet: i.e. new frame counter up value
  // ii) Retransmission of a previously transmitted US Packet (then the NS has to reply with an Ack): i.e. frame counter up already seen, seen longer than 1 second ago
  // iii) The same transmission received by a second Gateway (in this case we can drop the packet): i.e. frame counter up already seen, seen shorter than 1 second ago
  bool firstRX = it->second.m_nUSPackets == 0;
  bool processMACAck = true;
  if (frmHdr.getFrameCounter () <= it->second.m_fCntUp && !firstRX) {
    Time t = Simulator::Now () - it->second.m_lastSeen;
    if (t <= Seconds (1.0)) { // assume US packet is really a duplicate received by a second gateway
      // Duplicate, drop packet
      it->second.m_nUSDuplicates += 1;
      NS_LOG_INFO (this << " Duplicate detected: " << frmHdr.getFrameCounter () << " <= " << it->second.m_fCntUp << " &&  t = " << t << " < 1 second => dropping packet");
      // TODO: add trace for dropping duplicate packets?
      return;
    } else { // assume US packet is a retransmission
      it->second.m_nUSRetransmission += 1;
      processMACAck = false; // as we have already receive this US packet is a retransmission, we should not process the Ack flag set in the MAC header (but we should still open a RW or reply with an Ack if necessary)
    }
  } else { // new US frame counter value -> update number of unique packets received and US frame counter
    it->second.m_nUniqueUSPackets += 1;
    it->second.m_fCntUp = frmHdr.getFrameCounter (); // update US frame counter
  }

  // Update fields in LoRaWANEndDeviceInfoNS:
  it->second.m_lastSeen = Simulator::Now ();

  // Parse PhyRx Packet Tag
  LoRaWANPhyParamsTag phyParamsTag;
  if (packet->RemovePacketTag (phyParamsTag)) {
    it->second.m_lastChannelIndex = phyParamsTag.GetChannelIndex ();
    it->second.m_lastDataRateIndex = phyParamsTag.GetDataRateIndex ();
    it->second.m_lastCodeRate = phyParamsTag.GetCodeRate ();
    it->second.m_lastPreambleLength = phyParamsTag.GetPreambleLength ();
  } else {
    NS_LOG_WARN (this << " LoRaWANPhyParamsTag not found on packet.");
  }

  // Parse MAC Message Type Packet Tag
  LoRaWANMsgTypeTag msgTypeTag;
  if (packet->RemovePacketTag (msgTypeTag)) {
    LoRaWANMsgType msgType = msgTypeTag.GetMsgType();

    if (msgType == LORAWAN_CONFIRMED_DATA_UP) {
      it->second.m_setAck = true; // Set ack bit in next DS msg
      NS_LOG_DEBUG (this << " Received Confirmed Data UP. Next DS Packet will have Ack bit set");
    }
  } else {
    NS_LOG_WARN (this << " LoRaWANMsgTypeTag not found on packet.");
  }

  // Log that NS received an US packet:
  m_usMsgReceivedTrace (key, msgTypeTag.GetMsgType(), packet);

  // Parse Ack flag:
  if (processMACAck && frmHdr.getAck ()) {
    it->second.m_nUSAcks += 1;

    if (!it->second.m_downstreamQueue.empty ()) { // there is a DS message in the queue
      if (it->second.m_downstreamQueue.front()->m_downstreamMsgType == LORAWAN_CONFIRMED_DATA_DOWN) { // End device confirmed reception of DS packet, so we can remove it:
        LoRaWANNSDSQueueElement* ptr = it->second.m_downstreamQueue.front();

        // LOG that network server received an Acknowledgment for a DS packet
        m_dsMsgAckdTrace (key, ptr->m_downstreamTransmissionsRemaining, ptr->m_downstreamMsgType, ptr->m_downstreamPacket);

        this->DeleteFirstDSQueueElement (key);

        NS_LOG_DEBUG (this << " Received Ack for Confirmed DS packet, removing packet from DS queue for end device " << deviceAddr);
      } else {
        NS_LOG_ERROR (this << " Upstream frame has Ack bit set, but downstream frame msg type is not Confirmed (msgType = " << it->second.m_downstreamQueue.front()->m_downstreamMsgType << ")");
      }
    } else {
      // One occurence of this condition is when the NS receives a retransmission that re-acknowledges a previously send DS confirmed packet
      // This condition is fulfilled when the DS Ack for the previously transmitted US frame was sent by the NS but not received by the end device
      // Note that an end device retransmitting a frame will not change the Ack bit between retransmissions (ns-3 implementation limitation, to be fixed)
      NS_LOG_WARN (this << " Upstream frame has Ack bit set, but there is no downstream frame queued.");
    }
  }

  // We should always schedule a timer, even when m_downstreamPacket is NULL as a new DS packet might be generated between now and RW1
  if (it->second.m_rw1Timer.IsRunning()) {
    NS_LOG_ERROR (this << " Scheduling RW1 timer while RW1 timer was already scheduled for " << it->second.m_rw1Timer.GetTs ());
  }
  Time receiveDelay = MicroSeconds (RECEIVE_DELAY1);
  it->second.m_rw1Timer = Simulator::Schedule (receiveDelay, &LoRaWANNetworkServer::RW1TimerExpired, this, key);
}

bool
LoRaWANNetworkServer::HaveSomethingToSendToEndDevice (uint32_t deviceAddr)
{
  uint32_t key = deviceAddr;
  auto it_ed = m_endDevices.find (key);

  return it_ed->second.m_downstreamQueue.size() > 0 || it_ed->second.m_setAck;
}

void
LoRaWANNetworkServer::RW1TimerExpired (uint32_t deviceAddr)
{
  NS_LOG_FUNCTION (this << deviceAddr);

  uint32_t key = deviceAddr;
  auto it_ed = m_endDevices.find (key);

  // Check whether any GW in lastGWs can send a downstream transmission immediately (i.e. right now) in RW1
  bool foundGW = false;
  // The RW1 LoRa channel and data rate are the same as used in the last US transmission
  const uint8_t dsChannelIndex = it_ed->second.m_lastChannelIndex;
  const uint8_t dsDataRateIndex = it_ed->second.m_lastDataRateIndex;
  for (auto it_gw = it_ed->second.m_lastGWs.cbegin(); it_gw != it_ed->second.m_lastGWs.cend(); it_gw++) {
    if ((*it_gw)->CanSendImmediatelyOnChannel (dsChannelIndex, dsDataRateIndex)) {
      foundGW = true;
      this->SendDSPacket (deviceAddr, *it_gw, true, false);
      break;
    }
  }

  if (!foundGW) {
    NS_LOG_DEBUG (this << " No gateway available for transmission in RW1, scheduling timer for DS transmission in RW2");

    // Increment m_nrRW1Missed only if there is something to send:
    if (HaveSomethingToSendToEndDevice (deviceAddr)) {
      m_nrRW1Missed++;
    }

    if (it_ed->second.m_rw2Timer.IsRunning()) {
      NS_LOG_ERROR (this << " Scheduling RW2 timer while RW2 timer was already scheduled for " << it_ed->second.m_rw2Timer.GetTs ());
    }

    // Time receiveDelay = MicroSeconds (RECEIVE_DELAY2);
    Time receiveDelay = (it_ed->second.m_lastSeen + MicroSeconds (RECEIVE_DELAY2)) - Simulator::Now ();
    NS_ASSERT (receiveDelay > 0);
    it_ed->second.m_rw2Timer = Simulator::Schedule (receiveDelay, &LoRaWANNetworkServer::RW2TimerExpired, this, key);
  }
}

void
LoRaWANNetworkServer::RW2TimerExpired (uint32_t deviceAddr)
{
  NS_LOG_FUNCTION (this << deviceAddr);

  uint32_t key = deviceAddr;
  auto it_ed = m_endDevices.find (key);

  // Check whether any GW in lastGWs can send a downstream transmission immediately (i.e. right now) in RW2
  // The RW2 LoRa channel is a fixed channel depending on the region, for EU this is the high power 869.525 MHz channel
  const uint8_t dsChannelIndex = LoRaWAN::m_RW2ChannelIndex;
  const uint8_t dsDataRateIndex = LoRaWAN::m_RW2DataRateIndex;
  bool foundGW = false;
  for (auto it_gw = it_ed->second.m_lastGWs.cbegin(); it_gw != it_ed->second.m_lastGWs.cend(); it_gw++) {
    if ((*it_gw)->CanSendImmediatelyOnChannel (dsChannelIndex, dsDataRateIndex)) {
      foundGW = true;
      this->SendDSPacket (deviceAddr, *it_gw, false, true);
      break;
    }
  }

  if (!foundGW) {
    // Increment m_nrRW2Missed only if there is something to send:
    if (HaveSomethingToSendToEndDevice (deviceAddr)) {
      m_nrRW2Missed++;
      NS_LOG_INFO (this << " Unable to send DS transmission to device addr " << deviceAddr << " in RW1 and RW2, no gateway was available.");
    }
  }
}

void
LoRaWANNetworkServer::SendDSPacket (uint32_t deviceAddr, Ptr<LoRaWANGatewayApplication> gatewayPtr, bool RW1, bool RW2)
{
  // Search device in m_endDevices:
  auto it = m_endDevices.find (deviceAddr);
  if (it == m_endDevices.end ()) { // end device not found
    NS_LOG_ERROR (this << " Could not find device info struct in m_endDevices for dev addr " << deviceAddr << ". Aborting DS Transmission");
    return;
  }

  // Check if we have a last known GW for the device:
  // bool haveGW = it->second.m_lastGWs.size () > 0;
  // if (!haveGW) {
  //   NS_LOG_ERROR (this << " lastGW is not set for dev addr " << deviceAddr << ". Aborting DS Transmission");
  //   return;
  // }
  // Ptr <LoRaWANGatewayApplication> lastGW = *it->second.m_lastGWs.begin ();

  // Figure out which DS packet to send
  LoRaWANNSDSQueueElement elementToSend;
  bool deleteQueueElement = false;
  if (it->second.m_downstreamQueue.size() > 0) {
    LoRaWANNSDSQueueElement* element = it->second.m_downstreamQueue.front ();

    // Bookkeeping for Confirmed packets:
    if (element->m_downstreamMsgType == LORAWAN_CONFIRMED_DATA_DOWN) {
      // Count number of retransmissions:
      if (element->m_isRetransmission) {
        it->second.m_nDSRetransmission++;
      }

      // Update for next transmission:
      element->m_downstreamTransmissionsRemaining--; // decrement
      element->m_isRetransmission = true;
    }

    // Should we delete pending packet after transmission?
    if (element->m_downstreamMsgType != LORAWAN_CONFIRMED_DATA_DOWN) { // delete the queueelement object after the send operation
      deleteQueueElement = true;
    } else {
      if (element->m_downstreamTransmissionsRemaining == 0) {// in case of CONFIRMED_DATA_DOWN, delete the pending transmission when the number of remaining transmissions has reached 1
        deleteQueueElement = true;

        // LOG that network server will delete DS packet from queue
        m_dsMsgDroppedTrace (deviceAddr, 0, element->m_downstreamMsgType, element->m_downstreamPacket);
      }
    }

    elementToSend.m_downstreamPacket = element->m_downstreamPacket;
    elementToSend.m_downstreamMsgType = element->m_downstreamMsgType;
    elementToSend.m_downstreamFramePort = element->m_downstreamFramePort;
    elementToSend.m_downstreamTransmissionsRemaining = element->m_downstreamTransmissionsRemaining;
  } else {
    if (!it->second.m_setAck) {
      // Not really a warning as there is just no need to send a DS packet (i.e. no data and no Ack)
      NS_LOG_INFO (this << " No downstream packet found nor is ack bit set for dev addr " << deviceAddr << ". Aborting DS transmission");
      return;
    } else {
      NS_LOG_DEBUG (this << " Generating empty downstream packet to send Ack for dev addr " << deviceAddr);
      elementToSend.m_downstreamPacket = Create<Packet> (0); // create empty packet so that we can send the Ack
      elementToSend.m_downstreamMsgType = LORAWAN_UNCONFIRMED_DATA_DOWN; // should also set msg type
      elementToSend.m_downstreamFramePort = 0; // empty packet, so don't send frame port
      elementToSend.m_downstreamTransmissionsRemaining = 0;
    }
  }

  // LOG DS msg transmission
  uint8_t rwNumber = RW1 ? 1 : 2;
  m_dsMsgTransmittedTrace (deviceAddr, elementToSend.m_downstreamTransmissionsRemaining, elementToSend.m_downstreamMsgType, elementToSend.m_downstreamPacket, rwNumber);

  // Make a copy here, this is u
  Ptr<Packet> p;
  if (deleteQueueElement)
    p = elementToSend.m_downstreamPacket;
  else
    p = elementToSend.m_downstreamPacket->Copy (); // make a copy, so that we don't alter elementToSend.m_downstreamPacket as we might re-use this packet later (e.g. retransmission)

  // Construct Frame Header:
  LoRaWANFrameHeader fhdr;
  fhdr.setDevAddr (Ipv4Address (deviceAddr));
  fhdr.setAck (it->second.m_setAck);
  fhdr.setFramePending (it->second.m_framePending);
  fhdr.setFrameCounter (it->second.m_fCntDown++);
  if (elementToSend.m_downstreamFramePort > 0)
    fhdr.setFramePort (elementToSend.m_downstreamFramePort);

  p->AddHeader (fhdr);

  // Add Phy Packet tag to specify channel, data rate and code rate:
  uint8_t dsChannelIndex;
  uint8_t dsDataRateIndex;
  if (RW1) {
    dsChannelIndex = it->second.m_lastChannelIndex;
    dsDataRateIndex = LoRaWAN::GetRX1DataRateIndex (it->second.m_lastDataRateIndex, it->second.m_rx1DROffset);
  } else if (RW2) {
    dsChannelIndex = LoRaWAN::m_RW2ChannelIndex;
    dsDataRateIndex = LoRaWAN::m_RW2DataRateIndex;
  } else {
    NS_FATAL_ERROR (this << " Either RW1 or RW2 should be true");
    return;
  }

  LoRaWANPhyParamsTag phyParamsTag;
  phyParamsTag.SetChannelIndex (dsChannelIndex);
  phyParamsTag.SetDataRateIndex (dsDataRateIndex);
  phyParamsTag.SetCodeRate (it->second.m_lastCodeRate);
  phyParamsTag.SetPreambleLength (8) //TODO: magic number
  p->AddPacketTag (phyParamsTag);

  // Set Msg type
  LoRaWANMsgTypeTag msgTypeTag;
  msgTypeTag.SetMsgType (elementToSend.m_downstreamMsgType);
  p->AddPacketTag (msgTypeTag);

  // Update DS Packet counters:
  it->second.m_nDSPacketsSent += 1;
  if (RW1) {
    m_nrRW1Sent++;
    it->second.m_nDSPacketsSentRW1 += 1;
  } else if (RW2) {
    it->second.m_nDSPacketsSentRW2 += 1;
    m_nrRW2Sent++;
  }
  if (it->second.m_setAck)
    it->second.m_nDSAcks += 1;

  // Store gatewayPtr as last DS GW:
  it->second.m_lastDSGW = gatewayPtr;

  // Ask gateway application on lastseenGW to send the DS packet:
  gatewayPtr->SendDSPacket (p);
  NS_LOG_DEBUG (this << " Sent DS Packet to device addr " << deviceAddr << " via GW #" << gatewayPtr->GetNode()->GetId() << " in RW" << (RW1 ? "1" : "2"));

  // Reset data structures
  it->second.m_setAck = false; // we only sent an Ack once, see Note on page 75 of LoRaWAN std

  // For some cases (see deleteQueueElement bool), remove the pending DS packet here
  if (deleteQueueElement) {
    this->DeleteFirstDSQueueElement (deviceAddr);
  }
}

void
LoRaWANNetworkServer::DSTimerExpired (uint32_t deviceAddr)
{
  NS_LOG_FUNCTION (this << deviceAddr);

  auto it = m_endDevices.find (deviceAddr);
  if (it == m_endDevices.end ()) { // end device not found
    NS_LOG_ERROR (this << " Could not find device info struct in m_endDevices for dev addr " << deviceAddr);
    return;
  }

  // Generate a Downstream packet
  if (it->second.m_downstreamQueue.size () > 0)
    NS_LOG_INFO(this << " DS queue for end device " << Ipv4Address(deviceAddr) << " is not empty");

  NS_ASSERT (m_pktSize >= 8 + 1 + 4); // should be able to send at least frame header, MAC header and MAC MIC
  bool generatePacket = true;
  if (m_pktSize < 8 + 1 + 4) {
    NS_LOG_ERROR (this << " m_pktSize = " << m_pktSize << " is too small, not generating DS packets");
    generatePacket = false;
  }

  if (generatePacket) {
    uint8_t frmPayloadSize = m_pktSize - (8 + 1 + 4);

    Ptr<Packet> packet;
    if (frmPayloadSize >= sizeof(uint64_t)) { // check whether payload size is large enough to hold 64 bit integer
      // send decrementing counter as payload (note: globally shared counter)
      uint8_t* payload = new uint8_t[frmPayloadSize](); // the parenthesis initialize the allocated memory to zero
      if (payload) {
        const uint64_t counter = LoRaWANCounterSingleton::GetCounter ();
        ((uint64_t*)payload)[0] = counter; // copy counter to beginning of payload
        packet = Create<Packet> (payload, frmPayloadSize);
        delete[] payload;
      } else {
        packet = Create<Packet> (frmPayloadSize);
      }
    } else {
      packet = Create<Packet> (frmPayloadSize);
    }

    LoRaWANNSDSQueueElement* element = new LoRaWANNSDSQueueElement ();
    element->m_downstreamPacket = packet;
    element->m_downstreamFramePort = 1;
    if (m_confirmedData) {
      element->m_downstreamMsgType = LORAWAN_CONFIRMED_DATA_DOWN;
      element->m_downstreamTransmissionsRemaining = DEFAULT_NUMBER_DS_TRANSMISSIONS;
    } else {
      element->m_downstreamMsgType = LORAWAN_UNCONFIRMED_DATA_DOWN;
      element->m_downstreamTransmissionsRemaining = 1;
    }
    element->m_isRetransmission = false;
    it->second.m_downstreamQueue.push_back (element);
    it->second.m_nDSPacketsGenerated += 1;

    m_dsMsgGeneratedTrace (deviceAddr, element->m_downstreamTransmissionsRemaining, element->m_downstreamMsgType, element->m_downstreamPacket);
    NS_LOG_DEBUG (this << " Added downstream packet with size " << m_pktSize  << " to DS queue for end device " << Ipv4Address(deviceAddr) << ". queue size = " << it->second.m_downstreamQueue.size());
  }

  // Reschedule timer:
  Time t = Seconds (this->m_downstreamIATRandomVariable->GetValue ());
  it->second.m_downstreamTimer = Simulator::Schedule (t, &LoRaWANNetworkServer::DSTimerExpired, this, deviceAddr);
  NS_LOG_DEBUG (this << " DS Traffic Timer for end device " << it->second.m_deviceAddress << " scheduled at " << t);
}

void
LoRaWANNetworkServer::DeleteFirstDSQueueElement (uint32_t deviceAddr)
{
  auto it = m_endDevices.find (deviceAddr);
  if (it == m_endDevices.end ()) { // end device not found
    NS_LOG_ERROR (this << " Could not find device info struct in m_endDevices for dev addr " << deviceAddr << ". Unable to delete DS queue element.");
    return;
  }

  LoRaWANNSDSQueueElement* ptr = it->second.m_downstreamQueue.front ();
  delete ptr;
  it->second.m_downstreamQueue.pop_front ();
}

int64_t
LoRaWANNetworkServer::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_downstreamIATRandomVariable->SetStream (stream);
  m_ClassBdownstreamIATRandomVariable->SetStream (stream); //TODO: should use a different stream?
  return 1;
}

void
LoRaWANNetworkServer::SetConfirmedDataDown (bool confirmedData)
{
  NS_LOG_FUNCTION (this << confirmedData);
  m_confirmedData = confirmedData;
}

bool
LoRaWANNetworkServer::GetConfirmedDataDown (void) const
{
  return m_confirmedData;
}


////////////////////////////////////////////////////////////


//Code in here is modifications of the NetworkServer added by Joe.

// adding a stream of data specifically to be sent as Class B downlink traffic.
// based on DSTimerExpired 
void
LoRaWANNetworkServer::ClassBDSTimerExpired (uint32_t deviceAddr)
{
  NS_LOG_FUNCTION (this << deviceAddr);

  auto it = m_endDevices.find (deviceAddr);
  if (it == m_endDevices.end ()) { // end device not found
    NS_LOG_ERROR (this << " Could not find device info struct in m_endDevices for dev addr " << deviceAddr);
    return;
  }

  // Generate a Downstream packet
  if (it->second.m_ClassBdownstreamQueue.size () > 0)
    NS_LOG_INFO(this << " Class B DS queue for end device " << Ipv4Address(deviceAddr) << " is not empty");

  NS_ASSERT (m_ClassBpktSize >= 8 + 1 + 4); // should be able to send at least frame header, MAC header and MAC MIC
  bool generatePacket = true;
  if (m_ClassBpktSize < 8 + 1 + 4) {
    NS_LOG_ERROR (this << " m_pktSize = " << m_pktSize << " is too small, not generating Class B DS packets");
    generatePacket = false;
  }

  if (generatePacket) {
    uint8_t frmPayloadSize = m_ClassBpktSize - (8 + 1 + 4);

    Ptr<Packet> packet;
    if (frmPayloadSize >= sizeof(uint64_t)) { // check whether payload size is large enough to hold 64 bit integer
      // send decrementing counter as payload (note: globally shared counter)
      uint8_t* payload = new uint8_t[frmPayloadSize](); // the parenthesis initialize the allocated memory to zero
      if (payload) {
        const uint64_t counter = LoRaWANCounterSingleton::GetCounter ();
        ((uint64_t*)payload)[0] = counter; // copy counter to beginning of payload
        packet = Create<Packet> (payload, frmPayloadSize);
        delete[] payload;
      } else {
        packet = Create<Packet> (frmPayloadSize);
      }
    } else {
      packet = Create<Packet> (frmPayloadSize);
    }

    LoRaWANNSDSQueueElement* element = new LoRaWANNSDSQueueElement ();
    element->m_downstreamPacket = packet;
    element->m_downstreamFramePort = 1;
    if (m_confirmedData) {
      element->m_downstreamMsgType = LORAWAN_CONFIRMED_DATA_DOWN;
      element->m_downstreamTransmissionsRemaining = DEFAULT_NUMBER_DS_TRANSMISSIONS;
    } else {
      element->m_downstreamMsgType = LORAWAN_UNCONFIRMED_DATA_DOWN;
      element->m_downstreamTransmissionsRemaining = 1;
    }
    element->m_isRetransmission = false;
    it->second.m_ClassBdownstreamQueue.push_back (element);
    it->second.m_nClassBPacketsGenerated += 1;

    m_dsMsgGeneratedTrace (deviceAddr, element->m_downstreamTransmissionsRemaining, element->m_downstreamMsgType, element->m_downstreamPacket);
    NS_LOG_DEBUG (this << " Added downstream packet with size " << m_pktSize  << " to DS queue for end device " << Ipv4Address(deviceAddr) << ". queue size = " << it->second.m_downstreamQueue.size());
  }

  // Reschedule timer:
  Time t = Seconds (this->m_ClassBdownstreamIATRandomVariable->GetValue ());
  it->second.m_ClassBdownstreamTimer = Simulator::Schedule (t, &LoRaWANNetworkServer::ClassBDSTimerExpired, this, deviceAddr);
  NS_LOG_DEBUG (this << " Class B DS Traffic Timer for end device " << it->second.m_deviceAddress << " scheduled at " << t);
}


void
LoRaWANNetworkServer::ClassBSendBeacon (){

  //Simulator::Now() can be used to get the current time right at this instance.
  //This can be used to schedule a call to this method at exactly the right time
  //so this method should send Now in the correct format (in reality, number of seconds since 1980, but as long as all nodes agree the crypto should still work)
  //TODO: this is a marked difference between the simulation and real life
  

  /*TODO:
    The encryption works, BUT:
    the format of the devAddr is not the same as in LoRaWAN
    Need to double check the endianness that the timestamp and devaddr are inserted into the buffer with
    timestamp is current number of seconds since simulation began, not GMT time
    the gateway send of the beacons (add in gw part, then reference here.)
    gateway function should be given a timestamp + x to schedule the gateway, in order to ensure simultaneous sends
    don't use cout, use logging

    all of these things shouldn't affect the functionality of the method (as long as ping slot calculation is performed the same on end devices) 
    but they are all marked differences between the simulation and real life

    
  */
  std::cout << "In Class B beacon!" << std::endl;


  Time timestamp = Simulator::Now();
  Time nextBeacon = Seconds (128); //TODO: correct format? does this get the time for now + 128s, or is it just 128s? Does it matter in ns3?
  uint64_t t;

  uint64_t secs = timestamp.GetMilliSeconds() / 1000; //note: not using GetSeconds as it returns a double.
  if(m_scheduleFromUnixTime){
         
        uint64_t t_gps = getRelativeGPSTime(secs);
        t = t_gps & 0x00000000FFFFFFFF; // t should be the 4 LSB bytes of t_gps
  }
  else {
       t = secs & 0x00000000FFFFFFFF; // t should be the 4 LSB bytes of secs TODO: there must be a nicer way of doing this.
  }



  std::cout << "Current time is " << timestamp << std::endl;
  std::cout << "Next beacon will be sent at " << nextBeacon << std::endl;


  // build a beacon frame
  // unlike for Class A, where the gateway is just a relay, in Class B the beacons must be modified in the gateway, as some parameters are gateway dependent
  // crc must be calculated - check in spec for exact algorithm

  // build the majority of the beacon
  uint8_t beacon[17];

  // the beacon payload is in this format (EU868 only):
  // 2   4    2   7          2
  // RFU Time CRC GwSpecific CRC

  // RFU is 0,0
  beacon[0] = 0x00;
  beacon[1] = 0x00;

  // Time is seconds in GPS
  // the timestamp given as a parameter to this function is that GPS time
  // TODO: double check the values are going in in the right order
  // putting them in LSB first
  beacon[2] = (t >> 0)  & 0xFF;
  beacon[3] = (t >> 8)  & 0xFF;
  beacon[4] = (t >> 16) & 0xFF;
  beacon[5] = (t >> 24) & 0xFF;
 
  // CRC is defined in IEEE 802.15.4-2003 section 7.2.1.8, and is calculated on the bytes in the order they are sent over the air
  // e.g. so if the GPS time was 3422683136, then the hex of that is CC020000
  // and so the two byte CRC would be calculated on [00 00 00 00 02 CC]
  // we're not actually implementing the CRC check, but if we were this is where it would be done.
  beacon[6] = 0x00;
  beacon[7] = 0x00;

  Packet p =  Create<Packet> (beacon, 17);

  //add the tags to the packet
  //note that there is no frame header in beacons

  /*uint8_t beaconChannelIndex = 7; // 869.525MHz. Mandetory for Class B beacons. TODO: this is currently set as a high power channel in the phy layer implementation. Is this correct?
  // Note: this also appears to be the only channel outside of the main subband? 
  uint8_t beaconDataRateIndex = 3;  // SF9, 125kHz BW. Mandetory for Class B beacons.
  uint8_t beaconCodeRate = 3; //TODO: double check this.
  uint8_t beaconPreambleLength = 10;

  //TODO: this should be done here, but as the packet gets recreated in the gw->SendBeacon function, there's no point.
  //if the PeekData function is used instead, then the Tag can be added here.
  LoRaWANPhyParamsTag phyParamsTag;
  phyParamsTag.SetChannelIndex (beaconChannelIndex);
  phyParamsTag.SetDataRateIndex (beaconDataRateIndex);
  phyParamsTag.SetCodeRate (beaconCodeRate);
  phyParamsTag.SetPreambleLength (beaconPreambleLength);
  p->AddPacketTag (phyParamsTag);

  // Set Msg type
  LoRaWANMsgTypeTag msgTypeTag;
  msgTypeTag.SetMsgType (LORAWAN_BEACON);
  p->AddPacketTag (msgTypeTag);*/


  //indicate to gateways to send a beacon at exact right time
  for (auto gw = m_gateways.cbegin(); gw != m_gateways.cend(); gw++) {
    if ((*gw)->CanSendImmediatelyOnChannel (m_ClassBBeaconChannelIndex, m_ClassBBeaconDataRateIndex)) {
      gw->SendBeacon(p);
    }
    else{
      //TODO: log err
      //log err
    }
  }

  //schedule ping slots for all devices
  for (auto d = m_endDevices.cbegin(); d != m_endDevices.cend(); d++) {
    /*
    period  = (2^32)/slots
    R = AES_128_enc(16x 0x00, Time | DevAddr | pad16)
    O = (R[0] + R[1] * 256) % period

    then timings = {O + x*period | x < slots, x element of N}

    schedule the NS to call a sendDownlink function at each of those times, with the devAddr as parameter.
    then the DL works similarly to the current DL function here, but with a set DR and channel. Closest GW is chosen.
    Calculate all slots first, then schedule them - that way conflicting slots (same time and gw) can be handled.
    */

    uint64_t period = std::pow(2.0, 12) / d->second.m_ClassBPingSlots; //TODO: double-check this. 2^12 / 2 = 2048 - is that what we're looking for???
    uint8_t key[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };   
    uint8_t buf[16];

    
    Ipv4Address devAddr = d->second.m_deviceAddress;
    uint8_t addr[4];
    devAddr.Serialize(addr);
    buf[4] = addr[0];
    buf[5] = addr[1];
    buf[6] = addr[2];
    buf[7] = addr[3];

    double secs = timestamp.GetSeconds(); // convert to a 4 byte buffer
    uint32_t secsTruncated = (uint32_t) secs;

    uint8_t *sp = (uint8_t *)&secsTruncated;

    buf[0] = sp[0];
    buf[1] = sp[1];
    buf[2] = sp[2];
    buf[3] = sp[3];

    //pad16 is to ensure the buffer is 16 bytes long
    for(uint i=8; i<16;i++){
      buf[i] = 0;
    }

    AES aes;

    aes.SetKey(key, 16);
    aes.Encrypt(buf, 16);

    uint64_t O = (buf[0] + buf[1]*256) % period;

    Ipv4Address deviceAddr = d->m_deviceAddress;
    //NS_LOG_INFO(this << "Received packet from device addr = " << deviceAddr);
    uint32_t key = deviceAddr.Get ();

    //calculate and schedule ping slots for this device
    for(uint i=0;i< d->second.m_ClassBPingSlots; i++){
      uint64_t pingTime = O + (period * i);
      Simulator::Schedule (pingTime, &LoRaWANNetworkServer::ClassBPingSlot, key);
    }

    /*printf("%d ", period);
    printf("%d ", O);*/

    //std::cout << "Offset: " << O << std::endl;

    //Now: also: actually store the ping periods based on O, and use them to schedule firings of ClassBDSTimerExpired
    //LoRaWANGatewayApplication::SendDSPacket can still be used - the DS packet is then built by the NS.

  }

  //schedule next beacon
  m_beaconTimer = Simulator::Schedule (nextBeacon, &LoRaWANNetworkServer::ClassBSendBeacon, this);
  std::cout << "Class B beacon scheduled!" << std::endl;
  NS_LOG_DEBUG (this << " Class B beacon " << "scheduled at " << t);
}

void
LoRaWANNetworkServer::ClassBPingSlot(uint32_t devAddr)
{
  //get device to send downlink to
  auto it = m_endDevices.find (deviceAddr);
  if (it == m_endDevices.end ()) { // end device not found
    NS_LOG_ERROR (this << " Could not find device info struct in m_endDevices for dev addr " << deviceAddr);
    return;
  }

  //TODO: check queue, if a packet is waiting to be sent send via last seen gateway
  // v. similar to SendDSPacket, can it be modified and used directly?
}

// In real LoRaWAN networks the timings of beacons is defined to be every 128 seconds, starting from the beginning of GPS time. 
// GPS time is defined as the number of seconds since the 6th of Jan, 1980, not factoring in leap seconds (i.e current GPS time is 18 seconds ahead of UTC time)
// GPS time is managed and maintained by IERS, see their website for details: www.iers.org
// these following functions are C++ ports of functions from David Calhoun's GPS-time.js library
// https://github.com/davidcalhoun/gps-time.js/blob/master/gps-time.js
// Licensed under MIT (https://github.com/davidcalhoun/gps-time.js/blob/master/LICENSE)
// these functions aren't necessarily needed (it can be assumed for example that the first beacon gets sent 128s after the start of the simulation)
// but could be useful
// to use: get Unix time from OS via uint64_t t = static_cast<uint64_t>(time(NULL))
uint64_t
LoRaWANNetworkServer::getGPSTimeFromUnixTime(uint64_t unixMS)
{
    
    uint64_t msInSecond = 1000;
    // Difference in time between Jan 1, 1970 (Unix epoch) and Jan 6, 1980 (GPS epoch).
    uint64_t gpsUnixEpochDiffMS = 315964800000;

    //Fractional seconds indicate this is a leap second??? TODO: ensure unix time got from OS is in correct format for this to work.
    bool isLeap = (unixMS % msInSecond) != 0;
    if(isLeap) {
      unixMS -= msInSecond / 2;
    }

    uint64_t gpsMS = unixMS - gpsUnixEpochDiffMS;

    gpsMS += (countLeaps(gpsMS, true) * msInSecond);

    if(isLeap)
    {
      gpsMS += msInSecond;
    }

    return gpsMS;
}

uint64_t
LoRaWANNetworkServer::countLeaps(uint64_t gpsMS, bool isUnixToGPS)
{
  uint64_t numLeaps = 0;
  uint64_t msInSecond = 1000;
  // List of GPS leaps in milliseconds.  This will need to stay updated as new leap seconds are announced in
// the future.
  uint64_t gpsLeapMS[] = {
  46828800000, 78364801000, 109900802000, 173059203000, 252028804000, 315187205000, 346723206000,
  393984007000, 425520008000, 457056009000, 504489610000, 551750411000, 599184012000, 820108813000,
  914803214000, 1025136015000, 1119744016000, 1167264017000
  };

  for(uint i=0; i<sizeof(gpsLeapMS)/sizeof(gpsLeapMS[0]); i++)
  {
    if(shouldAddLeap(gpsMS, gpsLeapMS[i], i * msInSecond, isUnixToGPS)){
      numLeaps++;
    }
  }
  return numLeaps;
}

bool 
LoRaWANNetworkServer::shouldAddLeap(uint64_t gpsMS, uint64_t curGPSLeapMS, uint64_t totalLeapsMS, bool isUnixToGPS){
  if(isUnixToGPS)
  {
    // for unix->gps
    return gpsMS >= curGPSLeapMS - totalLeapsMS;
  }
  else
  {
    // for gps->unix
    return gpsMS >= curGPSLeapMS;
  }
}

/*NOTE: only can be used at very start of simulation*/
uint64_t
LoRaWANNetworkServer::getGPSTime()
{
  uint64_t t = static_cast<uint64_t>(time(NULL)); //seconds in Unix time
  m_simulationStartTime = t;
  t *= 1000; // rough convert to millis
  uint64_t t_gps = getGPSTimeFromUnixTime(t); // convert to GPS time
  t_gps /= 1000; // rough convert back to seconds
  return t_gps;
}

uint64_t
LoRaWANNetworkServer::getRelativeGPSTime(uint64_t secondsPassed)
{
  return m_simulationStartTime + secondsPassed;
}


////////////////////////////////////////////////////////////

LoRaWANGatewayApplication::LoRaWANGatewayApplication ()
  : m_socket (0),
    m_connected (false),
    m_totalRx (0)
{
  NS_LOG_FUNCTION (this);
}

TypeId
LoRaWANGatewayApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LoRaWANGatewayApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<LoRaWANGatewayApplication> ()
    .AddTraceSource ("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor (&LoRaWANGatewayApplication::m_txTrace),
                     "ns3::Packet::TracedCallback")
  ;
  return tid;
}

LoRaWANGatewayApplication::~LoRaWANGatewayApplication ()
{
  NS_LOG_FUNCTION (this);
}

void
LoRaWANGatewayApplication::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  std::cout << "testing!" << std::endl;
  this->m_lorawanNSPtr = LoRaWANNetworkServer::getLoRaWANNetworkServerPointer ();

  // chain up
  Application::DoInitialize ();
}

void
LoRaWANGatewayApplication::DoDispose (void)
{
  NS_LOG_FUNCTION (this);

  m_socket = 0;
  this->m_lorawanNSPtr = nullptr;
  // clear ref count in static member, as to destroy the LoRaWANNetworkServer object.
  // Note we should only destroy the NS object when the simulation is stopped and all gateway applications are destroyed.
  // So we assume that a gateway is not destroyed before the end of the simulation

  if (LoRaWANNetworkServer::haveLoRaWANNetworkServerObject ())
    LoRaWANNetworkServer::clearLoRaWANNetworkServerPointer ();

  // chain up
  Application::DoDispose ();
}

void
LoRaWANGatewayApplication::SetMaxBytes (uint64_t maxBytes)
{
  NS_LOG_FUNCTION (this << maxBytes);
  m_maxBytes = maxBytes;
}

Ptr<Socket>
LoRaWANGatewayApplication::GetSocket (void) const
{
  NS_LOG_FUNCTION (this);
  return m_socket;
}

int64_t
LoRaWANGatewayApplication::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  return LoRaWANNetworkServer::getLoRaWANNetworkServerPointer ()->AssignStreams (stream);
}

bool
LoRaWANGatewayApplication::CanSendImmediatelyOnChannel (uint8_t channelIndex, uint8_t dataRateIndex)
{
  NS_LOG_FUNCTION (this << (unsigned)channelIndex << (unsigned)dataRateIndex);

  Ptr<LoRaWANNetDevice> device = DynamicCast<LoRaWANNetDevice> (GetNode ()->GetDevice (0));

  if (!device) {
    NS_LOG_ERROR (this << " Cannot get LoRaWANNetDevice pointer belonging to this gateway");
    return false;
  } else {
    return device->CanSendImmediatelyOnChannel (channelIndex, dataRateIndex);
  }
}

void LoRaWANGatewayApplication::SendDSPacket (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this);
  // p represents MACPayload

  // Get the requested data rate from the packet tag
  uint8_t dataRateIndex = 12; // SF12 as default value

  LoRaWANPhyParamsTag phyParamsTag;
  if (p->PeekPacketTag (phyParamsTag)) {
    dataRateIndex = phyParamsTag.GetDataRateIndex();
  }

  // Set NetDevice MTU Data rate before calling socket::Send
  Ptr<LoRaWANNetDevice> netDevice = DynamicCast<LoRaWANNetDevice> (GetNode ()->GetDevice (0));
  netDevice->SetMTUSpreadingFactor(LoRaWAN::m_supportedDataRates [dataRateIndex].spreadingFactor);

  m_txTrace (p);
  m_socket->Send (p);

  NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds ()
                << "s LoRaWANGatewayApplication application on node #"
                << GetNode()->GetId()
                << " sent a downstream packet of size "
                <<  p->GetSize ());
}

// Application Methods
void LoRaWANGatewayApplication::StartApplication () // Called at time specified by Start
{
  NS_LOG_FUNCTION (this);

  // Create the socket if not already
  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), TypeId::LookupByName ("ns3::PacketSocketFactory"));
      m_socket->Bind ();

      PacketSocketAddress socketAddress;
      socketAddress.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ()); // Set the address to match only a specified NetDevice...
      m_socket->Connect (Address (socketAddress)); // packet-socket documentation mentions: "Send: send the input packet to the underlying NetDevices with the default destination address. The socket must be bound and connected."

      m_socket->Listen ();
      //m_socket->SetAllowBroadcast (true); // TODO: does not work on packet socket?
      m_socket->SetRecvCallback (MakeCallback (&LoRaWANGatewayApplication::HandleRead, this));

    }

  // instruct Network Server to populate end devices data structure:
  // NOTE that we call PopulateEndDevices in StartApplication and not in DoInitialize as the attributes for the NetworkServer object have not yet been set at the of DoInitialize()
  this->m_lorawanNSPtr->PopulateEndDevices ();
}

void LoRaWANGatewayApplication::StopApplication () // Called at time specified by Stop
{
  NS_LOG_FUNCTION (this);

  if(m_socket != 0)
    {
      m_socket->Close ();
    }
  else
    {
      NS_LOG_WARN ("LoRaWANGatewayApplication found null socket to close in StopApplication");
    }
}

void LoRaWANGatewayApplication::HandleRead (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom (from)))
    {
      if (packet->GetSize () == 0)
        { //EOF
          break;
        }
      m_totalRx += packet->GetSize ();

      if (PacketSocketAddress::IsMatchingType (from))
        {
          NS_LOG_INFO ("At time " << Simulator::Now ().GetSeconds ()
                       << "s gateway on node #"
                       << GetNode()->GetId()
                       <<  " received " << packet->GetSize () << " bytes from "
                       << PacketSocketAddress::ConvertFrom(from).GetPhysicalAddress ()
                       << ", total Rx " << m_totalRx << " bytes");

          this->m_lorawanNSPtr->HandleUSPacket (this, from, packet);
        }
      else
        {
          NS_LOG_WARN (this << " Unexpected address type");
        }
    }
}

void LoRaWANGatewayApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  m_connected = true;
}

void LoRaWANGatewayApplication::ConnectionFailed (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
}


////////////////////////////////////////////////////////////
//Modifications added by Joe to enable Class B

// timestamp is the number of seconds in GPS time % 2^32
void 
LoRaWANGatewayApplication::SendBeacon (Ptr<Packet> packet)
{
  //extract payload from packet
  uint8_t beacon[17];
  packet->CopyData(beacon, 17);

  // Get location from mobility model
  // the mobility model has a getPosition function.
  // get the current node attached to this gateway application, then get the mobility model attached to that node, then call getPosition
  // the position returned is in the format (x,y,z), in Carthesian coordinates (all doubles). 24 byte words for GPS latitude and longitude are expected in real LoRaWAN beacons
  // but as we are not planning to currently use the location data we will just use the first (LSB) 24 bytes of the position given in the mobility model 
  Vector position;
  Ptr<MobilityModel> mobility = GetNode()->GetObject<MobilityModel>();
  if (mobility){
    position = mobility->GetPosition();
  }
  else
  {
    position = Vector(0,0,0);
  }
  
  // GwSpecific gives GPS coordinates of the gateway
  // first byte: 0, 1, 2 specify GPS coordinates of 1st, 2nd, 3rd antennas respectively 
  // 3:127 are RFU
  // 128:255 are reserved for custom network specific broadcasts (TODO: we can use this?)
  // for now in our simulations we can assume first byte is 0.
  // the other 6 bytes encode the latitude and longitude of the antenna in a two's complement 24 bit word.
  beacon[8] = 0x00;
  
  // convert a 64-bit double to an unsigned 32-bit int. Then put the lowest 24 bits into the buffer. Works fine up to 2^32
  // TODO: note that this is not related to the proper LoRaWAN method.
  //lat
  double lat_d = position[0]; //TODO: probably accessing this wrong
  lat_d += 6755399441055744.0;
  uint32_t lat = reinterpret_cast<uint32_t&>(lat_d);
  beacon[9] = (lat >> 0);
  beacon[10] = (lat >> 8);
  beacon[11] = (lat >> 16);

  //long
  double longi_d = position[1]; //TODO: probably accessing this wrong
  longi_d += 6755399441055744.0;
  uint32_t longi = reinterpret_cast<uint32_t&>(lat_d);
  beacon[12] = (longi >> 0);
  beacon[13] = (longi >> 8);
  beacon[14] = (longi >> 16);

  //then another CRC check
  // we're not actually implementing the CRC check, but if we were this is where it would be done.
  beacon[15] = 0x00;
  beacon[16] = 0x00;

  //build a new packet wuth the modified data
  //an alternative to this would be to use PeekData to get a pointer to the data buffer, but this is less messy
  Packet p =  Create<Packet> (beacon, 17);

  //add the tags to the packet
  //note that there is no frame header in beacons

  //TODO: magic numbers
  uint8_t beaconChannelIndex = 7; // 869.525MHz. Mandetory for Class B beacons. TODO: this is currently set as a high power channel in the phy layer implementation. Is this correct?
  // Note: this also appears to be the only channel outside of the main subband? 
  uint8_t beaconDataRateIndex = 3;  // SF9, 125kHz BW. Mandetory for Class B beacons.
  uint8_t beaconCodeRate = 3; //TODO: double check this.
  uint8_t beaconPreambleLength = 10;

  LoRaWANPhyParamsTag phyParamsTag;
  phyParamsTag.SetChannelIndex (beaconChannelIndex);
  phyParamsTag.SetDataRateIndex (beaconDataRateIndex);
  phyParamsTag.SetCodeRate (beaconCodeRate);
  phyParamsTag.SetPreambleLength (beaconPreambleLength);
  p->AddPacketTag (phyParamsTag);

  // Set Msg type
  LoRaWANMsgTypeTag msgTypeTag;
  msgTypeTag.SetMsgType (LORAWAN_BEACON);
  p->AddPacketTag (msgTypeTag);


  //LoRaWANDataRequestParams  
  // MAC layer stuff:
  // beacon frame MUST have a longer preamble
  // beacon frame MUST be transmitted in radio packet implicit mode (no LoRa physical header, no CRC appended by the radio)
  // broadcast the frame
  // use set DR, CR, and channel (maybe given from NS?)
  // don't schedule any rx1 or rx2 check


  // then pass this beacon to the Net Device
  this->SendDSPacket (p);
}




////////////////////////////////////////////////////////////

} // Namespace ns3


