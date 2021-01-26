#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/format.hpp>

namespace log
{
	struct Logger {
		virtual ~Logger() {}

		virtual void putc(const char c) const = 0;
		virtual void flush() const {}
	};

	extern Logger* global_logger;

	enum class LoggerType { Early, Late };
	void select_logger(LoggerType type);
} // namespace log


template<typename... Args>
void print(const char* fmt, Args&&... args){
	static TicketLock printer_lock{};
	std::lock_guard guard{printer_lock};

	format::format_to(*log::global_logger, fmt, std::forward<Args>(args)...);
}