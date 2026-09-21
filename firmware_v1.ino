#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

// Configurações da Rede Wokwi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// URL do arquivo version.json no GitHub
const char* manifest_url = "https://raw.githubusercontent.com/LiCunxinn/repositorio-firmware/main/version.json";

// Pinos
const int LED_AZUL_PIN = 4;

// Versao Atual
const String CURRENT_VERSION = "1.0";

// Temporização
unsigned long tempoInicioSessao = 0;
unsigned long tempoUltimaLeitura = 0;
const unsigned long INTERVALO_SESSAO = 48000; // 48s entre inícios
const unsigned long INTERVALO_LEITURA = 2000;  // 2s entre leituras

int cicloSessaoCount = 0;
bool emSessao = false;
int leituraIndex = 0;
float leituras[5];

// Busca simples de valor no JSON (sem bibliotecas externas)
String extrairValorJson(String json, String chave) {
  String buscaChave = "\"" + chave + "\"";
  int posChave = json.indexOf(buscaChave);
  if (posChave == -1) return "";

  int posDoisPontos = json.indexOf(":", posChave);
  if (posDoisPontos == -1) return "";

  int posInicio = json.indexOf("\"", posDoisPontos);
  if (posInicio == -1) return "";

  int posFim = json.indexOf("\"", posInicio + 1);
  if (posFim == -1) return "";

  return json.substring(posInicio + 1, posFim);
}

void conectarWiFi() {
  Serial.print("Conectando ao Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Conectado!");
}

void executarSessaoLeitura() {
  unsigned long tempoAtual = millis();

  if (leituraIndex < 5) {
    if (tempoAtual - tempoUltimaLeitura >= INTERVALO_LEITURA || leituraIndex == 0) {
      tempoUltimaLeitura = tempoAtual;

      // Gera valor pseudoaleatório de vegetação entre 10 e 20 cm
      leituras[leituraIndex] = random(100, 201) / 10.0;
      Serial.print("Leitura ");
      Serial.print(leituraIndex + 1);
      Serial.print(": ");
      Serial.print(leituras[leituraIndex], 1);
      Serial.println(" cm");

      leituraIndex++;

      if (leituraIndex == 5) {
        float soma = 0;
        for (int i = 0; i < 5; i++) {
          soma += leituras[i];
        }
        float media = soma / 5.0;

        Serial.print("Média da sessão: ");
        Serial.print(media, 1);
        Serial.println(" cm");

        cicloSessaoCount++;
        emSessao = false;
        Serial.println("Próxima sessão em 48 segundos.\n");
      }
    }
  }
}

void realizarDownloadEUpdate(String urlBinary) {
  HTTPClient http;
  http.begin(urlBinary);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength);

    if (canBegin) {
      Serial.println("[OTA] Gravando nova versão no ESP32...");
      WiFiClient* stream = http.getStreamPtr();
      size_t written = Update.writeStream(*stream);

      if (written == contentLength) {
        Serial.println("[OTA] Gravação concluída com sucesso!");
      } else {
        Serial.printf("[OTA] Gravação incompleta: %d/%d bytes\n", written, contentLength);
      }

      if (Update.end()) {
        if (Update.isFinished()) {
          Serial.println("[OTA] Atualização com sucesso! Reiniciando...");
          ESP.restart();
        }
      } else {
        Serial.printf("[OTA] Erro no Update: %s\n", Update.errorString());
      }
    } else {
      Serial.println("[OTA] Espaço insuficiente para atualização.");
    }
  } else {
    Serial.printf("[OTA] Falha ao baixar .bin. HTTP: %d\n", httpCode);
  }
  http.end();
}

void checarAtualizacaoOTA() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] Sem conexão Wi-Fi.");
    return;
  }

  Serial.println("\n----------------------------------");
  Serial.println("[OTA] Consultando manifesto de versão...");

  HTTPClient http;
  http.begin(manifest_url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    String versionStr = extrairValorJson(payload, "version");
    String firmwareUrl = extrairValorJson(payload, "url");

    Serial.print("[OTA] Versão instalada: ");
    Serial.println(CURRENT_VERSION);
    Serial.print("[OTA] Versão disponível no servidor: ");
    Serial.println(versionStr);

    if (versionStr.length() > 0 && versionStr > CURRENT_VERSION) {
      Serial.println("[OTA] Nova versão detectada! Baixando firmware...");
      realizarDownloadEUpdate(firmwareUrl);
    } else {
      Serial.println("[OTA] O firmware já está na versão mais recente.");
    }
  } else {
    Serial.printf("[OTA] Falha ao acessar manifesto. Código HTTP: %d\n", httpCode);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_AZUL_PIN, OUTPUT);
  digitalWrite(LED_AZUL_PIN, HIGH);

  randomSeed(analogRead(0));
  conectarWiFi();

  Serial.println("\n==================================");
  Serial.println("MONITORAMENTO DE VEGETAÇÃO - FW 1.0");
  Serial.println("==================================\n");

  tempoInicioSessao = millis();
  emSessao = true;
  leituraIndex = 0;
}

void loop() {
  unsigned long tempoAtual = millis();

  // Garante início a cada 48s exatos a partir do início da anterior
  if (!emSessao && (tempoAtual - tempoInicioSessao >= INTERVALO_SESSAO)) {
    tempoInicioSessao = tempoAtual;
    emSessao = true;
    leituraIndex = 0;
  }

  if (emSessao) {
    executarSessaoLeitura();
  }

  // Verifica OTA após 3 sessões
  if (cicloSessaoCount >= 3) {
    checarAtualizacaoOTA();
    cicloSessaoCount = 0;
  }
}
