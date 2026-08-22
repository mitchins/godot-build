#include "fauxbuild/map_validate.hpp"

#include <algorithm>

namespace fauxbuild {

namespace {

constexpr std::size_t kMaxIssues = 64;

struct Sink {
    ValidationReport& report;
    std::size_t issues = 0;

    void add(Severity severity, ErrorCode code, const std::string& record,
             const std::string& detail) {
        if (report.issues.size() >= kMaxIssues) {
            report.truncated = true;
            return;
        }
        report.issues.push_back({severity, code, record, detail});
    }

    void error(ErrorCode code, const std::string& record, const std::string& detail) {
        add(Severity::Error, code, record, detail);
    }
};

} // namespace

bool ValidationReport::ok() const {
    return std::none_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
        return issue.severity == Severity::Error;
    });
}

std::size_t ValidationReport::error_count() const {
    return static_cast<std::size_t>(
        std::count_if(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
            return issue.severity == Severity::Error;
        }));
}

std::size_t ValidationReport::warning_count() const {
    return static_cast<std::size_t>(
        std::count_if(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
            return issue.severity == Severity::Warning;
        }));
}

ValidationReport validate_map(const mapv7::MapData& map) {
    ValidationReport report;
    Sink sink{report};

    const auto sector_count = static_cast<std::int64_t>(map.sectors.size());
    const auto wall_count = static_cast<std::int64_t>(map.walls.size());

    // --- Header: start sector -------------------------------------------
    const std::string header = "map.header";
    if (sector_count == 0) {
        if (map.start.sector != mapv7::kNoIndex) {
            sink.error(ErrorCode::InvalidStartSector, header,
                       "zero-sector map must use start sector sentinel -1, got " +
                           std::to_string(map.start.sector));
        }
    } else if (map.start.sector < 0 || map.start.sector >= sector_count) {
        sink.error(ErrorCode::InvalidStartSector, header,
                   "start sector " + std::to_string(map.start.sector) + " out of range [0, " +
                       std::to_string(sector_count) + ")");
    }

    // --- Sector wall ranges and wall ownership ---------------------------
    std::vector<std::int16_t> owner(map.walls.size(), mapv7::kNoIndex);
    for (std::size_t s = 0; s < map.sectors.size(); ++s) {
        const auto& sector = map.sectors[s];
        const std::string record = "sector[" + std::to_string(s) + "]";
        const auto begin = static_cast<std::int64_t>(sector.wallptr);
        const auto count = static_cast<std::int64_t>(sector.wallnum);
        if (sector.wallptr < 0 || sector.wallnum < 0 || begin + count > wall_count) {
            sink.error(ErrorCode::InvalidSectorWallRange, record,
                       "wall range [" + std::to_string(begin) + ", " +
                           std::to_string(begin + count) + ") invalid against " +
                           std::to_string(wall_count) + " walls");
            continue;
        }
        if (count == 0) {
            sink.error(ErrorCode::InvalidSectorWallRange, record, "sector owns no walls");
            continue;
        }
        for (std::int64_t w = begin; w < begin + count; ++w) {
            const auto index = static_cast<std::size_t>(w);
            if (owner[index] != mapv7::kNoIndex) {
                sink.error(ErrorCode::InvalidSectorWallRange, record,
                           "wall " + std::to_string(index) + " already claimed by sector " +
                               std::to_string(owner[index]));
            } else {
                owner[index] = static_cast<std::int16_t>(s);
            }
        }
    }
    for (std::size_t w = 0; w < map.walls.size(); ++w) {
        if (owner[w] == mapv7::kNoIndex) {
            sink.error(ErrorCode::InvalidTopology, "wall[" + std::to_string(w) + "]",
                       "wall is not owned by any sector");
        }
    }

    // --- point2 range pass (global) --------------------------------------
    for (std::size_t w = 0; w < map.walls.size(); ++w) {
        const auto p = static_cast<std::int64_t>(map.walls[w].point2);
        if (p < 0 || p >= wall_count) {
            sink.error(ErrorCode::InvalidPoint2, "wall[" + std::to_string(w) + "]",
                       "point2 " + std::to_string(p) + " out of range [0, " +
                           std::to_string(wall_count) + ")");
        }
    }

    // --- Loop closure per sector (explicitly bounded walks) --------------
    std::vector<std::uint8_t> visited(map.walls.size(), 0);
    for (std::size_t s = 0; s < map.sectors.size(); ++s) {
        const auto& sector = map.sectors[s];
        const std::string record = "sector[" + std::to_string(s) + "]";
        const auto begin = static_cast<std::int64_t>(sector.wallptr);
        const auto count = static_cast<std::int64_t>(sector.wallnum);
        if (sector.wallptr < 0 || sector.wallnum < 0 || begin + count > wall_count || count == 0) {
            continue; // already reported; walking an invalid range is pointless
        }

        for (std::int64_t start = begin; start < begin + count; ++start) {
            const auto start_index = static_cast<std::size_t>(start);
            if (visited[start_index] != 0) {
                continue;
            }
            // A closed loop within this sector visits at most `count` walls;
            // the bound is the hang-proof iteration limit (task §10).
            std::int64_t steps = 0;
            std::int64_t w = start;
            while (steps <= count) {
                if (visited[static_cast<std::size_t>(w)] != 0) {
                    if (w == start && steps > 0) {
                        break; // loop closed back at its start
                    }
                    sink.error(ErrorCode::InvalidTopology, record,
                               "wall loop entering already-visited wall " + std::to_string(w));
                    break;
                }
                visited[static_cast<std::size_t>(w)] = 1;
                ++steps;
                const auto p =
                    static_cast<std::int64_t>(map.walls[static_cast<std::size_t>(w)].point2);
                if (p < 0 || p >= wall_count) {
                    break; // already reported by the range pass
                }
                if (p < begin || p >= begin + count) {
                    sink.error(ErrorCode::InvalidTopology, record,
                               "point2 of wall " + std::to_string(w) +
                                   " escapes the "
                                   "sector wall range");
                    break;
                }
                w = p;
            }
            if (steps > count) {
                sink.error(ErrorCode::InvalidTopology, record,
                           "wall loop starting at " + std::to_string(start) +
                               " does not close within the sector's wall range");
            }
        }

        for (std::int64_t w = begin; w < begin + count; ++w) {
            if (visited[static_cast<std::size_t>(w)] == 0) {
                sink.error(ErrorCode::InvalidTopology, record,
                           "wall " + std::to_string(w) + " is not part of any closed loop");
            }
        }
    }

    // --- Portals ----------------------------------------------------------
    for (std::size_t w = 0; w < map.walls.size(); ++w) {
        const auto& wall = map.walls[w];
        const std::string record = "wall[" + std::to_string(w) + "]";
        const auto nw = static_cast<std::int64_t>(wall.nextwall);
        const auto ns = static_cast<std::int64_t>(wall.nextsector);

        if (nw == mapv7::kNoIndex) {
            if (ns != mapv7::kNoIndex) {
                sink.error(ErrorCode::InvalidNextSector, record,
                           "nextsector " + std::to_string(ns) + " set without nextwall");
            }
            continue;
        }
        if (nw < 0 || nw >= wall_count) {
            sink.error(ErrorCode::InvalidNextWall, record,
                       "nextwall " + std::to_string(nw) + " out of range [0, " +
                           std::to_string(wall_count) + ")");
            continue;
        }
        const auto& mirror = map.walls[static_cast<std::size_t>(nw)];
        if (static_cast<std::int64_t>(mirror.nextwall) != static_cast<std::int64_t>(w)) {
            sink.error(ErrorCode::InvalidNextWall, record,
                       "portal not reciprocal: nextwall " + std::to_string(nw) +
                           " points back at wall " + std::to_string(mirror.nextwall));
        }
        if (ns == mapv7::kNoIndex) {
            sink.error(ErrorCode::InvalidNextSector, record, "nextwall set without nextsector");
        } else if (ns < 0 || ns >= sector_count) {
            sink.error(ErrorCode::InvalidNextSector, record,
                       "nextsector " + std::to_string(ns) + " out of range [0, " +
                           std::to_string(sector_count) + ")");
        } else if (owner[static_cast<std::size_t>(nw)] != wall.nextsector) {
            sink.error(ErrorCode::InvalidNextSector, record,
                       "nextsector " + std::to_string(ns) + " does not match sector " +
                           std::to_string(owner[static_cast<std::size_t>(nw)]) +
                           ", which owns nextwall " + std::to_string(nw));
        }
    }

    // --- Sprites -----------------------------------------------------------
    for (std::size_t i = 0; i < map.sprites.size(); ++i) {
        const auto& sprite = map.sprites[i];
        const std::string record = "sprite[" + std::to_string(i) + "]";
        const auto sect = static_cast<std::int64_t>(sprite.sectnum);
        if (sect != mapv7::kNoIndex && (sect < 0 || sect >= sector_count)) {
            sink.error(ErrorCode::InvalidSpriteSector, record,
                       "sectnum " + std::to_string(sect) +
                           " is neither sentinel -1 nor "
                           "in range [0, " +
                           std::to_string(sector_count) + ")");
        }
    }

    return report;
}

} // namespace fauxbuild
