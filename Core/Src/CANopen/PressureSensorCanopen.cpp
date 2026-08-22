#include "PressureSensorCanopen.hpp"

#include <limits>

#include "CoSettings.hpp"
#include "CoTimestamp.hpp"

PressureSensorCanopen::PressureSensorCanopen(CAN_HandleTypeDef& canHandle) noexcept
    : CANoopEn::CoSlave(*this, objectDictionary_, *this, kNodeId, cobId_)
    , objectDictionary_(*this, kNodeId)
    , canDriver_(canHandle)
{
}

uint32_t PressureSensorCanopen::HalTickSource::GetTickCount()
{
    return HAL_GetTick();
}

bool PressureSensorCanopen::Init() noexcept
{
    CANoopEn::CoTimestamp::SetTickCountSource(tickSource_);
    schedulerTimer_.Start(HAL_GetTick(), CANoopEn::CoSettings::CoCycleTime);

    if (not canDriver_.Init()) {
        return false;
    }

    return Start();
}

void PressureSensorCanopen::Proceed()
{
    CANoopEn::CoCanMessage message;
    while (canDriver_.ReceiveCanMessage(message)) {
        // CANopen defines 0x80 as the NMT "enter pre-operational" command.
        // CANoopEn 3.1.10 uses the resulting state value 0x7D in its command
        // enum, so translate the standard wire value locally without changing
        // the pinned submodule.
        if ((message.GetId() == 0x000U)
            && (message.GetDataLength() == 2U)
            && (message.GetData(0) == 0x80U)) {
            message.SetData(
                0,
                static_cast<uint8_t>(CANoopEn::CoNmtCommand::EnterPreOperational));
        }

        (void)CANoopEn::CoSlave::Proceed(message);
    }

    if (schedulerTimer_.IsElapsed(HAL_GetTick())) {
        CANoopEn::CoSlave::Proceed();
    }
}

void PressureSensorCanopen::PublishMeasurement(int32_t rawAdc, uint32_t pressurePa, uint32_t depthMm) noexcept
{
    if (pressurePa > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
        MarkMeasurementInvalid();
        return;
    }

    uint8_t status = 0;
    (void)objectDictionary_.GetValue(0x6150, 0x01, status);
    status = static_cast<uint8_t>(status & static_cast<uint8_t>(~kNotValidMask));

    (void)objectDictionary_.SetValue(0x9100, 0x01, rawAdc);
    (void)objectDictionary_.SetValue(0x9130, 0x01, static_cast<int32_t>(pressurePa));
    (void)objectDictionary_.SetValue(0x2000, 0x00, depthMm);
    (void)objectDictionary_.SetValue(0x6150, 0x01, status);

    if (operational_) {
        (void)WritePdoAsync(1);
    }
}

void PressureSensorCanopen::MarkMeasurementInvalid() noexcept
{
    uint8_t status = 0;
    (void)objectDictionary_.GetValue(0x6150, 0x01, status);
    status = static_cast<uint8_t>(status | kNotValidMask);
    (void)objectDictionary_.SetValue(0x6150, 0x01, status);
}

void PressureSensorCanopen::OnDebugOutput(const char* formatSpecifiers, ...)
{
    (void)formatSpecifiers;
}

void PressureSensorCanopen::OnHeartbeatTimeout(uint16_t nodeId)
{
    (void)nodeId;
}

void PressureSensorCanopen::OnNmtStateChange(uint16_t nodeId, CANoopEn::CoNmtState nmtState)
{
    if (nodeId == kNodeId) {
        operational_ = nmtState == CANoopEn::CoNmtState::Operational;
    }
}

void PressureSensorCanopen::OnNmtCommand(uint16_t nodeId, CANoopEn::CoNmtCommand nmtCommand)
{
    (void)nodeId;
    (void)nmtCommand;
}

void PressureSensorCanopen::OnSdoAbort(
    uint16_t nodeId,
    CANoopEn::CoSdoAbortCode abortCode,
    uint16_t index,
    uint16_t subIndex)
{
    (void)nodeId;
    (void)abortCode;
    (void)index;
    (void)subIndex;
}

void PressureSensorCanopen::OnValueChanged(uint16_t nodeId, uint16_t index, uint16_t subIndex)
{
    (void)nodeId;
    (void)index;
    (void)subIndex;
}

bool PressureSensorCanopen::SendCanMessage(const CANoopEn::CoCanMessage& message)
{
    return canDriver_.SendCanMessage(message);
}
