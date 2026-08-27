#pragma once

#include <cstdint>

#include "CanDriver.hpp"
#include "CoCobIdCia301.hpp"
#include "CoSlave.hpp"
#include "CoTimer.hpp"
#include "ICoObjectDictionaryListener.hpp"
#include "ICoSendHandlerListener.hpp"
#include "ICoTickCountSource.hpp"
#include "PressureSensorOd.hpp"

class PressureSensorCanopen final : public CANoopEn::CoSlave,
                                    public CANoopEn::ICoListener,
                                    public CANoopEn::ICoObjectDictionaryListener,
                                    public CANoopEn::ICoSendHandlerListener
{
public:
    explicit PressureSensorCanopen(CAN_HandleTypeDef& canHandle) noexcept;

    [[nodiscard]] bool Init() noexcept;
    void Proceed() override;
    void PublishMeasurement(int32_t rawAdc, uint32_t pressurePa, uint32_t depthMm) noexcept;
    void MarkMeasurementInvalid() noexcept;

    void OnDebugOutput(const char* formatSpecifiers, ...) override;
    void OnHeartbeatTimeout(uint16_t nodeId) override;
    void OnNmtStateChange(uint16_t nodeId, CANoopEn::CoNmtState nmtState) override;
    void OnNmtCommand(uint16_t nodeId, CANoopEn::CoNmtCommand nmtCommand) override;
    void OnSdoAbort(uint16_t nodeId, CANoopEn::CoSdoAbortCode abortCode, uint16_t index, uint16_t subIndex) override;

    void OnValueChanged(uint16_t nodeId, uint16_t index, uint16_t subIndex) override;
    bool SendCanMessage(const CANoopEn::CoCanMessage& message) override;

private:
    class HalTickSource final : public CANoopEn::ICoTickCountSource
    {
    public:
        uint32_t GetTickCount() override;
    };

    static constexpr uint16_t kNodeId = 3;
    static constexpr uint8_t kNotValidMask = 0x01;

    CANoopEn::PressureSensorOd objectDictionary_;
    CANoopEn::CoCobIdCia301 cobId_;
    CANoopEn::CoTimer schedulerTimer_;
    CanDriver canDriver_;
    HalTickSource tickSource_;
    bool operational_ = false;
};
