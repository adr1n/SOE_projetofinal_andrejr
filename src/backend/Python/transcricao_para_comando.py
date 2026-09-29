import os
import json
from google import genai
from google.genai import types


SYSTEM_INSTRUCTION = """
Você é o cérebro de um assistente residencial autônomo. Sua função é converter a fala do usuário (transcrita por STT) em uma intenção estruturada em JSON.

Regras de Mapeamento:
1. Buscar clima/tempo -> tipo_comando: "CLIMA"
2. Responder qual dia é hoje -> tipo_comando: "DATA_ATUAL"
3. Agendar tarefas/eventos -> tipo_comando: "AGENDAR_TAREFA" (extraia descricao_evento e data_evento)
4. Falar eventos agendados -> tipo_comando: "LISTAR_TAREFAS"
5. Acionar lâmpada/relé -> tipo_comando: "ACIONAR_LAMPADA" (estado_lampada: "ligar" ou "desligar")
6. Anotar lista de compras -> tipo_comando: "ADICIONAR_COMPRAS" (item_compra)
7. Falar lista de compras -> tipo_comando: "LISTAR_COMPRAS"
8. Outros assuntos -> tipo_comando: "CONVERSA_GERAL" (forneça a resposta em conteudo.resposta_texto de forma sucinta para voz).
"""

def api_config():
    # 1. Tenta carregar a chave de API do arquivo se não estiver na variável de ambiente
    if not os.environ.get("GEMINI_API_KEY"):
        try:
            with open("gemini_api.txt", "r") as f:
                print("[Info]: Carregando GEMINI_API_KEY do arquivo 'gemini_api.txt'.")
                os.environ["GEMINI_API_KEY"] = f.read().strip()
        except FileNotFoundError:
            print("[Erro]: Arquivo 'gemini_api.txt' não encontrado e GEMINI_API_KEY não definida.")

    # 2. Inicializa o cliente oficial do Gemini
    client = genai.Client(api_key=os.environ.get("GEMINI_API_KEY"))

    # 3. Definição do Schema de Saída (Enumeração de Ações + Dados)
    RESPONSE_SCHEMA = types.Schema(
        type=types.Type.OBJECT,
        required=["tipo_comando", "conteudo"],
        properties={
            "tipo_comando": types.Schema(
                type=types.Type.STRING,
                enum=[
                    "CLIMA",
                    "DATA_ATUAL",
                    "AGENDAR_TAREFA",
                    "LISTAR_TAREFAS",
                    "ACIONAR_LAMPADA",
                    "ADICIONAR_COMPRAS",
                    "LISTAR_COMPRAS",
                    "CONVERSA_GERAL"
                ],
                description="A categoria exata da ação que o assistente deve executar."
            ),
            "conteudo": types.Schema(
                type=types.Type.OBJECT,
                description="Parâmetros específicos necessários para executar a ação.",
                properties={
                    "estado_lampada": types.Schema(type=types.Type.STRING, enum=["ligar", "desligar"]),
                    "item_compra": types.Schema(type=types.Type.STRING),
                    "descricao_evento": types.Schema(type=types.Type.STRING),
                    "data_evento": types.Schema(type=types.Type.STRING),
                    "resposta_texto": types.Schema(type=types.Type.STRING, description="Resposta em texto para o Piper TTS sintetizar")
                }
            )
        }
    )
    return client, RESPONSE_SCHEMA


def processar_comando_voz(client, RESPONSE_SCHEMA, texto_transcrito: str) -> dict:
    """Envia o texto transcrito ao Gemini 3.5 Flash Lite e retorna a ação estruturada."""
    
    # Usa o modelo ultra-rápido para agentes
    model = "gemini-3.5-flash-lite"

    # Para tarefas de classificação/extração simples com foco em latência,
    # utilizamos pensando mínimo/desativado para evitar overhead no tempo de resposta.
    config = types.GenerateContentConfig(
        system_instruction=SYSTEM_INSTRUCTION,
        response_mime_type="application/json",
        response_schema=RESPONSE_SCHEMA,
        thinking_config=types.ThinkingConfig(
            thinking_level="MINIMAL"  # Otimizado para máxima velocidade e menor latência
        )
    )

    try:
        response = client.models.generate_content(
            model=model,
            contents=texto_transcrito,
            config=config
        )
        # Retorna o dicionário Python extraído do JSON do Gemini
        return json.loads(response.text)
    except Exception as e:
        print(f"[Erro Gemini API]: {e}")
        return {
            "tipo_comando": "CONVERSA_GERAL",
            "conteudo": {"resposta_texto": "Desculpe, tive um problema ao processar seu comando."}
        }
def main(client=None, RESPONSE_SCHEMA=None):
    testes = [
        "Ligue a luz da cozinha por favor",
        "Anota leite e pão na lista de compras",
        "O que eu tenho agendado para amanhã?",
        "Como está o tempo hoje?"
    ]

    for comando in testes:
        print(f"\n[Entrada Voz]: \"{comando}\"")
        resultado = processar_comando_voz(client, RESPONSE_SCHEMA, comando)
        print(f"[JSON Estruturado]: {json.dumps(resultado, ensure_ascii=False, indent=2)}")

# --- Teste de Execução ---
if __name__ == "__main__":
    client, RESPONSE_SCHEMA = api_config()
    main(client, RESPONSE_SCHEMA)