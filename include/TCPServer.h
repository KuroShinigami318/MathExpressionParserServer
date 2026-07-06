#pragma once
#include "ServerEP.h"
#include "asio/ip/tcp.hpp"

struct ServerConfig;

class TCPServer : public ServerEP<asio::ip::tcp>
{
public:
   TCPServer(const ServerConfig& i_config);
   void Start() override;
   utils::unique_ref<ISession> CreateSession(asio::ip::tcp::socket&& i_socket) override;

private:
   asio::ip::tcp::acceptor m_acceptor;
   asio::ip::tcp::socket m_remoteSocket;
};