#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include "../utils/ErrorHandler.hpp"
#include "GraphicsEngine.hpp"

namespace WeatherStation {

/**
 * @brief UIComponentType - Types of UI components
 */
enum class UIComponentType {
    LABEL,
    BUTTON,
    SLIDER,
    SWITCH,
    PROGRESS_BAR,
    ICON,
    IMAGE,
    CHART,
    CONTAINER,
    CUSTOM
};

/**
 * @brief UIComponentState - State flags for UI components
 */
enum class UIComponentState : uint8_t {
    NORMAL      = 0x00,
    FOCUSED     = 0x01,
    PRESSED     = 0x02,
    DISABLED    = 0x04,
    HIDDEN      = 0x08,
    SELECTED    = 0x10,
    ERROR       = 0x20
};

inline UIComponentState operator|(UIComponentState a, UIComponentState b) {
    return static_cast<UIComponentState>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline UIComponentState operator&(UIComponentState a, UIComponentState b) {
    return static_cast<UIComponentState>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

/**
 * @brief UIComponentEvent - Events for UI components
 */
enum class UIComponentEvent {
    CLICK,
    VALUE_CHANGED,
    FOCUS_GAINED,
    FOCUS_LOST,
    PRESSED,
    RELEASED,
    LONG_PRESS,
    ERROR
};

/**
 * @brief UIComponentEventData - Event data structure
 */
struct UIComponentEventData {
    UIComponentEvent event;
    std::string data;
    int value = 0;
    int x = 0;
    int y = 0;
};

/**
 * @brief UIComponentCallback - Callback function type for UI events
 */
using UIComponentCallback = std::function<void(const UIComponentEventData&)>;

/**
 * @brief UIComponent - Abstract base class for all UI components
 */
class UIComponent : public std::enable_shared_from_this<UIComponent> {
public:
    UIComponent(const std::string& id, UIComponentType type);
    virtual ~UIComponent();

    // Getters
    std::string getId() const;
    UIComponentType getType() const;
    UIComponentState getState() const;
    std::string getLabel() const;
    int getX() const;
    int getY() const;
    int getWidth() const;
    int getHeight() const;
    int getZIndex() const;
    std::string getStyle() const;

    // Setters
    void setLabel(const std::string& label);
    void setPosition(int x, int y);
    void setSize(int width, int height);
    void setZIndex(int z);
    void setStyle(const std::string& style);
    void setState(UIComponentState state);
    void addState(UIComponentState state);
    void removeState(UIComponentState state);

    // Event handling
    void registerCallback(UIComponentCallback callback);
    void unregisterCallback();
    void notifyEvent(const UIComponentEventData& event_data);

    // Rendering
    virtual ErrorCode render(GraphicsEngine& engine) = 0;
    virtual ErrorCode update(GraphicsEngine& engine);
    virtual ErrorCode clear(GraphicsEngine& engine);

    // Hierarchy
    void addChild(std::shared_ptr<UIComponent> child);
    void removeChild(const std::string& child_id);
    std::shared_ptr<UIComponent> getChild(const std::string& child_id) const;
    std::vector<std::shared_ptr<UIComponent>> getChildren() const;
    std::weak_ptr<UIComponent> getParent() const;
    void setParent(std::shared_ptr<UIComponent> parent);

    // Focus and interaction
    virtual void focus();
    virtual void blur();
    virtual void press();
    virtual void release();
    virtual void longPress();
    virtual void select();
    virtual void deselect();

    // Utility
    bool isVisible() const;
    void setVisible(bool visible);
    bool isEnabled() const;
    void setEnabled(bool enabled);
    bool isSelected() const;
    void setSelected(bool selected);
    bool isError() const;
    void setError(bool error);

protected:
    std::string id_;
    UIComponentType type_;
    UIComponentState state_;
    std::string label_;
    int x_;
    int y_;
    int width_;
    int height_;
    int z_index_;
    std::string style_;
    bool visible_;
    bool enabled_;
    bool selected_;
    bool error_;
    std::weak_ptr<UIComponent> parent_;
    std::vector<std::shared_ptr<UIComponent>> children_;
    mutable std::mutex children_mutex_;
    UIComponentCallback callback_;
    mutable std::mutex callback_mutex_;
};

} // namespace WeatherStation 