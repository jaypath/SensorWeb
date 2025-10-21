#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <chrono>
#include "../utils/ErrorHandler.hpp"

namespace WeatherStation {

// Forward declarations
class Font;
class Image;
class Canvas;
class RenderTarget;

/**
 * @brief Color formats supported by the graphics engine
 */
enum class ColorFormat {
    RGB565,            // 16-bit RGB (5-6-5)
    RGB888,            // 24-bit RGB (8-8-8)
    RGBA8888,          // 32-bit RGBA (8-8-8-8)
    GRAYSCALE,         // 8-bit grayscale
    MONOCHROME,        // 1-bit monochrome
    INDEXED            // 8-bit indexed color
};

/**
 * @brief Drawing modes for graphics operations
 */
enum class DrawMode {
    NORMAL,            // Normal drawing mode
    XOR,               // XOR drawing mode
    ALPHA_BLEND,       // Alpha blending mode
    MASK,              // Mask drawing mode
    INVERT             // Invert drawing mode
};

/**
 * @brief Text alignment options
 */
enum class TextAlign {
    LEFT,              // Left alignment
    CENTER,            // Center alignment
    RIGHT,             // Right alignment
    JUSTIFY            // Justified alignment
};

/**
 * @brief Vertical text alignment options
 */
enum class TextVAlign {
    TOP,               // Top alignment
    MIDDLE,            // Middle alignment
    BOTTOM             // Bottom alignment
};

/**
 * @brief Line styles for drawing
 */
enum class LineStyle {
    SOLID,             // Solid line
    DASHED,            // Dashed line
    DOTTED,            // Dotted line
    DASH_DOT,          // Dash-dot line
    DASH_DOT_DOT       // Dash-dot-dot line
};

/**
 * @brief Fill patterns for shapes
 */
enum class FillPattern {
    SOLID,             // Solid fill
    HORIZONTAL_LINES,  // Horizontal lines
    VERTICAL_LINES,    // Vertical lines
    DIAGONAL_LINES,    // Diagonal lines
    CROSS_HATCH,       // Cross hatch
    DIAMOND,           // Diamond pattern
    CIRCLES,           // Circle pattern
    CUSTOM             // Custom pattern
};

/**
 * @brief Graphics context for drawing operations
 */
struct GraphicsContext {
    uint32_t foreground_color = 0xFFFFFF;
    uint32_t background_color = 0x000000;
    uint8_t alpha = 255;
    uint8_t line_width = 1;
    LineStyle line_style = LineStyle::SOLID;
    FillPattern fill_pattern = FillPattern::SOLID;
    DrawMode draw_mode = DrawMode::NORMAL;
    std::string font_name = "default";
    uint8_t font_size = 12;
    TextAlign text_align = TextAlign::LEFT;
    TextVAlign text_valign = TextVAlign::TOP;
    bool antialiasing = true;
    bool clipping_enabled = true;
    uint16_t clip_x = 0;
    uint16_t clip_y = 0;
    uint16_t clip_width = 0;
    uint16_t clip_height = 0;
};

/**
 * @brief Point structure for 2D coordinates
 */
struct Point {
    int16_t x;
    int16_t y;
    
    Point(int16_t x = 0, int16_t y = 0) : x(x), y(y) {}
    
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
    
    bool operator!=(const Point& other) const {
        return !(*this == other);
    }
};

/**
 * @brief Rectangle structure
 */
struct Rectangle {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
    
    Rectangle(int16_t x = 0, int16_t y = 0, uint16_t width = 0, uint16_t height = 0)
        : x(x), y(y), width(width), height(height) {}
    
    bool contains(const Point& point) const {
        return point.x >= x && point.x < x + width &&
               point.y >= y && point.y < y + height;
    }
    
    bool intersects(const Rectangle& other) const {
        return !(x + width <= other.x || other.x + other.width <= x ||
                y + height <= other.y || other.y + other.height <= y);
    }
    
    Rectangle intersection(const Rectangle& other) const {
        if (!intersects(other)) {
            return Rectangle();
        }
        
        int16_t left = std::max(x, other.x);
        int16_t top = std::max(y, other.y);
        int16_t right = std::min(x + width, other.x + other.width);
        int16_t bottom = std::min(y + height, other.y + other.height);
        
        return Rectangle(left, top, right - left, bottom - top);
    }
};

/**
 * @brief Color structure with alpha support
 */
struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    
    Color(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}
    
    Color(uint32_t rgba) {
        r = (rgba >> 24) & 0xFF;
        g = (rgba >> 16) & 0xFF;
        b = (rgba >> 8) & 0xFF;
        a = rgba & 0xFF;
    }
    
    uint32_t toRGBA() const {
        return (static_cast<uint32_t>(r) << 24) |
               (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) << 8) |
               static_cast<uint32_t>(a);
    }
    
    uint16_t toRGB565() const {
        uint8_t r5 = (r * 31) / 255;
        uint8_t g6 = (g * 63) / 255;
        uint8_t b5 = (b * 31) / 255;
        return (static_cast<uint16_t>(r5) << 11) |
               (static_cast<uint16_t>(g6) << 5) |
               static_cast<uint16_t>(b5);
    }
    
    static Color fromRGB565(uint16_t rgb565) {
        uint8_t r5 = (rgb565 >> 11) & 0x1F;
        uint8_t g6 = (rgb565 >> 5) & 0x3F;
        uint8_t b5 = rgb565 & 0x1F;
        
        uint8_t r = (r5 * 255) / 31;
        uint8_t g = (g6 * 255) / 63;
        uint8_t b = (b5 * 255) / 31;
        
        return Color(r, g, b);
    }
};

/**
 * @brief Graphics metrics for performance monitoring
 */
struct GraphicsMetrics {
    uint32_t draw_calls = 0;
    uint32_t pixels_drawn = 0;
    uint32_t primitives_drawn = 0;
    uint32_t render_time_total = 0;
    uint32_t render_time_avg = 0;
    uint32_t render_time_max = 0;
    uint32_t render_time_min = UINT32_MAX;
    uint32_t memory_used = 0;
    uint32_t texture_memory = 0;
    uint32_t font_memory = 0;
    uint32_t errors = 0;
    std::string last_error;
};

/**
 * @brief GraphicsEngine - Advanced graphics rendering engine
 * 
 * Provides high-level graphics operations including shapes, text, images,
 * and advanced rendering features optimized for embedded systems.
 */
class GraphicsEngine {
public:
    /**
     * @brief Constructor
     * @param width Display width
     * @param height Display height
     * @param color_format Color format
     */
    GraphicsEngine(uint16_t width, uint16_t height, ColorFormat color_format = ColorFormat::RGB565);
    
    /**
     * @brief Destructor
     */
    ~GraphicsEngine();
    
    /**
     * @brief Initialize the graphics engine
     * @return ErrorCode indicating success or failure
     */
    ErrorCode initialize();
    
    /**
     * @brief Shutdown the graphics engine
     */
    void shutdown();
    
    /**
     * @brief Check if graphics engine is initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * @brief Get display dimensions
     */
    std::pair<uint16_t, uint16_t> getDimensions() const;
    
    /**
     * @brief Get color format
     */
    ColorFormat getColorFormat() const;
    
    /**
     * @brief Get graphics context
     */
    GraphicsContext getContext() const;
    
    /**
     * @brief Set graphics context
     * @param context New graphics context
     */
    void setContext(const GraphicsContext& context);
    
    /**
     * @brief Save current graphics context
     */
    void saveContext();
    
    /**
     * @brief Restore previously saved graphics context
     */
    void restoreContext();
    
    /**
     * @brief Clear the entire display
     * @param color Background color
     * @return ErrorCode indicating success or failure
     */
    ErrorCode clear(const Color& color = Color(0, 0, 0));
    
    /**
     * @brief Clear a rectangular area
     * @param rect Rectangle to clear
     * @param color Background color
     * @return ErrorCode indicating success or failure
     */
    ErrorCode clearRect(const Rectangle& rect, const Color& color = Color(0, 0, 0));
    
    /**
     * @brief Draw a pixel
     * @param x X coordinate
     * @param y Y coordinate
     * @param color Pixel color
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawPixel(int16_t x, int16_t y, const Color& color);
    
    /**
     * @brief Get pixel color
     * @param x X coordinate
     * @param y Y coordinate
     * @return Pixel color
     */
    Color getPixel(int16_t x, int16_t y) const;
    
    /**
     * @brief Draw a line
     * @param x1 Start X coordinate
     * @param y1 Start Y coordinate
     * @param x2 End X coordinate
     * @param y2 End Y coordinate
     * @param color Line color
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const Color& color);
    
    /**
     * @brief Draw a line with thickness
     * @param x1 Start X coordinate
     * @param y1 Start Y coordinate
     * @param x2 End X coordinate
     * @param y2 End Y coordinate
     * @param color Line color
     * @param thickness Line thickness
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawThickLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, 
                           const Color& color, uint8_t thickness);
    
    /**
     * @brief Draw a rectangle
     * @param rect Rectangle to draw
     * @param color Rectangle color
     * @param filled Whether to fill the rectangle
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawRect(const Rectangle& rect, const Color& color, bool filled = false);
    
    /**
     * @brief Draw a rounded rectangle
     * @param rect Rectangle to draw
     * @param radius Corner radius
     * @param color Rectangle color
     * @param filled Whether to fill the rectangle
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawRoundRect(const Rectangle& rect, uint8_t radius, 
                           const Color& color, bool filled = false);
    
    /**
     * @brief Draw a circle
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @param radius Circle radius
     * @param color Circle color
     * @param filled Whether to fill the circle
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawCircle(int16_t x, int16_t y, uint16_t radius, 
                        const Color& color, bool filled = false);
    
    /**
     * @brief Draw an ellipse
     * @param x Center X coordinate
     * @param y Center Y coordinate
     * @param radius_x X radius
     * @param radius_y Y radius
     * @param color Ellipse color
     * @param filled Whether to fill the ellipse
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawEllipse(int16_t x, int16_t y, uint16_t radius_x, uint16_t radius_y,
                         const Color& color, bool filled = false);
    
    /**
     * @brief Draw a triangle
     * @param x1 First vertex X
     * @param y1 First vertex Y
     * @param x2 Second vertex X
     * @param y2 Second vertex Y
     * @param x3 Third vertex X
     * @param y3 Third vertex Y
     * @param color Triangle color
     * @param filled Whether to fill the triangle
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                          int16_t x3, int16_t y3, const Color& color, bool filled = false);
    
    /**
     * @brief Draw a polygon
     * @param points Array of polygon points
     * @param num_points Number of points
     * @param color Polygon color
     * @param filled Whether to fill the polygon
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawPolygon(const Point* points, size_t num_points,
                         const Color& color, bool filled = false);
    
    /**
     * @brief Draw text
     * @param x X coordinate
     * @param y Y coordinate
     * @param text Text to draw
     * @param color Text color
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawText(int16_t x, int16_t y, const std::string& text, const Color& color);
    
    /**
     * @brief Draw text with alignment
     * @param rect Bounding rectangle
     * @param text Text to draw
     * @param color Text color
     * @param halign Horizontal alignment
     * @param valign Vertical alignment
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawText(const Rectangle& rect, const std::string& text, const Color& color,
                      TextAlign halign = TextAlign::LEFT, TextVAlign valign = TextVAlign::TOP);
    
    /**
     * @brief Get text dimensions
     * @param text Text to measure
     * @return Text dimensions as rectangle
     */
    Rectangle getTextBounds(const std::string& text) const;
    
    /**
     * @brief Draw an image
     * @param x X coordinate
     * @param y Y coordinate
     * @param image Image to draw
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawImage(int16_t x, int16_t y, const std::shared_ptr<Image>& image);
    
    /**
     * @brief Draw a portion of an image
     * @param x Destination X coordinate
     * @param y Destination Y coordinate
     * @param image Image to draw
     * @param src_rect Source rectangle in image
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawImage(int16_t x, int16_t y, const std::shared_ptr<Image>& image,
                       const Rectangle& src_rect);
    
    /**
     * @brief Set clipping rectangle
     * @param rect Clipping rectangle
     */
    void setClipRect(const Rectangle& rect);
    
    /**
     * @brief Get current clipping rectangle
     */
    Rectangle getClipRect() const;
    
    /**
     * @brief Clear clipping rectangle
     */
    void clearClipRect();
    
    /**
     * @brief Load a font
     * @param name Font name
     * @param size Font size
     * @param data Font data
     * @return ErrorCode indicating success or failure
     */
    ErrorCode loadFont(const std::string& name, uint8_t size, const uint8_t* data);
    
    /**
     * @brief Get loaded font
     * @param name Font name
     * @param size Font size
     * @return Shared pointer to font or nullptr if not found
     */
    std::shared_ptr<Font> getFont(const std::string& name, uint8_t size) const;
    
    /**
     * @brief Load an image
     * @param name Image name
     * @param data Image data
     * @param width Image width
     * @param height Image height
     * @param format Image format
     * @return ErrorCode indicating success or failure
     */
    ErrorCode loadImage(const std::string& name, const uint8_t* data,
                       uint16_t width, uint16_t height, ColorFormat format);
    
    /**
     * @brief Get loaded image
     * @param name Image name
     * @return Shared pointer to image or nullptr if not found
     */
    std::shared_ptr<Image> getImage(const std::string& name) const;
    
    /**
     * @brief Create a canvas for off-screen drawing
     * @param width Canvas width
     * @param height Canvas height
     * @return Shared pointer to canvas
     */
    std::shared_ptr<Canvas> createCanvas(uint16_t width, uint16_t height);
    
    /**
     * @brief Draw canvas to display
     * @param x Destination X coordinate
     * @param y Destination Y coordinate
     * @param canvas Canvas to draw
     * @return ErrorCode indicating success or failure
     */
    ErrorCode drawCanvas(int16_t x, int16_t y, const std::shared_ptr<Canvas>& canvas);
    
    /**
     * @brief Get graphics metrics
     */
    GraphicsMetrics getMetrics() const;
    
    /**
     * @brief Reset graphics metrics
     */
    void resetMetrics();
    
    /**
     * @brief Get memory usage information
     */
    std::map<std::string, uint32_t> getMemoryUsage() const;
    
    /**
     * @brief Perform graphics engine self-test
     * @return ErrorCode indicating success or failure
     */
    ErrorCode selfTest();

private:
    // Display properties
    uint16_t width_;
    uint16_t height_;
    ColorFormat color_format_;
    bool initialized_;
    
    // Graphics context
    GraphicsContext current_context_;
    std::vector<GraphicsContext> context_stack_;
    
    // Resources
    std::map<std::string, std::shared_ptr<Font>> fonts_;
    std::map<std::string, std::shared_ptr<Image>> images_;
    mutable std::mutex resources_mutex_;
    
    // Metrics
    GraphicsMetrics metrics_;
    mutable std::mutex metrics_mutex_;
    
    // Internal methods
    void updateMetrics(uint32_t render_time);
    bool isPointInClipRect(int16_t x, int16_t y) const;
    ErrorCode drawLineBresenham(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const Color& color);
    ErrorCode drawCircleBresenham(int16_t x, int16_t y, uint16_t radius, const Color& color, bool filled);
    ErrorCode drawEllipseBresenham(int16_t x, int16_t y, uint16_t radius_x, uint16_t radius_y,
                                  const Color& color, bool filled);
    ErrorCode fillPolygon(const Point* points, size_t num_points, const Color& color);
    ErrorCode drawTextInternal(int16_t x, int16_t y, const std::string& text, const Color& color);
    Rectangle calculateTextBounds(const std::string& text) const;
    
    // Disable copy constructor and assignment
    GraphicsEngine(const GraphicsEngine&) = delete;
    GraphicsEngine& operator=(const GraphicsEngine&) = delete;
};

} // namespace WeatherStation 