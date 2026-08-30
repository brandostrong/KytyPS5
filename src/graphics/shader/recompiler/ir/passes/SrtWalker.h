#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTWALKER_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTWALKER_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"

#include <span>
#include <vector>

namespace Libs::Graphics::ShaderRecompiler::IR {

class Value;

using SrtMemoryReader = bool (*)(void* userdata, uint64_t address, uint32_t* value);

// One guest read performed while materializing, enough to replay and re-check it later.
struct SrtMemoryRead {
	SrtMemoryReader reader   = nullptr;
	void*           userdata = nullptr;
	uint64_t        address  = 0;
	uint32_t        value    = 0;
};

struct SrtRuntime {
	std::span<const uint32_t>   user_data;
	uint64_t                    shader_base                = 0;
	SrtMemoryReader             read_memory                = nullptr;
	void*                       userdata                   = nullptr;
	SrtMemoryReader             read_specialization_memory = nullptr;
	std::vector<SrtMemoryRead>* read_log                   = nullptr;
};

struct DescriptorSourceRequest {
	uint32_t source = 0;
};

// Collects reachable ReadConst values. Immediate offsets receive compact flat-buffer slots;
// dynamic offsets remain explicit and are never assigned a fake slot.
void BuildSrtPlan(Program& program);
bool ValidateRuntimeValue(const Program& program, Value value);

bool EvaluateDescriptorSource(const Program& program, uint32_t source, const SrtRuntime& runtime,
                              DescriptorValue& result);

// Evaluates one runtime snapshot transactionally. Scalar values and ReadConst results shared by
// several descriptors are memoized once across the batch.
bool EvaluateDescriptorSources(const Program&                           program,
                               std::span<const DescriptorSourceRequest> requests,
                               const SrtRuntime& runtime, std::vector<DescriptorValue>& results);

// Evaluates descriptor sources and the flattened immediate SRT with one memoized scalar walk.
// On failure neither destination is changed.
bool EvaluateRuntimeSources(const Program&                           program,
                            std::span<const DescriptorSourceRequest> requests,
                            const SrtRuntime& runtime, std::vector<DescriptorValue>& results,
                            std::vector<uint32_t>& flat, std::span<const uint8_t> clean_flat_slots);

bool WalkSrt(const Program& program, const SrtRuntime& runtime, std::vector<uint32_t>& flat);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SRTWALKER_H_ */
