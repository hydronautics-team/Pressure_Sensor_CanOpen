# Руководство по работе с Renode

## Запуск на Linux

### 1. Настройка виртуальных CAN
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```
### 2. Проверяем запуск CAN
```bash
ip link show type vcan
```

Должны получить что-то похожее
```bash
4: vcan0: <NOARP,UP,LOWER_UP> mtu 72 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 10000
    link/can
```

### 2. Запускаем renode

```bash
renode -e 'set bin_path @TARGET.elf' \
       -e 'set renode_core_path @subprojects/renode' \
       -e 'i START_SCRIPT.resc'
```

Где:
- `TARGET.elf` — путь к файлу прошивки (например: `build/Debug/Pressure_Sensor_SAUVC.elf`)
- `START_SCRIPT.resc` — путь к файлу запуска (например: `renode/pressure_sensor.resc`)

## Отладка

Скрипт (.resc) запускают GdbServer по порту 3333. Подключаемся к нему с помощью arm-none-eabi-gdb.

В папке helpers можно найти примеры конфигурации для vscode.