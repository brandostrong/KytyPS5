#include "kernel/freezeProbe.h"

#include "common/threads.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Libs::LibKernel {

namespace {

constexpr char const* PROBE_LOG_PATH     = "logs/freeze-probe.log";
constexpr int64_t     STALE_BLOCK_NS     = 5'000'000'000;      // report a block after 5 s
constexpr int64_t     WATCHDOG_PERIOD_NS = 2'000'000'000;      // watchdog period
constexpr uintptr_t   EBOOT_BASE         = 0x900000000ull;     // eboot base for RVA output

struct BlockRecord {
	int32_t   tid        = 0;
	uint64_t  object     = 0;
	uintptr_t detail     = 0;
	uintptr_t pc         = 0;
	char     kind        = 0;
	bool     infinite    = false;
	int64_t  entry_ns    = 0;
};

struct ProbeState {
	std::mutex           mutex;
	std::deque<BlockRecord> active_blocks;
	std::vector<std::string> lines;
	bool                started = false;
	std::thread         thread;
};

thread_local uintptr_t s_caller_pc = 0;

ProbeState& state() {
	static ProbeState s;
	return s;
}

int64_t now_ns() {
	static const auto t0 = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
	                                                            t0)
	           .count();
}

std::string format_line(const char* fmt, va_list ap) {
	char buf[512];
	std::vsnprintf(buf, sizeof(buf), fmt, ap);
	return std::string(buf);
}

void start_watchdog(ProbeState& s) {
	if (s.started) {
		return;
	}
	s.started = true;
	s.thread  = std::thread([&s]() {
		// Relative path first (emulator normally runs from the workspace root);
		// absolute fallback matches kyty-aio.bat's log directory.
		std::ofstream out;
		const char* const candidates[] = {PROBE_LOG_PATH, "G:/ps5-emu/logs/freeze-probe.log"};
		for (const auto* path: candidates) {
			out.open(path, std::ios::app);
			if (out) {
				break;
			}
		}
		if (!out) {
			return;
		}
		out << "# freezeProbe started (TEMPORARY diagnostic, do not commit)\n";
		out.flush();
		for (;;) {
			std::this_thread::sleep_for(std::chrono::nanoseconds(WATCHDOG_PERIOD_NS));
			std::lock_guard lock(s.mutex);
			const auto now   = now_ns();
			bool        emitted = false;
			for (const auto& b: s.active_blocks) {
				const int64_t waited = now - b.entry_ns;
				if (waited < STALE_BLOCK_NS) {
					continue;
				}
				emitted = true;
				out << "STALE tid=" << b.tid << " kind=" << b.kind
				    << " obj=0x" << std::hex << b.object << " detail=0x" << b.detail
				    << " pc=0x" << b.pc << " rva=0x" << (b.pc - EBOOT_BASE) << std::dec
				    << " inf=" << (b.infinite ? 1 : 0)
				    << " wait_ms=" << (waited / 1'000'000) << "\n";
			}
			for (const auto& line: s.lines) {
				out << line << "\n";
			}
			s.lines.clear();
			if (!emitted && s.lines.empty()) {
				out << "tick blocks_active=" << s.active_blocks.size() << "\n";
			}
			out.flush();
		}
	});
	s.thread.detach();
}

} // namespace

void FreezeProbe::SetCallerPc() {
	s_caller_pc = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
}

bool FreezeProbe::RecordBlock(char kind, uint64_t object, uintptr_t detail, bool infinite) {
	if (s_caller_pc == 0) {
		return false;
	}
	auto& s = state();
	std::lock_guard lock(s.mutex);
	start_watchdog(s);
	s.active_blocks.push_back(BlockRecord {
	    .tid       = static_cast<int32_t>(Common::Thread::GetThreadIdUnique()),
	    .object    = object,
	    .detail    = detail,
	    .pc        = s_caller_pc,
	    .kind      = kind,
	    .infinite  = infinite,
	    .entry_ns  = now_ns(),
	});
	return true;
}

void FreezeProbe::RecordWake(char kind, uint64_t object, uintptr_t detail) {
	auto& s = state();
	std::lock_guard lock(s.mutex);
	for (auto it = s.active_blocks.begin(); it != s.active_blocks.end(); ++it) {
		if (it->kind == kind && it->object == object) {
			const auto waited = now_ns() - it->entry_ns;
			char buf[256];
			std::snprintf(buf, sizeof(buf),
			             "WAKE tid=%d kind=%c obj=0x%llx detail=0x%llx wait_ms=%lld",
			             it->tid, kind, static_cast<unsigned long long>(object),
			             static_cast<unsigned long long>(detail),
			             static_cast<long long>(waited / 1'000'000));
			s.lines.push_back(buf);
			s.active_blocks.erase(it);
			return;
		}
	}
}

void FreezeProbe::Note(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	auto  line = format_line(fmt, ap);
	va_end(ap);

	auto& s = state();
	std::lock_guard lock(s.mutex);
	start_watchdog(s);
	s.lines.push_back(std::move(line));
}

} // namespace Libs::LibKernel