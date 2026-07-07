#include "app_utils.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

int app_utils_get_cpu_util_percent(void)
{
#if defined(CONFIG_THREAD_RUNTIME_STATS) && defined(CONFIG_SCHED_THREAD_USAGE)
	k_thread_runtime_stats_t stats;
	int ret;

	ret = k_thread_runtime_stats_all_get(&stats);
	if (ret < 0) {
		return -1;
	}

	if (stats.execution_cycles == 0U) {
		return 0;
	}

	uint64_t pct = (stats.total_cycles * 100U) / stats.execution_cycles;
	if (pct > 100U) {
		pct = 100U;
	}

	return (int)pct;
#else
	return -1;
#endif
}

int app_utils_get_ram_util_percent(void)
{
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
	struct k_heap *heaps = NULL;
	struct sys_memory_stats stats;
	size_t total_alloc = 0U;
	size_t total_free = 0U;
	size_t total;
	int heap_count;
	int ret;

	heap_count = k_heap_array_get(&heaps);
	if ((heap_count <= 0) || (heaps == NULL)) {
		return -1;
	}

	for (int i = 0; i < heap_count; i++) {
		ret = sys_heap_runtime_stats_get(&heaps[i].heap, &stats);
		if (ret < 0) {
			continue;
		}

		total_alloc += stats.allocated_bytes;
		total_free += stats.free_bytes;
	}

	total = total_alloc + total_free;
	if (total == 0U) {
		return -1;
	}

	return (int)((total_alloc * 100U) / total);
#else
	return -1;
#endif
}
