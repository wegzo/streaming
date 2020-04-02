#include "assert.h"
#include <iostream>
#include <sstream>
#include <atlbase.h>
#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

std::atomic_bool streaming::async_callback_error = false;
std::mutex streaming::async_callback_error_mutex;

void maybe_assert(bool expr)
{
    expr;
    /*assert_(expr);*/
}

streaming::exception::exception(HRESULT hr, int line_number, const char* filename) :
    hr(hr)
{
    assert_(false);

    std::ostringstream sts;
    report_error(sts, this->hr, line_number, filename);
    this->error_str = std::move(sts.str());
}

void streaming::report_error(std::ostream& stream, HRESULT hr, int line_number, const char* filename)
{
    stream << "HRESULT: " << "0x" << std::hex << hr << std::dec
        << " at line " << line_number << ", " << filename << std::endl;
}

void streaming::check_for_errors()
{
    if(async_callback_error)
    {
        async_callback_error_mutex.lock();
        async_callback_error_mutex.unlock();
        /*scoped_lock(::async_callback_error_mutex);*/
    }
}

void streaming::print_error_and_abort(const char* str)
{
    typedef std::lock_guard<std::mutex> scoped_lock;
    scoped_lock lock(async_callback_error_mutex);
    async_callback_error = true;

    std::cout << str;
    std::terminate();
}

HRESULT streaming::write_dump_file(
    const std::wstring_view& file, 
    LPEXCEPTION_POINTERS exception_pointers)
{
    static std::mutex dbghelp_mutex;
    std::lock_guard<std::mutex> lock(dbghelp_mutex);

    CHandle file_handle(CreateFile(
        file.data(),
        GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, 
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));

    // TODO: this should be ran in another process

    MINIDUMP_EXCEPTION_INFORMATION excp_info = {0};
    excp_info.ThreadId = GetCurrentThreadId();
    excp_info.ExceptionPointers = exception_pointers;
    excp_info.ClientPointers = TRUE;

    if(MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file_handle, MiniDumpWithFullMemory,
        exception_pointers ? &excp_info : nullptr,
        nullptr, nullptr) != TRUE)
        // it will be a hresult value
        return GetLastError();
    else
        return S_OK;
}