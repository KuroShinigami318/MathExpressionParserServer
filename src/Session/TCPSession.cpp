#include "ServerStdafx.h"
#include "Session/TCPSession.h"
#include "asio/write.hpp"

TCPSession::TCPSession(asio::ip::tcp::socket&& i_socket)
   : m_socket(std::move(i_socket))
{
}

TCPSession::~TCPSession()
{
   Stop();
}

const asio::any_io_executor& TCPSession::GetExecuter()
{
   return m_socket.get_executor();
}

void TCPSession::Start()
{
   DoRead();
}

void TCPSession::Stop()
{
   if (m_socket.is_open())
   {
      try
      {
         m_socket.close();
      }
      catch (const std::exception& e)
      {
         std::cerr << "Error closing socket: " << e.what() << std::endl;
      }
   }
}

void TCPSession::SendResult(const char* i_data, size_t i_size)
{
   DoWrite(i_data, i_size);
}

void TCPSession::DoRead()
{
   m_socket.async_read_some(asio::buffer(data_, max_length),
      [this](std::error_code ec, std::size_t length)
      {
         if (!ec)
         {
            utils::Access<SignalKey>(sig_onBytesReceived).Emit(data_, length);
            DoRead();
         }
         else if (ec == asio::error::connection_reset || ec == asio::error::eof)
         {
            utils::Access<SignalKey>(sig_onSessionClosed).Emit();
         }
      });
}

void TCPSession::DoWrite(const char* i_data, size_t i_size)
{
   std::shared_ptr<std::string> data = std::make_shared<std::string>(i_data, i_size);
   asio::async_write(m_socket, asio::buffer(*data),
      [this, data](std::error_code ec, std::size_t /*length*/)
      {
      });
}
