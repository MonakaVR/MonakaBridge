#include "monaka_bridge/network.hpp"
#include <chrono>
#include <stdexcept>
#include <cstdio>
#ifdef _WIN32
#include <winsock2.h>
#include <objbase.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <random>
#endif
namespace mb {
std::int64_t monotonicNs(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string uuid(){
#ifdef _WIN32
 GUID g{};if(FAILED(CoCreateGuid(&g)))throw std::runtime_error("UUID failure");char b[37];snprintf(b,sizeof(b),"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",g.Data1,g.Data2,g.Data3,g.Data4[0],g.Data4[1],g.Data4[2],g.Data4[3],g.Data4[4],g.Data4[5],g.Data4[6],g.Data4[7]);return b;
#else
 std::random_device rng;const char* h="0123456789abcdef";std::string b;for(int i=0;i<36;++i)b+=(i==8||i==13||i==18||i==23)?'-':h[rng()%16];b[14]='4';b[19]='8';return b;
#endif
}
struct Udp::Impl{
#ifdef _WIN32
 SOCKET socket=INVALID_SOCKET;bool initialized=false;
 ~Impl(){if(socket!=INVALID_SOCKET)closesocket(socket);if(initialized)WSACleanup();}
#else
 int socket=-1;~Impl(){if(socket>=0)close(socket);}
#endif
 std::uint16_t port=0;
};
Udp::Udp(std::uint16_t port):impl_(std::make_unique<Impl>()){
#ifdef _WIN32
 WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w))throw std::runtime_error("WSAStartup");impl_->initialized=true;
#endif
 impl_->socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
#ifdef _WIN32
 if(impl_->socket==INVALID_SOCKET)throw std::runtime_error("UDP socket");BOOL exclusive=TRUE;u_long nonblocking=1;
 if(setsockopt(impl_->socket,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))||ioctlsocket(impl_->socket,FIONBIO,&nonblocking))throw std::runtime_error("exclusive/nonblocking UDP");
#else
 if(impl_->socket<0||fcntl(impl_->socket,F_SETFL,O_NONBLOCK)<0)throw std::runtime_error("nonblocking UDP");
#endif
 sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);local.sin_port=htons(port);
 if(bind(impl_->socket,reinterpret_cast<const sockaddr*>(&local),sizeof(local)))throw std::runtime_error("loopback port already owned/unavailable");
#ifdef _WIN32
 int len=sizeof(local);
#else
 socklen_t len=sizeof(local);
#endif
 if(getsockname(impl_->socket,reinterpret_cast<sockaddr*>(&local),&len))throw std::runtime_error("getsockname");impl_->port=ntohs(local.sin_port);
}
Udp::~Udp()=default;
std::uint16_t Udp::port()const{return impl_->port;}
bool Udp::send(std::uint16_t port,std::string_view bytes)noexcept{if(bytes.size()>4096||port==0)return false;sockaddr_in target{};target.sin_family=AF_INET;target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);target.sin_port=htons(port);return sendto(impl_->socket,bytes.data(),static_cast<int>(bytes.size()),0,reinterpret_cast<const sockaddr*>(&target),sizeof(target))==static_cast<int>(bytes.size());}
std::optional<Datagram> Udp::receive(){char b[4097];sockaddr_in peer{};
#ifdef _WIN32
 int len=sizeof(peer);
#else
 socklen_t len=sizeof(peer);
#endif
 auto count=recvfrom(impl_->socket,b,sizeof(b),0,reinterpret_cast<sockaddr*>(&peer),&len);
#ifdef _WIN32
 if(count==SOCKET_ERROR&&WSAGetLastError()==WSAEMSGSIZE)return Datagram{std::string(4097,'!'),"oversized"};
#endif
 if(count<0)return {};if(peer.sin_addr.s_addr!=htonl(INADDR_LOOPBACK))return {};
 return Datagram{std::string(b,static_cast<std::size_t>(count)),"127.0.0.1:"+std::to_string(ntohs(peer.sin_port))};
}
}
