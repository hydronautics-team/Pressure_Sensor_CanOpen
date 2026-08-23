// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;

using Antmicro.Renode.Core;
using Antmicro.Renode.Core.CAN;
using Antmicro.Renode.Peripherals.Bus;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // The stock STM32F103 platform describes RCC through SVD tags. Cube HAL
    // needs writable clock status bits, so this compact model mirrors each
    // oscillator enable into its ready flag and SW into SWS.
    public sealed class STM32F1RCC : IDoubleWordPeripheral, IKnownSize
    {
        public STM32F1RCC()
        {
            registers = new Dictionary<long, uint>();
            Reset();
        }

        public void Reset()
        {
            registers.Clear();
            clockControl = 0x00000083;
            clockConfiguration = 0;
        }

        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
                case ClockControlOffset:
                    return clockControl;
                case ClockConfigurationOffset:
                    return clockConfiguration;
                default:
                    return registers.TryGetValue(offset, out var value) ? value : 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            switch(offset)
            {
                case ClockControlOffset:
                    clockControl = MirrorReadyBit(value, HsiEnable, HsiReady);
                    clockControl = MirrorReadyBit(clockControl, HseEnable, HseReady);
                    clockControl = MirrorReadyBit(clockControl, PllEnable, PllReady);
                    break;
                case ClockConfigurationOffset:
                    clockConfiguration = (value & ~SystemClockStatusMask)
                        | ((value & SystemClockSwitchMask) << 2);
                    break;
                default:
                    registers[offset] = value;
                    break;
            }
        }

        public long Size => 0x400;

        private static uint MirrorReadyBit(uint value, int enableBit, int readyBit)
        {
            var readyMask = 1u << readyBit;
            return (value & (1u << enableBit)) != 0 ? value | readyMask : value & ~readyMask;
        }

        private uint clockControl;
        private uint clockConfiguration;
        private readonly Dictionary<long, uint> registers;

        private const long ClockControlOffset = 0x00;
        private const long ClockConfigurationOffset = 0x04;
        private const uint SystemClockSwitchMask = 0x3;
        private const uint SystemClockStatusMask = 0xC;
        private const int HsiEnable = 0;
        private const int HsiReady = 1;
        private const int HseEnable = 16;
        private const int HseReady = 17;
        private const int PllEnable = 24;
        private const int PllReady = 25;
    }

    // Only ACR is used during startup, but retaining all writes makes the model
    // useful for firmware that later unlocks or inspects the flash controller.
    public sealed class STM32F1FlashController : IDoubleWordPeripheral, IKnownSize
    {
        public STM32F1FlashController()
        {
            registers = new Dictionary<long, uint>();
            Reset();
        }

        public void Reset()
        {
            registers.Clear();
            registers[0] = 0x30;
        }

        public uint ReadDoubleWord(long offset)
        {
            return registers.TryGetValue(offset, out var value) ? value : 0;
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            registers[offset] = value;
        }

        public long Size => 0x400;

        private readonly Dictionary<long, uint> registers;
    }
}

namespace Antmicro.Renode.Peripherals.CAN
{
    // STM32F1 HAL requests initialization while the bxCAN reset-state SLEEP
    // bit is still set. STMCAN expects these requests to be mutually exclusive,
    // so this adapter clears SLEEP when INRQ is asserted and delegates all CAN
    // traffic, filters, mailboxes and interrupts to the upstream model.
    public sealed class STM32F1CAN : IDoubleWordPeripheral, ICAN, INumberedGPIOOutput
    {
        public STM32F1CAN()
        {
            inner = new STMCAN();
            inner.FrameSent += frame => FrameSent?.Invoke(frame);
            inner.FrameReceived += (id, data) => FrameReceived?.Invoke(id, data);
        }

        public void Reset()
        {
            inner.Reset();
        }

        public uint ReadDoubleWord(long offset)
        {
            return inner.ReadDoubleWord(offset);
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            if(offset == MasterControlOffset && (value & InitializationRequest) != 0)
            {
                value &= ~SleepRequest;
            }
            inner.WriteDoubleWord(offset, value);
        }

        public void OnFrameReceived(CANMessageFrame message)
        {
            inner.OnFrameReceived(message);
        }

        public IReadOnlyDictionary<int, IGPIO> Connections => inner.Connections;

        public event Action<CANMessageFrame> FrameSent;
        public event Action<int, byte[]> FrameReceived;

        private readonly STMCAN inner;

        private const long MasterControlOffset = 0x00;
        private const uint InitializationRequest = 1u << 0;
        private const uint SleepRequest = 1u << 1;
    }
}
