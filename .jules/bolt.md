## $(date +%Y-%m-%d) - [ImGui Loop Deep Copy Bottleneck]
**Learning:** In hot render loops like ImGui's (running at 60fps), iterating over structures containing strings (or other complex objects) by value (e.g., `for (auto item : container)`) causes a massive amount of unnecessary deep copies and heap allocations. This is a common but devastating anti-pattern in C++ UI code.
**Action:** Always use `const auto&` for read-only iterations over complex objects in hot paths and render loops to avoid deep copying.
