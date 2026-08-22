#include "CanDriver.hpp"

namespace {
CanDriver* activeDriver = nullptr;
}

CanDriver::CanDriver(CAN_HandleTypeDef& handle) noexcept
    : handle_(handle)
{}

bool CanDriver::FrameQueue::Push(const CANoopEn::CoCanMessage& message) noexcept
{
    const uint8_t head = head_;
    if (static_cast<uint8_t>(head - tail_) >= kQueueCapacity) {
        return false;
    }

    messages_[head % kQueueCapacity] = message;
    __DMB();
    head_ = static_cast<uint8_t>(head + 1U);
    return true;
}

bool CanDriver::FrameQueue::Pop(CANoopEn::CoCanMessage& message) noexcept
{
    if (not Peek(message)) {
        return false;
    }
    RemoveFront();
    return true;
}

bool CanDriver::FrameQueue::Peek(CANoopEn::CoCanMessage& message) const noexcept
{
    const uint8_t tail = tail_;
    if (tail == head_) {
        return false;
    }

    __DMB();
    message = messages_[tail % kQueueCapacity];
    return true;
}

void CanDriver::FrameQueue::RemoveFront() noexcept
{
    __DMB();
    tail_ = static_cast<uint8_t>(tail_ + 1U);
}

bool CanDriver::Init() noexcept
{
    activeDriver = this;

    // Accept all 11-bit identifiers into FIFO0, but require IDE=0 in hardware.
    // The RX callback performs the same validation defensively.
    CAN_FilterTypeDef filter{};
    filter.FilterBank           = 0;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = 0;
    filter.FilterIdLow          = 0;
    filter.FilterMaskIdHigh     = 0;
    filter.FilterMaskIdLow      = 0x0004U; // IDE bit: reject extended identifiers.
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&handle_, &filter) != HAL_OK) {
        return false;
    }
    if (HAL_CAN_Start(&handle_) != HAL_OK) {
        return false;
    }

    constexpr uint32_t notifications =
        CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_TX_MAILBOX_EMPTY | CAN_IT_ERROR_WARNING | CAN_IT_BUSOFF | CAN_IT_ERROR;
    return HAL_CAN_ActivateNotification(&handle_, notifications) == HAL_OK;
}

bool CanDriver::SendCanMessage(const CANoopEn::CoCanMessage& message) noexcept
{
    if ((message.GetId() > 0x7FFU) || (message.GetDataLength() > 8U)) {
        return false;
    }

    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();

    const bool queued = txQueue_.Push(message);
    if (not queued) {
        droppedTxFrames_ = droppedTxFrames_ + 1U;
    }
    ProcessTransmitBuffer();

    __set_PRIMASK(interruptState);
    return queued;
}

bool CanDriver::ReceiveCanMessage(CANoopEn::CoCanMessage& message) noexcept
{
    return rxQueue_.Pop(message);
}

uint32_t CanDriver::DroppedRxFrames() const noexcept
{
    return droppedRxFrames_;
}

uint32_t CanDriver::DroppedTxFrames() const noexcept
{
    return droppedTxFrames_;
}

void CanDriver::ProcessTransmitBuffer() noexcept
{
    while (HAL_CAN_GetTxMailboxesFreeLevel(&handle_) > 0U) {
        CANoopEn::CoCanMessage message;
        if (not txQueue_.Peek(message)) {
            break;
        }

        CAN_TxHeaderTypeDef header{};
        header.StdId              = message.GetId();
        header.ExtId              = 0;
        header.IDE                = CAN_ID_STD;
        header.RTR                = message.GetRtr() ? CAN_RTR_REMOTE : CAN_RTR_DATA;
        header.DLC                = static_cast<uint32_t>(message.GetDataLength());
        header.TransmitGlobalTime = DISABLE;

        uint8_t bytes[8]{};
        (void)message.ToBytes(bytes, sizeof(bytes), 0, message.GetDataLength());
        uint32_t mailbox = 0;
        if (HAL_CAN_AddTxMessage(&handle_, &header, bytes, &mailbox) != HAL_OK) {
            break;
        }
        txQueue_.RemoveFront();
    }
}

void CanDriver::HandleReceiveInterrupt(CAN_HandleTypeDef* handle) noexcept
{
    while (HAL_CAN_GetRxFifoFillLevel(handle, CAN_RX_FIFO0) > 0U) {
        CAN_RxHeaderTypeDef header{};
        uint8_t bytes[8]{};
        if (HAL_CAN_GetRxMessage(handle, CAN_RX_FIFO0, &header, bytes) != HAL_OK) {
            break;
        }

        if ((header.IDE != CAN_ID_STD) || (header.StdId > 0x7FFU) || (header.DLC > 8U)) {
            continue;
        }

        CANoopEn::CoCanMessage message(static_cast<uint16_t>(header.StdId));
        message.SetRtr(header.RTR == CAN_RTR_REMOTE);
        message.SetDataLength(header.DLC);
        (void)message.FromBytes(bytes, sizeof(bytes), 0, header.DLC);
        if (not rxQueue_.Push(message)) {
            droppedRxFrames_ = droppedRxFrames_ + 1U;
        }
    }
}

void CanDriver::HandleInterrupt() noexcept
{
    if (activeDriver != nullptr) {
        HAL_CAN_IRQHandler(&activeDriver->handle_);
    }
}

extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef* handle)
{
    if (activeDriver != nullptr) {
        activeDriver->HandleReceiveInterrupt(handle);
    }
}

extern "C" void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef* handle)
{
    (void)handle;
    if (activeDriver != nullptr) {
        activeDriver->ProcessTransmitBuffer();
    }
}

extern "C" void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef* handle)
{
    (void)handle;
    if (activeDriver != nullptr) {
        activeDriver->ProcessTransmitBuffer();
    }
}

extern "C" void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef* handle)
{
    (void)handle;
    if (activeDriver != nullptr) {
        activeDriver->ProcessTransmitBuffer();
    }
}

extern "C" void HAL_CAN_ErrorCallback(CAN_HandleTypeDef* handle)
{
    (void)handle;
    // bxCAN ABOM performs the ISO 11898 bus-off recovery automatically.
}

extern "C" void USB_HP_CAN1_TX_IRQHandler(void)
{
    CanDriver::HandleInterrupt();
}

extern "C" void USB_LP_CAN1_RX0_IRQHandler(void)
{
    CanDriver::HandleInterrupt();
}

extern "C" void CAN1_SCE_IRQHandler(void)
{
    CanDriver::HandleInterrupt();
}
