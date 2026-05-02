# Roadmap: "Look At My Work" Feature

## Overview
The "Look At My Work" feature aims to provide developers, researchers, and emulator users a seamless way to share their progress, debug states, and achievements. By bundling emulator state, visual previews, and debugging metadata into a single easily sharable package, users can collaborate more effectively.

## Core Objectives
1. **Easy Sharing**: One-click generation of a shareable package.
2. **Comprehensive State**: Include save states, RAM dumps, register states, and current screen capture.
3. **Debug Context**: Include current breakpoints, memory watches, and a snippet of the disassembly/execution history.
4. **Platform Integration**: Integration with platforms like Discord or GitHub for easy sharing.

## Phases of Implementation

### Phase 1: Data Aggregation & Packaging (Foundation)
**Goal:** Gather all necessary emulator states and package them into a single archive format (e.g., `.casiowork` or `.zip`).

*   **Step 1:** Define the payload structure.
    *   `screenshot.png` (Current LCD state)
    *   `state.bin` (Standard emulator save state)
    *   `metadata.json` (Emulator version, model info, timestamp)
    *   `debug_context.json` (Registers, active breakpoints, current PC, memory watches)
*   **Step 2:** Implement the packaging logic.
    *   Create a new C++ class `WorkExporter` in `CasioEmuMsvc`.
    *   Utilize existing snapshot system APIs to grab the memory state.
    *   Serialize debug variables and breakpoints from `Gui/` components.
*   **Step 3:** Add a basic UI trigger.
    *   Add a "Share My Work" button in the main menu bar.
    *   Prompt the user for a save location and generate the archive.

### Phase 2: Enhanced Visuals & Export Options
**Goal:** Make the shared output more appealing and provide flexible export formats.

*   **Step 1:** GIF/Video Generation.
    *   Instead of just a static screenshot, capture the last 3-5 seconds of screen output.
    *   Integrate a lightweight GIF encoder.
*   **Step 2:** Export to Markdown / Text.
    *   Generate a `summary.md` file within the package containing the disassembly around the current PC, register states, and a summary of the machine state.
*   **Step 3:** UI Enhancements.
    *   Create a dedicated ImGui window for "Export Work" where users can toggle what gets included (e.g., "Include RAM dump", "Include Screen GIF", "Include Key Log").

### Phase 3: Cloud & Social Integration
**Goal:** Reduce the friction of sharing by integrating with external services.

*   **Step 1:** Discord Integration.
    *   Extend the current Discord RPC to include a "View My Work" button if a cloud link is available.
    *   Allow dropping a `.casiowork` file directly into Discord (as a standard zip/file upload).
*   **Step 2:** Web Viewer / Cloud Storage (Optional/Long-term).
    *   Develop a basic web viewer that can parse `metadata.json` and display the screenshot and debug context.
    *   Implement an API to upload the package to a temporary hosting service (e.g., a pastebin for emulator states) and generate a shortlink.

### Phase 4: Import & Collaboration (The "Look" part)
**Goal:** Allow other users to easily view and resume the shared work.

*   **Step 1:** Implement "Load Work" functionality.
    *   Extract the `.casiowork` archive.
    *   Load the save state, restore breakpoints, and synchronize the UI.
*   **Step 2:** Read-Only Viewer Mode.
    *   Allow loading a package in a "Review Mode" where the user can step through the provided debug context without accidentally modifying the original state.
*   **Step 3:** Feedback Loop.
    *   Allow the reviewing user to add annotations (e.g., memory comments) and re-export a modified package.

## Technical Considerations
*   **Security:** Ensure loading arbitrary state files doesn't lead to buffer overflows or code execution vulnerabilities in the emulator itself.
*   **File Size:** RAM dumps can be large. Use compression (e.g., zlib, which may already be available or easily integrated via CMake) to keep package sizes small.
*   **Cross-Platform:** The packaging format and paths must be OS-agnostic so a state saved on Android can be debugged on Windows.
