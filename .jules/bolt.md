## 2026-05-05 - [C++ Heterogeneous Lookup in unordered_map]
**Learning:** By default in C++20, `std::hash<std::string_view>` is not transparent. Using an `std::unordered_map` with `std::string` keys and looking up with `std::string_view` or `const char*` requires implicit construction of `std::string` and heap allocation, which can cause significant performance penalties when called frequently (like in UI rendering loops).
**Action:** Define a custom transparent hash struct (e.g., `struct StringHash { using is_transparent = void; ... }`) and use it in `std::unordered_map` to enable C++20 heterogeneous lookup and avoid `std::string` heap allocations.

## 2026-05-05 - [C++ Avoiding deep copies in ImGui render loops]
**Learning:** Using `auto` or value types in range-based for loops or variable assignments inside ImGui rendering loops (which run up to 60 times per second) can cause severe performance degradation if the types contain heap-allocated members like `std::string` or are large structs. This results in excessive memory allocation/deallocation overhead.
**Action:** Always use `const auto&` for iteration and local variable binding of complex or large structures (e.g., `for (const auto& kv : records)` instead of `for (auto kv : records)`) in rendering logic to avoid implicit deep copies.
