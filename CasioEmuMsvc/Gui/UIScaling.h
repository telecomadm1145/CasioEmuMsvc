#pragma once
#include <imgui.h>
#include <algorithm>
#include <SDL.h>
#include <cmath>

namespace UI {
    struct Scaling {
        inline static float fontScale;
        inline static float padding;
        inline static float buttonHeight; 
        inline static float minColumnWidth;
        inline static float labelWidth;
        inline static float windowWidth;
        inline static float windowHeight;
        inline static float aspectRatio;
        
        static float GetDensityDpi() {
            SDL_DisplayMode displayMode;
            float densityDpi = 160.0f; // Default fallback
            
            if (SDL_GetCurrentDisplayMode(0, &displayMode) == 0) {
                float diagonalPixels = sqrt(pow(displayMode.w, 2) + pow(displayMode.h, 2));
                float physicalWidth, physicalHeight;
                if (SDL_GetDisplayDPI(0, &densityDpi, &physicalWidth, &physicalHeight) != 0) {
                    // If SDL_GetDisplayDPI fails, estimate based on resolution
                    if (displayMode.h <= 480) densityDpi = 120.0f;       // ldpi
                    else if (displayMode.h <= 800) densityDpi = 160.0f;  // mdpi
                    else if (displayMode.h <= 1280) densityDpi = 240.0f; // hdpi
                    else if (displayMode.h <= 1920) densityDpi = 320.0f; // xhdpi
                    else if (displayMode.h <= 2560) densityDpi = 480.0f; // xxhdpi
                    else densityDpi = 640.0f;                            // xxxhdpi
                }
            }
            return densityDpi;
        }
        
        static void UpdateUIScale() {
            ImGuiIO& io = ImGui::GetIO();
            windowWidth = io.DisplaySize.x;
            windowHeight = io.DisplaySize.y;
            aspectRatio = windowWidth / windowHeight;
            
            // Calculate base scale considering both resolution and density
            float densityDpi = GetDensityDpi();
            float densityScale = densityDpi / 160.0f; // Using mdpi as baseline
            
            // Calculate base scale based on screen resolution
            float baseScale = std::min(windowWidth / 1920.0f, windowHeight / 1080.0f);
            
            // Adjust scale based on screen size category
            float screenSizeAdjustment = 1.0f;
            float diagonalPixels = sqrt(pow(windowWidth, 2) + pow(windowHeight, 2));
            float diagonalInches = diagonalPixels / densityDpi;
            
            if (diagonalInches <= 4.0f) {
                screenSizeAdjustment = 0.75f;        // Very small phones
            } else if (diagonalInches <= 5.0f) {
                screenSizeAdjustment = 0.8f;         // Small phones
            } else if (diagonalInches <= 6.0f) {
                screenSizeAdjustment = 0.90f;         // Standard phones
            } else if (diagonalInches <= 7.0f) {
                screenSizeAdjustment = 1.0f;         // Large phones
            } else if (diagonalInches <= 10.0f) {
                screenSizeAdjustment = 1.1f;         // Tablets
            } else {
                screenSizeAdjustment = 1.2f;         // Large tablets
            }

            // Calculate final font scale
            fontScale = baseScale * screenSizeAdjustment * sqrt(densityScale);
            
            // More aggressive clamping for Android
            fontScale = std::clamp(fontScale, 0.5f, 1.5f);
            
            // Update global font scale with minimum readable size
            io.FontGlobalScale = std::max(fontScale, 0.75f);

            // Adjust touch-friendly sizes
            float touchScale = std::max(fontScale, 1.0f); // Ensure minimum touch target size
            padding = 9.5f * touchScale;
            buttonHeight = 38.0f * touchScale;
            minColumnWidth = 55.0f * fontScale;
            labelWidth = 85.0f * fontScale;

            // Update ImGui style
            ImGuiStyle& style = ImGui::GetStyle();
            
            // increase padding for touch interfaces
            style.WindowPadding = ImVec2(padding, padding);
            style.FramePadding = ImVec2(padding * 0.8f, padding * 0.8f);
            style.ItemSpacing = ImVec2(padding * 0.7f, padding * 0.7f);
            style.ItemInnerSpacing = ImVec2(padding * 0.5f, padding * 0.5f);
            style.TouchExtraPadding = ImVec2(padding * 0.6f, padding * 0.6f);
            
            // Adjust sizes for touch interaction
            style.ScrollbarSize = 24.0f * touchScale;
            style.GrabMinSize = 30.0f * touchScale;
            style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
            style.MouseCursorScale = 1.2f * touchScale;

            float rounding = 7.5f * std::min(touchScale, 1.3f);
            style.WindowRounding = rounding;
            style.ChildRounding = rounding;
            style.FrameRounding = rounding;
            style.ScrollbarRounding = rounding;
            style.GrabRounding = rounding;
            style.TabRounding = rounding;
            style.PopupRounding = rounding;
            
            // Adjust spacing for wide screens
            if (aspectRatio > 1.8f) {  // More aggressive for ultra-wide
                style.ItemSpacing.x *= 1.15f;
                style.WindowPadding.x *= 1.15f;
            }
            
            // Ensure minimum touch target size
            style.FramePadding.y = std::max(buttonHeight * 0.1f, 0.85f);
        }
    };
}