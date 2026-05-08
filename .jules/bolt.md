## 2026-05-05 - [C++ Heterogeneous Lookup in unordered_map]
**Learning:** By default in C++20, `std::hash<std::string_view>` is not transparent. Using an `std::unordered_map` with `std::string` keys and looking up with `std::string_view` or `const char*` requires implicit construction of `std::string` and heap allocation, which can cause significant performance penalties when called frequently (like in UI rendering loops).
**Action:** Define a custom transparent hash struct (e.g., `struct StringHash { using is_transparent = void; ... }`) and use it in `std::unordered_map` to enable C++20 heterogeneous lookup and avoid `std::string` heap allocations.
## 2026-05-08 - [ImGui Render Loop Deep Copy Prevention]
**Learning:** In highly repetitive UI render loops like ImGui's `DrawFindContent()` (up to 60fps), iterating over containers of complex objects (e.g., `std::unordered_map` values containing `std::string`) using `auto` by value causes massive performance degradation due to continuous heap allocations.
**Action:** Always use `const auto&` in range-based `for` loops within rendering functions when modifying elements is not required.
