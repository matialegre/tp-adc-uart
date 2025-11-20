#include <Arduino.h>

const int UART_TX_PIN = 17;
const int UART_RX_PIN = 16;
const int ADC_INPUT_PIN = 34;
const int GPIO_OUT_PIN = 25;

const unsigned long SERIAL_BAUD_PC = 9600;
const uint8_t PATTERN_SEQ[] = "Aguante Digitales 3";
const size_t PATTERN_LEN = sizeof(PATTERN_SEQ) - 1;
const unsigned long PATTERN_PERIOD_MS = 50;

const int ADC_THRESHOLD = 2048;
const float SAMPLING_FREQUENCY_HZ = 20000.0f;

hw_timer_t* samplingTimer = nullptr;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool sampleFlag = false;

unsigned long totalBytesSent = 0;
unsigned long totalBytesReceived = 0;
unsigned long totalBitsCompared = 0;
unsigned long totalBitErrors = 0;
uint8_t lastReceivedByte = 0;
bool hasLastReceivedByte = false;
size_t txPatternIndex = 0;
size_t rxPatternIndex = 0;
uint8_t lastExpectedByte = 0;
char rxWord[PATTERN_LEN + 1];

unsigned long currentWordBytes = 0;
unsigned long currentWordBitsCompared = 0;
unsigned long currentWordBitErrors = 0;
unsigned long lastWordBytes = 0;
unsigned long lastWordBitsCompared = 0;
unsigned long lastWordBitErrors = 0;
bool wordReadyForLog = false;

const unsigned long LOG_INTERVAL_MS = 1000;
unsigned long lastPatternMillis = 0;
unsigned long lastLogMillis = 0;

const bool ENABLE_RX_DEBUG = false;
const unsigned long BAUD_STEP_INTERVAL_MS = 1000;
unsigned long lastBaudChangeMillis = 0;
const unsigned long SERIAL1_BAUDS[] = {1200, 2400, 4800, 9600, 9900, 10000, 11000, 19200};
const size_t NUM_SERIAL1_BAUDS = sizeof(SERIAL1_BAUDS) / sizeof(SERIAL1_BAUDS[0]);
size_t currentBaudIndex = 0;

void setupUARTs();
void setupADCandGPIO();
void setupSamplingTimer();

void IRAM_ATTR onSamplingTimer();
void handleAdcSamplingBaseline();

void sendTestPatternIfNeeded();
void handleUartReceptionAndBER();
void updateBaudRateIfNeeded();

uint8_t countDifferentBits(uint8_t a, uint8_t b);
void updateBERStats(uint8_t receivedByte);
void printStatisticsIfNeeded();
void printByteAsBits(uint8_t value);
void printRxDebug(uint8_t receivedByte);

void setup() {
  Serial.begin(SERIAL_BAUD_PC);
  delay(1000);
  setupUARTs();
  setupADCandGPIO();
  setupSamplingTimer();
  lastPatternMillis = millis();
  lastLogMillis = millis();
  lastBaudChangeMillis = millis();
}

void loop() {
  updateBaudRateIfNeeded();
  sendTestPatternIfNeeded();

  bool doSample = false;
  portENTER_CRITICAL(&timerMux);
  if (sampleFlag) {
    sampleFlag = false;
    doSample = true;
  }
  portEXIT_CRITICAL(&timerMux);

  if (doSample) {
    handleAdcSamplingBaseline();
  }

  handleUartReceptionAndBER();
  printStatisticsIfNeeded();
}

void setupUARTs() {
  unsigned long initialBaud = SERIAL1_BAUDS[currentBaudIndex];
  Serial1.begin(initialBaud, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
}

void setupADCandGPIO() {
  pinMode(ADC_INPUT_PIN, INPUT);
  analogReadResolution(12);
  pinMode(GPIO_OUT_PIN, OUTPUT);
  digitalWrite(GPIO_OUT_PIN, LOW);
}

void setupSamplingTimer() {
  samplingTimer = timerBegin(static_cast<uint32_t>(SAMPLING_FREQUENCY_HZ));
  timerAttachInterrupt(samplingTimer, &onSamplingTimer);
  timerAlarm(samplingTimer, 1, true, 0);
}

void IRAM_ATTR onSamplingTimer() {
  sampleFlag = true;
}

void handleAdcSamplingBaseline() {
  int adcValue = analogRead(ADC_INPUT_PIN);
  bool decidedBit = (adcValue > ADC_THRESHOLD);
  digitalWrite(GPIO_OUT_PIN, decidedBit ? HIGH : LOW);
}

void sendTestPatternIfNeeded() {
  unsigned long now = millis();
  if (now - lastPatternMillis >= PATTERN_PERIOD_MS) {
    lastPatternMillis += PATTERN_PERIOD_MS;
    uint8_t b = PATTERN_SEQ[txPatternIndex];
    Serial1.write(b);
    totalBytesSent++;
    txPatternIndex = (txPatternIndex + 1) % PATTERN_LEN;
  }
}

void updateBaudRateIfNeeded() {
  unsigned long now = millis();
  if (now - lastBaudChangeMillis >= BAUD_STEP_INTERVAL_MS) {
    lastBaudChangeMillis += BAUD_STEP_INTERVAL_MS;
    currentBaudIndex = (currentBaudIndex + 1) % NUM_SERIAL1_BAUDS;
    unsigned long newBaud = SERIAL1_BAUDS[currentBaudIndex];
    Serial1.flush();
    Serial1.end();
    Serial1.begin(newBaud, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    totalBytesSent = 0;
    totalBytesReceived = 0;
    totalBitsCompared = 0;
    totalBitErrors = 0;
    hasLastReceivedByte = false;
    txPatternIndex = 0;
    rxPatternIndex = 0;
    rxWord[0] = '\0';
    currentWordBytes = 0;
    currentWordBitsCompared = 0;
    currentWordBitErrors = 0;
    lastWordBytes = 0;
    lastWordBitsCompared = 0;
    lastWordBitErrors = 0;
    wordReadyForLog = false;
  }
}

void handleUartReceptionAndBER() {
  while (Serial1.available() > 0) {
    int incoming = Serial1.read();
    if (incoming >= 0) {
      uint8_t received = static_cast<uint8_t>(incoming);
      updateBERStats(received);
      if (ENABLE_RX_DEBUG) {
        printRxDebug(received);
      }
      lastReceivedByte = received;
      hasLastReceivedByte = true;
    }
  }
}

uint8_t countDifferentBits(uint8_t a, uint8_t b) {
  uint8_t x = a ^ b;
  uint8_t count = 0;
  while (x != 0) {
    count += (x & 0x01);
    x >>= 1;
  }
  return count;
}

void printByteAsBits(uint8_t value) {
  for (int8_t i = 7; i >= 0; --i) {
    uint8_t bit = (value >> i) & 0x01;
    Serial.print(bit);
  }
}

void printRxDebug(uint8_t receivedByte) {
  uint8_t bitErrors = countDifferentBits(receivedByte, lastExpectedByte);
  Serial.print("UART RX sent: 0x");
  if (lastExpectedByte < 16) Serial.print('0');
  Serial.print(lastExpectedByte, HEX);
  Serial.print(" = ");
  printByteAsBits(lastExpectedByte);
  Serial.print(" | recv: 0x");
  if (receivedByte < 16) Serial.print('0');
  Serial.print(receivedByte, HEX);
  Serial.print(" = ");
  printByteAsBits(receivedByte);
  Serial.print(" errBits:");
  Serial.println(bitErrors);
}

void updateBERStats(uint8_t receivedByte) {
  totalBytesReceived++;
  uint8_t expected = PATTERN_SEQ[rxPatternIndex];
  lastExpectedByte = expected;
  uint8_t bitErrors = countDifferentBits(receivedByte, expected);
  totalBitErrors += bitErrors;
  totalBitsCompared += 8;
  currentWordBitErrors += bitErrors;
  currentWordBitsCompared += 8;
  currentWordBytes++;
  rxWord[rxPatternIndex] = static_cast<char>(receivedByte);
  rxPatternIndex = (rxPatternIndex + 1) % PATTERN_LEN;
  if (rxPatternIndex == 0) {
    rxWord[PATTERN_LEN] = '\0';
    lastWordBitErrors = currentWordBitErrors;
    lastWordBitsCompared = currentWordBitsCompared;
    lastWordBytes = currentWordBytes;
    currentWordBitErrors = 0;
    currentWordBitsCompared = 0;
    currentWordBytes = 0;
    wordReadyForLog = true;
  }
}

void printStatisticsIfNeeded() {
  if (!wordReadyForLog) {
    return;
  }
  wordReadyForLog = false;
  if (lastWordBitsCompared == 0) {
    return;
  }
  float ber = static_cast<float>(lastWordBitErrors) / static_cast<float>(lastWordBitsCompared);
  Serial.print("TX:");
  Serial.print(lastWordBytes);
  Serial.print(" RX:");
  Serial.print(lastWordBytes);
  Serial.print(" BitsCmp:");
  Serial.print(lastWordBitsCompared);
  Serial.print(" Err:");
  Serial.print(lastWordBitErrors);
  Serial.print(" BER:");
  Serial.print(ber * 100.0f, 2);
  Serial.print("% BR:");
  Serial.print(SERIAL1_BAUDS[currentBaudIndex]);
  Serial.print(" WORD:");
  rxWord[PATTERN_LEN] = '\0';
  Serial.print(rxWord);
  Serial.println();
}
