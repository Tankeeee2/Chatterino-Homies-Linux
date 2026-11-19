#!/usr/bin/env bash
# ==============================================================================
# Chatterino Homies Linux Updater
# Actualiza el repositorio, compila e instala en el sistema solicitando sudo.
# ==============================================================================

set -e

# Estilos y colores
BOLD='\033[1m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}${BOLD}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}${BOLD}║           Actualizador de Chatterino Homies                ║${NC}"
echo -e "${CYAN}${BOLD}╚════════════════════════════════════════════════════════════╝${NC}\n"

# 1. Localizar el directorio del repositorio
REPO_DIR=""

if [[ -n "$1" && -d "$1/.git" ]]; then
    REPO_DIR="$1"
fi

if [[ -z "$REPO_DIR" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd)"
    if [[ -d "$SCRIPT_DIR/../.git" ]]; then
        REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
    fi
fi

if [[ -z "$REPO_DIR" ]]; then
    CANDIDATES=(
        "$PWD"
        "$HOME/chatterino7"
        "$HOME/Chatterino-Homies-Linux"
        "$HOME/.local/src/chatterino7"
        "/home/juanjo/chatterino7"
    )
    for c in "${CANDIDATES[@]}"; do
        if [[ -d "$c/.git" ]]; then
            REPO_DIR="$c"
            break
        fi
    done
fi

if [[ -z "$REPO_DIR" ]]; then
    echo -e "${YELLOW}>> No se encontró un repositorio local existente.${NC}"
    REPO_DIR="$HOME/.local/src/chatterino7"
    echo -e "${CYAN}>> Clonando repositorio en: $REPO_DIR${NC}"
    mkdir -p "$(dirname "$REPO_DIR")"
    git clone --recurse-submodules https://github.com/Tankeeee2/Chatterino-Homies-Linux.git "$REPO_DIR"
fi

echo -e "${CYAN}>> Directorio del repositorio:${NC} ${BOLD}$REPO_DIR${NC}"
cd "$REPO_DIR"

# 2. Descargar actualizaciones
echo -e "\n${YELLOW}>> Comprobando commits en origin/main...${NC}"
git fetch origin main

CURRENT_HASH=$(git rev-parse --short HEAD 2>/dev/null || echo "desconocido")
REMOTE_HASH=$(git rev-parse --short origin/main 2>/dev/null || echo "desconocido")

echo -e "   Commit actual: ${BOLD}$CURRENT_HASH${NC}"
echo -e "   Commit remoto: ${BOLD}$REMOTE_HASH${NC}\n"

HAS_STASH=0
if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    echo -e "${YELLOW}>> Hay cambios locales sin confirmar. Guardando en stash...${NC}"
    git stash
    HAS_STASH=1
fi

echo -e "${YELLOW}>> Aplicando git pull origin main...${NC}"
git pull --rebase origin main || git pull origin main
echo -e "${YELLOW}>> Sincronizando y actualizando submódulos...${NC}"
git submodule sync --recursive
git submodule update --init --recursive

if [[ "$HAS_STASH" -eq 1 ]]; then
    echo -e "${YELLOW}>> Restaurando cambios locales de stash...${NC}"
    git stash pop || true
fi

# 3. Compilación
echo -e "\n${YELLOW}>> Configurando CMake (Release)...${NC}"
cmake -B build \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DUSE_PRECOMPILED_HEADERS=OFF \
    -DCHATTERINO_LTO=OFF \
    -DCHATTERINO_UPDATER=ON \
    -DCMAKE_BUILD_TYPE=Release

NPROC=$(nproc 2>/dev/null || echo 4)
echo -e "\n${CYAN}>> Compilando con $NPROC hilos...${NC}"
cmake --build build -j"$NPROC"

# 4. Instalación
echo -e "\n${GREEN}${BOLD}>> Compilación completada.${NC}"
echo -e "${YELLOW}${BOLD}>> Instalando en el sistema (/usr). Por favor introduce tu contraseña de root:${NC}\n"

sudo cmake --install build

# Actualizar symlinks de usuario si existen
if [[ -L "$HOME/.local/bin/chatterino" || -L "$HOME/.local/bin/chatterino-homies" ]]; then
    echo -e "\n${CYAN}>> Actualizando accesos directos en ~/.local/bin/...${NC}"
    mkdir -p "$HOME/.local/bin"
    ln -sf /usr/bin/chatterino-homies "$HOME/.local/bin/chatterino"
    ln -sf /usr/bin/chatterino-homies "$HOME/.local/bin/chatterino-homies"
fi

echo -e "\n${GREEN}${BOLD}════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}${BOLD} ✔ ¡Chatterino Homies se ha actualizado correctamente!       ${NC}"
echo -e "${GREEN}${BOLD}════════════════════════════════════════════════════════════${NC}\n"

read -p "¿Deseas reiniciar Chatterino ahora? [S/n]: " -r RESP
RESP=${RESP:-S}
if [[ "$RESP" =~ ^[SsYy]$ ]]; then
    echo -e "\n${CYAN}>> Reiniciando Chatterino...${NC}"
    pkill -x chatterino || pkill -x chatterino-homies || true
    sleep 1
    nohup /usr/bin/chatterino >/dev/null 2>&1 &
    disown
    echo -e "${GREEN}>> ¡Chatterino reiniciado!${NC}"
    sleep 1
fi
