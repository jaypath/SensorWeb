#include "AnimationSystem.hpp"
#include "../utils/ErrorHandler.hpp"
#include <algorithm>
#include <cmath>

namespace WeatherStation {

// Animation implementation
Animation::Animation(const std::string& id, AnimationType type, std::shared_ptr<UIComponent> target,
                    uint32_t duration_ms, AnimationEasing easing)
    : id_(id)
    , type_(type)
    , state_(AnimationState::STOPPED)
    , easing_(easing)
    , duration_ms_(duration_ms)
    , progress_(0.0f)
    , target_(target) {
}

Animation::~Animation() {
    stop();
}

std::string Animation::getId() const {
    return id_;
}

AnimationType Animation::getType() const {
    return type_;
}

AnimationState Animation::getState() const {
    return state_;
}

AnimationEasing Animation::getEasing() const {
    return easing_;
}

uint32_t Animation::getDuration() const {
    return duration_ms_;
}

float Animation::getProgress() const {
    return progress_;
}

std::shared_ptr<UIComponent> Animation::getTarget() const {
    return target_;
}

void Animation::setCallback(AnimationCallback callback) {
    callback_ = callback;
}

void Animation::start() {
    if (state_ == AnimationState::RUNNING) {
        return;
    }
    
    state_ = AnimationState::RUNNING;
    progress_ = 0.0f;
    start_time_ = std::chrono::steady_clock::now();
    
    if (callback_) {
        UIComponentEventData event_data;
        event_data.event = UIComponentEvent::STARTED;
        event_data.data = "Animation started: " + id_;
        callback_(event_data);
    }
}

void Animation::pause() {
    if (state_ != AnimationState::RUNNING) {
        return;
    }
    
    state_ = AnimationState::PAUSED;
    pause_time_ = std::chrono::steady_clock::now();
}

void Animation::resume() {
    if (state_ != AnimationState::PAUSED) {
        return;
    }
    
    state_ = AnimationState::RUNNING;
    auto pause_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - pause_time_).count();
    start_time_ += std::chrono::milliseconds(pause_duration);
}

void Animation::stop() {
    if (state_ == AnimationState::STOPPED) {
        return;
    }
    
    state_ = AnimationState::STOPPED;
    progress_ = 0.0f;
    
    if (callback_) {
        UIComponentEventData event_data;
        event_data.event = UIComponentEvent::CANCELLED;
        event_data.data = "Animation cancelled: " + id_;
        callback_(event_data);
    }
}

void Animation::update(uint32_t elapsed_ms) {
    if (state_ != AnimationState::RUNNING) {
        return;
    }
    
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        current_time - start_time_).count();
    
    if (elapsed >= duration_ms_) {
        // Animation completed
        progress_ = 1.0f;
        state_ = AnimationState::COMPLETED;
        
        if (callback_) {
            UIComponentEventData event_data;
            event_data.event = UIComponentEvent::COMPLETED;
            event_data.data = "Animation completed: " + id_;
            event_data.progress = progress_;
            callback_(event_data);
        }
    } else {
        // Update progress
        progress_ = static_cast<float>(elapsed) / static_cast<float>(duration_ms_);
        
        if (callback_) {
            UIComponentEventData event_data;
            event_data.event = UIComponentEvent::UPDATED;
            event_data.data = "Animation updated: " + id_;
            event_data.progress = progress_;
            callback_(event_data);
        }
    }
}

// AnimationSystem implementation
AnimationSystem::AnimationSystem()
    : initialized_(false) {
}

AnimationSystem::~AnimationSystem() {
    shutdown();
}

ErrorCode AnimationSystem::initialize() {
    if (initialized_) {
        return ErrorCode::SUCCESS;
    }
    
    ErrorHandler& error_handler = ErrorHandler::getInstance();
    
    try {
        // Initialize animation system
        // This could include setting up timing systems, etc.
        
        initialized_ = true;
        
        error_handler.logInfo("AnimationSystem initialized successfully");
        return ErrorCode::SUCCESS;
        
    } catch (const std::exception& e) {
        return error_handler.logError(ErrorCode::ANIMATION_INIT_FAILED, 
                                    "Animation initialization exception: " + std::string(e.what()));
    }
}

void AnimationSystem::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Stop all animations
    clearAll();
    
    initialized_ = false;
    
    ErrorHandler::getInstance().logInfo("AnimationSystem shutdown complete");
}

bool AnimationSystem::isInitialized() const {
    return initialized_;
}

std::string AnimationSystem::createAnimation(AnimationType type, std::shared_ptr<UIComponent> target,
                                            uint32_t duration_ms, AnimationEasing easing) {
    if (!initialized_) {
        return "";
    }
    
    try {
        // Generate unique animation ID
        static int animation_counter = 0;
        std::string animation_id = "anim_" + std::to_string(animation_counter++);
        
        // Create animation
        auto animation = std::make_shared<Animation>(animation_id, type, target, duration_ms, easing);
        
        // Set global callback if available
        if (global_callback_) {
            animation->setCallback(global_callback_);
        }
        
        // Add to animations list
        std::lock_guard<std::mutex> lock(mutex_);
        animations_[animation_id] = animation;
        
        ErrorHandler::getInstance().logInfo("Animation created: " + animation_id);
        return animation_id;
        
    } catch (const std::exception& e) {
        ErrorHandler::getInstance().logError(ErrorCode::ANIMATION_CREATE_FAILED, 
                                           "Animation creation exception: " + std::string(e.what()));
        return "";
    }
}

bool AnimationSystem::removeAnimation(const std::string& animation_id) {
    if (!initialized_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = animations_.find(animation_id);
    if (it != animations_.end()) {
        it->second->stop();
        animations_.erase(it);
        
        ErrorHandler::getInstance().logInfo("Animation removed: " + animation_id);
        return true;
    }
    
    return false;
}

std::shared_ptr<Animation> AnimationSystem::getAnimation(const std::string& animation_id) const {
    if (!initialized_) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = animations_.find(animation_id);
    if (it != animations_.end()) {
        return it->second;
    }
    
    return nullptr;
}

std::vector<std::shared_ptr<Animation>> AnimationSystem::getAnimations() const {
    if (!initialized_) {
        return {};
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<Animation>> result;
    result.reserve(animations_.size());
    
    for (const auto& pair : animations_) {
        result.push_back(pair.second);
    }
    
    return result;
}

void AnimationSystem::updateAll(uint32_t elapsed_ms) {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update all animations
    for (auto it = animations_.begin(); it != animations_.end();) {
        auto animation = it->second;
        
        animation->update(elapsed_ms);
        
        // Remove completed animations
        if (animation->getState() == AnimationState::COMPLETED) {
            it = animations_.erase(it);
        } else {
            ++it;
        }
    }
}

void AnimationSystem::clearAll() {
    if (!initialized_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Stop all animations
    for (auto& pair : animations_) {
        pair.second->stop();
    }
    
    animations_.clear();
    
    ErrorHandler::getInstance().logInfo("All animations cleared");
}

void AnimationSystem::setGlobalCallback(AnimationCallback callback) {
    global_callback_ = callback;
}

} // namespace WeatherStation 