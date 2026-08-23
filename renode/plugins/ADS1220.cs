// SPDX-License-Identifier: MIT

using System;

using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.SPI;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Sensors
{
    // Minimal ADS1220 model for the command sequence used by this firmware.
    // It implements RESET, START/SYNC, RDATA, RREG and WREG, and drives the
    // active-low DRDY output after a deterministic single-shot conversion.
    public sealed class ADS1220 : ISPIPeripheral, IGPIOReceiver
    {
        public ADS1220(IMachine machine, int conversionPeriodMilliseconds = 50)
        {
            if(conversionPeriodMilliseconds <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(conversionPeriodMilliseconds));
            }

            registers = new byte[RegisterCount];
            conversionTimer = new LimitTimer(
                machine.ClockSource,
                1000,
                this,
                "conversionTimer",
                limit: (ulong)conversionPeriodMilliseconds,
                direction: Direction.Ascending,
                enabled: false,
                eventEnabled: true,
                autoUpdate: false,
                workMode: WorkMode.OneShot);
            conversionTimer.LimitReached += CompleteConversion;

            DataReady = new GPIO();
            Reset();
        }

        public void Reset()
        {
            Array.Clear(registers, 0, registers.Length);
            registers[1] = 0x04;

            state = State.Command;
            bytesRemaining = 0;
            registerIndex = 0;
            dataIndex = 0;
            latchedConversion = 0;
            ConversionCount = 0;
            conversionTimer.Reset();
            conversionTimer.Enabled = false;
            DataReady.Set();
        }

        public byte Transmit(byte data)
        {
            switch(state)
            {
                case State.WriteRegisters:
                    registers[registerIndex++] = data;
                    if(--bytesRemaining == 0)
                    {
                        state = State.Command;
                    }
                    return 0;

                case State.ReadRegisters:
                    var registerValue = registers[registerIndex++];
                    if(--bytesRemaining == 0)
                    {
                        state = State.Command;
                    }
                    return registerValue;

                case State.ReadData:
                    var shift = (ConversionByteCount - 1 - dataIndex) * 8;
                    var result = (byte)((latchedConversion >> shift) & 0xFF);
                    if(++dataIndex == ConversionByteCount)
                    {
                        state = State.Command;
                    }
                    return result;
            }

            if(data == ResetCommand)
            {
                Reset();
            }
            else if(data == StartCommand)
            {
                StartConversion();
            }
            else if(data == ReadDataCommand)
            {
                dataIndex = 0;
                state = State.ReadData;
            }
            else if((data & RegisterCommandMask) == WriteRegisterCommand)
            {
                BeginRegisterTransfer(data, State.WriteRegisters);
            }
            else if((data & RegisterCommandMask) == ReadRegisterCommand)
            {
                BeginRegisterTransfer(data, State.ReadRegisters);
            }
            else
            {
                this.Log(LogLevel.Warning, "Unsupported ADS1220 command 0x{0:X2}", data);
            }

            // Command responses are not used by the ADS1220 driver.
            return 0;
        }

        public void FinishTransmission()
        {
            state = State.Command;
            bytesRemaining = 0;
        }

        // GPIO input 0 is chip select. STM32SPI does not model chip select, but
        // observing PA4 lets the model delimit transactions exactly as hardware
        // would. Transmit remains usable if a platform omits this optional wire.
        public void OnGPIO(int number, bool value)
        {
            if(number != ChipSelectInput)
            {
                this.Log(LogLevel.Warning, "Unsupported ADS1220 GPIO input {0}", number);
                return;
            }

            if(value)
            {
                FinishTransmission();
            }
            else
            {
                state = State.Command;
                bytesRemaining = 0;
            }
        }

        public GPIO DataReady { get; }

        public int ConversionCount { get; private set; }

        public int PressureRawValue
        {
            get => pressureRawValue;
            set => pressureRawValue = ClampTo24Bits(value);
        }

        public int TemperatureRawValue
        {
            get => temperatureRawValue;
            set => temperatureRawValue = ClampTo24Bits(value);
        }

        private void BeginRegisterTransfer(byte command, State transferState)
        {
            registerIndex = (command >> 2) & 0x3;
            bytesRemaining = (command & 0x3) + 1;

            if(registerIndex + bytesRemaining > RegisterCount)
            {
                this.Log(LogLevel.Warning, "ADS1220 register transfer exceeds the register map");
                state = State.Command;
                bytesRemaining = 0;
                return;
            }

            state = transferState;
        }

        private void StartConversion()
        {
            DataReady.Set();
            conversionTimer.Value = 0;
            conversionTimer.Enabled = true;
        }

        private void CompleteConversion()
        {
            var mux = (registers[0] >> 4) & 0xF;
            latchedConversion = mux == TemperatureMux ? TemperatureRawValue : PressureRawValue;
            ConversionCount++;
            DataReady.Unset();
            this.Log(LogLevel.Noisy, "ADS1220 conversion complete: {0}", latchedConversion);
        }

        private static int ClampTo24Bits(int value)
        {
            return Math.Max(Minimum24BitValue, Math.Min(Maximum24BitValue, value));
        }

        private State state;
        private int registerIndex;
        private int bytesRemaining;
        private int dataIndex;
        private int latchedConversion;
        private int pressureRawValue = 1000000;
        private int temperatureRawValue = 500000;

        private readonly byte[] registers;
        private readonly LimitTimer conversionTimer;

        private const byte ResetCommand = 0x06;
        private const byte StartCommand = 0x08;
        private const byte ReadDataCommand = 0x10;
        private const byte ReadRegisterCommand = 0x20;
        private const byte WriteRegisterCommand = 0x40;
        private const byte RegisterCommandMask = 0xE0;
        private const int RegisterCount = 4;
        private const int ConversionByteCount = 3;
        private const int ChipSelectInput = 0;
        private const int TemperatureMux = 0xA;
        private const int Minimum24BitValue = -8388608;
        private const int Maximum24BitValue = 8388607;

        private enum State
        {
            Command,
            WriteRegisters,
            ReadRegisters,
            ReadData,
        }
    }
}
