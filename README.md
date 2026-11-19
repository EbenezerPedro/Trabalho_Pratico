Arquivo README
# TP2 — Chamada de Sistema tp2_info_tarefa() no NuttX/ESP32

## Visão geral

Este trabalho implementa uma chamada de sistema customizada no NuttX chamada tp2_info_tarefa(), que permite a uma tarefa em espaço de usuário consultar o TCB (Task Control Block) de outra tarefa (ou de si mesma) e receber um relatório consolidado com informações de gerenciamento de tarefas, escalonamento, uso de pilha e uso de CPU. O problema resolvido é a ausência de uma interface pública e segura para que aplicações de usuário inspecionem o estado interno de tarefas do sistema sem acessar diretamente estruturas internas do kernel. A solução usa seção crítica, controle de acesso por grupo e medições opcionais de pilha (stack coloration) e CPU (cpuload), com flags de validade para cada medição. Tecnologias principais: linguagem C, NuttX RTOS, ESP32 (esp32-devkitc), pthreads, CMake/Make e Kconfig.

## Objetivo

O trabalho tem como objetivo implementar uma interface de consulta de informações das tarefas do NuttX, contemplando os seguintes aspectos:

I - Gerenciamento de tarefas: identificação de tarefas comuns, threads e threads do kernel.

II - Escalonamento: consulta da prioridade atual, prioridade base e identificação de herança de prioridade.

III - Gerenciamento de memória: consulta do tamanho total e do pico de utilização da pilha.

IV - Monitoramento de recursos: consulta da utilização da CPU.

V - Segurança: validação dos parâmetros recebidos e controle de acesso às informações das tarefas.

A função retorna os dados por meio da estrutura struct tp2_info_tarefa_s, preenchida com as informações obtidas pelo kernel.

## Funcionalidades

1 - Consulta das informações da própria tarefa utilizando pid = 0 ou de outra tarefa acessível por meio do seu PID.

2 - Identificação do tipo de tarefa:

- TP2_TIPO_TAREFA: tarefa comum;

- TP2_TIPO_PTHREAD: thread criada com pthread_create();

- TP2_TIPO_KERNEL: thread pertencente ao kernel.

3 - Consulta do PID da thread, PID do grupo/processo e PID do processo pai, quando suportado pela configuração do NuttX.

4 - Consulta da prioridade atual e da prioridade base, incluindo a identificação de herança de prioridade.

5 - Consulta do estado numérico da tarefa (tstate_e) e sua representação textual, como READY, RUNNING e WAITING.

6 - Consulta do nome da tarefa.

7 - Consulta do tamanho total da pilha e do pico de utilização, quando CONFIG_STACK_COLORATION estiver habilitado.

8 - Consulta da utilização da CPU em milésimos, quando o monitoramento de carga da CPU estiver habilitado.

9 - Utilização de sinalizadores para indicar quais informações foram efetivamente medidas:

- TP2_SINAL_PILHA_MEDIDA;

- TP2_SINAL_CPU_MEDIDA;

- TP2_SINAL_PRIO_HERDADA.

Compatibilidade da interface com C e C++, por meio de extern "C".

### Descrição dos arquivos:



- tp2_info_tarefa.h: define as constantes, os tipos, os sinalizadores, a estrutura de informações e a declaração da função.

- tp2_info+tarefa.c: contém a implementação da função responsável por consultar as informações das tarefas.

- tp2info_main.c: contém o programa principal responsável por executar os testes e apresentar os resultados.

- CMakeLists.txt: define as configurações necessárias para compilar a aplicação utilizando CMake.

- Kconfig: disponibiliza as opções de configuração da aplicação.

- Makefile: contém as regras de compilação da aplicação, quando utilizado o sistema de build correspondente.

## Instalação e Execução

### Clonar NuttX e apps

```c
mkdir ~/nuttx-workspace && cd ~/nuttx-workspace
git clone https://github.com/apache/nuttx.git nuttx
git clone https://github.com/apache/nuttx-apps.git apps
```

### Copiar os arquivos do TP2

```c
 # Cabeçalho público
cp tp2_info_tarefa.h  nuttx/include/nuttx/

# Implementação da syscall (subsistema sched)
cp tp2_info_tarefa.c  nuttx/sched/sched/

# Aplicação de teste
mkdir -p apps/examples/tp2info
cp tp2info_main.c     apps/examples/tp2info/
cp CMakeLists.txt     apps/examples/tp2info/
# Se estiver usando build via Make, copie também Makefile e Kconfig
# cp Makefile Kconfig  apps/examples/tp2info/
```

###  Comandos para execução

```c
cd nuttx
./tools/configure.sh esp32-devkitc:nsh
```


```c
 make menuconfig
```

## Compilar

```c
make -j$(nproc)
```

## Gravar no ESP32

Conectar a placa ESP32 ao computador e identificar a porta serial utilizada.

```c
ls /dev/ttyACM*
```

```c
make download ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BAUD=115200
```

## Abrir o console serial

```c
picocom -b 115200 /dev/ttyACM0
```

```c

```

## Executar no shell NSH

```c
nsh> tp2info
```

## Resultado esperado

```c
============================================================
  TP2 - Chamada de sistema tp2_info_tarefa() no NuttX/ESP32
============================================================

--- TESTE 1: consultando a propria tarefa (pid = 0) ---
  versao da estrutura : 1
  pid da thread (tid) : 3
  pid do processo     : 3
  pid do processo pai : 1
  nome da tarefa      : tp2info
  tipo                : TAREFA
  estado              : 1 (RUNNING)
  prioridade atual    : 100
  prioridade base     : 100
  pilha total         : 2048 bytes
  pilha usada (pico)  : 1234 bytes (60,25%)
  uso de CPU          : 12,50%

Criando thread auxiliar...
[auxiliar] thread 4 terminando o trabalho (soma=...)

--- TESTE 2: consultando a thread auxiliar ---
  versao da estrutura : 1
  pid da thread (tid) : 4
  pid do processo     : 3
  pid do processo pai : 1
  nome da tarefa      : tp2info
  tipo                : PTHREAD
  estado              : 3 (WAITING)
  prioridade atual    : 100
  prioridade base     : 100
  pilha total         : 2048 bytes
  pilha usada (pico)  : 512 bytes (25,00%)
  uso de CPU          : 5,00%

--- TESTES DE ERRO / ENTRADA INVALIDA ---
  [OK] pid inexistente (pid = 4242)          -> erro -2 (No such file or directory)
  [OK] pid negativo (pid = -1)               -> erro -22 (Invalid argument)
  [OK] ponteiro de saida nulo (NULL)         -> erro -22 (Invalid argument)
  [OK] campo 'tamanho' invalido (0)          -> erro -22 (Invalid argument)
  [OK] tarefa de outro grupo (pid 1 = init)  -> erro -13 (Permission denied)

============================================================
  Fim dos testes da chamada de sistema tp2_info_tarefa()
============================================================
```
