#pragma once
#include "IServer.h"
#include "asio/ip/basic_endpoint.hpp"
#include "ConstantTypes/ActionTypesFwd.h"

namespace asio
{
class any_io_executor;
template <typename Protocol, typename Executor>
class basic_stream_socket;
} // namespace asio

class ISession;
class IAction;

template <typename Protocol>
class ServerEP : public IServer
{
protected:
   struct SessionHolder
   {
      SessionHolder(utils::unique_ref<ISession> i_session)
         : session(std::move(i_session))
      {
      }
      utils::unique_ref<ISession> session;
      std::vector<utils::Connection> connections;
   };

public:
   virtual utils::unique_ref<ISession> CreateSession(asio::basic_stream_socket<Protocol, asio::any_io_executor>&& i_socket) = 0;
   bool AcceptSocket(asio::basic_stream_socket<Protocol, asio::any_io_executor>&& i_socket);

protected:
   virtual void OnSessionClosedAsync(asio::ip::basic_endpoint<Protocol> i_endpoint);
   virtual void OnSessionClosed(asio::ip::basic_endpoint<Protocol> i_endpoint);
   virtual void OnBytesReceived(asio::ip::basic_endpoint<Protocol> i_endpoint, const char* i_data, size_t i_size);
   virtual void OnActionFinished(Constants::ActionTypes i_actionType, ISession& i_session, const std::string& i_result);

protected:
   std::unordered_map<asio::ip::basic_endpoint<Protocol>, SessionHolder> m_sessions;
   std::unordered_map<Constants::ActionTypes, utils::unique_ref<IAction>> m_actions;
};