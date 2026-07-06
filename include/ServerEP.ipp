#pragma once
#include "asio/basic_stream_socket.hpp"
#include "Actions/IAction.h"
#include "ConstantTypes/ActionTypes.h"
#include "Session/ISession.h"
#include "MacrosUtils.h"

template <typename Protocol>
bool ServerEP<Protocol>::AcceptSocket(asio::basic_stream_socket<Protocol>&& i_socket)
{
   if (m_sessions.find(i_socket.remote_endpoint()) != m_sessions.end())
   {
      return false;
   }

   asio::ip::basic_endpoint<Protocol> remoteEndpoint = i_socket.remote_endpoint();
   auto [iter, inserted] = m_sessions.try_emplace(remoteEndpoint, CreateSession(std::move(i_socket)));
   if (!inserted)
   {
      return false;
   }

   iter->second.connections.emplace_back(iter->second.session->sig_onBytesReceived.Connect(&ServerEP<Protocol>::OnBytesReceived, this, remoteEndpoint));
   iter->second.connections.emplace_back(iter->second.session->sig_onSessionClosed.Connect(&ServerEP<Protocol>::OnSessionClosedAsync, this, remoteEndpoint));

   auto actionIter = m_actions.find(Constants::ActionTypes::ParseMathExpression);
   ASSERT_PLAIN_MSG(actionIter != m_actions.end(), "Action not found for {}", Constants::ActionTypes::ParseMathExpression);
   if (actionIter != m_actions.end())
   {
      actionIter->second->RegisterSession(*iter->second.session).assertSuccess();
      iter->second.connections.emplace_back(actionIter->second->sig_onActionCompleted.Connect(&ServerEP<Protocol>::OnActionFinished, this, Constants::ActionTypes::ParseMathExpression));
   }

   iter->second.session->Start();

   return true;
}

template<typename Protocol>
void ServerEP<Protocol>::OnSessionClosedAsync(asio::ip::basic_endpoint<Protocol> i_endpoint)
{
   asio::post(m_sessions.at(i_endpoint).session->GetExecuter(), utils::CallableBound{&ServerEP<Protocol>::OnSessionClosed, this, i_endpoint});
}

template<typename Protocol>
void ServerEP<Protocol>::OnSessionClosed(asio::ip::basic_endpoint<Protocol> i_endpoint)
{
   auto actionIter = m_actions.find(Constants::ActionTypes::ParseMathExpression);
   ASSERT_PLAIN_MSG(actionIter != m_actions.end(), "Action not found for {}", Constants::ActionTypes::ParseMathExpression);
   if (actionIter != m_actions.end())
   {
      actionIter->second->UnregisterSession(*m_sessions.at(i_endpoint).session).assertSuccess();
   }
   m_sessions.erase(i_endpoint);
}

template<typename Protocol>
void ServerEP<Protocol>::OnBytesReceived(asio::ip::basic_endpoint<Protocol> i_endpoint, const char* i_data, size_t i_size)
{
   auto actionIter = m_actions.find(Constants::ActionTypes::ParseMathExpression);
   ASSERT_PLAIN_MSG(actionIter != m_actions.end(), "Action not found for {}", Constants::ActionTypes::ParseMathExpression);
   if (actionIter == m_actions.end())
   {
      return;
   }

   if (ActionResultTypeFwd processedResult = actionIter->second->ProcessRawData(*m_sessions.at(i_endpoint).session, i_data, i_size); processedResult.isErr())
   {
      std::string errorMessage = processedResult.unwrapErr().What();
      m_sessions.at(i_endpoint).session->SendResult(errorMessage.c_str(), errorMessage.size());
   }
}

template<typename Protocol>
void ServerEP<Protocol>::OnActionFinished(Constants::ActionTypes i_actionType, ISession& i_session, const std::string& i_result)
{
   i_session.SendResult(i_result.c_str(), i_result.size());
}
