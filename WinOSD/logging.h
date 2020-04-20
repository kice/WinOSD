#pragma once

#include <cstdarg>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

inline void dprint(const char *str)
{
    fprintf(stderr, str);

#ifdef _WIN32
    OutputDebugStringA(str);
    if (IsDebuggerPresent()) {
        // the debug output window of visual studio won't add a newline to each output
        OutputDebugStringA("\n");
    }

#endif
}

inline void dprintf(const char *format, ...)
{
    char buf[4096] = { 0 };
    va_list args;

    va_start(args, format);
    int written = _vsnprintf(buf, sizeof(buf) - 1, format, args);
    va_end(args);

    dprint(buf);
}

class TextStream
{
private:
    std::ostringstream _ss;
    std::function<void(std::string)> _f;

public:

    TextStream(std::function<void(const std::string &)> f) : _f(f)
    {}

    auto &stream() { return _ss; }

    ~TextStream()
    {
        _f(_ss.str());
    }
};

class LogStream
{
private:
    std::ostringstream _ss;
    std::function<void(std::string)> _f;

public:

    LogStream(const char *file, std::function<void(const std::string &)> f)
        : _f(f), _ss(file, std::ios_base::ate)
    {}

    auto &stream() { return _ss; }

    ~LogStream()
    {
        _f(_ss.str());
    }
};

class EmptyStream
{
public:
    template<typename T>
    inline EmptyStream &operator<<(T &&)
    {
        return *this;
    }
};

#define __LOGGING_STRINGIFY(x) #x
#define __LOGGING_TOSTRING(x) __LOGGING_STRINGIFY(x)
#define __LOGGING_FILE (__FILE__ "(" __LOGGING_TOSTRING(__LINE__) "): ")

#ifdef _DEBUG
#define DBG LogStream(__LOGGING_FILE, [](auto msg){ dprint(msg.c_str()); }).stream()
#else
#define DBG EmptyStream()
#endif

#ifdef _WIN32

namespace {
std::string ErrorDescription(HRESULT hr)
{
    if (FACILITY_WINDOWS == HRESULT_FACILITY(hr)) {
        hr = HRESULT_CODE(hr);
    }

    char *szErrMsg = nullptr;
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                       NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       (LPSTR)&szErrMsg, 0, NULL) == 0) {
        return std::string("Could not find a description for error code ") + std::to_string(hr);
    }
    std::string err = szErrMsg;
    LocalFree(szErrMsg);
    return err;
}
};

class hrresult_error : std::runtime_error
{
public:
    hrresult_error(HRESULT hr) : std::runtime_error(ErrorDescription(hr)) {}
};

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) {
#ifdef _DEBUG
        __debugbreak();
#endif // _DEBUG

        throw new hrresult_error(hr);
    }
}

#endif
