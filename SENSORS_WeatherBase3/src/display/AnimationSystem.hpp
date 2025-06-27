#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <map>
#include <chrono>
#include "../utils/ErrorHandler.hpp"
#include "UIComponent.hpp"

namespace WeatherStation {

/**
 * @brief AnimationType - Types of supported animations
 */
enum class AnimationType {
    FADE_IN,
    FADE_OUT,
    SLIDE_LEFT,
    SLIDE_RIGHT,
    SLIDE_UP,
    SLIDE_DOWN,
    SCALE_UP,
    SCALE_DOWN,
    ROTATE,
    BOUNCE,
    CUSTOM
};

/**
 * @brief AnimationEasing - Easing functions for animations
 */
enum class AnimationEasing {
    LINEAR,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT,
    BOUNCE,
    ELASTIC,
    CUSTOM
};

/**
 * @brief AnimationState - State of an animation
 */
enum class AnimationState {
    STOPPED,
    RUNNING,
    PAUSED,
    COMPLETED
};

/**
 * @brief AnimationEvent - Events for animation lifecycle
 */
enum class AnimationEvent {
    STARTED,
    UPDATED,
    COMPLETED,
    CANCELLED,
    ERROR
};

/**
 * @brief AnimationEventData - Data for animation events
 */
struct AnimationEventData {
    AnimationEvent event;
    std::string animation_id;
    std::string data;
    float progress = 0.0f;
};

/**
 * @brief AnimationCallback - Callback function for animation events
 */
using AnimationCallback = std::function<void(const AnimationEventData&)>;

/**
 * @brief Animation - Represents a single animation
 */
class Animation {
public:
    Animation(const std::string& id, AnimationType type, std::shared_ptr<UIComponent> target,
              uint32_t duration_ms, AnimationEasing easing = AnimationEasing::LINEAR);
    ~Animation();

    std::string getId() const;
    AnimationType getType() const;
    AnimationState getState() const;
    AnimationEasing getEasing() const;
    uint32_t getDuration() const;
    float getProgress() const;
    std::shared_ptr<UIComponent> getTarget() const;

    void setCallback(AnimationCallback callback);
    void start();
    void pause();
    void resume();
    void stop();
    void update(uint32_t elapsed_ms);

private:
    std::string id_;
    AnimationType type_;
    AnimationState state_;
    AnimationEasing easing_;
    uint32_t duration_ms_;
    float progress_;
    std::shared_ptr<UIComponent> target_;
    AnimationCallback callback_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point pause_time_;
    mutable std::mutex mutex_;
};

/**
 * @brief AnimationSystem - Manages all UI animations
 */
class AnimationSystem {
public:
    AnimationSystem();
    ~AnimationSystem();

    ErrorCode initialize();
    void shutdown();
    bool isInitialized() const;

    std::string createAnimation(AnimationType type, std::shared_ptr<UIComponent> target,
                               uint32_t duration_ms, AnimationEasing easing = AnimationEasing::LINEAR);
    bool removeAnimation(const std::string& animation_id);
    std::shared_ptr<Animation> getAnimation(const std::string& animation_id) const;
    std::vector<std::shared_ptr<Animation>> getAnimations() const;
    void updateAll(uint32_t elapsed_ms);
    void clearAll();

    void setGlobalCallback(AnimationCallback callback);

private:
    std::map<std::string, std::shared_ptr<Animation>> animations_;
    AnimationCallback global_callback_;
    bool initialized_;
    mutable std::mutex mutex_;
};

} // namespace WeatherStation 