#pragma once

#if defined(_WIN32)
    #define FAY_OS_WINDOWS 1
#elif defined(__linux__)
    #define FAY_OS_LINUX 1
#else
    #error "Unsupported OS"
#endif

#ifndef FAY_OS_WINDOWS
    #define FAY_OS_WINDOWS 0
#endif
#ifndef FAY_OS_LINUX
    #define FAY_OS_LINUX 0
#endif

#define FAY_OS_POSIX (FAY_OS_LINUX )

#if defined(NDEBUG)
    #define FAY_DEBUG 0
    #define FAY_RELEASE 1
#else
    #define FAY_DEBUG 1
    #define FAY_RELEASE 0
#endif
