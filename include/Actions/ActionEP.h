#pragma once
#include "IAction.h"

class IData : public Interface
{
public:
   virtual ~IData() = 0;
};

class ActionEP : public IAction
{
public:
   virtual ~ActionEP() = 0;
   ActionResultTypeFwd RegisterSession(ISession& i_session) override;
   ActionResultTypeFwd UnregisterSession(ISession& i_session) override;

protected:
   void FinishAction(ISession& i_session, const std::string& i_result)
   {
      utils::Access<SignalKey>(sig_onActionCompleted).Emit(i_session, i_result);
   }

protected:
   std::unordered_map<ISession*, utils::unique_ptr<IData>> m_sessionDataMap;
};