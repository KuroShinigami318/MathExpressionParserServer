#pragma once

#if defined(_DEBUG)
#define DEBUG_ONLY(code) code
#define RELEASE_ONLY(code)
#define RELEASE_DEV_ONLY(code)
#define NOT_RELEASE(code) code
#elif defined(_RELEASE_DEV)
#define DEBUG_ONLY(code)
#define RELEASE_ONLY(code)
#define RELEASE_DEV_ONLY(code) code
#define NOT_RELEASE(code) code
#elif defined(_RELEASE)
#define DEBUG_ONLY(code)
#define RELEASE_ONLY(code) code
#define RELEASE_DEV_ONLY(code)
#define NOT_RELEASE(code)
#undef ASSERT
#define ASSERT(x, ...)
#undef ASSERT_PLAIN_MSG
#define ASSERT_PLAIN_MSG(x, msg, ...)
#endif