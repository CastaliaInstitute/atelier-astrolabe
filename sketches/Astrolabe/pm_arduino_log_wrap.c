#include <stdarg.h>

int log_printfv(const char *format, va_list arg);

__attribute__((weak)) int __wrap_log_printf(const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  const int ret = log_printfv(format, arg);
  va_end(arg);
  return ret;
}
