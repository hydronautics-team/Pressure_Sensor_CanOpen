#pragma once

#include <cstddef>
#include <cstdint>

#include "CoCanMessage.hpp"
#include "main.h"

class CanDriver final
{
public:
    explicit CanDriver(CAN_HandleTypeDef& handle) noexcept;

    [[nodiscard]] bool Init() noexcept;
    [[nodiscard]] bool SendCanMessage(const CANoopEn::CoCanMessage& message) noexcept;
    [[nodiscard]] bool ReceiveCanMessage(CANoopEn::CoCanMessage& message) noexcept;

    [[nodiscard]] uint32_t DroppedRxFrames() const noexcept;
    [[nodiscard]] uint32_t DroppedTxFrames() const noexcept;

    static void HandleInterrupt() noexcept;
    void HandleReceiveInterrupt(CAN_HandleTypeDef* handle) noexcept;
    void ProcessTransmitBuffer() noexcept;

private:
    static constexpr uint8_t kQueueCapacity = 16;

    class FrameQueue final
    {
    public:
        [[nodiscard]] bool Push(const CANoopEn::CoCanMessage& message) noexcept;
        [[nodiscard]] bool Pop(CANoopEn::CoCanMessage& message) noexcept;
        [[nodiscard]] bool Peek(CANoopEn::CoCanMessage& message) const noexcept;
        void RemoveFront() noexcept;

    private:
        CANoopEn::CoCanMessage messages_[kQueueCapacity]{};
        volatile uint8_t head_ = 0;
        volatile uint8_t tail_ = 0;
    };

    CAN_HandleTypeDef& handle_;
    FrameQueue rxQueue_{};
    FrameQueue txQueue_{};
    volatile uint32_t droppedRxFrames_ = 0;
    volatile uint32_t droppedTxFrames_ = 0;
};
