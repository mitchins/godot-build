#pragma once

namespace fauxbuild {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 0;
inline constexpr int kVersionPatch = 0;

inline constexpr const char* version_string() { return "0.0.0"; }

const char* build_config();

} // namespace fauxbuild
