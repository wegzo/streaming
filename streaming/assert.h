#pragma once

#include <exception>
#include <string>
#include <string_view>
#include <cassert>
#include <mutex>
#include <atomic>
#include <Windows.h>

#ifdef _DEBUG
#define assert_(_Expression) (void)( (!!(_Expression)) || (DebugBreak(), 0) )
#else
#define assert_(_Expression) ((void)0)
#endif

void maybe_assert(bool);

namespace streaming {

extern std::atomic_bool async_callback_error;
extern std::mutex async_callback_error_mutex;

void terminate_handler_f();

class exception : public std::exception
{
private:
    std::string error_str;
    HRESULT hr;
public:
    exception(HRESULT, int line_number, const char* filename);

    const char* what() const override { return this->error_str.c_str(); }
    HRESULT get_hresult() const { return this->hr; }
};

void report_error(std::ostream& stream, HRESULT, int line_number, const char* filename);
void check_for_errors();
void print_error_and_abort(const char*);

HRESULT write_dump_file(const std::wstring_view& file, LPEXCEPTION_POINTERS = nullptr);

}

#define HR_EXCEPTION(hr_) streaming::exception(hr_, __LINE__, BASE_FILE)
#define PRINT_ERROR_STREAM(hr_, stream_) { \
    stream_ << "EXCEPTION THROWN: "; \
    streaming::report_error(stream_, hr_, __LINE__, BASE_FILE); \
}
#define PRINT_ERROR(hr_) PRINT_ERROR_STREAM(hr_, std::cout)