#pragma once
#include "IAction.h"

class IData : public Interface
{
public:
   virtual ~IData() = 0;
};

class ActionEP : public IAction
{
protected:
   struct DataHolder
   {
      utils::unique_ptr<IData> data;
      size_t pendingProcessing{ 0 };
   };

public:
   virtual ~ActionEP() = 0;
   ActionResultTypeFwd RegisterSession(ISession& i_session) override;
   ActionResultTypeFwd UnregisterSession(ISession& i_session) override;

protected:
   void FinishAction(ISession& i_session, const std::string& i_result);

protected:
   std::unordered_map<ISession*, DataHolder> m_sessionDataMap;
   std::unordered_set<ISession*> m_staleSession;
};