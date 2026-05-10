## 2026-05-05 - [C++ Heterogeneous Lookup in unordered_map]
**Learning:** By default in C++20, `std::hash<std::string_view>` is not transparent. Using an `std::unordered_map` with `std::string` keys and looking up with `std::string_view` or `const char*` requires implicit construction of `std::string` and heap allocation, which can cause significant performance penalties when called frequently (like in UI rendering loops).
**Action:** Define a custom transparent hash struct (e.g., `struct StringHash { using is_transparent = void; ... }`) and use it in `std::unordered_map` to enable C++20 heterogeneous lookup and avoid `std::string` heap allocations.

## 2024-05-24 - [C++ Heterogeneous Lookup missing token_length]
**Learning:** When transitioning from `id.assign(token_begin, token_end)` to `std::string_view` for heterogeneous lookup, remember that `std::string_view` requires `(ptr, length)` or C++20 iterators. If `token_length` is not available, use `token_end - token_begin` as the length argument.
**Action:** Always check the constructor signature of `std::string_view` when replacing `std::string` allocations to avoid compilation errors due to undefined variables.
