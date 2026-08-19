/**
 * @file text_renderer.hpp
 * @brief Plain text output renderer with configuration support.
 */

#ifndef SYSMON_TEXT_RENDERER_HPP
#define SYSMON_TEXT_RENDERER_HPP

#include "sysmon/stats.hpp"
#include "sysmon/config.hpp"
#include <string>
#include <vector>

/**
 * @brief Renders all collected stats in a clean, plain text table.
 */
class TextRenderer {
public:
    /**
     * @brief Render everything to stdout according to config flags.
     */
    void render(const SystemStats&                   system,
                const CpuStats&                      cpu,
                const MemoryStats&                   memory,
                const std::vector<GpuStats>&         gpus,
                const LoadStats&                     load,
                const std::vector<DiskStats>&        disks,
                const std::vector<DiskIOStats>&      disk_io,
                const std::vector<NetworkStats>&     net,
                const std::vector<NetConnectionStats>& conns,
                const std::vector<ProcessStats>&     procs,
                const TemperatureStats&              temps,
                const Config&                        cfg);

private:
    void render_system(const SystemStats& stats);
    void render_cpu(const CpuStats& stats, const Config& cfg);
    void render_gpu(const std::vector<GpuStats>& gpus, const Config& cfg);
    void render_memory(const MemoryStats& stats, const Config& cfg);
    void render_load(const LoadStats& stats);
    void render_disks(const std::vector<DiskStats>& stats, const std::vector<DiskIOStats>& io, const Config& cfg);
    void render_network(const std::vector<NetworkStats>& stats, const Config& cfg);
    void render_connections(const std::vector<NetConnectionStats>& conns, const Config& cfg);
    void render_processes(const std::vector<ProcessStats>& stats, const Config& cfg);
    void render_temperatures(const TemperatureStats& stats, const Config& cfg);

    std::string format_bytes(uint64_t bytes) const;
    std::string format_bps(double bps) const;
};

#endif // SYSMON_TEXT_RENDERER_HPP