// Hardware protocol adapted from Meridian/Waveshare's ESP-IDF reference.
// See THIRD_PARTY.md. The factory calibration flash is only ever read.
#include "WaveshareSenxor.h"
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <hal/spi_ll.h>
#include <hal/gpio_ll.h>
#include <soc/spi_struct.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <esp_heap_caps.h>
#include <cstring>
extern "C" {
#include "vendor/SenXorLib.h"
#include "vendor/Customer_Interface.h"
MCU_REG MCU_REGISTER;
uint8_t PowerMode = 0, SPI_Tx_Done = 0;
}
namespace {
constexpr int CS = 16, FlashCS = 5, Reset = 17, Enable = 3, Ready = 4;
constexpr size_t CalibrationBytes = 0x17180;
spi_device_handle_t device = nullptr;
portMUX_TYPE spiMux = portMUX_INITIALIZER_UNLOCKED;
uint16_t* calibration = nullptr;
uint16_t* image = nullptr;
volatile uint32_t failures = 0;
volatile uint32_t readyEdges = 0;
bool fresh = false, calibrationOk = false, paused = false;
volatile bool active = false;
bool recoveryPending = false;
uint32_t recoveryCount = 0, errorCode = 0, lastRecoveryMs = 0, faultMs = 0;
unsigned discardFrames = 0;
float divisor = 10.f;
uint64_t timeoutAt = 0;
const char* lastError = "not initialized";
char receiveError[96]{};

// No driver locks or dynamic memory inside the FIFO ISR. All SPI2 users are
// confined to core 1 and task register accesses mask interrupts with spiMux.
uint16_t transfer(uint16_t value, int bits) {
  uint8_t tx[4] = {uint8_t(value >> 8), uint8_t(value), 0, 0}, rx[4]{};
  if (bits == 8) tx[0] = uint8_t(value);
  spi_ll_clear_int_stat(&GPSPI2);
  spi_ll_set_mosi_bitlen(&GPSPI2, bits);
  spi_ll_set_miso_bitlen(&GPSPI2, bits);
  spi_ll_write_buffer(&GPSPI2, tx, bits);
  spi_ll_enable_mosi(&GPSPI2, true); spi_ll_enable_miso(&GPSPI2, true);
  spi_ll_apply_config(&GPSPI2); spi_ll_user_start(&GPSPI2);
  const int64_t start = esp_timer_get_time();
  while (!spi_ll_usr_is_done(&GPSPI2)) {
    if (esp_timer_get_time() - start > 1000) { SenXorError |= 0x08; return 0; }
  }
  spi_ll_read_buffer(&GPSPI2, rx, bits);
  return bits == 8 ? rx[0] : uint16_t(rx[0] << 8 | rx[1]);
}
void dataReady(void*) {
  ++readyEdges;
  if (!active || SenXorError) return;
  GetReceiveFrameBuffer();
  const uint32_t count = DATA_AV_Threshold;
  if (!ReceiveFrame || count == 0 || count > 5200 || PixelCnt > FrameSize ||
      count > FrameSize - PixelCnt || PixelCnt + count > sizeof(ReceiveFrame->TXBuf)/2) {
    SenXorError |= 0x20; return;
  }
  for (uint32_t n = 0; n < count; ++n) {
    gpio_ll_set_level(&GPIO, CS, 0);
    const uint16_t word = transfer(0x8000, 16);
    gpio_ll_set_level(&GPIO, CS, 1);
    if (SenXorError) return;
    ReceiveFrame->TXBuf[PixelCnt++] = word;
  }
  if (PixelCnt == FrameSize) CaptureProcessFrame(ReceiveFrame->TXBuf[PixelCnt - 1]);
}
}
extern "C" {
void Drv_SPI_SENXOR_Init(uint32_t speed, uint8_t) {
  if (speed < 5'000'000) {
    const uint32_t choices[] = {5'000'000, 14'000'000, 10'000'000, 6'000'000, 20'000'000};
    speed = speed < 5 ? choices[speed] : 14'000'000;
  }
  gpio_intr_disable(gpio_num_t(Ready));
  if (device) { spi_bus_remove_device(device); device = nullptr; }
  spi_device_interface_config_t cfg{};
  cfg.clock_speed_hz = speed; cfg.mode = 0; cfg.spics_io_num = -1; cfg.queue_size = 1;
  if (spi_bus_add_device(SPI2_HOST, &cfg, &device) != ESP_OK) {
    lastError = "SPI device configuration failed"; ++failures; return;
  }
  // Apply the driver's clock/pin configuration with both chip selects inactive.
  spi_transaction_t t{}; t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA; t.length = 16;
  if (spi_device_polling_transmit(device, &t) != ESP_OK) ++failures;
  if (active) gpio_intr_enable(gpio_num_t(Ready));
}
int Drv_SPI_Senxor_Read_Reg(uint8_t reg) {
  portENTER_CRITICAL(&spiMux);
  gpio_ll_set_level(&GPIO, CS, 0);
  const uint16_t v = transfer(uint16_t(reg) << 9, 16);
  gpio_ll_set_level(&GPIO, CS, 1);
  portEXIT_CRITICAL(&spiMux); return v & 0xff;
}
int Drv_SPI_Senxor_Write_Reg(uint8_t reg, uint8_t value) {
  portENTER_CRITICAL(&spiMux);
  gpio_ll_set_level(&GPIO, CS, 0);
  const uint16_t v = transfer((uint16_t(reg) << 9) | 0x100 | value, 16);
  gpio_ll_set_level(&GPIO, CS, 1);
  portEXIT_CRITICAL(&spiMux); return v;
}
void Read_CalibrationData() {
  calibrationOk = false; CalData_Available = 0;
  if (!calibration) return;
  Drv_SPI_SENXOR_Init(3, 1);  // Vendor selector 3 = 6 MHz.
  gpio_ll_set_level(&GPIO, FlashCS, 0); transfer(0x05, 8);
  const uint8_t status = transfer(0xaa, 8); gpio_ll_set_level(&GPIO, FlashCS, 1);
  if (status & 1) { lastError = "calibration flash busy/unreadable"; return; }
  gpio_ll_set_level(&GPIO, FlashCS, 0);
  transfer(0x03, 8); transfer(0, 8); transfer(0, 8); transfer(0, 8);
  uint8_t allAnd = 0xff, allOr = 0;
  auto bytes = reinterpret_cast<uint8_t*>(calibration);
  for (size_t i = 0; i < CalibrationBytes; ++i) {
    bytes[i] = uint8_t(transfer(0xaa, 8)); allAnd &= bytes[i]; allOr |= bytes[i];
    if ((i & 1023) == 1023) delay(1);
  }
  gpio_ll_set_level(&GPIO, FlashCS, 1);
  calibrationOk = allAnd != 0xff && allOr != 0;
  if (!calibrationOk) lastError = "factory calibration is empty/unreadable";
}
// Called by the binary after radiometric conversion, before optional visual
// filters. The callback starts at pixel zero (no transport/header rows).
void Customer_imageprocessing(uint16_t* pixels, int length, uint16_t, uint16_t) {
  if (!image || length < WaveshareSenxor::Pixels) return;
  memcpy(image, pixels, WaveshareSenxor::Pixels * sizeof(uint16_t));
  divisor = (Acces_Read_Reg(0xB9) & 0x80) ? 100.f : 10.f;
  fresh = true;
}
void DataFrameReceiveError() {
  if (!SenXorError) return;
  ++failures; errorCode = SenXorError;
  fresh = false; recoveryPending = true;
  faultMs = millis();
  // Do not keep receiving into a partial frame. Recovery runs in read(), after
  // the vendor receive routine returns, never recursively or from the ISR.
  gpio_intr_disable(gpio_num_t(Ready));
  SenXorError = 0;
  snprintf(receiveError,sizeof(receiveError),"receive 0x%lx: pixels=%lu/%lu edges=%lu ready=%d state=%lu",
    (unsigned long)errorCode,(unsigned long)PixelCnt,(unsigned long)FrameSize,
    (unsigned long)readyEdges,gpio_get_level(gpio_num_t(Ready)),(unsigned long)Capture_Process_Status);
  lastError = receiveError;
}
void Drv_Timer_TimerDelay(int ms) {
  if (ms <= 0) return;
  if (xPortInIsrContext()) esp_rom_delay_us(uint32_t(ms) * 1000);
  else delay(ms);
}
void Drv_Timer_Start_TimeOut_Timer_mSec(int ms) { timeoutAt = esp_timer_get_time() + int64_t(ms > 0 ? ms : 1410) * 1000; }
int Drv_Timer_TimeOut_Occured() { return timeoutAt && uint64_t(esp_timer_get_time()) >= timeoutAt; }
void Drv_Gpio_RESET_N_PIN_Set(uint8_t level) { gpio_ll_set_level(&GPIO, Reset, level); }
// These hooks are also no-ops in the vendor MI0802 reference, including CRC.
void Drv_Boot_Setting() {}
void Drv_Crc_Open() {}
uint16_t Drv_Crc_WriteCRC(uint16_t) { return 0; }
uint32_t Drv_Crc_GetCRCcheckSum() { return 0; }
void Drv_Gpio_CAPTURE_PIN_Set(uint8_t) {}
void Drv_Gpio_Disable_Capture_interupt() {}
void Drv_Gpio_Enable_Capture_interupt() {}
void Drv_Gpio_Enable_MCU_PIN(int) {}
void Drv_Gpio_HOST_DATA_READY_Set(uint8_t) {}
void Drv_Gpio_WRPROT_PIN_Set(uint8_t) {}
void Drv_SPI_Host_PDMA_Disable() {}
void Host_I2C_RX_Event() {}  // No host register command interface is exposed.
}
bool WaveshareSenxor::begin() {
  if (active) return true;
  if (!psramFound()) { lastError = "enable OPI PSRAM in Arduino Tools"; return false; }
  calibration = static_cast<uint16_t*>(heap_caps_malloc(CalibrationBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  image = static_cast<uint16_t*>(heap_caps_malloc(Pixels*2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!calibration || !image) { lastError = "sensor buffer allocation failed"; return false; }
  for (int pin : {CS, FlashCS, Reset, Enable}) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }
  pinMode(Ready, INPUT);
  ledc_timer_config_t timer{};
  timer.speed_mode = LEDC_LOW_SPEED_MODE; timer.timer_num = LEDC_TIMER_1;
  timer.duty_resolution = LEDC_TIMER_3_BIT; timer.freq_hz = 4'000'000; timer.clk_cfg = LEDC_USE_XTAL_CLK;
  ledc_channel_config_t channel{};
  channel.gpio_num = 18; channel.speed_mode = LEDC_LOW_SPEED_MODE; channel.channel = LEDC_CHANNEL_0;
  channel.timer_sel = LEDC_TIMER_1; channel.duty = 4;
  if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&channel) != ESP_OK) {
    lastError = "4 MHz sensor clock failed"; return false;
  }
  spi_bus_config_t bus{}; bus.mosi_io_num = 15; bus.miso_io_num = 7; bus.sclk_io_num = 6;
  bus.quadwp_io_num = bus.quadhd_io_num = -1;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) {
    lastError = "SPI bus initialization failed"; return false;
  }
  Drv_SPI_SENXOR_Init(14'000'000, 0);
  if (!device) return false;
  Initialize_McuRegister(); Power_On_Senxor(1);
  if (Initialize_SenXor(1)) { lastError = "MI0802 initialization failed"; return false; }
  Read_CalibrationData(); if (!calibrationOk) return false;
  Process_CalibrationData(1, calibration); Read_AGC_LUT();
  Drv_SPI_SENXOR_Init(14'000'000, 0);
  gpio_set_intr_type(gpio_num_t(Ready), GPIO_INTR_POSEDGE);
  const esp_err_t isr = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
  if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) { lastError = "GPIO ISR setup failed"; return false; }
  if (gpio_isr_handler_add(gpio_num_t(Ready), dataReady, nullptr) != ESP_OK) {
    lastError = "sensor FIFO ISR setup failed"; return false;
  }
  active = true; gpio_intr_enable(gpio_num_t(Ready));
  Acces_Write_Reg(0xB4, 1);  // One frame per average; detector supplies persistence.
  Acces_Write_Reg(0xB1, 3);  // Continuous calibrated acquisition, as in the vendor demo.
  lastError = "none";
  return true;
}
bool WaveshareSenxor::read(float* out) {
  if (!active) return false;
  if (SenXorError) DataFrameReceiveError();
  if (recoveryPending) {
    // Reset the receive state and start a new FIFO transaction, just as on
    // resume after an NVS save. Do not power-cycle the sensor: the reference
    // B0=3 reset path did not restart this MI0802 reliably during live tests.
    // Keep calibration and all allocated buffers. Allow status consumers to
    // observe unavailable, and bound retries for a persistent hardware fault.
    if (uint32_t(millis()-faultMs)<250) return false;
    if (recoveryCount && uint32_t(millis()-lastRecoveryMs)<1000) return false;
    active = false;
    Acces_Write_Reg(0xB1, 0);
    Halt_Capture(); timeoutAt = 0;
    ++recoveryCount; lastRecoveryMs = millis();
    if (SenXorError) { DataFrameReceiveError(); active = true; return false; }
    Acces_Write_Reg(0xB4, 1);
    Acces_Write_Reg(0xB1, 3);
    recoveryPending = false; discardFrames = 2; fresh = false;
    active = true; gpio_intr_enable(gpio_num_t(Ready));
    return false;
  }
  fresh = false; DataFrameReceiveSenxor();
  if (SenXorError) DataFrameReceiveError();
  if (recoveryPending) return false;
  // This is also the vendor's buffer-consumed handshake, even though our copy
  // comes from Customer_imageprocessing rather than the transport frame.
  (void)DataFrameGetPointer();
  DataFrameProcess();
  if (SenXorError) DataFrameReceiveError();
  if (recoveryPending) return false;
  if (!fresh) return false;
  if (discardFrames) { --discardFrames; return false; }
  for (int i = 0; i < Pixels; ++i) out[i] = image[i] / divisor - 273.15f;
  lastError = "none";
  return true;
}
const char* WaveshareSenxor::error() const { return lastError; }
void WaveshareSenxor::pause() {
  if (!active) return;
  active = false; paused = true;
  gpio_intr_disable(gpio_num_t(Ready));
  if (SenXorError) DataFrameReceiveError();
  Acces_Write_Reg(0xB1, 0); Halt_Capture(); timeoutAt = 0;
  fresh = false;
}
void WaveshareSenxor::resume() {
  if (!paused) return;
  paused = false;
  // Initial_Capture_FIFO_Mode reconfigures/starts the FIFO on the next read.
  // Do not power-cycle the sensor just to save a palette or ROI.
  Halt_Capture(); timeoutAt = 0; discardFrames = 2;
  if (!recoveryPending) lastError = "resuming after settings save";
  Acces_Write_Reg(0xB1, 3);
  active = true;
  if (!recoveryPending) gpio_intr_enable(gpio_num_t(Ready));
}
void WaveshareSenxor::requestRecovery() {
  if (!active) return;
  gpio_intr_disable(gpio_num_t(Ready));
  recoveryPending = true; fresh = false;
  faultMs = millis();
  lastError = "sensor recovery requested";
}
uint32_t WaveshareSenxor::errors() const { return failures; }
uint32_t WaveshareSenxor::recoveries() const { return recoveryCount; }
uint32_t WaveshareSenxor::lastErrorCode() const { return errorCode; }
bool WaveshareSenxor::recovering() const { return recoveryPending || discardFrames || SenXorError; }
