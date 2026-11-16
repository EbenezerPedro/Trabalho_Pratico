#!/bin/bash
# ============================================================================
# aplicar_tp2.sh - aplica o Segundo Trabalho Pratico ao clone do Apache NuttX
#
# Uso:
#   ./aplicar_tp2.sh [CAMINHO_NUTTX] [CAMINHO_APPS]
#
# Exemplo:
#   ./aplicar_tp2.sh ~/nuttx-workspace/nuttx ~/nuttx-workspace/apps
#
# O script:
#   1) copia o cabecalho publico e a implementacao da chamada de sistema
#   2) copia a aplicacao de teste
#   3) aplica o patch nas edicoes dos arquivos do NuttX (Make.defs, CMakeLists,
#      syscall.csv)
# ============================================================================

set -e

NUTTX_DIR="${1:-$HOME/nuttx-workspace/nuttx}"
APPS_DIR="${2:-$HOME/nuttx-workspace/apps}"
ORIGEM="$(cd "$(dirname "$0")" && pwd)"

echo "== NuttX : $NUTTX_DIR"
echo "== apps  : $APPS_DIR"
echo "== pacote: $ORIGEM"

if [ ! -d "$NUTTX_DIR/sched/sched" ]; then
  echo "ERRO: '$NUTTX_DIR' nao parece ser o clone do NuttX (falta sched/sched)."
  exit 1
fi

# ---------------------------------------------------------------------------
# 1) Arquivos novos do kernel
# ---------------------------------------------------------------------------
echo "-> copiando arquivos novos do kernel"
cp -v "$ORIGEM/kernel/include/nuttx/tp2_info_tarefa.h" "$NUTTX_DIR/include/nuttx/"
cp -v "$ORIGEM/kernel/sched/sched/tp2_info_tarefa.c"   "$NUTTX_DIR/sched/sched/"

# ---------------------------------------------------------------------------
# 2) Aplicacao de teste
# ---------------------------------------------------------------------------
echo "-> copiando a aplicacao de teste"
mkdir -p "$APPS_DIR/examples/tp2info"
cp -v "$ORIGEM"/apps/examples/tp2info/* "$APPS_DIR/examples/tp2info/"

# ---------------------------------------------------------------------------
# 3) Patch nas edicoes em arquivos existentes do NuttX
# ---------------------------------------------------------------------------
echo "-> aplicando patch nas edicoes dos arquivos do NuttX"
cd "$NUTTX_DIR"
if git apply --check "$ORIGEM/patch/nuttx_tp2.patch" 2>/dev/null; then
  git apply "$ORIGEM/patch/nuttx_tp2.patch"
  echo "   patch aplicado com git apply"
else
  echo "   git apply nao pode ser usado; tentando patch -p1"
  patch -p1 < "$ORIGEM/patch/nuttx_tp2.patch"
  echo "   patch aplicado com patch -p1"
fi

echo
echo "== CONCLUIDO =="
echo "Arquivos novos/alterados esperados:"
echo "  $NUTTX_DIR/include/nuttx/tp2_info_tarefa.h        (novo)"
echo "  $NUTTX_DIR/sched/sched/tp2_info_tarefa.c           (novo)"
echo "  $NUTTX_DIR/sched/sched/Make.defs                   (alterado)"
echo "  $NUTTX_DIR/sched/sched/CMakeLists.txt              (alterado)"
echo "  $NUTTX_DIR/syscall/syscall.csv                     (alterado)"
echo "  $APPS_DIR/examples/tp2info/                        (novo diretorio)"
echo
echo "Proximo passo:"
echo "  cd $NUTTX_DIR"
echo "  ./tools/configure.sh esp32-devkitc:nsh"
echo "  cp $ORIGEM/configs_tp2/defconfig_tp2 .config && make olddefconfig"
echo "  make -j\$(nproc)"
