#include "catalog_details.hpp"

namespace picodbg {
void describePeripheralRegisters(Chip chip, PeripheralDescription& device) {
    if (device.name == "ADC") describeAdc(chip, device);
    else if (device.name == "I2C0" || device.name == "I2C1") describeI2c(device);
    else if (device.name == "DMA") describeDma(chip, device);
    else if (device.name == "PIO0" || device.name == "PIO1" || device.name == "PIO2") describePio(chip, device);
    else if (device.name == "UART0" || device.name == "UART1" || device.name == "SPI0" || device.name == "SPI1")
        describeSerialFields(device);
}
}
