#include "GraphicsEngine.hpp"
#include "../utils/ErrorHandler.hpp"
#include "../utils/StringUtils.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace WeatherStation {

GraphicsEngine::GraphicsEngine(uint16_t width, uint16_t height, ColorFormat color_format)
    : width_(width)
    , height_(height)
    , color_format_(color_format)
    , initialized_(false) {
    
    // Initialize metrics
    memset(&metrics_, 0, sizeof(metrics_));
    metrics_.render_time_min = UINT32_MAX;
    
    // Set default context
    current_context_.foreground_color = 0xFFFFFF;
    current_context_.background_color = 0x000000;
    current_context_.alpha = 255;
    current_context_.line_width = 1;
    current_context_.line_style = LineStyle::SOLID;
    current_context_.fill_pattern = FillPattern::SOLID;
    current_context_.draw_mode = DrawMode::NORMAL;
    current_context_.font_name = "default";
    current_context_.font_size = 12;
    current_context_.text_align = TextAlign::LEFT;
    current_context_.text_valign = TextVAlign::TOP;
    current_context_.antialiasing = true;
    current_context_.clipping_enabled = true;
    current_context_.clip_x = 0;
    current_context_.clip_y = 0;
    current_context_.clip_width = width;
    current_context_.clip_height = height;
}

GraphicsEngine::~GraphicsEngine() {
    shutdown();
}

ErrorCode GraphicsEngine::initialize() {
    if (initialized_) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        // Initialize display buffer based on color format
        // This is a simplified implementation - real implementation would
        // allocate actual display memory and initialize hardware
        
        // Load default font
        // This would load a built-in font for text rendering
        
        // Load default images
        // This would load any built-in images
        
        initialized_ = true;
        
        error_handler.logInfo("GraphicsEngine initialized successfully");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::GRAPHICS_INIT_FAILED, 
                                    "Graphics initialization exception: " + std::string(e.what()));
    }
}

void GraphicsEngine::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Clear resources
    {
        std::lock_guard<std::mutex> lock(resources_mutex_);
        fonts_.clear();
        images_.clear();
    }
    
    // Clear context stack
    context_stack_.clear();
    
    initialized_ = false;
    
    ErrorHandler::getInstance().logInfo("GraphicsEngine shutdown complete");
}

std::pair<uint16_t, uint16_t> GraphicsEngine::getDimensions() const {
    return std::make_pair(width_, height_);
}

ColorFormat GraphicsEngine::getColorFormat() const {
    return color_format_;
}

GraphicsContext GraphicsEngine::getContext() const {
    return current_context_;
}

void GraphicsEngine::setContext(const GraphicsContext& context) {
    current_context_ = context;
}

void GraphicsEngine::saveContext() {
    context_stack_.push_back(current_context_);
}

void GraphicsEngine::restoreContext() {
    if (!context_stack_.empty()) {
        current_context_ = context_stack_.back();
        context_stack_.pop_back();
    }
}

ErrorCode GraphicsEngine::clear(const Color& color) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Clear entire display with background color
        // This is a simplified implementation - real implementation would
        // clear actual display memory
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.draw_calls++;
        metrics_.pixels_drawn += width_ * height_;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_CLEAR_FAILED, 
                                           "Clear exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_CLEAR_FAILED;
    }
}

ErrorCode GraphicsEngine::clearRect(const Rectangle& rect, const Color& color) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Clear rectangular area
        // This is a simplified implementation - real implementation would
        // clear specific area in display memory
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.draw_calls++;
        metrics_.pixels_drawn += rect.width * rect.height;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_CLEAR_FAILED, 
                                           "ClearRect exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_CLEAR_FAILED;
    }
}

ErrorCode GraphicsEngine::drawPixel(int16_t x, int16_t y, const Color& color) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    if (current_context_.clipping_enabled && !isPointInClipRect(x, y)) {
        return ErrorCode::SUCCESS; // Pixel outside clip area
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Draw pixel at specified coordinates
        // This is a simplified implementation - real implementation would
        // write to actual display memory
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.draw_calls++;
        metrics_.pixels_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawPixel exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

Color GraphicsEngine::getPixel(int16_t x, int16_t y) const {
    if (!initialized_ || x < 0 || x >= width_ || y < 0 || y >= height_) {
        return Color();
    }
    
    // This is a simplified implementation - real implementation would
    // read from actual display memory
    return Color(0, 0, 0); // Return black as default
}

ErrorCode GraphicsEngine::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const Color& color) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    return drawLineBresenham(x1, y1, x2, y2, color);
}

ErrorCode GraphicsEngine::drawThickLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, 
                                       const Color& color, uint8_t thickness) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    if (thickness == 1) {
        return drawLine(x1, y1, x2, y2, color);
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Draw thick line by drawing multiple parallel lines
        // This is a simplified implementation
        
        // Calculate perpendicular vector
        int16_t dx = x2 - x1;
        int16_t dy = y2 - y1;
        float length = std::sqrt(dx * dx + dy * dy);
        
        if (length == 0) {
            return drawPixel(x1, y1, color);
        }
        
        // Normalize perpendicular vector
        float perp_x = -dy / length;
        float perp_y = dx / length;
        
        // Draw multiple lines
        int16_t half_thickness = thickness / 2;
        for (int16_t i = -half_thickness; i <= half_thickness; i++) {
            int16_t offset_x = static_cast<int16_t>(perp_x * i);
            int16_t offset_y = static_cast<int16_t>(perp_y * i);
            drawLine(x1 + offset_x, y1 + offset_y, x2 + offset_x, y2 + offset_y, color);
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawThickLine exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawRect(const Rectangle& rect, const Color& color, bool filled) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (filled) {
            // Draw filled rectangle
            for (int16_t y = rect.y; y < rect.y + rect.height; y++) {
                for (int16_t x = rect.x; x < rect.x + rect.width; x++) {
                    drawPixel(x, y, color);
                }
            }
        } else {
            // Draw rectangle outline
            drawLine(rect.x, rect.y, rect.x + rect.width - 1, rect.y, color);
            drawLine(rect.x + rect.width - 1, rect.y, rect.x + rect.width - 1, rect.y + rect.height - 1, color);
            drawLine(rect.x + rect.width - 1, rect.y + rect.height - 1, rect.x, rect.y + rect.height - 1, color);
            drawLine(rect.x, rect.y + rect.height - 1, rect.x, rect.y, color);
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawRect exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawRoundRect(const Rectangle& rect, uint8_t radius, 
                                       const Color& color, bool filled) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Draw rounded rectangle using circle arcs for corners
        // This is a simplified implementation
        
        if (filled) {
            // Draw filled rounded rectangle
            // Fill main rectangle
            Rectangle main_rect(rect.x + radius, rect.y, rect.width - 2 * radius, rect.height);
            drawRect(main_rect, color, true);
            
            // Fill side rectangles
            Rectangle left_rect(rect.x, rect.y + radius, radius, rect.height - 2 * radius);
            Rectangle right_rect(rect.x + rect.width - radius, rect.y + radius, radius, rect.height - 2 * radius);
            drawRect(left_rect, color, true);
            drawRect(right_rect, color, true);
            
            // Fill corner circles
            drawCircle(rect.x + radius, rect.y + radius, radius, color, true);
            drawCircle(rect.x + rect.width - radius - 1, rect.y + radius, radius, color, true);
            drawCircle(rect.x + radius, rect.y + rect.height - radius - 1, radius, color, true);
            drawCircle(rect.x + rect.width - radius - 1, rect.y + rect.height - radius - 1, radius, color, true);
        } else {
            // Draw rounded rectangle outline
            // Draw straight lines
            drawLine(rect.x + radius, rect.y, rect.x + rect.width - radius - 1, rect.y, color);
            drawLine(rect.x + rect.width - 1, rect.y + radius, rect.x + rect.width - 1, rect.y + rect.height - radius - 1, color);
            drawLine(rect.x + radius, rect.y + rect.height - 1, rect.x + rect.width - radius - 1, rect.y + rect.height - 1, color);
            drawLine(rect.x, rect.y + radius, rect.x, rect.y + rect.height - radius - 1, color);
            
            // Draw corner arcs
            // This would require arc drawing implementation
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawRoundRect exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawCircle(int16_t x, int16_t y, uint16_t radius, 
                                    const Color& color, bool filled) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    return drawCircleBresenham(x, y, radius, color, filled);
}

ErrorCode GraphicsEngine::drawEllipse(int16_t x, int16_t y, uint16_t radius_x, uint16_t radius_y,
                                     const Color& color, bool filled) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    return drawEllipseBresenham(x, y, radius_x, radius_y, color, filled);
}

ErrorCode GraphicsEngine::drawTriangle(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                                      int16_t x3, int16_t y3, const Color& color, bool filled) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (filled) {
            // Draw filled triangle
            Point points[3] = {{x1, y1}, {x2, y2}, {x3, y3}};
            fillPolygon(points, 3, color);
        } else {
            // Draw triangle outline
            drawLine(x1, y1, x2, y2, color);
            drawLine(x2, y2, x3, y3, color);
            drawLine(x3, y3, x1, y1, color);
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawTriangle exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawPolygon(const Point* points, size_t num_points,
                                     const Color& color, bool filled) {
    if (!initialized_ || !points || num_points < 3) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (filled) {
            fillPolygon(points, num_points, color);
        } else {
            // Draw polygon outline
            for (size_t i = 0; i < num_points; i++) {
                size_t next = (i + 1) % num_points;
                drawLine(points[i].x, points[i].y, points[next].x, points[next].y, color);
            }
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawPolygon exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawText(int16_t x, int16_t y, const std::string& text, const Color& color) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    return drawTextInternal(x, y, text, color);
}

ErrorCode GraphicsEngine::drawText(const Rectangle& rect, const std::string& text, const Color& color,
                                  TextAlign halign, TextVAlign valign) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Calculate text bounds
        Rectangle text_bounds = calculateTextBounds(text);
        
        // Calculate position based on alignment
        int16_t x = rect.x;
        int16_t y = rect.y;
        
        switch (halign) {
            case TextAlign::LEFT:
                x = rect.x;
                break;
            case TextAlign::CENTER:
                x = rect.x + (rect.width - text_bounds.width) / 2;
                break;
            case TextAlign::RIGHT:
                x = rect.x + rect.width - text_bounds.width;
                break;
            case TextAlign::JUSTIFY:
                x = rect.x;
                break;
        }
        
        switch (valign) {
            case TextVAlign::TOP:
                y = rect.y;
                break;
            case TextVAlign::MIDDLE:
                y = rect.y + (rect.height - text_bounds.height) / 2;
                break;
            case TextVAlign::BOTTOM:
                y = rect.y + rect.height - text_bounds.height;
                break;
        }
        
        // Draw text
        ErrorCode result = drawTextInternal(x, y, text, color);
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        return result;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawText exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

Rectangle GraphicsEngine::getTextBounds(const std::string& text) const {
    return calculateTextBounds(text);
}

ErrorCode GraphicsEngine::drawImage(int16_t x, int16_t y, const std::shared_ptr<Image>& image) {
    if (!initialized_ || !image) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Draw image at specified position
        // This is a simplified implementation - real implementation would
        // copy image data to display memory
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.draw_calls++;
        metrics_.pixels_drawn += image->getWidth() * image->getHeight();
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawImage exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawImage(int16_t x, int16_t y, const std::shared_ptr<Image>& image,
                                   const Rectangle& src_rect) {
    if (!initialized_ || !image) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Draw portion of image at specified position
        // This is a simplified implementation - real implementation would
        // copy specific portion of image data to display memory
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.draw_calls++;
        metrics_.pixels_drawn += src_rect.width * src_rect.height;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawImage exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

void GraphicsEngine::setClipRect(const Rectangle& rect) {
    current_context_.clip_x = rect.x;
    current_context_.clip_y = rect.y;
    current_context_.clip_width = rect.width;
    current_context_.clip_height = rect.height;
    current_context_.clipping_enabled = true;
}

Rectangle GraphicsEngine::getClipRect() const {
    return Rectangle(current_context_.clip_x, current_context_.clip_y,
                    current_context_.clip_width, current_context_.clip_height);
}

void GraphicsEngine::clearClipRect() {
    current_context_.clipping_enabled = false;
}

ErrorCode GraphicsEngine::loadFont(const std::string& name, uint8_t size, const uint8_t* data) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    try {
        // Create font object and store it
        // This is a simplified implementation - real implementation would
        // parse font data and create font object
        
        std::string font_key = name + "_" + std::to_string(size);
        // fonts_[font_key] = std::make_shared<Font>(name, size, data);
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_FONT_LOAD_FAILED, 
                                           "Font load exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_FONT_LOAD_FAILED;
    }
}

std::shared_ptr<Font> GraphicsEngine::getFont(const std::string& name, uint8_t size) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    std::string font_key = name + "_" + std::to_string(size);
    auto it = fonts_.find(font_key);
    if (it != fonts_.end()) {
        return it->second;
    }
    
    return nullptr;
}

ErrorCode GraphicsEngine::loadImage(const std::string& name, const uint8_t* data,
                                   uint16_t width, uint16_t height, ColorFormat format) {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    try {
        // Create image object and store it
        // This is a simplified implementation - real implementation would
        // create image object from data
        
        // images_[name] = std::make_shared<Image>(data, width, height, format);
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_IMAGE_LOAD_FAILED, 
                                           "Image load exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_IMAGE_LOAD_FAILED;
    }
}

std::shared_ptr<Image> GraphicsEngine::getImage(const std::string& name) const {
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    auto it = images_.find(name);
    if (it != images_.end()) {
        return it->second;
    }
    
    return nullptr;
}

std::shared_ptr<Canvas> GraphicsEngine::createCanvas(uint16_t width, uint16_t height) {
    if (!initialized_) {
        return nullptr;
    }
    
    try {
        // Create canvas object
        // This is a simplified implementation - real implementation would
        // create canvas with allocated memory
        
        // return std::make_shared<Canvas>(width, height);
        return nullptr;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_CANVAS_CREATE_FAILED, 
                                           "Canvas creation exception: " + std::string(e.what()));
        return nullptr;
    }
}

ErrorCode GraphicsEngine::drawCanvas(int16_t x, int16_t y, const std::shared_ptr<Canvas>& canvas) {
    if (!initialized_ || !canvas) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Draw canvas to display
        // This is a simplified implementation - real implementation would
        // copy canvas data to display memory
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.draw_calls++;
        metrics_.pixels_drawn += canvas->getWidth() * canvas->getHeight();
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawCanvas exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

GraphicsMetrics GraphicsEngine::getMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

void GraphicsEngine::resetMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    memset(&metrics_, 0, sizeof(metrics_));
    metrics_.render_time_min = UINT32_MAX;
}

std::map<std::string, uint32_t> GraphicsEngine::getMemoryUsage() const {
    std::map<std::string, uint32_t> usage;
    
    std::lock_guard<std::mutex> lock(resources_mutex_);
    
    // Calculate memory usage for different components
    usage["fonts"] = metrics_.font_memory;
    usage["images"] = metrics_.texture_memory;
    usage["total"] = metrics_.memory_used;
    
    return usage;
}

ErrorCode GraphicsEngine::selfTest() {
    if (!initialized_) {
        return ErrorCode::GRAPHICS_NOT_INITIALIZED;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        error_handler.logInfo("Starting graphics engine self-test");
        
        // Test basic drawing operations
        ErrorCode result = clear(Color(0, 0, 0));
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Clear failed");
        }
        
        result = drawRect(Rectangle(10, 10, 100, 50), Color(255, 255, 255), true);
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Rectangle failed");
        }
        
        result = drawCircle(50, 50, 20, Color(255, 0, 0), false);
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Circle failed");
        }
        
        result = drawText(20, 30, "TEST", Color(0, 0, 0));
        if (result != ErrorCode::SUCCESS) {
            return error_handler.logError(result, "Self-test: Text failed");
        }
        
        error_handler.logInfo("Graphics engine self-test completed successfully");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::GRAPHICS_SELFTEST_FAILED, 
                                    "Self-test exception: " + std::string(e.what()));
    }
}

// Private methods

void GraphicsEngine::updateMetrics(uint32_t render_time) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    metrics_.render_time_total += render_time;
    metrics_.render_time_avg = metrics_.render_time_total / metrics_.draw_calls;
    
    if (render_time > metrics_.render_time_max) {
        metrics_.render_time_max = render_time;
    }
    
    if (render_time < metrics_.render_time_min) {
        metrics_.render_time_min = render_time;
    }
}

bool GraphicsEngine::isPointInClipRect(int16_t x, int16_t y) const {
    if (!current_context_.clipping_enabled) {
        return true;
    }
    
    return x >= current_context_.clip_x && 
           x < current_context_.clip_x + current_context_.clip_width &&
           y >= current_context_.clip_y && 
           y < current_context_.clip_y + current_context_.clip_height;
}

ErrorCode GraphicsEngine::drawLineBresenham(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const Color& color) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Bresenham's line algorithm
        int16_t dx = abs(x2 - x1);
        int16_t dy = abs(y2 - y1);
        int16_t sx = (x1 < x2) ? 1 : -1;
        int16_t sy = (y1 < y2) ? 1 : -1;
        int16_t err = dx - dy;
        
        int16_t x = x1;
        int16_t y = y1;
        
        while (true) {
            drawPixel(x, y, color);
            
            if (x == x2 && y == y2) break;
            
            int16_t e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x += sx;
            }
            if (e2 < dx) {
                err += dx;
                y += sy;
            }
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawLineBresenham exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawCircleBresenham(int16_t x, int16_t y, uint16_t radius, 
                                             const Color& color, bool filled) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Bresenham's circle algorithm
        int16_t x_pos = radius;
        int16_t y_pos = 0;
        int16_t err = 0;
        
        while (x_pos >= y_pos) {
            if (filled) {
                // Draw filled circle
                drawLine(x - x_pos, y + y_pos, x + x_pos, y + y_pos, color);
                drawLine(x - y_pos, y + x_pos, x + y_pos, y + x_pos, color);
                drawLine(x - x_pos, y - y_pos, x + x_pos, y - y_pos, color);
                drawLine(x - y_pos, y - x_pos, x + y_pos, y - x_pos, color);
            } else {
                // Draw circle outline
                drawPixel(x + x_pos, y + y_pos, color);
                drawPixel(x - x_pos, y + y_pos, color);
                drawPixel(x + x_pos, y - y_pos, color);
                drawPixel(x - x_pos, y - y_pos, color);
                drawPixel(x + y_pos, y + x_pos, color);
                drawPixel(x - y_pos, y + x_pos, color);
                drawPixel(x + y_pos, y - x_pos, color);
                drawPixel(x - y_pos, y - x_pos, color);
            }
            
            if (err <= 0) {
                y_pos += 1;
                err += 2 * y_pos + 1;
            }
            if (err > 0) {
                x_pos -= 1;
                err -= 2 * x_pos + 1;
            }
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawCircleBresenham exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawEllipseBresenham(int16_t x, int16_t y, uint16_t radius_x, uint16_t radius_y,
                                              const Color& color, bool filled) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Simplified ellipse drawing using circle approximation
        // Real implementation would use proper ellipse algorithm
        
        // For now, just draw a circle with the larger radius
        uint16_t radius = std::max(radius_x, radius_y);
        drawCircleBresenham(x, y, radius, color, filled);
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.primitives_drawn++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawEllipseBresenham exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::fillPolygon(const Point* points, size_t num_points, const Color& color) {
    if (!points || num_points < 3) {
        return ErrorCode::INVALID_PARAMETER;
    }
    
    try {
        // Simple polygon filling using scan-line algorithm
        // Find bounding box
        int16_t min_y = points[0].y;
        int16_t max_y = points[0].y;
        
        for (size_t i = 1; i < num_points; i++) {
            if (points[i].y < min_y) min_y = points[i].y;
            if (points[i].y > max_y) max_y = points[i].y;
        }
        
        // For each scan line, find intersections and fill
        for (int16_t y = min_y; y <= max_y; y++) {
            std::vector<int16_t> intersections;
            
            // Find intersections with polygon edges
            for (size_t i = 0; i < num_points; i++) {
                size_t next = (i + 1) % num_points;
                int16_t y1 = points[i].y;
                int16_t y2 = points[next].y;
                
                if ((y1 <= y && y < y2) || (y2 <= y && y < y1)) {
                    if (y1 != y2) {
                        int16_t x = points[i].x + (points[next].x - points[i].x) * (y - y1) / (y2 - y1);
                        intersections.push_back(x);
                    }
                }
            }
            
            // Sort intersections and fill pairs
            std::sort(intersections.begin(), intersections.end());
            for (size_t i = 0; i < intersections.size(); i += 2) {
                if (i + 1 < intersections.size()) {
                    drawLine(intersections[i], y, intersections[i + 1], y, color);
                }
            }
        }
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "FillPolygon exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

ErrorCode GraphicsEngine::drawTextInternal(int16_t x, int16_t y, const std::string& text, const Color& color) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // Simple text rendering - draw each character as a rectangle
        // Real implementation would use actual font data
        int16_t char_width = 8;  // Simplified character width
        int16_t char_height = 12; // Simplified character height
        
        for (size_t i = 0; i < text.length(); i++) {
            int16_t char_x = x + i * char_width;
            int16_t char_y = y;
            
            // Draw character (simplified as a rectangle)
            drawRect(Rectangle(char_x, char_y, char_width, char_height), color, false);
        }
        
        // Update metrics
        auto end_time = std::chrono::steady_clock::now();
        uint32_t render_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        updateMetrics(render_time);
        
        metrics_.draw_calls++;
        
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::GRAPHICS_DRAW_FAILED, 
                                           "DrawTextInternal exception: " + std::string(e.what()));
        return ErrorCode::GRAPHICS_DRAW_FAILED;
    }
}

Rectangle GraphicsEngine::calculateTextBounds(const std::string& text) const {
    // Simplified text bounds calculation
    // Real implementation would use actual font metrics
    int16_t char_width = 8;   // Simplified character width
    int16_t char_height = 12; // Simplified character height
    
    return Rectangle(0, 0, text.length() * char_width, char_height);
}

} // namespace WeatherStation 