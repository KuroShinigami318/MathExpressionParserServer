#pragma once
#include "ISession.h"
#include "asio/ip/tcp.hpp"

class TCPSession : public ISession
{
public:
   TCPSession(asio::ip::tcp::socket&& i_socket);
   ~TCPSession();
   const asio::any_io_executor& GetExecuter() override;
   void Start() override;
   void Stop() override;
   void SendResult(const char* i_data, size_t i_size) override;

private:
   void DoRead();
   void DoWrite(const char* i_data, size_t i_size);

private:
   asio::ip::tcp::socket m_socket;
   enum { max_length = 1024 };
   char data_[max_length]{};
};