/**
 * @file json_renderer.hpp
 * @brief Machine-readable JSON output of a full snapshot.
 */

#ifndef SYSMON_JSON_RENDERER_HPP
#define SYSMON_JSON_RENDERER_HPP

#include "sysmon/config.hpp"
#include "sysmon/stats.hpp"
#include <ostream>
#include <string>

/**
 * @brief Serialises a Snapshot as JSON.
 *
 * Every metric sysmon collects appears here, including the ones the text and
 * TUI renderers leave out for space.  Metrics a platform cannot measure are
 * emitted as `null`, never as a zero, so a consumer can tell "not supported"
 * from "measured as zero".
 */
class JsonRenderer {
public:
    /** @brief Write the snapshot as JSON to stdout. */
    void render(const Snapshot& snap, const Config& cfg);

    /** @brief Write the snapshot as JSON to an arbitrary stream. */
    void render_to(std::ostream& out, const Snapshot& snap, const Config& cfg);

    /** @brief Build the JSON document as a string. */
    std::string to_string(const Snapshot& snap, const Config& cfg);

    /** @brief Whether to indent the output (default) or emit one dense line. */
    void set_pretty(bool pretty) { pretty_ = pretty; }

private:
    bool pretty_{true};
};

#endif // SYSMON_JSON_RENDERER_HPP
