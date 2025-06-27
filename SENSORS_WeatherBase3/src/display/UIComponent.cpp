#include "UIComponent.hpp"
#include "../utils/ErrorHandler.hpp"
#include <algorithm>

namespace WeatherStation {

UIComponent::UIComponent(const std::string& id, UIComponentType type)
    : id_(id)
    , type_(type)
    , state_(UIComponentState::NORMAL)
    , x_(0)
    , y_(0)
    , width_(0)
    , height_(0)
    , z_index_(0)
    , visible_(true)
    , enabled_(true)
    , selected_(false)
    , error_(false) {
}

UIComponent::~UIComponent() {
    // Clean up children
    std::lock_guard<std::mutex> lock(children_mutex_);
    children_.clear();
}

std::string UIComponent::getId() const {
    return id_;
}

UIComponentType UIComponent::getType() const {
    return type_;
}

UIComponentState UIComponent::getState() const {
    return state_;
}

std::string UIComponent::getLabel() const {
    return label_;
}

int UIComponent::getX() const {
    return x_;
}

int UIComponent::getY() const {
    return y_;
}

int UIComponent::getWidth() const {
    return width_;
}

int UIComponent::getHeight() const {
    return height_;
}

int UIComponent::getZIndex() const {
    return z_index_;
}

std::string UIComponent::getStyle() const {
    return style_;
}

void UIComponent::setLabel(const std::string& label) {
    label_ = label;
}

void UIComponent::setPosition(int x, int y) {
    x_ = x;
    y_ = y;
}

void UIComponent::setSize(int width, int height) {
    width_ = width;
    height_ = height;
}

void UIComponent::setZIndex(int z) {
    z_index_ = z;
}

void UIComponent::setStyle(const std::string& style) {
    style_ = style;
}

void UIComponent::setState(UIComponentState state) {
    state_ = state;
}

void UIComponent::addState(UIComponentState state) {
    state_ = state_ | state;
}

void UIComponent::removeState(UIComponentState state) {
    state_ = static_cast<UIComponentState>(static_cast<uint8_t>(state_) & ~static_cast<uint8_t>(state));
}

void UIComponent::registerCallback(UIComponentCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = callback;
}

void UIComponent::unregisterCallback() {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = nullptr;
}

void UIComponent::notifyEvent(const UIComponentEventData& event_data) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (callback_) {
        callback_(event_data);
    }
}

ErrorCode UIComponent::update(GraphicsEngine& engine) {
    // Default implementation - subclasses can override
    return ErrorCode::SUCCESS;
}

ErrorCode UIComponent::clear(GraphicsEngine& engine) {
    // Default implementation - clear the component area
    if (!visible_) {
        return ErrorCode::SUCCESS;
    }
    
    try {
        Rectangle rect(x_, y_, width_, height_);
        return engine.clearRect(rect, Color(0, 0, 0));
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::COMPONENT_CLEAR_FAILED, 
                                           "Component clear exception: " + std::string(e.what()));
        return ErrorCode::COMPONENT_CLEAR_FAILED;
    }
}

void UIComponent::addChild(std::shared_ptr<UIComponent> child) {
    if (!child) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(children_mutex_);
    
    // Remove child from its current parent
    if (auto current_parent = child->getParent().lock()) {
        current_parent->removeChild(child->getId());
    }
    
    // Set this as the new parent
    child->setParent(shared_from_this());
    
    // Add to children list
    children_.push_back(child);
}

void UIComponent::removeChild(const std::string& child_id) {
    std::lock_guard<std::mutex> lock(children_mutex_);
    
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if ((*it)->getId() == child_id) {
            (*it)->setParent(std::weak_ptr<UIComponent>());
            children_.erase(it);
            break;
        }
    }
}

std::shared_ptr<UIComponent> UIComponent::getChild(const std::string& child_id) const {
    std::lock_guard<std::mutex> lock(children_mutex_);
    
    for (const auto& child : children_) {
        if (child->getId() == child_id) {
            return child;
        }
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<UIComponent>> UIComponent::getChildren() const {
    std::lock_guard<std::mutex> lock(children_mutex_);
    return children_;
}

std::weak_ptr<UIComponent> UIComponent::getParent() const {
    return parent_;
}

void UIComponent::setParent(std::shared_ptr<UIComponent> parent) {
    parent_ = parent;
}

void UIComponent::focus() {
    if (!enabled_) {
        return;
    }
    
    addState(UIComponentState::FOCUSED);
    
    UIComponentEventData event_data;
    event_data.event = UIComponentEvent::FOCUS_GAINED;
    event_data.data = "Component focused: " + id_;
    notifyEvent(event_data);
}

void UIComponent::blur() {
    removeState(UIComponentState::FOCUSED);
    
    UIComponentEventData event_data;
    event_data.event = UIComponentEvent::FOCUS_LOST;
    event_data.data = "Component blurred: " + id_;
    notifyEvent(event_data);
}

void UIComponent::press() {
    if (!enabled_) {
        return;
    }
    
    addState(UIComponentState::PRESSED);
    
    UIComponentEventData event_data;
    event_data.event = UIComponentEvent::PRESSED;
    event_data.data = "Component pressed: " + id_;
    notifyEvent(event_data);
}

void UIComponent::release() {
    if (!enabled_) {
        return;
    }
    
    bool was_pressed = (state_ & UIComponentState::PRESSED) != UIComponentState::NORMAL;
    removeState(UIComponentState::PRESSED);
    
    if (was_pressed) {
        UIComponentEventData event_data;
        event_data.event = UIComponentEvent::RELEASED;
        event_data.data = "Component released: " + id_;
        notifyEvent(event_data);
        
        // Trigger click event
        event_data.event = UIComponentEvent::CLICK;
        event_data.data = "Component clicked: " + id_;
        notifyEvent(event_data);
    }
}

void UIComponent::longPress() {
    if (!enabled_) {
        return;
    }
    
    UIComponentEventData event_data;
    event_data.event = UIComponentEvent::LONG_PRESS;
    event_data.data = "Component long pressed: " + id_;
    notifyEvent(event_data);
}

void UIComponent::select() {
    if (!enabled_) {
        return;
    }
    
    selected_ = true;
    addState(UIComponentState::SELECTED);
}

void UIComponent::deselect() {
    selected_ = false;
    removeState(UIComponentState::SELECTED);
}

bool UIComponent::isVisible() const {
    return visible_;
}

void UIComponent::setVisible(bool visible) {
    visible_ = visible;
}

bool UIComponent::isEnabled() const {
    return enabled_;
}

void UIComponent::setEnabled(bool enabled) {
    enabled_ = enabled;
    
    if (!enabled) {
        addState(UIComponentState::DISABLED);
    } else {
        removeState(UIComponentState::DISABLED);
    }
}

bool UIComponent::isSelected() const {
    return selected_;
}

void UIComponent::setSelected(bool selected) {
    selected_ = selected;
    
    if (selected) {
        addState(UIComponentState::SELECTED);
    } else {
        removeState(UIComponentState::SELECTED);
    }
}

bool UIComponent::isError() const {
    return error_;
}

void UIComponent::setError(bool error) {
    error_ = error;
    
    if (error) {
        addState(UIComponentState::ERROR);
        
        UIComponentEventData event_data;
        event_data.event = UIComponentEvent::ERROR;
        event_data.data = "Component error: " + id_;
        notifyEvent(event_data);
    } else {
        removeState(UIComponentState::ERROR);
    }
}

} // namespace WeatherStation 