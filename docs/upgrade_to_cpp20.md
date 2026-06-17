Upgrade plan: Move Thirdeye to C++20 and consolidate filesystem usage on std::filesystem

Goal
- Target C++20 and consolidate filesystem usage around `std::filesystem`.

Overview
- Update top-level `CMakeLists.txt` to require C++20.
- Use standard library filesystem APIs instead of legacy compatibility shims.
- Update includes and code where needed to work with the standard filesystem API.
- Build and run configuration to verify the result.

Phase 1 — CMake and build system
1. Update `CMakeLists.txt` to set `CMAKE_CXX_STANDARD` to 20 and `CMAKE_CXX_STANDARD_REQUIRED ON`.
2. Ensure standard library filesystem is available and included via `<filesystem>`.

Phase 2 — Direct std::filesystem conversion
1. Replace all filesystem includes with `#include <filesystem>`.
2. Use `std::filesystem::` symbols consistently.
3. Use `path.stem().string()` for path basename extraction.

Phase 3 — Finish and verify
- Re-run CMake configure and capture remaining compatibility issues.
- Build the project and fix any std::filesystem-specific compatibility issues.

Notes and risks
- Legacy filesystem helpers may require small refactors to adapt to the standard API.

Next actions
- Update `CMakeLists.txt` to set C++20.
- Replace filesystem includes and API references with `std::filesystem`.
- Re-run CMake to verify the conversion.
