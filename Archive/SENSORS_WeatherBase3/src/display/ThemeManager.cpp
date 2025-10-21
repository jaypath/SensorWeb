#include "ThemeManager.hpp"
#include "../utils/ErrorHandler.hpp"
#include <algorithm>

namespace WeatherStation {

ThemeManager::ThemeManager()
    : initialized_(false) {
}

ThemeManager::~ThemeManager() {
    shutdown();
}

ErrorCode ThemeManager::initialize() {
    if (initialized_) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    try {
        // Load default theme
        Theme default_theme;
        default_theme.name = "Default";
        default_theme.colors[ThemeColorRole::BACKGROUND] = Color(30, 30, 30);
        default_theme.colors[ThemeColorRole::FOREGROUND] = Color(220, 220, 220);
        default_theme.colors[ThemeColorRole::PRIMARY] = Color(0, 120, 215);
        default_theme.colors[ThemeColorRole::SECONDARY] = Color(32, 32, 32);
        default_theme.colors[ThemeColorRole::ACCENT] = Color(255, 140, 0);
        default_theme.colors[ThemeColorRole::ERROR] = Color(220, 20, 60);
        default_theme.colors[ThemeColorRole::WARNING] = Color(255, 215, 0);
        default_theme.colors[ThemeColorRole::SUCCESS] = Color(50, 205, 50);
        default_theme.colors[ThemeColorRole::INFO] = Color(0, 191, 255);
        default_theme.colors[ThemeColorRole::TEXT] = Color(255, 255, 255);
        default_theme.colors[ThemeColorRole::BORDER] = Color(80, 80, 80);
        default_theme.fonts[ThemeFontRole::DEFAULT] = "default";
        default_theme.fonts[ThemeFontRole::TITLE] = "title";
        default_theme.fonts[ThemeFontRole::SUBTITLE] = "subtitle";
        default_theme.fonts[ThemeFontRole::BODY] = "body";
        default_theme.fonts[ThemeFontRole::CAPTION] = "caption";
        default_theme.fonts[ThemeFontRole::BUTTON] = "button";
        
        addTheme(default_theme);
        setActiveTheme("Default");
        
        initialized_ = true;
        error_handler.logInfo("ThemeManager initialized successfully");
        return ErrorCode::SUCCESS;
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::THEME_INIT_FAILED, 
                                    "ThemeManager initialization exception: " + std::string(e.what()));
    }
}

void ThemeManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    themes_.clear();
    active_theme_.reset();
    initialized_ = false;
    ErrorHandler::getInstance().logInfo("ThemeManager shutdown complete");
}

bool ThemeManager::isInitialized() const {
    return initialized_;
}

bool ThemeManager::addTheme(const Theme& theme) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (themes_.count(theme.name) > 0) {
        return false;
    }
    themes_[theme.name] = std::make_shared<Theme>(theme);
    return true;
}

bool ThemeManager::removeTheme(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = themes_.find(name);
    if (it != themes_.end()) {
        if (active_theme_ && active_theme_->name == name) {
            active_theme_.reset();
        }
        themes_.erase(it);
        return true;
    }
    return false;
}

std::shared_ptr<Theme> ThemeManager::getTheme(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = themes_.find(name);
    if (it != themes_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> ThemeManager::getThemeNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& pair : themes_) {
        names.push_back(pair.first);
    }
    return names;
}

bool ThemeManager::setActiveTheme(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = themes_.find(name);
    if (it != themes_.end()) {
        active_theme_ = it->second;
        return true;
    }
    return false;
}

std::shared_ptr<Theme> ThemeManager::getActiveTheme() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_theme_;
}

Color ThemeManager::getColor(ThemeColorRole role) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_theme_ && active_theme_->colors.count(role)) {
        return active_theme_->colors.at(role);
    }
    return Color(0, 0, 0);
}

std::string ThemeManager::getFont(ThemeFontRole role) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_theme_ && active_theme_->fonts.count(role)) {
        return active_theme_->fonts.at(role);
    }
    return "default";
}

std::string ThemeManager::getCustomProperty(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_theme_ && active_theme_->custom_properties.count(key)) {
        return active_theme_->custom_properties.at(key);
    }
    return "";
}

void ThemeManager::setColor(ThemeColorRole role, const Color& color) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_theme_) {
        active_theme_->colors[role] = color;
    }
}

void ThemeManager::setFont(ThemeFontRole role, const std::string& font_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_theme_) {
        active_theme_->fonts[role] = font_name;
    }
}

void ThemeManager::setCustomProperty(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_theme_) {
        active_theme_->custom_properties[key] = value;
    }
}

} // namespace WeatherStation 