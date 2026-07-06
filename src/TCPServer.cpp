#include "ServerStdafx.h"
#include "TCPServer.h"
#include "ServerEP.ipp"
#include "ServerConfig.h"
#include "Session/TCPSession.h"
#include "Actions/MathExpressionParserAction.h"

TCPServer::TCPServer(const ServerConfig& i_config)
   : m_acceptor(i_config.ioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), i_config.port))
   , m_remoteSocket(i_config.ioContext)
{
   std::cout << "TCP Server started on port " << i_config.port << std::endl;
   m_actions.try_emplace(Constants::ActionTypes::ParseMathExpression, utils::make_unique<MathExpressionParserAction>());
   Start();
}

void TCPServer::Start()
{
   m_acceptor.async_accept(m_remoteSocket, [this](std::error_code ec)
   {
      if (!ec)
      {
         AcceptSocket(std::move(m_remoteSocket));
      }
      else
      {
         std::cerr << "Error accepting connection: " << ec.message() << std::endl;
      }
      Start();
   });
}

utils::unique_ref<ISession> TCPServer::CreateSession(asio::ip::tcp::socket&& i_socket)
{
   return utils::make_unique<TCPSession>(std::move(i_socket));
}
