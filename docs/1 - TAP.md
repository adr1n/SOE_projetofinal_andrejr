# Termo de Abertura do Projeto

- **Nome do Projeto: Assistente de Voz para Automação Residencial**
- **Data de Início: 28/08/2026**
- **Data de Término: 04/12/2026**

## Visão Geral do Projeto

### Descrição do Problema

<!-- Descrevam de forma objetiva o problema que o projeto pretende solucionar. Indiquem quem é afetado pelo problema e quais são suas principais características. -->

Este projeto tem como objetivo proporcionar uma de interagir com uma automação residencial por comando de voz. Este produto é geralmente utilizado pelos mais diversos usuários, mas com o objetivo de controlar, em exemplo: lâmpadas, ar condicionados e demais equipamentos inteligentes como tvs, geladeiras, caixas de som, projetores, etc. Sendo assim, o projeto busca integrar um Assistente de Voz ao próprio Home Assistant nativo do Raspberry Pi OS para automação residencial.

### Estado da arte

<!-- Apresentem brevemente as principais soluções existentes para o problema, considerando, quando pertinente, soluções acadêmicas, sistemas, produtos ou serviços disponíveis no mercado.

As informações utilizadas devem ser fundamentadas em fontes confiáveis e devidamente referenciadas na última seção deste documento utilizando [este formato](https://researchguides.njit.edu/ieee-citation/ieeereferencing). -->

Atualmente, existem soluções comerciais disponíveis no mercado como o Echo Dot (dispositivo com microfone e auto-falante) com a Alexa (assistente virtual que processa comandos de voz). Como o objetivo do projeto é montar tanto o dispositivo estilo Echo Dot quanto instalar um assistente virtual neste, seria interessante utilizar da Alexa para isto. Porém, a Amazon acabou com a distribuição do software da Alexa para uso livre na Raspberry Pi 3, inviabilizando o uso desta ferramenta.

Há também a opção de utilizar ferramentas para processamento de áudio em texto rodando localmente na raspberry como o Vosk [1], o WHisper.cpp/FastWhisper [2], as quais provavelmente serão utilizadas.

Para o processamento de Palavra de Ativação (Wake Word) há bibliotecas como o Open Wake Word [3] que rodam localmente na Rasp e processam palavras como "Alexa!" ou "Ok, Google" para ativar o Whisper que irá processar a fala em texto. Há ainda o Speex [4] que serve para a supressão de ruído exclusivamente para a fala.

Para o processamento de linguagem e tradução para comandos, é possível tanto o uso de LLM local com o Ollama como também o uso de APIs gratuitas como o do Google Gemini que podem receber o texto e retornar um arquivo JSON formatado que é intepretado como comandos.

Por fim, para completar o ciclo de interação humana tornando o assistente capaz de responder audivelmente, utiliza-se a síntese de voz (Text-to-Speech - TTS). Entre as ferramentas modernas e otimizadas para processadores ARM, destaca-se o Piper TTS [6], um motor local e de código aberto que gera voz natural em Português do Brasil com baixíssima latência e consumo reduzido de memória RAM no Raspberry Pi 3.

### Objetivos

#### Objetivo Geral

Desenvolver um sistema distribuído e embarcado de assistente de voz para automação residencial executado em Raspberry Pi, capaz de realizar processamento e detecção local de comandos de fala, interpretar a intenção do usuário por meio de inteligência artificial generativa e executar ações físicas e lógicas em tempo real com atualização de interface gráfica.

#### Objetivos Específicos

- Implementar a detecção local da palavra de ativação (wake word) em tempo real no hardware embarcado via openwakeword, garantindo baixa latência e consumo reduzido de processamento.  

- Integrar um motor de conversão de fala em texto (Speech-to-Text - STT) local e offline baseado em Vosk para transcrição eficiente dos comandos de áudio capture após o acionamento da palavra de chave.

- Estruturar a comunicação de dados via API com o modelo Google Gemini utilizando Structured Outputs (Pydantic) para garantir a conversão precisa e determinística da linguagem natural em esquemas JSON padronizados.

- Criar um dispatcher em Python responsável pela leitura do arquivo JSON gerado e pelo acionamento dos atuadores (relés, GPIOs e protocolos de automação).

- Implementar a síntese de voz local (Text-to-Speech - TTS) utilizando o Piper TTS (com modelos em Português do Brasil), permitindo que o sistema responda audivelmente pela caixa de som conectada à placa.

### Escopo do Projeto

1. **Módulo de Entrada de Áudio e Wake Word:** Algoritmo de captura contínua de áudio via microfone USB/HAT e detecção local offline da palavra de chamada no Raspberry Pi.

2. **Módulo de Transcrição (STT Local):** Processamento local da voz capturada em arquivo/stream e conversão para texto usando modelos otimizados (Vosk/Kaldi).

3. **Módulo de Processamento Semântico (Nuvem):** Envio do texto transcrito para a API do Google Gemini (gemini-2.5-flash) e recebimento da resposta estritamente estruturada em formato JSON.

4. **Módulo Backend & Execução de Hardware:** Aplicação principal em Python (FastAPI/Flask) para mapear os parâmetros do JSON e controlar os estados das saídas do Raspberry Pi (pino GPIO/simulação de dispositivos).

5. **Módulo de Resposta Sonora (TTS Local - Piper):** Geração local e execução de áudio em Português do Brasil utilizando o Piper TTS para dar retorno falado ao usuário logo após a interpretação do comando.

6. **Módulo de Controle de Hardware:** Aplicação Python executada como daemon/serviço no Raspberry Pi para chaveamento dos pinos GPIO (relés/dispositivos simulação).

7. **Circuito Protótipo:** Circuito com LEDs que simulam uma casa, são conectados aos GPIOs da Rasp.


<!-- Descrevam o que será desenvolvido no projeto e **delimitem claramente** aquilo que faz parte e não faz parte do escopo. -->

### _Stakeholders_

<!-- Identifiquem as pessoas, grupos ou organizações que possuem interesse ou podem ser afetados pelo projeto, indicando, quando pertinente, sua relação com o projeto. -->


## Recursos do Projeto

### Membros da Equipe

| **Nome** | **Matrícula** | **Curso** | **Funções** |
|----------|---------------|-----------|-------------|
|André Jacinto Rodrigues|221007822|Engenharia Eletrônica|Líder|

### Orçamento estimado (R$)

| Item / Componente | Descrição / Modelo Sugerido | Qtd | Valor Estimado (R$) |
| :--- | :--- | :---: | :---: |
| **Raspberry Pi 3 (1GB)** | Placa principal para rodar o backend, STT (Vosk) e controle | 1 | R$ 0,00 *(Já possui)*
| **Fonte 5V 3A (USB-C)** | Alimentação  | 1 | R$ 0,00 *(Já possui)* |
| **Cartão MicroSD 32GB/64GB** | Classe 10 / A1 para o SO e armazenamento dos modelos locais | 1 | R$ 0,00 *(Já possui)* |
| **Placa de Som USB Estéreo** | Interface para entrada de microfone P2 e saída de áudio | 1 | R$ 15,00 - R$ 30,00 |
| **Microfone Omnidirecional / Webcam** | Gravação de áudio| 1 | R$ 0,00 *(Já possui)* |
| **Caixa de Som Auxiliar** | Caixa (Conexão via P2 na placa USB) | 1 | R$ 0,00 *(Já possui)* |
| **Custo de APIs (Gemini)** | Plano gratuito (Free Tier) até 15 requisições/minuto | - | R$ 0,00 |
<!-- Discutam dentro da equipe a verba possível disponível para o desenvolvimento do projeto, com base na complexidade do projeto, na quantidade de membros e na realidade de cada um. -->

### Esforço estimado (horas)

A estimativa foi calculada considerando o desenvolvimento individual (1 integrante), dividindo a carga horária em **10 a 12 horas semanais** dedicadas ao projeto. A complexidade intermediária exige atenção especial à configuração de dependências do ecossistema C/Python e ao ajuste fino dos tempos de execução (*picos de CPU*) no Raspberry Pi 3.

---

#### Estimativa por Etapa do Projeto

| Etapa de Desenvolvimento | Descrição das Atividades | Tempo Estimado (Semanas) | Horas Estimadas |
| :--- | :--- | :---: | :---: |
| **1. Configuração do Hardware e SO** | Instalação do Raspberry Pi OS Lite (64-bit), ajuste da placa de som USB, testes do microfone/caixa de som P2 e configuração do ambiente Python (`venv`). | 1 semana | 10h |
| **2. Pipeline de Entrada (Wake Word)** | Instalação do `openwakeword`, testes de escuta do áudio via *buffer* e ajuste da sensibilidade (*threshold*) para evitar falsos positivos. | 1,5 semana | 15h |
| **3. Módulo de Transcrição (STT)** | Integração do `Vosk` (modelo `small-pt`), criação do script de gravação pós-wake word e otimização da limpeza de memória RAM. | 1,5 semana | 15h |
| **4. Processamento Semântico (Gemini API)** | Configuração do SDK da Google Gemini API, definição das classes Pydantic (*Structured Outputs*) e tratamento de erros de conexão/JSON. | 1 semana | 10h |
| **5. Módulo de Resposta Sonora (Piper TTS)** | Compilação/instalação do `Piper TTS`, download do modelo em Português (`pt_BR`) e criação da função de reprodução de áudio síncrona. | 1,5 semana | 15h |
| **6. Controle de Hardware (GPIO & Dispatcher)** | Mapeamento dos pinos GPIO (relés), desenvolvimento do script principal (*loop/daemon*) unificando o fluxo sequencial completo. | 1,5 semana | 15h |
| **7. Testes de Integração e Ajuste de Latência** | Testes end-to-end de comandos de voz, calibração dos picos de processamento na CPU e ajustes no tempo de resposta do assistente. | 1 semana | 10h |
| **8. Documentação Técnica e Validação Final** | Redação do relatório final, organização do repositório de código e validação dos objetivos do projeto. | 1 semana | 10h |

---

#### Resumo da Estimativa

* **Carga Horária Total:** **90 horas de trabalho direto.**
* **Duração Estimada:** **9 a 10 semanas** (aproximadamente 2,5 meses) de desenvolvimento contínuo individual.

---

#### Análise Sincera de Riscos e Desafios (Desenvolvimento Solo)

* **Compilação e Conflito de Dependências:** O maior risco de atraso na fase inicial é o processo de instalação de bibliotecas de áudio e C/C++ no Raspberry Pi OS (como o `PortAudio`, `sounddevice` e os *bindings* do Piper). Alocar 1 semana extra de margem para lidar com *builds* de ambiente é fundamental.
* **Gargalo de Processamento Unificado:** Por ser um desenvolvedor único, o projeto avança em modo estritamente sequencial. Caso um módulo (ex: o STT) demore mais do que o esperado para responder sem estourar a RAM do Pi 3, o progresso das etapas seguintes ficará pausado até a resolução do gargalo.


## Referências

[1] D. Ward *et al.*, "openWakeWord: An open-source audio wake word detection framework," 2023. [Online]. Disponível em: https://github.com/dscripka/openWakeWord

[2] Xiph.Org Foundation, "The Speex speech codec and Speech Processing Library," 2023. [Online]. Disponível em: https://www.speex.org/

[3] Alpha Cephei, "Vosk Speech Recognition Toolkit," 2023. [Online]. Disponível em: https://alphacephei.com/vosk/

[4] G. Gerganov, "whisper.cpp: High-performance inference of OpenAI's Whisper model in C/C++," 2023. [Online]. Disponível em: https://github.com/ggml-org/whisper.cpp

[5] Google, "Gemini API Documentation: Structured Outputs with Pydantic," *Google AI for Developers*, 2024. [Online]. Disponível em: https://ai.google.dev/

[6] Rhasspy, "Piper: A fast, local neural text-to-speech system," 2023. [Online]. Disponível em: https://github.com/rhasspy/piper