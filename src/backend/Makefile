# ======================================================================
# Makefile: despatcher (SQLite + nlohmann/json), oww (ONNX Runtime + PortAudio)
# e tts_manager (Gerado dentro da pasta piper/) + testes (Gemini e STT)
#
# Estrutura (a partir da pasta deste Makefile):
#    despatcher.cpp, armazenamento.hpp, armazenamento.cpp
#    wakeword/oww.cpp
#    piper/tts_manager.cpp
#    wakeword/libs/onnxruntime-linux-<arch>-<versão>/
#
# Binários gerados:
#    ./despatcher
#    ./piper/tts_manager
#    ./wakeword/oww
#    ./teste_gemini
#    ./teste_stt
# ======================================================================

CXX      := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -MMD -MP
BUILD    := build

.DEFAULT_GOAL := all
.PHONY: all clean info oww tts

# --- Pasta deste Makefile (termina com "/")
ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# --- Pastas dos módulos
WW         := $(ROOT)wakeword
PIPER_DIR  := $(ROOT)piper

# --- Binários
OWW_BIN     := $(WW)/oww
TTS_BIN     := $(PIPER_DIR)/tts_manager
GEMINI_BIN  := teste_gemini
STT_BIN     := teste_stt
BINS        := despatcher $(TTS_BIN) $(OWW_BIN) $(GEMINI_BIN) $(STT_BIN)

# --- Arquitetura -> nome usado nos pacotes do ONNX Runtime
ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
  ORT_ARCH := x64
else
  ORT_ARCH := $(ARCH)
endif

# --- Procura o onnxruntime já extraído
ORT_CAND := $(patsubst %/include,%,$(wildcard $(WW)/libs/onnxruntime-linux-$(ORT_ARCH)-*/include))
ORT      ?= $(firstword $(sort $(ORT_CAND)))

# --- Exige o ONNX Runtime só quando o alvo envolve o oww
ifneq ($(if $(MAKECMDGOALS),$(filter all oww $(OWW_BIN),$(MAKECMDGOALS)),all),)
  ifeq ($(ORT),)
    $(error ONNX Runtime não encontrado em $(WW)/libs (arquitetura: $(ORT_ARCH)). Use: make ORT=/caminho/... ou make WW=/caminho/wakeword)
  endif
endif

# ----------------------------------------------------------------------
# Alvos
# ----------------------------------------------------------------------
all: $(BINS)

# despatcher: nlohmann/json (só header) + SQLite
despatcher: $(BUILD)/despatcher.o $(BUILD)/armazenamento.o
	$(CXX) $^ -o $@ -lsqlite3

# tts_manager: gerado diretamente dentro da pasta piper/
$(TTS_BIN): $(BUILD)/tts_manager.o
	$(CXX) $^ -o $@

# teste_gemini: usa libcurl para se comunicar com a API do Gemini
teste_gemini: $(BUILD)/teste_gemini.o $(BUILD)/gemini.o
	$(CXX) $^ -o $@ -lcurl

# teste_stt: usa libcurl e libportaudio
teste_stt: $(BUILD)/teste_stt.o $(BUILD)/stt.o
	$(CXX) $^ -o $@ -lcurl -lportaudio

# atalho: "make oww" gera wakeword/oww
oww: $(OWW_BIN)

# oww: ONNX Runtime + PortAudio, binário gerado dentro de wakeword/
$(OWW_BIN): CXXFLAGS += -O3 -I$(ORT)/include
$(OWW_BIN): $(BUILD)/oww.o
	$(CXX) $^ -o $@ -L$(ORT)/lib -lonnxruntime -lportaudio -Wl,-rpath,$(ORT)/lib

# Regra específica para o tts_manager.cpp que está dentro de piper/
$(BUILD)/tts_manager.o: $(PIPER_DIR)/tts_manager.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# oww.cpp fica dentro de wakeword/
$(BUILD)/oww.o: $(WW)/oww.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Regra genérica .cpp -> .o (para os fontes na raiz do Makefile)
$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(BINS)

# Mostra os caminhos que o make resolveu (útil para depurar)
info:
	@echo "ROOT       = $(ROOT)"
	@echo "WW         = $(WW)"
	@echo "PIPER_DIR  = $(PIPER_DIR)"
	@echo "OWW_BIN    = $(OWW_BIN)"
	@echo "TTS_BIN    = $(TTS_BIN)"
	@echo "ARCH       = $(ARCH)  (ORT_ARCH = $(ORT_ARCH))"
	@echo "ORT        = $(if $(ORT),$(ORT),(não encontrado))"

# Dependências de headers geradas pelo -MMD
-include $(wildcard $(BUILD)/*.d)
