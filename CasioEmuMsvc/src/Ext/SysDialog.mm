#ifdef __APPLE__
#include "SysDialog.h"
#import <AppKit/AppKit.h>
#include <string>

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] objectAtIndex:0];
            callback(std::filesystem::path([[url path] UTF8String]));
        }
    }
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setNameFieldStringValue:[NSString stringWithUTF8String:preferred_name.c_str()]];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [panel URL];
            callback(std::filesystem::path([[url path] UTF8String]));
        }
    }
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] objectAtIndex:0];
            callback(std::filesystem::path([[url path] UTF8String]));
        }
    }
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
    OpenFolderDialog(callback);
}
#endif