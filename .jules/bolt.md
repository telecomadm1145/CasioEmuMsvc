## 2026-05-05 - [C++ Heterogeneous Lookup in unordered_map]
**Learning:** By default in C++20, `std::hash<std::string_view>` is not transparent. Using an `std::unordered_map` with `std::string` keys and looking up with `std::string_view` or `const char*` requires implicit construction of `std::string` and heap allocation, which can cause significant performance penalties when called frequently (like in UI rendering loops).
**Action:** Define a custom transparent hash struct (e.g., `struct StringHash { using is_transparent = void; ... }`) and use it in `std::unordered_map` to enable C++20 heterogeneous lookup and avoid `std::string` heap allocations.

## 2026-05-05 - [Avoid deep copies in UI loops]
**Learning:** C++ range-based for loops using `auto` instead of `const auto&` on collections with objects that allocate dynamically (e.g. `std::string`) will trigger heap allocations and copying every iteration. In an ImGui application, this happens 60 times a second, creating measurable GC/allocation bottlenecks.
**Action:** Always inspect loops in render/UI methods for unintentional pass-by-value of complex types. Always prefer `const auto&` for iteration over maps/vectors of structs.