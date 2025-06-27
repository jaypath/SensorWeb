#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include "../utils/ErrorHandler.hpp"

namespace WeatherStation {

// Forward declarations
class GraphicsEngine;
class UIComponent;
class AnimationSystem;
class ThemeManager;

/**
 * @brief Display types supported by the system
 */
enum class DisplayType {
    OLED_SSD1306,      // 128x64 OLED display
    OLED_SSD1309,      // 128x64 OLED display (variant)
    TFT_ILI9341,       // 240x320 TFT display
    TFT_ST7735,        // 128x160 TFT display
    E_PAPER,           // E-paper display
    LED_MATRIX,        // LED matrix display
    LCD_16X2,          // 16x2 LCD display
    CUSTOM             // Custom display type
};

/**
 * @brief Display orientation settings
 */
enum class DisplayOrientation {
    PORTRAIT,          // Portrait orientation
    LANDSCAPE,         // Landscape orientation
    PORTRAIT_FLIP,     // Portrait flipped
    LANDSCAPE_FLIP     // Landscape flipped
};

/**
 * @brief Display power states
 */
enum class DisplayPowerState {
    ON,                // Display is on and active
    OFF,               // Display is off (power saving)
    DIM,               // Display is dimmed
    SLEEP              // Display is in sleep mode
};

/**
 * @brief Display configuration structure
 */
struct DisplayConfig {
    DisplayType type = DisplayType::OLED_SSD1306;
    DisplayOrientation orientation = DisplayOrientation::PORTRAIT;
    uint16_t width = 128;
    uint16_t height = 64;
    uint8_t brightness = 255;
    bool auto_off = true;
    uint32_t auto_off_delay = 30000; // 30 seconds
    bool backlight_control = true;
    uint8_t spi_cs_pin = 5;
    uint8_t spi_dc_pin = 4;
    uint8_t spi_rst_pin = 2;
    uint8_t i2c_sda_pin = 21;
    uint8_t i2c_scl_pin = 22;
    uint32_t i2c_frequency = 400000;
    uint32_t spi_frequency = 10000000;
};

/**
 * @brief Display metrics and statistics
 */
struct DisplayMetrics {
    uint32_t frames_rendered = 0;
    uint32_t render_time_total = 0;
    uint32_t render_time_avg = 0;
    uint32_t render_time_max = 0;
    uint32_t render_time_min = UINT32_MAX;
    uint32_t power_on_time = 0;
    uint32_t power_off_time = 0;
    uint32_t brightness_changes = 0;
    uint32_t orientation_changes = 0;
    uint32_t errors = 0;
    uint32_t last_error_time = 0;
    std::string last_error_message;
};

/**
 * @brief Display event types
 */
enum class DisplayEventType {
    POWER_ON,
    POWER_OFF,
    BRIGHTNESS_CHANGE,
    ORIENTATION_CHANGE,
    ERROR,
    RENDER_COMPLETE,
    TOUCH_EVENT,
    BUTTON_PRESS
};

/**
 * @brief Display event structure
 */
struct DisplayEvent {
    DisplayEventType type;
    uint64_t timestamp;
    std::string data;
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t pressure = 0;
};

/**
 * @brief Display callback function type
 */
using DisplayCallback = std::function<void(const DisplayEvent&)>;

/**
 * @brief DisplayManager - Unified display interface management
 * 
 * Provides a unified interface for managing different types of displays
 * with support for graphics rendering, UI components, animations, and themes.
 */
class DisplayManager {
public:
    /**
     * @brief Get singleton instance
     */
    static DisplayManager* getInstance();
    
    /**
     * @brief Destroy singleton instance
     */
    static void destroyInstance();
    
    /**
     * @brief Initialize the display manager
     * @param config Display configuration
     * @return ErrorCode indicating success or failure
     */
    ErrorCode initialize(const DisplayConfig& config);
    
    /**
     * @brief Shutdown the display manager
     */
    void shutdown();
    
    /**
     * @brief Check if display manager is initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * @brief Get current display configuration
     */
    DisplayConfig getConfig() const;
    
    /**
     * @brief Update display configuration
     * @param config New configuration
     * @return ErrorCode indicating success or failure
     */
    ErrorCode setConfig(const DisplayConfig& config);
    
    /**
     * @brief Get display dimensions
     */
    std::pair<uint16_t, uint16_t> getDimensions() const;
    
    /**
     * @brief Get display type
     */
    DisplayType getDisplayType() const;
    
    /**
     * @brief Set display power state
     * @param state New power state
     * @return ErrorCode indicating success or failure
     */
    ErrorCode setPowerState(DisplayPowerState state);
    
    /**
     * @brief Get current power state
     */
    DisplayPowerState getPowerState() const;
    
    /**
     * @brief Set display brightness
     * @param brightness Brightness value (0-255)
     * @return ErrorCode indicating success or failure
     */
    ErrorCode setBrightness(uint8_t brightness);
    
    /**
     * @brief Get current brightness
     */
    uint8_t getBrightness() const;
    
    /**
     * @brief Set display orientation
     * @param orientation New orientation
     * @return ErrorCode indicating success or failure
     */
    ErrorCode setOrientation(DisplayOrientation orientation);
    
    /**
     * @brief Get current orientation
     */
    DisplayOrientation getOrientation() const;
    
    /**
     * @brief Clear the display
     * @param color Background color (default: black)
     * @return ErrorCode indicating success or failure
     */
    ErrorCode clear(uint32_t color = 0x000000);
    
    /**
     * @brief Update the display (refresh)
     * @return ErrorCode indicating success or failure
     */
    ErrorCode update();
    
    /**
     * @brief Draw a pixel
     * @param x X coordinate
     * @param y Y coordinate
     * @param color Pixel color
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawPixel(uint16_t x, uint16_t y, uint32_t color);
    
    /**
     * @brief Draw a line
     * @param x1 Start X coordinate
     * @param y1 Start Y coordinate
     * @param x2 End X coordinate
     * @param y2 End Y coordinate
     * @param color Line color
     * @param thickness Line thickness
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, 
                      uint32_t color, uint8_t thickness = 1);
    
    /**
     * @brief Draw a rectangle
     * @param x X coordinate
     * @param y Y coordinate
     * @param width Rectangle width
     * @param height Rectangle height
     * @param color Rectangle color
     * @param filled Whether to fill the rectangle
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, 
                      uint32_t color, bool filled = false);
    
    /**
     * @brief Draw a circle
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @param radius Circle radius
     * @param color Circle color
     * @param filled Whether to fill the circle
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawCircle(uint16_t x, uint16_t y, uint16_t radius, 
                        uint32_t color, bool filled = false);
    
    /**
     * @brief Draw text
     * @param x X coordinate
     * @param y Y coordinate
     * @param text Text to draw
     * @param color Text color
     * @param font_size Font size
     * @param font_name Font name
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawText(uint16_t x, uint16_t y, const std::string& text, 
                      uint32_t color, uint8_t font_size = 12, 
                      const std::string& font_name = "default");
    
    /**
     * @brief Draw an image
     * @param x X coordinate
     * @param y Y coordinate
     * @param image_data Image data
     * @param width Image width
     * @param height Image height
     * @param format Image format
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawImage(uint16_t x, uint16_t y, const uint8_t* image_data, 
                       uint16_t width, uint16_t height, 
                       const std::string& format = "RGB565");
    
    /**
     * @brief Get graphics engine instance
     */
    std::shared_ptr<GraphicsEngine> getGraphicsEngine() const;
    
    /**
     * @brief Get animation system instance
     */
    std::shared_ptr<AnimationSystem> getAnimationSystem() const;
    
    /**
     * @brief Get theme manager instance
     */
    std::shared_ptr<ThemeManager> getThemeManager() const;
    
    /**
     * @brief Add a UI component
     * @param component UI component to add
     * @return ErrorCode indicating success or failure
     */
    ErrorCode addComponent(std::shared_ptr<UIComponent> component);
    
    /**
     * @brief Remove a UI component
     * @param component_id Component ID to remove
     * @return ErrorCode indicating success or failure
     */
    ErrorCode removeComponent(const std::string& component_id);
    
    /**
     * @brief Get a UI component by ID
     * @param component_id Component ID
     * @return Shared pointer to UI component or nullptr if not found
     */
    std::shared_ptr<UIComponent> getComponent(const std::string& component_id) const;
    
    /**
     * @brief Get all UI components
     */
    std::vector<std::shared_ptr<UIComponent>> getComponents() const;
    
    /**
     * @brief Render all UI components
     * @return ErrorCode indicating success or failure
     */
    ErrorCode renderComponents();
    
    /**
     * @brief Register display event callback
     * @param callback Function to call on display events
     */
    void registerCallback(DisplayCallback callback);
    
    /**
     * @brief Unregister display event callback
     */
    void unregisterCallback();
    
    /**
     * @brief Get display metrics
     */
    DisplayMetrics getMetrics() const;
    
    /**
     * @brief Reset display metrics
     */
    void resetMetrics();
    
    /**
     * @brief Check if display supports touch input
     */
    bool supportsTouch() const;
    
    /**
     * @brief Check if display supports backlight control
     */
    bool supportsBacklight() const;
    
    /**
     * @brief Check if display supports color
     */
    bool supportsColor() const;
    
    /**
     * @brief Get display capabilities
     */
    std::vector<std::string> getCapabilities() const;
    
    /**
     * @brief Perform display self-test
     * @return ErrorCode indicating success or failure
     */
    ErrorCode selfTest();
    
    /**
     * @brief Get display information string
     */
    std::string getDisplayInfo() const;

private:
    // Singleton instance
    static DisplayManager* instance_;
    static std::mutex mutex_;
    
    // Display state
    bool initialized_;
    DisplayConfig config_;
    DisplayPowerState power_state_;
    DisplayMetrics metrics_;
    
    // Subsystems
    std::shared_ptr<GraphicsEngine> graphics_engine_;
    std::shared_ptr<AnimationSystem> animation_system_;
    std::shared_ptr<ThemeManager> theme_manager_;
    
    // UI components
    std::vector<std::shared_ptr<UIComponent>> components_;
    mutable std::mutex components_mutex_;
    
    // Event handling
    DisplayCallback event_callback_;
    mutable std::mutex callback_mutex_;
    
    // Internal methods
    void updateMetrics(uint32_t render_time);
    void notifyEvent(const DisplayEvent& event);
    ErrorCode initializeDisplay();
    void cleanupDisplay();
    bool detectDisplayType();
    ErrorCode configureDisplay();
    void handlePowerManagement();
    
    // Timing
    std::chrono::steady_clock::time_point last_render_time_;
    std::chrono::steady_clock::time_point last_power_check_;
    
    // Private constructor for singleton
    DisplayManager();
    
    // Disable copy constructor and assignment
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;
};

} // namespace WeatherStation 