#include "DisplayManager.hpp"
#include "GraphicsEngine.hpp"
#include "AnimationSystem.hpp"
#include "ThemeManager.hpp"
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"
#include <esp_timer.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/i2c.h>

namespace WeatherStation {

// Static member initialization
DisplayManager* DisplayManager::instance_ = nullptr;
std::mutex DisplayManager::mutex_;

DisplayManager::DisplayManager() 
    : initialized_(false)
    , power_state_(DisplayPowerState::OFF)
    , graphics_engine_(nullptr)
    , animation_system_(nullptr)
    , theme_manager_(nullptr) {
    
    // Initialize metrics
    memset(&metrics_, 0, sizeof(metrics_));
    metrics_.render_time_min = UINT32_MAX;
}

DisplayManager::~DisplayManager() {
    shutdown();
}

DisplayManager* DisplayManager::getInstance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_ == nullptr) {
        instance_ = new DisplayManager();
    }
    return instance_;
}

void DisplayManager::destroyInstance() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instance_ != nullptr) {
        delete instance_;
        instance_ = nullptr;
    }
}

ErrorCode DisplayManager::initialize(const DisplayConfig& config) {
    if (initialized_) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        // Store configuration
        config_ = config;
        
        // Initialize display hardware
        ErrorCode result = initializeDisplay();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Failed to initialize display hardware");
        }
        
        // Create graphics engine
        graphics_engine_ = std::make_shared<GraphicsEngine>(config_.width, config_.height);
        result = graphics_engine_->initialize();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Failed to initialize graphics engine");
        }
        
        // Create animation system
        animation_system_ = std::make_shared<AnimationSystem>();
        result = animation_system_->initialize();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Failed to initialize animation system");
        }
        
        // Create theme manager
        theme_manager_ = std::make_shared<ThemeManager>();
        result = theme_manager_->initialize();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Failed to initialize theme manager");
        }
        
        // Set initial power state
        power_state_ = DisplayPowerState::ON;
        
        // Initialize timing
        last_render_time_ = std::chrono::steady_clock::now();
        last_power_check_ = std::chrono::steady_clock::now();
        
        initialized_ = true;
        
        error_handler.logInfo("DisplayManager initialized successfully");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::DISPLAY_INIT_FAILED, 
                                    "Display initialization exception: " + std::string(e.what()));
    }
}

void DisplayManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Clear all components
    {
        std::lock_guard<std::mutex> lock(components_mutex_);
        components_.clear();
    }
    
    // Shutdown subsystems
    if (animation_system_) {
        animation_system_->shutdown();
        animation_system_.reset();
    }
    
    if (graphics_engine_) {
        graphics_engine_->shutdown();
        graphics_engine_.reset();
    }
    
    if (theme_manager_) {
        theme_manager_->shutdown();
        theme_manager_.reset();
    }
    
    // Cleanup display hardware
    cleanupDisplay();
    
    // Reset state
    initialized_ = false;
    power_state_ = DisplayPowerState::OFF;
    
    ErrorHandler::getInstance().logInfo("DisplayManager shutdown complete");
}

DisplayConfig DisplayManager::getConfig() const {
    return config_;
}

ErrorCode DisplayManager::setConfig(const DisplayConfig& config) {
    if (!initialized_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        // Store new configuration
        config_ = config;
        
        // Reconfigure display hardware
        ErrorCode result = configureDisplay();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Failed to configure display");
        }
        
        // Update graphics engine
        if (graphics_engine_) {
            graphics_engine_->shutdown();
            graphics_engine_ = std::make_shared<GraphicsEngine>(config_.width, config_.height);
            result = graphics_engine_->initialize();
            if (result != ErrorCode::SUCCESS) {
                return error_handler.logError(result, "Failed to reinitialize graphics engine");
            }
        }
        
        error_handler.logInfo("Display configuration updated");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::DISPLAY_CONFIG_FAILED, 
                                    "Display configuration exception: " + std::string(e.what()));
    }
}

std::pair<uint16_t, uint16_t> DisplayManager::getDimensions() const {
    return std::make_pair(config_.width, config_.height);
}

DisplayType DisplayManager::getDisplayType() const {
    return config_.type;
}

ErrorCode DisplayManager::setPowerState(DisplayPowerState state) {
    if (!initialized_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    if (power_state_ == state) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        DisplayPowerState old_state = power_state_;
        power_state_ = state;
        
        // Handle power state change
        switch (state) {
            case DisplayPowerState::ON:
                if (old_state == DisplayPowerState::OFF) {
                    // Turn on display
                    // Implementation depends on specific display hardware
                    error_handler.logInfo("Display powered on");
                }
                break;
                
            case DisplayPowerState::OFF:
                // Turn off display
                // Implementation depends on specific display hardware
                error_handler.logInfo("Display powered off");
                break;
                
            case DisplayPowerState::DIM:
                // Dim display
                // Implementation depends on specific display hardware
                error_handler.logInfo("Display dimmed");
                break;
                
            case DisplayPowerState::SLEEP:
                // Put display in sleep mode
                // Implementation depends on specific display hardware
                error_handler.logInfo("Display in sleep mode");
                break;
        }
        
        // Notify event
        DisplayEvent event;
        event.type = DisplayEventType::POWER_ON;
        event.timestamp = esp_timer_get_time() / 1000;
        event.data = "Power state changed to " + std::to_string(static_cast<int>(state));
        notifyEvent(event);
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::DISPLAY_POWER_FAILED, 
                                    "Power state change exception: " + std::string(e.what()));
    }
}

DisplayPowerState DisplayManager::getPowerState() const {
    return power_state_;
}

ErrorCode DisplayManager::setBrightness(uint8_t brightness) {
    if (!initialized_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    if (config_.brightness == brightness) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        uint8_t old_brightness = config_.brightness;
        config_.brightness = brightness;
        
        // Set hardware brightness
        // Implementation depends on specific display hardware
        
        metrics_.brightness_changes++;
        
        // Notify event
        DisplayEvent event;
        event.type = DisplayEventType::BRIGHTNESS_CHANGE;
        event.timestamp = esp_timer_get_time() / 1000;
        event.data = "Brightness changed from " + std::to_string(old_brightness) + 
                    " to " + std::to_string(brightness);
        notifyEvent(event);
        
        error_handler.logInfo("Display brightness updated: " + std::to_string(brightness));
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::DISPLAY_BRIGHTNESS_FAILED, 
                                    "Brightness change exception: " + std::string(e.what()));
    }
}

uint8_t DisplayManager::getBrightness() const {
    return config_.brightness;
}

ErrorCode DisplayManager::setOrientation(DisplayOrientation orientation) {
    if (!initialized_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    if (config_.orientation == orientation) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        DisplayOrientation old_orientation = config_.orientation;
        config_.orientation = orientation;
        
        // Set hardware orientation
        // Implementation depends on specific display hardware
        
        metrics_.orientation_changes++;
        
        // Notify event
        DisplayEvent event;
        event.type = DisplayEventType::ORIENTATION_CHANGE;
        event.timestamp = esp_timer_get_time() / 1000;
        event.data = "Orientation changed from " + std::to_string(static_cast<int>(old_orientation)) + 
                    " to " + std::to_string(static_cast<int>(orientation));
        notifyEvent(event);
        
        error_handler.logInfo("Display orientation updated");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::DISPLAY_ORIENTATION_FAILED, 
                                    "Orientation change exception: " + std::string(e.what()));
    }
}

DisplayOrientation DisplayManager::getOrientation() const {
    return config_.orientation;
}

ErrorCode DisplayManager::clear(uint32_t color) {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    return graphics_engine_->clear(Color(color));
}

ErrorCode DisplayManager::update() {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Update animations
        if (animation_system_) {
            uint32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                start_time - last_render_time_).count();
            animation_system_->updateAll(elapsed_ms);
        }
        
        // Render all components
        ErrorCode result = renderComponents();
        if (result != ErrorCode::SUCCESS) {
            return result;
        }
        
        // Update display hardware
        // Implementation depends on specific display hardware
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        last_render_time_ = end_time;
        
        // Handle power management
        handlePowerManagement();
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::DISPLAY_UPDATE_FAILED, 
                                           "Display update exception: " + std::string(e.what()));
        return ErrorCode::DISPLAY_UPDATE_FAILED;
    }
}

ErrorCode DisplayManager::drawPixel(uint16_t x, uint16_t y, uint32_t color) {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    return graphics_engine_->drawPixel(x, y, Color(color));
}

ErrorCode DisplayManager::drawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, 
                                  uint32_t color, uint8_t thickness) {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    if (thickness == 1) {
        return graphics_engine_->drawLine(x1, y1, x2, y2, Color(color));
    } else {
        return graphics_engine_->drawThickLine(x1, y1, x2, y2, Color(color), thickness);
    }
}

ErrorCode DisplayManager::drawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, 
                                  uint32_t color, bool filled) {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    Rectangle rect(x, y, width, height);
    return graphics_engine_->drawRect(rect, Color(color), filled);
}

ErrorCode DisplayManager::drawCircle(uint16_t x, uint16_t y, uint16_t radius, 
                                    uint32_t color, bool filled) {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    return graphics_engine_->drawCircle(x, y, radius, Color(color), filled);
}

ErrorCode DisplayManager::drawText(uint16_t x, uint16_t y, const std::string& text, 
                                  uint32_t color, uint8_t font_size, const std::string& font_name) {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    return graphics_engine_->drawText(x, y, text, Color(color));
}

ErrorCode DisplayManager::drawImage(uint16_t x, uint16_t y, const uint8_t* image_data, 
                                   uint16_t width, uint16_t height, const std::string& format) {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    // Create temporary image and draw it
    // This is a simplified implementation
    return ErrorCode::SUCCESS;
}

std::shared_ptr<GraphicsEngine> DisplayManager::getGraphicsEngine() const {
    return graphics_engine_;
}

std::shared_ptr<AnimationSystem> DisplayManager::getAnimationSystem() const {
    return animation_system_;
}

std::shared_ptr<ThemeManager> DisplayManager::getThemeManager() const {
    return theme_manager_;
}

ErrorCode DisplayManager::addComponent(std::shared_ptr<UIComponent> component) {
    if (!initialized_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    if (!component) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    std::lock_guard<std::mutex> lock(components_mutex_);
    
    // Check if component with same ID already exists
    for (const auto& existing : components_) {
        if (existing->getId() == component->getId()) {
            return ErrorCode::COMPONENT_ALREADY_EXISTS;
        }
    }
    
    components_.push_back(component);
    
    ErrorHandler::getInstance().logInfo("UI component added: " + component->getId());
    return ErrorCode::SUCCESS;
}

ErrorCode DisplayManager::removeComponent(const std::string& component_id) {
    if (!initialized_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    std::lock_guard<std::mutex> lock(components_mutex_);
    
    for (auto it = components_.begin(); it != components_.end(); ++it) {
        if ((*it)->getId() == component_id) {
            components_.erase(it);
            ErrorHandler::getInstance().logInfo("UI component removed: " + component_id);
            return ErrorCode::SUCCESS;
        }
    }
    
    return ErrorCode::COMPONENT_NOT_FOUND;
}

std::shared_ptr<UIComponent> DisplayManager::getComponent(const std::string& component_id) const {
    std::lock_guard<std::mutex> lock(components_mutex_);
    
    for (const auto& component : components_) {
        if (component->getId() == component_id) {
            return component;
        }
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<UIComponent>> DisplayManager::getComponents() const {
    std::lock_guard<std::mutex> lock(components_mutex_);
    return components_;
}

ErrorCode DisplayManager::renderComponents() {
    if (!initialized_ || !graphics_engine_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    std::lock_guard<std::mutex> lock(components_mutex_);
    
    try {
        // Sort components by Z-index
        std::vector<std::shared_ptr<UIComponent>> sorted_components = components_;
        std::sort(sorted_components.begin(), sorted_components.end(),
                 [](const std::shared_ptr<UIComponent>& a, const std::shared_ptr<UIComponent>& b) {
                     return a->getZIndex() < b->getZIndex();
                 });
        
        // Render each component
        for (const auto& component : sorted_components) {
            if (component->isVisible()) {
                ErrorCode result = component->render(*graphics_engine_);
                if (result != ErrorCode::SUCCESS) {
                    ErrorHandler::getInstance().logError(result, 
                                                       "Failed to render component: " + component->getId());
                }
            }
        }
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::DISPLAY_RENDER_FAILED, 
                                           "Component rendering exception: " + std::string(e.what()));
        return ErrorCode::DISPLAY_RENDER_FAILED;
    }
}

void DisplayManager::registerCallback(DisplayCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = callback;
}

void DisplayManager::unregisterCallback() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = nullptr;
}

DisplayMetrics DisplayManager::getMetrics() const {
    return metrics_;
}

void DisplayManager::resetMetrics() {
    memset(&metrics_, 0, sizeof(metrics_));
    metrics_.render_time_min = UINT32_MAX;
}

bool DisplayManager::supportsTouch() const {
    // Implementation depends on specific display hardware
    return false;
}

bool DisplayManager::supportsBacklight() const {
    return config_.backlight_control;
}

bool DisplayManager::supportsColor() const {
    switch (config_.type) {
        case DisplayType::OLED_SSD1306:
        case DisplayType::OLED_SSD1309:
        case DisplayType::LCD_16X2:
        case DisplayType::LED_MATRIX:
            return false;
        case DisplayType::TFT_ILI9341:
        case DisplayType::TFT_ST7735:
        case DisplayType::E_PAPER:
            return true;
        default:
            return false;
    }
}

std::vector<std::string> DisplayManager::getCapabilities() const {
    std::vector<std::string> capabilities;
    
    capabilities.push_back("Graphics");
    capabilities.push_back("Text");
    capabilities.push_back("Shapes");
    
    if (supportsColor()) {
        capabilities.push_back("Color");
    }
    
    if (supportsTouch()) {
        capabilities.push_back("Touch");
    }
    
    if (supportsBacklight()) {
        capabilities.push_back("Backlight");
    }
    
    if (animation_system_) {
        capabilities.push_back("Animations");
    }
    
    if (theme_manager_) {
        capabilities.push_back("Themes");
    }
    
    return capabilities;
}

ErrorCode DisplayManager::selfTest() {
    if (!initialized_) {
        return ErrorCode::DISPLAY_NOT_INITIALIZED;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        error_handler.logInfo("Starting display self-test");
        
        // Test basic drawing operations
        ErrorCode result = clear(0x000000);
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Clear failed");
        }
        
        result = drawRect(10, 10, 100, 50, 0xFFFFFF, true);
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Rectangle failed");
        }
        
        result = drawText(20, 30, "TEST", 0x000000);
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Text failed");
        }
        
        result = update();
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Update failed");
        }
        
        error_handler.logInfo("Display self-test completed successfully");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::DISPLAY_SELFTEST_FAILED, 
                                    "Self-test exception: " + std::string(e.what()));
    }
}

std::string DisplayManager::getDisplayInfo() const {
    std::string info = "Display: " + std::to_string(config_.width) + "x" + std::to_string(config_.height);
    info += ", Type: " + std::to_string(static_cast<int>(config_.type));
    info += ", Orientation: " + std::to_string(static_cast<int>(config_.orientation));
    info += ", Power: " + std::to_string(static_cast<int>(power_state_));
    info += ", Brightness: " + std::to_string(config_.brightness);
    info += ", Components: " + std::to_string(components_.size());
    return info;
}

// Private methods

void DisplayManager::updateMetrics(uint32_t render_time) {
    metrics_.frames_rendered++;
    metrics_.render_time_total += render_time;
    metrics_.render_time_avg = metrics_.render_time_total / metrics_.frames_rendered;
    
    if (render_time > metrics_.render_time_max) {
        metrics_.render_time_max = render_time;
    }
    
    if (render_time < metrics_.render_time_min) {
        metrics_.render_time_min = render_time;
    }
}

void DisplayManager::notifyEvent(const DisplayEvent& event) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (event_callback_) {
        event_callback_(event);
    }
}

ErrorCode DisplayManager::initializeDisplay() {
    // This is a placeholder implementation
    // Real implementation would depend on specific display hardware
    ErrorHandler::getInstance().logInfo("Display hardware initialized");
    return ErrorCode::SUCCESS;
}

void DisplayManager::cleanupDisplay() {
    // This is a placeholder implementation
    // Real implementation would depend on specific display hardware
    ErrorHandler::getInstance().logInfo("Display hardware cleaned up");
}

bool DisplayManager::detectDisplayType() {
    // This is a placeholder implementation
    // Real implementation would detect actual display hardware
    return true;
}

ErrorCode DisplayManager::configureDisplay() {
    // This is a placeholder implementation
    // Real implementation would configure actual display hardware
    ErrorHandler::getInstance().logInfo("Display hardware configured");
    return ErrorCode::SUCCESS;
}

void DisplayManager::handlePowerManagement() {
    if (!config_.auto_off) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto time_since_last_activity = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_power_check_).count();
    
    if (time_since_last_activity > config_.auto_off_delay && 
        power_state_ == DisplayPowerState::ON) {
        setPowerState(DisplayPowerState::OFF);
    }
    
    last_power_check_ = now;
}

} // namespace WeatherStation 