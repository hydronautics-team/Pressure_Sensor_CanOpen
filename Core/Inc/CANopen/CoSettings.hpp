#pragma once

#include <cstddef>
#include <cstdint>

namespace CANoopEn
{

class CoSettings
{
public:
    static constexpr int32_t CoCycleTime = 10;
    static constexpr int32_t SdoTimeoutTime = 2000;

    static constexpr size_t MaxNumberOfOdEntries = 40;
    static constexpr size_t MaxNumberOfRemoteNodes = 1;
    static constexpr size_t MaxStringLength = 64;
    static constexpr size_t MaxNumberOfSdoChannels = 1;

    static constexpr bool SdoBlocTransferUseCrc = false;
    static constexpr size_t MaxSdoBlockSize = 1;
    static constexpr size_t SdoProtocolSwitchThreshold = 0;

    // There are no RPDOs, but the stack's fixed-size container requires a
    // non-zero compile-time capacity.  Only TPDO mapping number 1 is used.
    static constexpr size_t MaxNumberOfRpdoMappingParameters = 1;
    static constexpr size_t MaxNumberOfTpdoMappingParameters = 1;
    static constexpr size_t MaxNumberOfPdoMappingParameterEntries = 2;

    // EMCY is disabled; keep the compile-time container valid.
    static constexpr size_t MaxNumberOfEmcyErrors = 1;
};

} // namespace CANoopEn
