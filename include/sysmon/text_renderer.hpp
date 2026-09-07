/**
 * @file text_renderer.hpp
 * @brief Plain text output renderer with configuration support.
 */

#ifndef SYSMON_TEXT_RENDERER_HPP
#define SYSMON_TEXT_RENDERER_HPP

#include "sysmon/config.hpp"
#include "sysmon/stats.hpp"
#include <ostream>
#include <string>
#include <vector>

/**
 * @brief Renders a snapshot as plain text tables.
 *
 * Every column goes through utils::fit(), so an over-long mountpoint, process
 * name or interface name is truncated rather than pushing the columns after it
 * out of alignment.
 */
class TextRenderer {
public:
    /** @brief Render everything to stdout according to the config flags. */
    void render(const Snapshot& snap, const Config& cfg);

    /** @brief Render to an arbitrary stream (used by the tests). */
    void render_to(std::ostream& out, const Snapshot& snap, const Config& cfg);

private:
    /**
     * @brief One block of headline numbers, for DetailLevel::Compact.
     *
     * `--compact` used to be accepted and then ignored entirely in text mode,
     * so the flag looked as though it had worked while the output was
     * identical.
     */
    void render_summary(std::ostream& out, const Snapshot& snap, const Config& cfg);

    void render_system(std::ostream& out, const SystemStats& stats, const Config& cfg);
    void render_cpu(std::ostream& out, const CpuStats& stats, const Config& cfg);
    void render_gpu(std::ostream& out, const std::vector<GpuStats>& gpus, const Config& cfg);
    void render_memory(std::ostream& out, const MemoryStats& stats, const Config& cfg);
    void render_load(std::ostream& out, const LoadStats& stats);
    void render_battery(std::ostream& out, const BatteryStats& stats);
    void render_disks(std::ostream& out, const std::vector<DiskStats>& stats,
                      const std::vector<DiskIOStats>& io, const Config& cfg);
    void render_network(std::ostream& out, const std::vector<NetworkStats>& stats,
                        const NetGlobalStats& global, const Config& cfg);
    void render_connections(std::ostream& out, const std::vector<NetConnectionStats>& conns,
                            const Config& cfg);
    void render_processes(std::ostream& out, const std::vector<ProcessStats>& stats,
                          const Config& cfg);
    void render_temperatures(std::ostream& out, const TemperatureStats& stats, const Config& cfg);
};

#endif // SYSMON_TEXT_RENDERER_HPP
