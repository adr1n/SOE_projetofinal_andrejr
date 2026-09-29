import os
import io
import wave
import time
import sounddevice as sd
from groq import Groq

with open("groq_api_key") as key:
    api_key = key.read().strip()
    print(f"Groq API: {api_key}")
    # os.environ["GROQ_API_KEY"] = api_key
        
# Inicializa o cliente do Groq (lê automaticamente a variável GROQ_API_KEY)
client = Groq(api_key = api_key)

SAMPLE_RATE = 16000
CHANNELS = 1


def gravar_audio_comando(duracao_segundos=4) -> io.BytesIO:
    """Grava o áudio do microfone e retorna um buffer em memória no formato WAV."""
    print(f"\n[STT Groq]: Gravando comando por {duracao_segundos} segundos...")
    
    # Captura o áudio do microfone
    audio_data = sd.rec(
        int(duracao_segundos * SAMPLE_RATE), 
        samplerate=SAMPLE_RATE, 
        channels=CHANNELS, 
        dtype='int16'
    )
    sd.wait()  # Aguarda a gravação finalizar
    
    # Empacota os dados brutos de áudio em formato WAV na memória RAM (sem salvar no cartão SD)
    wav_buffer = io.BytesIO()
    with wave.open(wav_buffer, 'wb') as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(2)  # 16-bit = 2 bytes
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(audio_data.tobytes())
    
    wav_buffer.seek(0)
    wav_buffer.name = "comando.wav"  # O SDK do Groq exige um nome de arquivo fictício
    return wav_buffer

def transcrever_com_groq(buffer_audio: io.BytesIO) -> str:
    """Envia o buffer de áudio para a API do Groq usando o Whisper Large v3."""
    inicio = time.time()
    print("[STT Groq]: Enviando áudio para a nuvem...")
    
    transcription = client.audio.transcriptions.create(
        file=(buffer_audio.name, buffer_audio.read()),
        model="whisper-large-v3",
        language="pt",  # Força a transcrição direta em Português
        response_format="text"
    )
    
    latencia = (time.time() - inicio) * 1000
    print(f"[STT Groq]: Concluído em {latencia:.0f} ms!")
    return transcription.strip()

# --- Teste do Módulo ---
if __name__ == "__main__":
    #with open("groq_api_key") as key:
    #    api_key = file.read()
    #    print(api_key)

    buffer = gravar_audio_comando(duracao_segundos=3)
    texto_transcrito = transcrever_com_groq(buffer)
    print(f"\n[Resultado da Transcrição]: \"{texto_transcrito}\"")
