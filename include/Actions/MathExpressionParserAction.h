#pragma once
#include "ActionEP.h"

namespace math_expression_parser
{
struct MathExpressionData;
struct ActionData;
}

class MathExpressionParserAction : public ActionEP
{
public:
   MathExpressionParserAction();
   ActionResultTypeFwd ProcessRawData(ISession& i_session, const char* i_data, size_t i_size) override;

private:
   void PostExpressionToPool(ISession& i_session, math_expression_parser::ActionData& i_actionData, std::string& o_expression, std::string::iterator i_begin, std::string::iterator i_end);
   void ParseMathExpression(ISession& i_session, math_expression_parser::MathExpressionData* i_mathExpressionData);
   void FinalizeParseMathExpression(ISession& i_session, utils::unique_ref<IData> i_actionData);

private:
   utils::message_threadpool m_workerThreadPool;
};