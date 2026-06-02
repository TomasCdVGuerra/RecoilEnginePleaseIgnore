#include "../CpuTopology.h"

namespace cpu_topology {

    // Return an empty cache structure, letting macOS manage cache awareness.
    ProcessorCaches GetProcessorCache() {
        return ProcessorCaches{}; 
    }

    // Tell the engine that all available CPU cores are generic "performance" cores,
    // and let macOS Grand Central Dispatch handle the actual thread distribution.
    ProcessorMasks GetProcessorMasks() {
        ProcessorMasks masks;
        masks.performanceCoreMask = ~0u; // ~0u means all bits set to 1
        return masks;
    }

    // Explicitly tell the engine's threading system NOT to pin threads on Mac.
    ThreadPinPolicy GetThreadPinPolicy() {
        return THREAD_PIN_POLICY_NONE; 
    }

} // namespace cpu_topology