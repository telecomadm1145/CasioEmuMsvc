#pragma once
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class LocalizationException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Localization {
public:
    void Load() {
        std::fstream fs("locale.txt", std::ios::in);
        std::string locale;
        if (fs >> locale) {
            ChangeLanguage(locale);
        }
        else {
            ChangeLanguage("en_US");
        }
    }

    bool ChangeLanguage(const std::string& localeName) {
        try {
            m_translations.clear();
            m_pluralRules.clear();
            m_currentLocale = localeName;

            LoadTranslations(localeName);
            std::fstream fs("locale.txt", std::ios::out);
            fs << localeName;
            return true;
        }
        catch (const std::exception& e) {
            char buffer[512];
            snprintf(buffer, sizeof(buffer), "Failed to load language %s: %s", 
                    localeName.c_str(), e.what());
            throw LocalizationException(buffer);
        }
    }

    std::string GetCurrentLanguage() const {
        return m_currentLocale;
    }

    std::string Get(std::string_view key) const {
        auto iter = m_translations.find(std::string(key));
        if (iter == m_translations.end())
            return std::string(key);
        return iter->second;
    }

    const char* GetCStr(const char* key) const {
        auto iter = m_translations.find(key);
        if (iter == m_translations.end())
            return key;
        return iter->second.c_str();
    }

    template <typename... Args>
    std::string Format(std::string_view key, const Args&... args) const {
        std::string text = Get(key);
        char buffer[1024];
        try {
            snprintf(buffer, sizeof(buffer), text.c_str(), 
                    ToString(args)...);
            return std::string(buffer);
        }
        catch (const std::exception& e) {
            char errBuffer[512];
            snprintf(errBuffer, sizeof(errBuffer), 
                    "Format error for key '%s': %s", 
                    std::string(key).c_str(), e.what());
            throw LocalizationException(errBuffer);
        }
    }

    std::string GetPlural(std::string_view key, int count) const {
        std::string baseKey(key);
        auto pluralRule = m_pluralRules.find(baseKey);

        if (pluralRule != m_pluralRules.end()) {
            const auto& rules = pluralRule->second;
            for (const auto& [condition, form] : rules) {
                if (EvaluatePluralCondition(condition, count)) {
                    return Format(form, count);
                }
            }
        }

        return Format(Get(key), count);
    }

    std::string operator[](std::string_view key) const {
        return Get(key);
    }

private:
    struct PluralRule {
        std::string condition;
        std::string form;
    };

    std::string m_basePath = "./locales/";
    std::string m_currentLocale = "en_US";
    std::unordered_map<std::string, std::string> m_translations;
    std::unordered_map<std::string, std::vector<PluralRule>> m_pluralRules;

    // Helper method to convert arguments to const char*
    template<typename T>
    static auto ToString(const T& value) -> decltype(value) {
        return value;
    }
    
    static const char* ToString(const std::string& str) {
        return str.c_str();
    }

    void LoadTranslations(const std::string& localeName) {
        std::filesystem::path filePath = 
            std::filesystem::path(m_basePath) / (localeName + ".lc");

        std::ifstream file(filePath);
        if (!file.is_open()) {
            char buffer[512];
            snprintf(buffer, sizeof(buffer), "Cannot open locale file: %s", 
                    filePath.string().c_str());
            throw LocalizationException(buffer);
        }

        std::string line;
        int lineNumber = 0;
        while (std::getline(file, line)) {
            lineNumber++;
            if (line.empty() || line[0] == '#')
                continue;

            try {
                ProcessLine(line);
            }
            catch (const std::exception& e) {
                char buffer[512];
                snprintf(buffer, sizeof(buffer), "Error at line %d: %s", 
                        lineNumber, e.what());
                throw LocalizationException(buffer);
            }
        }
    }

    void ProcessLine(const std::string& line) {
        std::istringstream lineStream(line);
        std::string key, value;

        if (!std::getline(lineStream, key, '=')) {
            throw LocalizationException("Invalid format");
        }
        std::getline(lineStream, value);

        key = Trim(key);
        value = Trim(value);

        if (key.empty()) {
            throw LocalizationException("Empty key");
        }

        if (key.ends_with("|plural")) {
            ProcessPluralForm(key.substr(0, key.length() - 7), value);
        }
        else {
            m_translations[key] = DecodeEscapes(value);
        }
    }

    void ProcessPluralForm(const std::string& key, const std::string& value) {
        std::istringstream ss(value);
        std::string rule;

        while (std::getline(ss, rule, ';')) {
            size_t pos = rule.find(':');
            if (pos == std::string::npos) {
                throw LocalizationException("Invalid plural rule format");
            }

            std::string condition = Trim(rule.substr(0, pos));
            std::string form = Trim(rule.substr(pos + 1));

            m_pluralRules[key].push_back({condition, DecodeEscapes(form)});
        }
    }

    bool EvaluatePluralCondition(const std::string& condition, int count) const {
        if (condition == "one")
            return count == 1;
        if (condition == "zero")
            return count == 0;
        if (condition == "many")
            return count >= 5;
        if (condition == "few")
            return count >= 2 && count <= 4;
        return condition == "other";
    }

    static std::string DecodeEscapes(const std::string& input) {
        std::string result;
        result.reserve(input.length());

        for (size_t i = 0; i < input.length(); ++i) {
            if (input[i] == '\\' && i + 1 < input.length()) {
                switch (input[++i]) {
                case 'n':
                    result += '\n';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '=':
                    result += '=';
                    break;
                default:
                    result += input[i];
                    break;
                }
            }
            else {
                result += input[i];
            }
        }
        return result;
    }

    static std::string Trim(std::string_view str) {
        const auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
            return std::string();

        const auto end = str.find_last_not_of(" \t\r\n");
        return std::string(str.substr(start, end - start + 1));
    }
};

extern Localization g_local;

inline std::string operator""_l(const char* str, size_t) {
    return g_local[str];
}

inline const char* operator""_lc(const char* str, size_t) {
    return g_local.GetCStr(str);
}

template <typename... Args>
inline std::string localstr(std::string_view key, const Args&... args) {
    return g_local.Format(key, args...);
}

inline std::string plural(std::string_view key, int count) {
    return g_local.GetPlural(key, count);
}