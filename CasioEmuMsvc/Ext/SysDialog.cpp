#include "SysDialog.h"

#ifdef _WIN32
#include <ShlObj.h>
#include <SysDialog.h>
#include <Windows.h>
#include <commdlg.h>

std::filesystem::path SystemDialogs::OpenFileDialog() {
    auto cwd = std::filesystem::current_path();
    OPENFILENAME ofn;
    TCHAR szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = TEXT("All\0*.*\0");
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileName(&ofn) == TRUE) {
        std::filesystem::current_path(cwd);
        return std::filesystem::path(ofn.lpstrFile);
    }
    std::filesystem::current_path(cwd);
    return {};
}

std::filesystem::path SystemDialogs::SaveFileDialog(std::string prefered_name) {
    auto cwd = std::filesystem::current_path();
    OPENFILENAME ofn;
    TCHAR szFile[260] = {0};
    MultiByteToWideChar(65001, 0, prefered_name.c_str(), prefered_name.size(), szFile, 260);
    // strcpy_s(szFile, prefered_name.c_str());
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = TEXT("All\0*.*\0");
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetSaveFileName(&ofn) == TRUE) {
        std::filesystem::current_path(cwd);
        return std::filesystem::path(ofn.lpstrFile);
    }
    std::filesystem::current_path(cwd);
    return {};
}

std::filesystem::path SystemDialogs::OpenFolderDialog() {
    auto cwd = std::filesystem::current_path();
    BROWSEINFO bi = {0};
    bi.lpszTitle = TEXT("Select Folder");
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    std::filesystem::current_path(cwd);
    if (pidl != 0) {
        TCHAR path[MAX_PATH];
        if (SHGetPathFromIDList(pidl, path)) {
            return std::filesystem::path(path);
        }
    }
    return {};
}

std::filesystem::path SystemDialogs::SaveFolderDialog() {
    return OpenFolderDialog(); // In Windows, folder dialogs are usually used for both opening and saving.
}
#elif defined(__ANDROID__)

#include <jni.h>
#include <SDL.h>
#include <filesystem>

// Implement the C++ methods
std::filesystem::path SystemDialogs::OpenFileDialog() {
    auto env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jclass cls = env->FindClass("com/tele/u8emulator/SystemDialogs");
    jmethodID openFileDialogMethod = env->GetStaticMethodID(cls, "openFileDialog",
                                                      "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFileDialogMethod = env->GetStaticMethodID(cls, "saveFileDialog",
                                                      "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
    jmethodID openFolderDialogMethod = env->GetStaticMethodID(cls, "openFolderDialog",
                                                        "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFolderDialogMethod = env->GetStaticMethodID(cls, "saveFolderDialog",
                                                        "(Landroid/app/Activity;)Ljava/lang/String;");
    // Get the current activity from the SDL Android backend
    jobject activity = (jobject) SDL_AndroidGetActivity();

    // Call the Java method
    jstring result = (jstring) env->CallStaticObjectMethod(cls, openFileDialogMethod, activity);

    // Convert the Java string to a C++ string
    const char *path = env->GetStringUTFChars(result, nullptr);
    std::string pathStr = path;
    env->ReleaseStringUTFChars(result, path);

    return std::filesystem::path(pathStr);
}

std::filesystem::path SystemDialogs::SaveFileDialog(std::string preferredName) {
    auto env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jclass cls = env->FindClass("com/tele/u8emulator/SystemDialogs");
    jmethodID openFileDialogMethod = env->GetStaticMethodID(cls, "openFileDialog",
                                                            "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFileDialogMethod = env->GetStaticMethodID(cls, "saveFileDialog",
                                                            "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
    jmethodID openFolderDialogMethod = env->GetStaticMethodID(cls, "openFolderDialog",
                                                              "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFolderDialogMethod = env->GetStaticMethodID(cls, "saveFolderDialog",
                                                              "(Landroid/app/Activity;)Ljava/lang/String;");
    // Get the current activity from the SDL Android backend
    jobject activity = (jobject) SDL_AndroidGetActivity();

    // Create a Java string for the preferred name
    jstring preferredNameStr = env->NewStringUTF(preferredName.c_str());

    // Call the Java method
    jstring result = (jstring) env->CallStaticObjectMethod(cls, saveFileDialogMethod, activity,
                                                     preferredNameStr);

    // Convert the Java string to a C++ string
    const char *path = env->GetStringUTFChars(result, nullptr);
    std::string pathStr = path;
    env->ReleaseStringUTFChars(result, path);

    return std::filesystem::path(pathStr);
}

std::filesystem::path SystemDialogs::OpenFolderDialog() {
    auto env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jclass cls = env->FindClass("com/tele/u8emulator/SystemDialogs");
    jmethodID openFileDialogMethod = env->GetStaticMethodID(cls, "openFileDialog",
                                                            "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFileDialogMethod = env->GetStaticMethodID(cls, "saveFileDialog",
                                                            "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
    jmethodID openFolderDialogMethod = env->GetStaticMethodID(cls, "openFolderDialog",
                                                              "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFolderDialogMethod = env->GetStaticMethodID(cls, "saveFolderDialog",
                                                              "(Landroid/app/Activity;)Ljava/lang/String;");
    // Get the current activity from the SDL Android backend
    jobject activity = (jobject) SDL_AndroidGetActivity();

    // Call the Java method
    jstring result = (jstring) env->CallStaticObjectMethod(cls, openFolderDialogMethod, activity);

    // Convert the Java string to a C++ string
    const char *path = env->GetStringUTFChars(result, nullptr);
    std::string pathStr = path;
    env->ReleaseStringUTFChars(result, path);

    return std::filesystem::path(pathStr);
}

std::filesystem::path SystemDialogs::SaveFolderDialog() {
    auto env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jclass cls = env->FindClass("com/tele/u8emulator/SystemDialogs");
    jmethodID openFileDialogMethod = env->GetStaticMethodID(cls, "openFileDialog",
                                                            "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFileDialogMethod = env->GetStaticMethodID(cls, "saveFileDialog",
                                                            "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
    jmethodID openFolderDialogMethod = env->GetStaticMethodID(cls, "openFolderDialog",
                                                              "(Landroid/app/Activity;)Ljava/lang/String;");
    jmethodID saveFolderDialogMethod = env->GetStaticMethodID(cls, "saveFolderDialog",
                                                              "(Landroid/app/Activity;)Ljava/lang/String;");
    // Get the current activity from the SDL Android backend
    jobject activity = (jobject) SDL_AndroidGetActivity();

    // Call the Java method
    jstring result = (jstring) env->CallStaticObjectMethod(cls, saveFolderDialogMethod, activity);

    // Convert the Java string to a C++ string
    const char *path = env->GetStringUTFChars(result, nullptr);
    std::string pathStr = path;
    env->ReleaseStringUTFChars(result, path);

    return std::filesystem::path(pathStr);
}

#elif defined(__linux__)
#include <cstdlib>
#include <iostream>

std::filesystem::path RunKDialog(const std::string& args) {
    std::string command = "kdialog " + args;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return std::filesystem::path();

    char buffer[128];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    pclose(pipe);
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return std::filesystem::path(result);
}

std::filesystem::path SystemDialogs::OpenFileDialog() {
    return RunKDialog("--getopenfilename");
}

std::filesystem::path SystemDialogs::SaveFileDialog(std::string prefered_name) {
    return RunKDialog("--getsavefilename");
}

std::filesystem::path SystemDialogs::OpenFolderDialog() {
    return RunKDialog("--getexistingdirectory");
}

std::filesystem::path SystemDialogs::SaveFolderDialog() {
    return OpenFolderDialog();
}

#endif
