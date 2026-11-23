#pragma once

#include <memory>
#include <string>

// ============================================================================
// Estrutura do Estado de Detecção
// ============================================================================
struct StatusDeteccao {
    bool detectado;      // true no instante exato em que a wake word é detectada
    float probabilidade; // valor contínuo da confiança do modelo (0.0 a 1.0)
    bool em_cooldown;    // true se estiver no período de resfriamento (~2s após ativar)
};

// Declaração opaca da struct interna do Detector
struct Detector;

// Deleter que permite usar std::unique_ptr sem expor headers internos do ONNX
struct DetectorDeleter {
    void operator()(Detector* p) const;
};

// Tipo de ponteiro gerenciado para o Detector
using DetectorPtr = std::unique_ptr<Detector, DetectorDeleter>;

// ============================================================================
// Funções Públicas da Biblioteca de Wake Word
// ============================================================================

// 1. Inicializa o ambiente ONNX, carrega os 3 modelos neurais e abre o microfone via PortAudio.
// Retorna um DetectorPtr gerenciado, ou nullptr em caso de erro.
DetectorPtr inicializar_detector(const std::string& modelo_ww = "alexa_v0.1.onnx",
                                const std::string& pasta_modelos = "");

// 2. Executa um passo de escuta: lê 80ms de áudio do microfone e processa nos modelos.
// Deve ser chamada continuamente dentro do seu loop.
StatusDeteccao detectar_passo(Detector& det, float limiar = 0.5f);

// 3. Versão simplificada: retorna true diretamente se a palavra foi detectada.
bool detectar_wakeword(Detector& det, float limiar = 0.5f);

// 4. Pausa a escuta do microfone (chame antes de tocar áudios com aplay).
void pausar_escuta(Detector& det);

// 5. Retoma a escuta do microfone com buffers limpos (chame após o aplay terminar).
void retomar_escuta(Detector& det);
