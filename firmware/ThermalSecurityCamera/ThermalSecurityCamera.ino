#include "src/Application.h"
#include "src/UsbDiagnostics.h"
void setup() { thermal::startApplication(); }
void loop() { thermal::serviceUsbDiagnostics(); delay(10); }
