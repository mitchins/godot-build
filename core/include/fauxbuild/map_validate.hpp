#pragma once

#include <string>
#include <vector>

#include "fauxbuild/map_v7.hpp"
#include "fauxbuild/result.hpp"

namespace fauxbuild {

enum class Severity {
    Error,
    Warning,
};

struct ValidationIssue {
    Severity severity = Severity::Error;
    ErrorCode code = ErrorCode::InvalidTopology;
    std::string record; // e.g. "sector[3]", "wall[17]", "map.header"
    std::string detail;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;
    bool truncated = false; // issue cap reached; more problems may exist

    bool ok() const;
    std::size_t error_count() const;
    std::size_t warning_count() const;
};

// Bounded structural validation of a parsed MAP v7 world. Checks header
// references, sector wall ranges and ownership, wall-loop closure (explicit
// per-loop step bounds — no unbounded topology walks), portal reciprocity,
// and sprite sector references. The map is never mutated or repaired.
//
// Sentinel rules (D0012, proposed): wall nextwall/nextsector are -1 or valid;
// sprite sectnum -1 is the documented "no sector" sentinel; a start sector of
// -1 is valid only for a zero-sector map.
ValidationReport validate_map(const mapv7::MapData& map);

} // namespace fauxbuild
