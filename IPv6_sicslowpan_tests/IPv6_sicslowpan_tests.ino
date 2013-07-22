/*
  Arduino IPv6 stack example
 
 This sketch connects two Arduino Mega with wireless shield and Xbee
 mounted. This sketch demonstrates use of the IPv6 stack library.
 
 Circuit:
 * Arduino Mega 2560
 * Wireless shield w/ Xbee Series 1
 
 created 29 June 2012
 by Alejandro Lampropulos
 Telecom Bretagne Rennes, France
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
 3. Neither the name of the Institute nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 SUCH DAMAGE.
 
 /*---------------------------------------------------------------------------------------------------------------------------------------------------------------
   This example test the interoperability of different Arduino Mega + Xbee S1 to send messages and respond them inverted.
   The example was tested within a scenario of 4 nodes (a root, an intermediate router node, and two leaves):
   
         O
        / \
       O   O
      /
     O
   
   In this case, the start sending multicast messages and respond to received messages with the data inverted. 
   If they get a prefix from a DIO, they change the prefix of the source ip address of the message received and they send it to that address, inverted.
   This allows to see the routing of the router nodes.
   In case the device is an intermediate router, it will not send broadcast messages but it will answer to received messages (only if it has already got a prefix)
   In case the device is a border router (root of the DoDAG), it will never send messages or answer to them, but it sets a hardcoded prefix used for the network.   
 ---------------------------------------------------------------------------------------------------------------------------------------------------------------*/

#include <IPv6Stack.h>
#include <XBeeMACLayer.h>
#include <string.h>
#include <stdlib.h>

#define UDP_PORT 61616//5683

#define IS_INTERMEDIATE_ROUTER  (UIP_CONF_ROUTER && 0)// FOR INTERMEDIATE ROUTERS: 1, FOR NODES: 0 -> UIP_CONF_ROUTER MUST BE 1

#define IS_BORDER_ROUTER (UIP_CONF_ROUTER && !IS_INTERMEDIATE_ROUTER)

enum CommandType{
  IP_PING,
  COAP_PING
};

//This function calculates the RAM memory left
int mem(){
  uint8_t * heapptr, * stackptr;
  stackptr = (uint8_t *)malloc(4);
  heapptr = stackptr;
  free(stackptr);               
  stackptr = (uint8_t *)(SP);
  return stackptr - heapptr;
}

//We need a specific object to implement the MACLayer interface (the virtual methods). In this case we use XBee but could be anyone capable of implementing that interface
XBeeMACLayer macLayer;

//This will be the destination IP address
IPv6Address addr_dest;

//This will be the ping destination IP address
IPv6Address ping_addr_dest;

#if IS_BORDER_ROUTER
  //If we are configured to be a router, this is our prefix. Up to now we hardcode it here. Note: The aaaa::/64 prefix is the only known by the 6loPAN context so it will achieve a better compression.
  IPv6Address prefix(0xbb, 0xbb, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
#endif

//We use a timer in order to send data if we do not have to respond to a message received.
IPv6Timer send_timer;

char udp_data[50];
int udp_data_length = 0;
IPv6Address sender_address;
uint16_t dest_port;

#define COMMAND_MAX_BYTES 100
char buffer_command[COMMAND_MAX_BYTES];
u16_t addr[8];

char coap_ping[4] = {0x40, 0x01, 0xbe, 0xbe};

void udp_callback(char *data, int datalen, int sender_port, IPv6Address &sender_addr){
  //take the message that should have the format: VERSION (2 bits) | TYPE (2 bits) | OC (4 bits) | CODE (8 bits) | MID (16 bits)
  //we put the VERSION to 1, TYPE to 3, the OC to 0 and the code to 0. We leave the same MID
  data[0] = 0x70;
  data[1] = 0;
  //now we send this to the sender. It is only 4 bytes
  IPv6Stack::udpSend(sender_addr, sender_port, data, 4);
  delay(50);
}

char getIpAddress(char* address, u16_t* addr){
  char* str;  
  int i = 0;
  SERIAL.println(address);
  while ((str = strtok_r(address, ":", &address)) != NULL){
    addr[i] = (u16_t) strtoul(str, NULL, 16);
    ++i;
  }
  if (i==8)
    return 1;
  return 0;
}

void send_coap_ping(IPv6Address& ping_addr_dest, u16_t port){
  IPv6Stack::udpSend(ping_addr_dest, port, coap_ping, 4);
  delay(50);
}

void hostInput()
{
    char* cmd;
    char* arg;
    int a = 0;
    u8_t arg_num = 0;
    u8_t hl = 0;
    u16_t port = 0;
    CommandType type;
    
    if(SERIAL.available()){
      a = SERIAL.readBytesUntil('\n', buffer_command, COMMAND_MAX_BYTES);
    }
    if (a != 0){
      buffer_command[a] = 0;
      SERIAL.println(buffer_command);  
      cmd = buffer_command;
      while ((arg = strtok_r(cmd, " ", &cmd)) != NULL){
        arg_num++;
        switch(arg_num){
          case 1:
            if(strncmp(arg,"ping6", 5) != 0){
              if(strncmp(arg,"coaping", 5) != 0){
                SERIAL.println("---");
                SERIAL.println("invalid command!");
                SERIAL.println("---");
                return;
              }else{
                type = COAP_PING; 
              }
            }else{
              type = IP_PING; 
            }
            break;
          case 2:
            if (getIpAddress(arg, addr)){
        
              ping_addr_dest.setAddress(addr[0], addr[1],addr[2],
                          addr[3],addr[4],addr[5],addr[6],addr[7]);
                
            }else{
              SERIAL.println("---");
              SERIAL.println("invalid address!");
              SERIAL.println("---");
              return;
            }
            break;
          case 3:
            if (type == IP_PING){
              hl = (u8_t) strtoul(arg, NULL, 10);
              break;
            }else{
              if (type == COAP_PING){
                //get the destination port
                port = (u16_t) strtoul(arg, NULL, 10);
                break;
              } 
            }
        }
        
      }
      if (arg_num >= 2){
        if (type == IP_PING){
          IPv6Stack::ping(ping_addr_dest, 4, hl);
          SERIAL.print("PING ");
          if (hl){
            SERIAL.print("(HL=");
            SERIAL.print(hl);
            SERIAL.print(") ");
          }        
          SERIAL.print("sent to address ");
          ping_addr_dest.print();
        }else{
          if (type == COAP_PING){
            if (port != 0){
              send_coap_ping(ping_addr_dest, port);
            }else{
               SERIAL.print("Port was not specified or incorrect");
            }
          }
        }
      }
    }
}

void establishContact() {
  while (SERIAL.available() <= 0) {
    SERIAL.println("0");   // send an initial string
    delay(300);
  }
  while(SERIAL.available()){
    SERIAL.read();
  }
}


void setup(){  
  SERIAL.begin(9600);
  delay(1000);
  //establishContact();
  SERIAL.println();
  SERIAL.print("MEMORY LEFT:");
  SERIAL.println(mem());
  delay(100);
  
  // init network-device
  if (!IPv6Stack::initMacLayer(&macLayer)){
    SERIAL.println("CANNOT INITIALIZE XBEE MODULE.. CANNOT CONTINUE");
    while (true){};
  }
  
  //init IP Stack
  IPv6Stack::initIpStack();  
  SERIAL.println("IPV6 INITIALIZED");
  delay(100);
  
  //init UDP
  IPv6Stack::initUdp(UDP_PORT);
  SERIAL.println("UDP INITIALIZED");
  delay(100);
  
  //If Border Router, set prefix. If not, set a timer to send data
  #if IS_BORDER_ROUTER      //This line is added to specify our prefix    
      IPv6Stack::setPrefix(prefix, 64); 
  #endif /*IS_BORDER_ROUTER*/
  
  SERIAL.println("SETUP FINISHED");
  delay(100);
}

void loop(){
  //Always need to poll timers in order to make the IPv6 Stack work
  IPv6Stack::pollTimers();  
  //If we are not a router (any kind), we also send messages
#if !UIP_CONF_ROUTER
  if (send_timer.expired()){
      send_timer.reset();

  }
#endif
  //We always check if we got anything. If we did, process that with the IPv6 Stack
  if (IPv6Stack::receivePacket()){
#if !IS_BORDER_ROUTER
      //If we are not configured as border router, check if udp data is available and run the callback with it
      if (IPv6Stack::udpDataAvailable()){
        udp_data_length = IPv6Stack::getUdpDataLength();
        IPv6Stack::getUdpData(udp_data);
        IPv6Stack::getUdpSenderIpAddress(sender_address);
        udp_callback(udp_data, udp_data_length, IPv6Stack::getUdpSenderPort(), sender_address);
      }
#endif
  }
  hostInput();  
  delay(10);
}

/*---------------------------------------------------------------------------*/
