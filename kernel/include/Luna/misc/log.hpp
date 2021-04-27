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
	extern TicketLock global_lock;

	enum class LoggerType { Early, Late };
	void select_logger(LoggerType type);
} // namespace log


template<typename... Args>
void print(const char* fmt, Args&&... args){
	std::lock_guard guard{log::global_lock};

	format::format_to(*log::global_logger, fmt, std::forward<Args>(args)...);
}