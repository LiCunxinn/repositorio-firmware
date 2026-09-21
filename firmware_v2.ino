#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

// Configurações da Rede Wokwi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// URL do arquivo version.json no GitHub
const char* manifest_url = "https://raw.githubusercontent.com/LiCunxinn/repositorio-firmware/main/version.json";

// Pinos do LED RGB
const int LED_VERMELHO_PIN = 17; // Pino Vermelho (ALERTA - FW 2.0)
const int LED_VERDE_PIN    = 16; // Pino Verde (NORMAL - FW 2.0)
const int LED_AZUL_PIN     = 4;  // Pino Azul (FW 1.0)

// Versao Atual
const String CURRENT_VERSION = "2.0";

// Estados da Histerese
enum EstadoSistema { NORMAL, ALERTA };
EstadoSistema estadoAtual = NORMAL;

// Variáveis de Temporização e Controle
unsigned long tempoInicioSessao = 0;
unsigned long tempoUltimaLeitura = 0;
const unsigned long INTERVALO_SESSAO = 48000; // 48s entre inícios de sessão
const unsigned long INTERVALO_LEITURA = 2000;  // 2s entre leituras

int cicloSessaoCount = 0;
bool emSessao = false;
int leituraIndex = 0;
float leituras[5];

// Extração simples de chave no JSON
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

void atualizarLEDs() {
  if (estadoAtual == NORMAL) {
    digitalWrite(LED_VERMELHO_PIN, LOW);
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_AZUL_PIN, LOW);
  } else { // ALERTA
    digitalWrite(LED_VERMELHO_PIN, HIGH);
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_AZUL_PIN, LOW);
  }
}

// Algoritmo de ordenação (Bubble Sort)
void ordenarVetor(float entrada[], float saida[], int tamanho) {
  for (int i = 0; i < tamanho; i++) {
    saida[i] = entrada[i];
  }
  for (int i = 0; i < tamanho - 1; i++) {
    for (int j = 0; j < tamanho - i - 1; j++) {
      if (saida[j] > saida[j + 1]) {
        float temp = saida[j];
        saida[j] = saida[j + 1];
        saida[j + 1] = temp;
      }
    }
  }
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
        // 1. Cálculo da Média
        float soma = 0;
        for (int i = 0; i < 5; i++) {
          soma += leituras[i];
        }
        float media = soma / 5.0;

        // 2. Ordenação das leituras
        float leiturasOrdenadas[5];
        ordenarVetor(leituras, leiturasOrdenadas, 5);

        // 3. Obtenção da Mediana (3º elemento do vetor ordenado)
        float mediana = leiturasOrdenadas[2];

        // 4. Lógica de Histerese
        if (mediana >= 16.0) {
          estadoAtual = ALERTA;
        } else if (mediana <= 14.0) {
          estadoAtual = NORMAL;
        }
        // Se a mediana estiver entre 14.0 e 16.0 cm, mantém o estado anterior

        // Atualiza a indicação do LED com base no novo estado
        atualizarLEDs();

        // Exibição dos resultados no Serial Monitor
        Serial.print("Valores originais: ");
        for (int i = 0; i < 5; i++) {
          Serial.print(leituras[i], 1);
          Serial.print(i < 4 ? ", " : "\n");
        }

        Serial.print("Valores ordenados: ");
        for (int i = 0; i < 5; i++) {
          Serial.print(leiturasOrdenadas[i], 1);
          Serial.print(i < 4 ? ", " : "\n");
        }

        Serial.print("Média da sessão: ");
        Serial.print(media, 1);
        Serial.println(" cm");

        Serial.print("Mediana da sessão: ");
        Serial.print(mediana, 1);
        Serial.println(" cm");

        Serial.print("Estado do sistema: ");
        Serial.println(estadoAtual == NORMAL ? "NORMAL (LED Verde)" : "ALERTA (LED Vermelho)");

        cicloSessaoCount++;
        emSessao = false;
        Serial.println("Próxima sessão em 48 segundos.\n");
      }
    }
  }
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

    Serial.print("[OTA] Versão instalada: ");
    Serial.println(CURRENT_VERSION);
    Serial.print("[OTA] Versão disponível no servidor: ");
    Serial.println(versionStr);

    if (versionStr.length() > 0 && versionStr > CURRENT_VERSION) {
      Serial.println("[OTA] Nova versão detectada! Baixando firmware...");
      // Função de download mantida para demonstração
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

  pinMode(LED_VERMELHO_PIN, OUTPUT);
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_AZUL_PIN, OUTPUT);

  // Inicia estado visual de acordo com estado padrão (NORMAL)
  atualizarLEDs();

  randomSeed(analogRead(0));
  conectarWiFi();

  Serial.println("\n==================================");
  Serial.println("MONITORAMENTO DE VEGETAÇÃO - FW 2.0");
  Serial.println("==================================\n");

  tempoInicioSessao = millis();
  emSessao = true;
  leituraIndex = 0;
}

void loop() {
  unsigned long tempoAtual = millis();

  if (!emSessao && (tempoAtual - tempoInicioSessao >= INTERVALO_SESSAO)) {
    tempoInicioSessao = tempoAtual;
    emSessao = true;
    leituraIndex = 0;
  }

  if (emSessao) {
    executarSessaoLeitura();
  }

  if (cicloSessaoCount >= 3) {
    checarAtualizacaoOTA();
    cicloSessaoCount = 0;
  }
}
