#include "ServerStdafx.h"
#include "Actions/ActionEP.h"
#include "ConstantTypes/ActionResultTypes.h"

IData::~IData() = default;

ActionEP::~ActionEP() = default;

ActionResultTypeFwd ActionEP::RegisterSession(ISession& i_session)
{
   if (m_sessionDataMap.find(&i_session) != m_sessionDataMap.end())
   {
      return make_error<ActionErrorTypeFwd>(Constants::ActionErrorCode::SessionAlreadyRegistered, "Session is already registered.");
   }

   m_sessionDataMap.try_emplace(&i_session);

   return utils::Ok();
}

ActionResultTypeFwd ActionEP::UnregisterSession(ISession& i_session)
{
   if (m_sessionDataMap.find(&i_session) == m_sessionDataMap.end())
   {
      return make_error<ActionErrorTypeFwd>(Constants::ActionErrorCode::SessionNotRegistered, "Session is not registered.");
   }

   m_sessionDataMap.erase(&i_session);

   return utils::Ok();
}

void ActionEP::FinishAction(ISession& i_session, const std::string& i_result)
{
   if (m_staleSession.find(&i_session) != m_staleSession.end())
   {
      m_staleSession.erase(&i_session);
      return;
   }
   utils::Access<SignalKey>(sig_onActionCompleted).Emit(i_session, i_result);
   size_t& pendingProcessing = m_sessionDataMap.at(&i_session).pendingProcessing;
   if (pendingProcessing > 0)
   {
      --pendingProcessing;
   }
}