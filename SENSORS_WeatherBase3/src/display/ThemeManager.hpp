#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <mutex>
#include "../utils/ErrorHandler.hpp"
#include "GraphicsEngine.hpp"

namespace WeatherStation {

/**
 * @brief ThemeColorRole - Roles for theme colors
 */
enum class ThemeColorRole {
    BACKGROUND,
    FOREGROUND,
    PRIMARY,
    SECONDARY,
    ACCENT,
    ERROR,
    WARNING,
    SUCCESS,
    INFO,
    TEXT,
    BORDER,
    CUSTOM
};

/**
 * @brief ThemeFontRole - Roles for theme fonts
 */
enum class ThemeFontRole {
    DEFAULT,
    TITLE,
    SUBTITLE,
    BODY,
    CAPTION,
    BUTTON,
    CUSTOM
};

/**
 * @brief Theme - Represents a visual theme
 */
struct Theme {
    std::string name;
    std::map<ThemeColorRole, Color> colors;
    std::map<ThemeFontRole, std::string> fonts;
    std::map<std::string, std::string> custom_properties;
};

/**
 * @brief ThemeManager - Manages visual themes for the UI
 */
class ThemeManager {
public:
    ThemeManager();
    ~ThemeManager();

    ErrorCode initialize();
    void shutdown();
    bool isInitialized() const;

    bool addTheme(const Theme& theme);
    bool removeTheme(const std::string& name);
    std::shared_ptr<Theme> getTheme(const std::string& name) const;
    std::vector<std::string> getThemeNames() const;
    bool setActiveTheme(const std::string& name);
    std::shared_ptr<Theme> getActiveTheme() const;

    Color getColor(ThemeColorRole role) const;
    std::string getFont(ThemeFontRole role) const;
    std::string getCustomProperty(const std::string& key) const;

    void setColor(ThemeColorRole role, const Color& color);
    void setFont(ThemeFontRole role, const std::string& font_name);
    void setCustomProperty(const std::string& key, const std::string& value);

private:
    std::map<std::string, std::shared_ptr<Theme>> themes_;
    std::shared_ptr<Theme> active_theme_;
    bool initialized_;
    mutable std::mutex mutex_;
};

} // namespace WeatherStation 