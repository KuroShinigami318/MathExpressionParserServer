#pragma once

namespace Constants
{
DeclareScopedEnumWithOperatorDefined(ActionErrorCode, Constants, uint8_t,
   SessionNotRegistered,
   SessionAlreadyRegistered,
   InvalidData)
}