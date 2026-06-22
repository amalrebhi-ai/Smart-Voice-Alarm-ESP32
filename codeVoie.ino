#include <WiFi.h>
#include "driver/i2s.h"

const char* ssid = "Amal";
const char* password = "amal1234amalamal";
const char* serverIP = "192.168.152.109";

#define I2S_WS 25
#define I2S_SD 33
#define I2S_SCK 26
#define I2S_PORT I2S_NUM_0

#define SAMPLE_RATE 16000
#define RECORD_TIME 5
#define BUFFER_SIZE 1024
#define SILENCE_THRESHOLD 250

WiFiClient client;

// ================= I2S =================
void setupI2S() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

// ================= WAV HEADER (IMPORTANT) =================
void sendWavHeader(WiFiClient &client, int dataSize) {
  uint8_t header[44];
  int fileSize = dataSize + 36;
  int byteRate = SAMPLE_RATE * 2;
  int blockAlign = 2;

  memcpy(header, "RIFF", 4);
  header[4] = fileSize & 0xff;
  header[5] = (fileSize >> 8) & 0xff;
  header[6] = (fileSize >> 16) & 0xff;
  header[7] = (fileSize >> 24) & 0xff;

  memcpy(header + 8, "WAVEfmt ", 8);

  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0;
  header[22] = 1; header[23] = 0;

  header[24] = SAMPLE_RATE & 0xff;
  header[25] = (SAMPLE_RATE >> 8) & 0xff;
  header[26] = (SAMPLE_RATE >> 16) & 0xff;
  header[27] = (SAMPLE_RATE >> 24) & 0xff;

  header[28] = byteRate & 0xff;
  header[29] = (byteRate >> 8) & 0xff;
  header[30] = (byteRate >> 16) & 0xff;
  header[31] = (byteRate >> 24) & 0xff;

  header[32] = blockAlign; header[33] = 0;
  header[34] = 16; header[35] = 0;

  memcpy(header + 36, "data", 4);

  header[40] = dataSize & 0xff;
  header[41] = (dataSize >> 8) & 0xff;
  header[42] = (dataSize >> 16) & 0xff;
  header[43] = (dataSize >> 24) & 0xff;

  client.write(header, 44);
}

// ================= VOIX =================
bool detectVoice() {
  uint8_t buffer[BUFFER_SIZE];
  size_t bytesRead;

  i2s_read(I2S_PORT, buffer, BUFFER_SIZE, &bytesRead, portMAX_DELAY);

  for (size_t i = 0; i < bytesRead; i += 4) {
    int32_t sample = *((int32_t*)(buffer + i));
    sample >>= 13;
    int16_t s = (int16_t)sample;

    if (abs(s) > SILENCE_THRESHOLD) return true;
  }
  return false;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi OK");
  Serial.println(WiFi.localIP());

  setupI2S();
}

// ================= LOOP =================
void loop() {

  Serial.println("En attente voix...");

  if (!detectVoice()) {
    delay(200);
    return;
  }

  Serial.println("Voix détectée !");

  int totalBytes = SAMPLE_RATE * RECORD_TIME * 2;

  if (!client.connect(serverIP, 5000)) {
    Serial.println("Connexion FAIL");
    delay(2000);
    return;
  }

  client.setNoDelay(true);

  // ================= HTTP =================
  client.println("POST /upload HTTP/1.1");
  client.print("Host: "); client.println(serverIP);
  client.println("Content-Type: audio/wav");
  client.print("Content-Length: "); client.println(totalBytes + 44);
  client.println();

  // ================= WAV HEADER (RESTORED) =================
  sendWavHeader(client, totalBytes);

  uint8_t buffer[BUFFER_SIZE];
  size_t bytesRead;
  int bytesSent = 0;

  while (bytesSent < totalBytes) {

    i2s_read(I2S_PORT, buffer, BUFFER_SIZE, &bytesRead, portMAX_DELAY);

    for (size_t i = 0; i < bytesRead; i += 4) {
      int32_t sample = *((int32_t*)(buffer + i));
      sample >>= 13;
      int16_t s = (int16_t)sample;
      *((int16_t*)(buffer + i / 2)) = s;
    }

    bytesRead /= 2;
    client.write(buffer, bytesRead);
    bytesSent += bytesRead;
  }

  // ================= RESPONSE STABLE =================
  Serial.println("Attente réponse serveur...");

  String response = "";
  unsigned long start = millis();

  while (millis() - start < 30000) {

    if (client.available()) {
      char c = client.read();
      response += c;
      start = millis();
    }

    delay(1);
  }

  client.stop();

  Serial.println("===== REPONSE COMPLETE =====");
  Serial.println(response);
  Serial.println("============================");

  int jsonStart = response.indexOf("{");

  if (jsonStart != -1) {

    String json = response.substring(jsonStart);

    Serial.println("JSON:");
    Serial.println(json);

    int actionPos = json.indexOf("\"action\":\"");
    if (actionPos != -1) {
      int start = actionPos + 10;
      int end = json.indexOf("\"", start);
      Serial.println("Action: " + json.substring(start, end));
    }

    int heurePos = json.indexOf("\"heure\":\"");
    if (heurePos != -1) {
      int start = heurePos + 9;
      int end = json.indexOf("\"", start);
      Serial.println("Heure: " + json.substring(start, end));
    }

  } else {
    Serial.println("Aucun JSON trouvé !");
  }

  delay(3000);
}