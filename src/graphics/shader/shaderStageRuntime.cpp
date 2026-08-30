#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"
#include "graphics/shader/shader.h"

#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Libs::Graphics {

namespace {

constexpr size_t SRT_CACHE_MAX_PER_PROGRAM = 4096;

// A materialized stage plus every guest word it was derived from. Reuse is only sound while all
// of those words still read back the same, so the entry carries its own validity proof.
struct SrtCacheEntry {
	ShaderStageRuntime                               stage;
	std::vector<ShaderRecompiler::IR::SrtMemoryRead> reads;
};

using SrtCache = std::unordered_map<const void*, std::unordered_map<uint64_t, SrtCacheEntry>>;

SrtCache& SrtCacheTable() {
	static thread_local SrtCache table;
	return table;
}

uint64_t SrtCacheKey(std::span<const uint32_t> user_data, uint64_t shader_base) {
	uint64_t key = shader_base;
	for (const auto word: user_data) {
		key = (key ^ word) * 0x100000001b3ULL;
	}
	return key;
}

bool SrtCacheEntryStillValid(const SrtCacheEntry& entry) {
	for (const auto& read: entry.reads) {
		uint32_t word = 0;
		if (read.reader != nullptr) {
			if (!read.reader(read.userdata, read.address, &word)) {
				return false;
			}
		} else {
			std::memcpy(&word, reinterpret_cast<const void*>(read.address), sizeof(word));
		}
		if (word != read.value) {
			return false;
		}
	}
	return true;
}

} // namespace

bool ShaderMaterializeStageRuntime(std::shared_ptr<const ShaderRecompiler::IR::Program> program,
                                   std::span<const uint32_t> user_data, uint64_t shader_base,
                                   ShaderStageRuntime& stage,
                                   ShaderSpecializationMemoryReader read_specialization_memory,
                                   void*                            read_memory_data) {
	if (program == nullptr) {
		return false;
	}
	const uint64_t key     = SrtCacheKey(user_data, shader_base);
	auto&          entries = SrtCacheTable()[program.get()];
	if (auto it = entries.find(key); it != entries.end()) {
		if (SrtCacheEntryStillValid(it->second)) {
			stage = it->second.stage;
			return true;
		}
		entries.erase(it);
	}

	std::vector<ShaderRecompiler::IR::SrtMemoryRead> reads;
	ShaderRecompiler::IR::SrtRuntime                 runtime;
	runtime.user_data                  = user_data;
	runtime.shader_base                = shader_base;
	runtime.read_specialization_memory = read_specialization_memory;
	runtime.userdata                   = read_memory_data;
	runtime.read_log                   = &reads;
	ShaderRecompiler::IR::ResourceSnapshot snapshot;
	if (!ShaderRecompiler::IR::MaterializeResources(*program, runtime, snapshot) ||
	    !ShaderRecompiler::IR::ValidateResourceSpecialization(*program, snapshot)) {
		return false;
	}
	auto resources =
	    std::make_shared<const ShaderRecompiler::IR::ResourceSnapshot>(std::move(snapshot));
	stage = {program, std::move(resources)};
	if (entries.size() >= SRT_CACHE_MAX_PER_PROGRAM) {
		entries.clear();
	}
	entries[key] = {stage, std::move(reads)};
	return true;
}

} // namespace Libs::Graphics
