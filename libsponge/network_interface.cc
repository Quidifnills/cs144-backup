#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    
    // Check if we already know the Ethernet address for this IP
    auto arp_it = _arp_table.find(next_hop_ip);
    if (arp_it != _arp_table.end()) {
        // We know the Ethernet address, send the frame immediately
        EthernetFrame frame;
        frame.header().dst = arp_it->second.eth_addr;
        frame.header().src = _ethernet_address;
        frame.header().type = EthernetHeader::TYPE_IPv4;
        frame.payload() = dgram.serialize();
        _frames_out.push(frame);
        return;
    }
    
    // We don't know the Ethernet address, need to send ARP request
    // But first check if we recently sent an ARP request for this IP
    auto arp_req_it = _arp_request_time.find(next_hop_ip);
    bool should_send_arp = true;
    
    if (arp_req_it != _arp_request_time.end()) {
        // We sent an ARP request for this IP before, check if cooldown has passed
        if (arp_req_it->second > 0) {
            should_send_arp = false;  // Still in cooldown period
        }
    }
    
    if (should_send_arp) {
        // Send ARP request
        ARPMessage arp_request;
        arp_request.opcode = ARPMessage::OPCODE_REQUEST;
        arp_request.sender_ethernet_address = _ethernet_address;
        arp_request.sender_ip_address = _ip_address.ipv4_numeric();
        arp_request.target_ethernet_address = {};  // Unknown, that's what we're asking for
        arp_request.target_ip_address = next_hop_ip;
        
        EthernetFrame arp_frame;
        arp_frame.header().dst = ETHERNET_BROADCAST;
        arp_frame.header().src = _ethernet_address;
        arp_frame.header().type = EthernetHeader::TYPE_ARP;
        arp_frame.payload() = arp_request.serialize();
        _frames_out.push(arp_frame);
        
        // Record that we sent an ARP request for this IP
        _arp_request_time[next_hop_ip] = ARP_COOLDOWN_MS;
    }
    
    // Queue the datagram for later transmission
    _waiting_datagrams.emplace_back(dgram, next_hop);
    
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // Discard the packet
    if(frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST) { 
        return nullopt;
    }
    // IPv4
    if(frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if(dgram.parse(frame.payload()) == ParseResult::NoError) {
            return dgram;
        }
        return nullopt;
    }
    
    if(frame.header().type == EthernetHeader::TYPE_ARP){
        // Handle ARP frames
        ARPMessage arp_msg;
        if (arp_msg.parse(frame.payload()) != ParseResult::NoError) {
            return nullopt;  // Parse error
        }
        
        // Learn the mapping from the sender (both for requests and replies)
        const uint32_t sender_ip = arp_msg.sender_ip_address;
        const EthernetAddress sender_eth = arp_msg.sender_ethernet_address;
        
        // Add/update ARP table entry
        _arp_table[sender_ip] = {sender_eth, ARP_TTL_MS};
        // DO NOT LEARN THE TARGET 
        // const uint32_t target_ip = arp_msg.target_ip_address;
        // const EthernetAddress target_eth = arp_msg.target_ethernet_address;
        // _arp_table[target_ip] = {target_eth, ARP_TTL_MS};

        // Check if we have any queued datagrams for this IP
        auto it = _waiting_datagrams.begin();
        while (it != _waiting_datagrams.end()) {
            if (it->next_hop.ipv4_numeric() == sender_ip) {
                // Send the queued datagram
                EthernetFrame queued_frame;
                queued_frame.header().dst = sender_eth;
                queued_frame.header().src = _ethernet_address;
                queued_frame.header().type = EthernetHeader::TYPE_IPv4;
                queued_frame.payload() = it->datagram.serialize();
                _frames_out.push(queued_frame);
                
                // Remove from queue
                it = _waiting_datagrams.erase(it);
            } else {
                ++it;
            }
        }
    
        // If this is an ARP request for our IP, send a reply
        if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST && 
            arp_msg.target_ip_address == _ip_address.ipv4_numeric()) {
            
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = arp_msg.sender_ethernet_address;
            arp_reply.target_ip_address = arp_msg.sender_ip_address;
            
            EthernetFrame reply_frame;
            reply_frame.header().dst = arp_msg.sender_ethernet_address;
            reply_frame.header().src = _ethernet_address;
            reply_frame.header().type = EthernetHeader::TYPE_ARP;
            reply_frame.payload() = arp_reply.serialize();
            _frames_out.push(reply_frame);
        }

            return nullopt;
        }

    return nullopt;
}
//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { 
    // Update ARP table TTLs and remove expired entries
    auto arp_it = _arp_table.begin();
    while (arp_it != _arp_table.end()) {
        if (arp_it->second.ttl <= ms_since_last_tick) {
            // Entry expired, remove it
            arp_it = _arp_table.erase(arp_it);
        } else {
            // Update TTL
            arp_it->second.ttl -= ms_since_last_tick;
            ++arp_it;
        }
    }
    
    // Update ARP request cooldowns
    auto req_it = _arp_request_time.begin();
    while (req_it != _arp_request_time.end()) {
        if (req_it->second <= ms_since_last_tick) {
            // Cooldown expired, remove entry
            req_it = _arp_request_time.erase(req_it);
        } else {
            // Update cooldown
            req_it->second -= ms_since_last_tick;
            ++req_it;
        }
    }
}
