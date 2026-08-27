#ifndef EMULATOR_SRC_KERNEL_FREEZE_PROBE_H_
#define EMULATOR_SRC_KERNEL_FREEZE_PROBE_H_

// TEMPORARY diagnostic probe for the GTA V cutscene->gameplay freeze.
// Captures which guest threads block on which kernel objects (semaphore,
// event flag, event queue, condition), at which guest caller PC, and logs
// EOP-interrupt delivery events. Disposes into logs/freeze-probe.log via a
// 2 s watchdog. Disposable: delete after the freeze investigation, never
// commit.

#include <cstdint>

namespace Libs::LibKernel {

class FreezeProbe {
public:
	// Call from a guest-facing wait wrapper, before the kernel wait: records
	// the guest return address (eboot RVA) of the thread about to block.
	static void SetCallerPc();

	// Call from a kernel wait implementation where the thread actually starts
	// blocking. kind: 'S' semaphore, 'V' event flag, 'E' event queue,
	// 'C' condition variable. Returns true when a record was created.
	static bool RecordBlock(char kind, uint64_t object, uintptr_t detail, bool infinite);

	// Call when the thread leaves a wait that was recorded via RecordBlock.
	static void RecordWake(char kind, uint64_t object, uintptr_t detail = 0);

	// Free-form event line (e.g. EOP interrupt delivery).
	static void Note(const char* fmt, ...);
};

} // namespace Libs::LibKernel

#endif // EMULATOR_SRC_KERNEL_FREEZE_PROBE_H_