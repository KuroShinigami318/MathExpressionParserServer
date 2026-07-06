#pragma once

namespace Constants
{
enum class ActionErrorCode : uint8_t;
}

using ActionErrorTypeFwd = utils::Error<Constants::ActionErrorCode>;
using ActionResultTypeFwd = utils::Result<void, ActionErrorTypeFwd>;