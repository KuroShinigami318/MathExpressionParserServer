#include "ServerStdafx.h"
#include "Actions/MathExpressionParserAction.h"
#include "ConstantTypes/ActionResultTypes.h"
#include "Session/ISession.h"
#include "asio/any_io_executor.hpp"
#include "asio/post.hpp"

namespace
{
enum class OperatorType
{
   OpenParenthesis,
   CloseParenthesis,
   Add,
   Subtract,
   Multiply,
   Divide,
};

enum class PriorityType
{
   Low,
   Normal,
   High
};

enum class ErrorCodeType
{
   InvalidCharacter,
   MismatchedParentheses,
   DivisionByZero,
   InvalidExpression,
};

using ErrorType = utils::Error<ErrorCodeType>;

int GetOperatorPrecedence(OperatorType i_operator)
{
   switch (i_operator)
   {
   case OperatorType::OpenParenthesis:
   case OperatorType::CloseParenthesis:
      return 0;
   case OperatorType::Add:
   case OperatorType::Subtract:
      return 1;
   case OperatorType::Multiply:
   case OperatorType::Divide:
      return 2;
   default:
      return -1; // Invalid operator
   }
}

bool IsOperator(char c, OperatorType& o_operator)
{
   switch (c)
   {
   case '+':
      o_operator = OperatorType::Add;
      return true;
   case '-':
      o_operator = OperatorType::Subtract;
      return true;
   case '*':
      o_operator = OperatorType::Multiply;
      return true;
   case '/':
      o_operator = OperatorType::Divide;
      return true;
   case '(':
      o_operator = OperatorType::OpenParenthesis;
      return true;
   case ')':
      o_operator = OperatorType::CloseParenthesis;
      return true;
   default:
      return false;
   }
}

bool IsWhitespace(char c)
{
   return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

template <typename IntegralType> requires std::is_integral_v<IntegralType>
utils::Result<IntegralType, ErrorType> ApplyOperator(IntegralType a, IntegralType b, OperatorType i_operator)
{
   switch (i_operator)
   {
   case OperatorType::Add:
      return a + b;
   case OperatorType::Subtract:
      return a - b;
   case OperatorType::Multiply:
      return a * b;
   case OperatorType::Divide:
      if (b == 0)
      {
         return make_error<ErrorType>(ErrorCodeType::DivisionByZero, "Division by zero");
      }
      return a / b;
   default:
      return make_error<ErrorType>(ErrorCodeType::InvalidCharacter, "Invalid operator: {}", i_operator);
   }
}

template <typename IntegralType> requires std::is_integral_v<IntegralType>
utils::Result<IntegralType, ErrorType> ApplyOperator(std::list<IntegralType>& i_nums, OperatorType i_operator)
{
   if (i_nums.size() < 2)
   {
      return make_error<ErrorType>(ErrorCodeType::InvalidExpression, "Insufficient operands for operator");
   }
   IntegralType b = i_nums.back();
   i_nums.pop_back();
   IntegralType a = i_nums.back();
   i_nums.pop_back();
   auto applyResult = ApplyOperator(a, b, i_operator);
   if (applyResult.isErr())
   {
      return applyResult;
   }
   return i_nums.emplace_back(applyResult.unwrap());
}

template <typename IntegralType> requires std::is_integral_v<IntegralType>
utils::Result<IntegralType, ErrorType> ApplyOperators(std::list<IntegralType>& i_nums, std::list<OperatorType>& i_operators, size_t i_pendingBrackets)
{
   while (!i_operators.empty())
   {
      OperatorType currentOperator = i_operators.back();
      IntegralType rightOperand{};
      if (GetOperatorPrecedence(i_operators.back()) >= static_cast<int>(PriorityType::Normal))
      {
         i_operators.pop_back();
         if (i_nums.size() < 2)
         {
            return make_error<ErrorType>(ErrorCodeType::InvalidExpression, "Insufficient operands for operator");
         }
         rightOperand = i_nums.back();
         i_nums.pop_back();
         if (i_operators.empty() || i_operators.back() != OperatorType::CloseParenthesis)
         {
            IntegralType leftOperand = i_nums.back();
            i_nums.pop_back();
            auto applyResult = ApplyOperator(leftOperand, rightOperand, currentOperator);
            if (applyResult.isErr())
            {
               return applyResult;
            }
            i_nums.push_back(applyResult.unwrap());
         }
      }
      if (!i_operators.empty() && i_operators.back() == OperatorType::CloseParenthesis)
      {
         i_operators.pop_back();
         auto applyResult = ApplyOperators(i_nums, i_operators, i_pendingBrackets + 1);
         if (applyResult.isErr())
         {
            return applyResult;
         }
         if (currentOperator != OperatorType::CloseParenthesis)
         {
            i_nums.push_back(rightOperand);
         }
      }
      else if (!i_operators.empty() && i_operators.back() == OperatorType::OpenParenthesis)
      {
         if (i_pendingBrackets == 0)
         {
            return make_error<ErrorType>(ErrorCodeType::MismatchedParentheses, "Mismatched parentheses");
         }
         i_operators.pop_back();
         break;
      }
   }

   if (i_nums.empty())
   {
      return make_error<ErrorType>(ErrorCodeType::InvalidExpression, "Insufficient operands for operator");
   }
   return i_nums.back();
}

constexpr size_t _max_size = 1024;
}

namespace math_expression_parser
{
struct ActionData : public IData
{
   std::list<MathExpressionData> mathExpressionDataList;
   std::string pendingMathExpression;
   std::atomic_size_t pendingProcessingCount{ 0 };
};

struct MathExpressionData
{
   ActionData& actionDataRef;
   std::atomic_bool isValid{ true };
   std::atomic_bool isCancelled{ false };
   std::list<long long> numsStack;
   std::list<OperatorType> operatorsStack;
   std::string mathExpression;
   utils::async_waitable<void> waitable;

   MathExpressionData(ActionData& i_actionDataRef)
      : actionDataRef(i_actionDataRef)
   {
   }
};
}

using namespace math_expression_parser;

MathExpressionParserAction::MathExpressionParserAction()
   : m_workerThreadPool(utils::threadpool_config{})
{
}

ActionResultTypeFwd MathExpressionParserAction::ProcessRawData(ISession& i_session, const char* i_data, size_t i_size)
{
   if (m_sessionDataMap.find(&i_session) == m_sessionDataMap.end())
   {
      return make_error<ActionErrorTypeFwd>(Constants::ActionErrorCode::SessionNotRegistered, "Session is not registered.");
   }

   if (i_size < 2)
   {
      return make_error<ActionErrorTypeFwd>(Constants::ActionErrorCode::InvalidData, "Data size is too small.");
   }

   auto& sessionData = m_sessionDataMap[&i_session];
   if (sessionData.data == nullptr)
   {
      sessionData.data = std::make_unique<ActionData>();
      sessionData.pendingProcessing++;
   }

   auto& actionData = static_cast<ActionData&>(*sessionData.data);
   size_t lastCharIndex = i_size - 1;
   while (lastCharIndex > 0 && IsWhitespace(i_data[lastCharIndex]))
   {
      lastCharIndex--;
   }
   actionData.pendingMathExpression.append(i_data, i_size);
   if (i_data[i_size - 1] == '\n')
   {
      PostExpressionToPool(i_session, actionData, actionData.pendingMathExpression, actionData.pendingMathExpression.begin(), actionData.pendingMathExpression.end());
   }
   if (actionData.pendingMathExpression.size() >= _max_size)
   {
      auto foundLowOperatorIt = std::find_if(actionData.pendingMathExpression.rbegin(), actionData.pendingMathExpression.rend(),
         [](char c)
         {
            OperatorType _operator;
            return IsOperator(c, _operator) && GetOperatorPrecedence(_operator) <= static_cast<int>(PriorityType::Normal);
         });
      if (foundLowOperatorIt != actionData.pendingMathExpression.rend())
      {
         PostExpressionToPool(i_session, actionData, actionData.pendingMathExpression, actionData.pendingMathExpression.begin(), foundLowOperatorIt.base());
      }
   }

   return utils::Ok();
}

ActionResultTypeFwd MathExpressionParserAction::UnregisterSession(ISession& i_session)
{
   if (auto foundIt = m_sessionDataMap.find(&i_session); foundIt != m_sessionDataMap.end())
   {
      if (foundIt->second.data)
      {
         ActionData& actionData = static_cast<ActionData&>(*foundIt->second.data);
         for (MathExpressionData& mathExpressionData : actionData.mathExpressionDataList)
         {
            mathExpressionData.waitable.Cancel();
            mathExpressionData.waitable.Wait();
         }
      }
      else if (foundIt->second.pendingProcessing > 0)
      {
         m_staleSession.emplace(&i_session);
      }
   }

   return ActionEP::UnregisterSession(i_session);
}

void MathExpressionParserAction::PostExpressionToPool(ISession& i_session, ActionData& i_actionData, std::string& o_expression, std::string::iterator i_begin, std::string::iterator i_end)
{
   i_actionData.pendingProcessingCount.fetch_add(1, std::memory_order_relaxed);
   MathExpressionData& mathExpressionData = i_actionData.mathExpressionDataList.emplace_back(i_actionData);
   mathExpressionData.mathExpression.assign(i_begin, i_end);
   mathExpressionData.waitable = utils::async(utils::CallableBound<utils::MessageHandleStatus()>
   {
      [&mathExpressionData]() -> utils::MessageHandleStatus
      {
         mathExpressionData.isCancelled.store(true, std::memory_order_relaxed);
         return utils::MessageHandleStatus::SUCCESS;
      }
   }, m_workerThreadPool, &MathExpressionParserAction::ParseMathExpression, this, i_session, &mathExpressionData);
   o_expression.erase(i_begin, i_end);
}

void MathExpressionParserAction::ParseMathExpression(ISession& i_session, MathExpressionData* i_mathExpressionData)
{
   utils::Epilogue cleanup([this, &i_session, i_mathExpressionData]()
   {
      if (i_mathExpressionData->isCancelled.load(std::memory_order_relaxed) == true)
      {
         return;
      }
      if (i_mathExpressionData->actionDataRef.pendingProcessingCount.fetch_sub(1, std::memory_order_seq_cst) == 1)
      {
         FinalizeParseMathExpression(i_session, std::move(m_sessionDataMap[&i_session].data));
      }
   });
   const std::string& mathExpression = i_mathExpressionData->mathExpression;
   std::list<long long>& numsStack = i_mathExpressionData->numsStack;
   std::list<OperatorType>& operatorsStack = i_mathExpressionData->operatorsStack;
   OperatorType _operator;
   for (size_t index = 0; index < mathExpression.size(); index++)
   {
      if (i_mathExpressionData->isCancelled.load(std::memory_order_relaxed) == true)
      {
         return;
      }

      if (IsWhitespace(mathExpression[index]))
      {
         continue;
      }

      if (mathExpression[index] == '(')
      {
         operatorsStack.push_back(OperatorType::OpenParenthesis);
      }
      else if (mathExpression[index] == ')')
      {
         while (!operatorsStack.empty() && GetOperatorPrecedence(operatorsStack.back()) >= static_cast<int>(PriorityType::Normal))
         {
            auto applyResult = ApplyOperator(numsStack, operatorsStack.back());
            if (applyResult.isErr())
            {
               i_mathExpressionData->isValid.store(false, std::memory_order_relaxed);
               return;
            }
            operatorsStack.pop_back();
         }
         if (!operatorsStack.empty() && operatorsStack.back() == OperatorType::OpenParenthesis)
         {
            operatorsStack.pop_back();
         }
         else
         {
            operatorsStack.push_back(OperatorType::CloseParenthesis);
         }
      }
      else if (IsOperator(mathExpression[index], _operator))
      {
         while (!operatorsStack.empty() && GetOperatorPrecedence(operatorsStack.back()) >= GetOperatorPrecedence(_operator))
         {
            auto applyResult = ApplyOperator(numsStack, operatorsStack.back());
            if (applyResult.isErr())
            {
               i_mathExpressionData->isValid.store(false, std::memory_order_relaxed);
               return;
            }
            operatorsStack.pop_back();
         }
         operatorsStack.push_back(_operator);
      }
      else
      {
         long long number = 0;
         while (index < mathExpression.size() && isalnum(mathExpression[index]))
         {
            number = number * 10 + mathExpression[index++] - '0';
         }
         if (index < mathExpression.size() && !IsWhitespace(mathExpression[index]) && !IsOperator(mathExpression[index], _operator))
         {
            i_mathExpressionData->isValid.store(false, std::memory_order_relaxed);
            return;
         }
         --index;
         numsStack.push_back(number);
      }
   }
}

void MathExpressionParserAction::FinalizeParseMathExpression(ISession& i_session, utils::unique_ref<IData> i_actionData)
{
   ActionData& actionData = static_cast<ActionData&>(*i_actionData);
   std::list<long long> numsStack;
   std::list<OperatorType> operatorsStack;
   for (auto& mathExpressionData : actionData.mathExpressionDataList)
   {
      if (!mathExpressionData.isValid.load(std::memory_order_relaxed))
      {
         asio::post(i_session.GetExecuter(), [this, &i_session]()
         {
            FinishAction(i_session, "Error: Invalid expression");
         });
         return;
      }
      numsStack.splice(numsStack.end(), mathExpressionData.numsStack);
      operatorsStack.splice(operatorsStack.end(), mathExpressionData.operatorsStack);
   }

   auto applyResult = ApplyOperators(numsStack, operatorsStack, 0);
   asio::post(i_session.GetExecuter(), [this, &i_session, applyResult = std::move(applyResult)]() mutable
   {
      if (applyResult.isErr())
      {
         FinishAction(i_session, "Error: " + applyResult.unwrapErr().What());
      }
      else
      {
         FinishAction(i_session, std::to_string(applyResult.unwrap()));
      }
   });
}
