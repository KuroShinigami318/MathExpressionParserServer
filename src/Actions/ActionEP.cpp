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

   m_sessionDataMap.try_emplace(&i_session, nullptr);

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