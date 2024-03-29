#pragma once

#include <Luna/common.hpp>
#include <Luna/misc/format.hpp>

namespace log
{
	struct Logger {
		virtual ~Logger() {}

		virtual void putc(const char c) = 0;
		virtual void puts(const char* str, size_t len) {
			for(size_t i = 0; i < len; i++)
				putc(str[i]);
		}

		virtual void flush() {}

		struct FormatIterator {
			Logger* log;

			void putc(const char c) { log->putc(c); }
			void puts(const char* str, size_t len) { log->puts(str, len); }
			void flush() { log->flush(); }
		};

		FormatIterator format_it() { return {this}; }
	};

	extern Logger* global_logger;
	extern IrqTicketLock global_lock;

	enum class LoggerType { Early, Late };
	void select_logger(LoggerType type);
} // namespace log


template<typename... Args>
void print(const char* fmt, Args&&... args){
	std::lock_guard guard{log::global_lock};

	format::format_to(log::global_logger->format_it(), fmt, std::forward<Args>(args)...);
	log::global_logger->flush();
}