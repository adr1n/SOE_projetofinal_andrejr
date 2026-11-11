#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>
#include <algorithm>

// Configurações de Caminhos e Voz
const std::string PIPER_BIN = "../.venv/bin/piper";
const std::string MODEL_PATH = "./models/pt_BR-edresson-low.onnx";
const std::string CACHE_DIR = "./audio_cache";

// Função utilitária para verificar se um arquivo existe
bool arquivo_existe(const std::string& caminho) {
    struct stat buffer;
    return (stat(caminho.c_str(), &buffer) == 0);
}

// Cria diretórios necessários se não existirem
void inicializar_diretorios() {
    system("mkdir -p ./audio_cache");
}

// Normaliza o texto para criar um nome de arquivo de cache seguro
std::string gerar_nome_cache(const std::string& texto) {
    std::string nome = texto;
    // Converte para minúsculas e substitui caracteres não alfanuméricos por "_"
    std::transform(nome.begin(), nome.end(), nome.begin(), ::tolower);
    for (char& c : nome) {
        if (!isalnum(c)) {
            c = '_';
        }
    }
    // Limita o tamanho para evitar nomes de arquivos gigantescos
    if (nome.length() > 30) {
        nome = nome.substr(0, 30);
    }
    return CACHE_DIR + "/" + nome + ".wav";
}

// Função principal de síntese e reprodução de voz
void sintetizar_e_falar(const std::string& texto, bool usar_cache = true) {
    std::string caminho_cache = gerar_nome_cache(texto);

    // 1. Se for frase fixa e o arquivo de cache já existir, toca instantaneamente via aplay
    if (usar_cache && arquivo_existe(caminho_cache)) {
        std::cout << "[Cache Hit]: Tocando áudio estático para: \"" << texto << "\"" << std::endl;
        std::string cmd_tocar = "aplay -q " + caminho_cache;
        system(cmd_tocar.c_str());
        return;
    }

    // 2. Se não estiver em cache (ou se usar_cache for falso), gera o arquivo WAV com o Piper
    std::cout << "[TTS Dinâmico]: Sintetizando com Piper..." << std::endl;
    
    // Monta o comando do piper salvando diretamente no arquivo de cache
    // Usamos pipes do sistema para injetar o texto via STDIN de forma segura
    std::string comando_piper = "echo \"" + texto + "\" | " + PIPER_BIN + " --model " + MODEL_PATH + " --output_file " + caminho_cache;
    
    int status = system(comando_piper.c_str());
    if (status != 0) {
        std::cerr << "[Erro]: Falha ao executar o Piper para síntese de voz." << std::endl;
        return;
    }

    // 3. Reproduz o áudio recém-gerado
    std::string cmd_tocar = "aplay -q " + caminho_cache;
    system(cmd_tocar.c_str());
}

int main() {
    inicializar_diretorios();

    // Teste 1: Frases Fixas (A primeira gera o cache, a segunda usa o Cache Hit instantâneo)
    std::cout << "\n--- Teste 1: Frases Fixas (Com Cache) ---" << std::endl;
    sintetizar_e_falar("Lâmpada ligada.");
    sintetizar_e_falar("Lâmpada ligada."); // Deve disparar o Cache Hit

    sintetizar_e_falar("Não entendi. Poderia repetir?");

    // Teste 2: Frases Dinâmicas (Respostas livres vindas do Gemini)
    std::cout << "\n--- Teste 2: Resposta Dinâmica do Gemini ---" << std::endl;
    std::string resposta_gemini = "Adicionei leite e pão na sua lista de compras com sucesso.";
    sintetizar_e_falar(resposta_gemini, false); // usar_cache = false para textos que não se repetem

    return 0;
}

