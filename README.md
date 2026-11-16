# TP2 — Nova chamada de sistema no Apache NuttX (ESP32-DevKitC, build FLAT)

**Instituição / disciplina:** Sistemas Operacionais
**Trabalho:** Segundo Trabalho Prático — desenvolvimento de uma nova interface (chamada de sistema) no kernel do Apache NuttX
**Placa:** ESP32-DevKitC (ESP32, Xtensa LX6)
**Ambiente de compilação:** Linux Mint dentro do VirtualBox
**Build:** FLAT (imagem única `nuttx` + `nuttx.bin`, sem separação kernel/usuário)

---

## 1. Descrição da funcionalidade implementada

Foi incorporada ao kernel do NuttX uma **nova chamada de sistema** chamada:

```c
int tp2_info_tarefa(pid_t pid, FAR struct tp2_info_tarefa_s *info);
```

Ela **não existe no NuttX original**. Ela foi escrita dentro do subsistema de
escalonamento (`sched/sched/`) e faz o papel de um “getpid novo”, como pedido em
aula: em vez de apenas devolver um número, ela **abre o TCB da tarefa, lê as
estruturas internas do sistema operacional e devolve ao espaço de usuário um
instantâneo completo da tarefa**.

Para uma tarefa alvo (a própria tarefa chamadora, se `pid == 0`, ou qualquer
`pid`/`tid` informado), a chamada devolve:

| Campo | Origem no kernel | Subsistema |
|---|---|---|
| `pid` | `tcb->pid` | tarefas / escalonamento |
| `pid_grupo` | `tcb->group->tg_pid` | task group |
| `pid_pai` | `tcb->group->tg_ppid` | task group / hierarquia |
| `tipo` | `tcb->flags & TCB_FLAG_TTYPE_MASK` | tarefas |
| `prioridade` | `tcb->sched_priority` | escalonamento |
| `prioridade_base` | `tcb->base_priority` (herança) ou `tcb->init_priority` | escalonamento |
| `estado` + `estado_texto` | `tcb->task_state` + `nxsched_get_stateinfo()` | escalonamento |
| `nome` | `tcb->name` | tarefas |
| `pilha_total` | `tcb->adj_stack_size` | gerenciamento de memória |
| `pilha_usada` | `up_check_tcbstack()` | memória / stack coloration |
| `pilha_uso_permil` | cálculo `usada*1000/total` | memória |
| `cpu_permil` | `clock_cpuload()` | monitoramento de recursos |
| `sinalizadores` | bitmap `TP2_SINAL_*` | metadados |

Ou seja, a funcionalidade cobre, de uma só vez, quatro áreas do enunciado:
**gerenciamento de tarefas/processos, escalonamento, gerenciamento de memória
(pilha) e monitoramento de recursos**.

### Contrato de ABI

O chamador **preenche** `info->tamanho = sizeof(struct tp2_info_tarefa_s)` antes
de chamar. O kernel valida esse campo e só então escreve na estrutura. Isso
garante que o kernel nunca escreva além do buffer do usuário e permite evoluir o
layout da estrutura no futuro.

### Códigos de retorno

| Retorno | Situação |
|---|---|
| `OK` (0) | consulta realizada com sucesso |
| `-EINVAL` | `info == NULL`, `info->tamanho` inválido (`< sizeof`) ou `pid < 0` |
| `-ENOENT` | o `pid` não corresponde a nenhuma tarefa ativa |
| `-EACCES` | tarefa de aplicação tentando ler TCB de outro grupo de tarefas |

---

## 2. Identificação dos arquivos do NuttX modificados ou adicionados

Todos os trechos alterados no código original do NuttX estão marcados com o
comentário:

```
/* --- MODIFICACAO TP2 (inicio) --- */
...
/* --- MODIFICACAO TP2 (fim) --- */
```

### 2.1 Arquivos NOVOS (criados por este trabalho)

| Caminho | Papel |
|---|---|
| `nuttx/include/nuttx/tp2_info_tarefa.h` | interface pública: protótipo, `struct tp2_info_tarefa_s`, constantes `TP2_*` |
| `nuttx/sched/sched/tp2_info_tarefa.c` | implementação no kernel (subsistema de escalonamento) |
| `apps/examples/tp2info/Kconfig` | registro da aplicação no menuconfig |
| `apps/examples/tp2info/Make.defs` | inclusão do diretório na compilação (Make) |
| `apps/examples/tp2info/Makefile` | regras de compilação da aplicação |
| `apps/examples/tp2info/CMakeLists.txt` | equivalente para o build CMake |
| `apps/examples/tp2info/tp2info_main.c` | aplicação de teste (5 cenários) |

### 2.2 Arquivos do NuttX ALTERADOS

| Caminho | Alteração |
|---|---|
| `nuttx/sched/sched/Make.defs` | `CSRCS += tp2_info_tarefa.c` (integração ao sistema de compilação) |
| `nuttx/sched/sched/CMakeLists.txt` | `tp2_info_tarefa.c` na lista `SRCS` (integração ao build CMake) |
| `nuttx/syscall/syscall.csv` | linha de registro da interface na infraestrutura de chamadas de sistema |

#### Sobre o `syscall.csv` (ponto importante, explicado em aula)

No NuttΧ, o arquivo `syscall/syscall.csv` é o **catálogo de chamadas de sistema**;
a partir dele a ferramenta `tools/mksyscall` gera `include/sys/syscall.h`
(números `SYS_*`), `syscall/syscall_lookup.h`, os *stubs* e os *wraps*.

Porém, **todo esse diretório só é compilado quando `CONFIG_LIB_SYSCALL=y`**
(ver `syscall/Makefile`, linha 35), o que só acontece nos builds **KERNEL** e
**PROTECTED**. No build **FLAT** — que é o exigido neste trabalho — não existe
travessia de fronteira kernel/usuário: a aplicação chama a função do kernel
diretamente pelo símbolo, porque tudo está ligado no **mesmo binário**.

Conclusão: no FLAT o registro no CSV é **opcional**. Ele foi incluído mesmo
assim porque:
1. documenta formalmente a interface (nome, header, tipo de retorno, parâmetros) — requisito “Registro da interface na infraestrutura de chamadas de sistema”;
2. deixa o trabalho pronto para um eventual build KERNEL/PROTECTED sem mudanças.

O `make` do build FLAT foi executado com essa linha presente e **compilou sem
erros**, comprovando que a inclusão é inofensiva em FLAT.

---

## 3. Estruturas e funções internas do NuttX utilizadas

### 3.1 `struct tcb_s` — `include/nuttx/sched.h`

O **TCB (Task Control Block)** é a estrutura que representa uma tarefa/thread no
NuttX. Campos usados:

```c
struct tcb_s
{
  FAR struct tcb_s *flink, *blink;      /* listas de tarefas             */
  FAR struct task_group_s *group;       /* grupo de tarefas (processo)   */
  pid_t    pid;                         /* ID da thread                  */
  uint8_t  sched_priority;              /* prioridade efetiva            */
  uint8_t  init_priority;               /* prioridade inicial            */
  tstate_t task_state;                  /* estado no escalonador         */
  uint32_t flags;                       /* TCB_FLAG_TTYPE_* (tipo)       */
#ifdef CONFIG_PRIORITY_INHERITANCE
  uint8_t  boost_priority, base_priority;
#endif
  size_t    adj_stack_size;             /* tamanho da pilha              */
  FAR void *stack_alloc_ptr;            /* base alocada da pilha         */
  FAR void *stack_base_ptr;             /* topo ajustado da pilha        */
#if CONFIG_TASK_NAME_SIZE > 0
  char name[CONFIG_TASK_NAME_SIZE + 1];
#endif
};
```

### 3.2 `struct task_group_s` — `include/nuttx/sched.h`

Agrupa as threads de um mesmo processo: `tg_pid`, `tg_ppid`, `tg_members`,
entre outros. É o que permite distinguir **pid da thread** de **pid do processo**
— exatamente a distinção pedida em sala.

### 3.3 Funções internas reutilizadas

| Função | Arquivo | Por que foi usada |
|---|---|---|
| `this_task()` | `include/nuttx/sched.h` (inline) | obtém o TCB da tarefa corrente, base para `pid == 0` |
| `nxsched_get_tcb(pid)` | `sched/sched/sched_gettcb.c` | varre a lista de tarefas e devolve o TCB do `pid` (ou `NULL`) |
| `nxsched_get_stateinfo()` | `sched/sched/sched_get_stateinfo.c` | traduz o `enum tstate_e` para texto, sem duplicar tabela de nomes |
| `up_check_tcbstack()` | `arch/xtensa/src/common/xtensa_checkstack.c` | mede o pico de uso da pilha pela marca de coloração |
| `clock_cpuload()` | `sched/clock/` | devolve tiques totais/ativos da tarefa |
| `enter_critical_section()` / `leave_critical_section()` | `include/nuttx/irq.h` | proteção das estruturas internas (desabilita interrupções de forma aninhável) |

### 3.4 Sincronização e concorrência

A leitura do TCB acontece dentro de uma **seção crítica**:

```c
flags = enter_critical_section();
  ...
  qtcb = nxsched_get_tcb(pid);
  info->pid       = qtcb->pid;
  info->pid_grupo = qtcb->group->tg_pid;
  ...
leave_critical_section(flags);
```

Sem isso, uma interrupção de timer poderia trocar a tarefa corrente no meio da
leitura e produzir uma cópia inconsistente (ex.: prioridade nova com estado
antigo). As duas medições **demoradas** — varredura da pilha e carga de CPU —
ficam **fora** da seção crítica, para não manter as interrupções desabilitadas
por muito tempo. Esse é exatamente o cuidado que o professor cobrou na aula
(“desabilita as interrupções, faz as alterações, desabilita as interrupções”).

---

## 4. Instruções de compilação e execução

> Guia detalhado, a partir da VM vazia, em **`GUIA_PASSO_A_PASSO.md`**.

### 4.1 Resumo dos comandos

```bash
# 1) dependências (Linux Mint / Ubuntu)
sudo apt update
sudo apt install -y git make gcc g++ flex bison libncurses5-dev \
                    libncursesw5-dev python3 python3-pip minicom
python3 -m pip install --user esptool   # fornece esptool.py

# 2) código-fonte (branch master, como pedido em aula)
mkdir -p ~/nuttx-workspace && cd ~/nuttx-workspace
git clone https://github.com/apache/nuttx.git nuttx
git clone https://github.com/apache/nuttx-apps.git apps

# 3) aplicar ESTE trabalho (o pacote traz o script)
cd ~/nuttx-workspace
./aplicar_tp2.sh          # copia os arquivos e aplica o patch

# 4) toolchain Xtensa (ESP32)
mkdir -p ~/tools && cd ~/tools
wget https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz
tar xf xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz
echo 'export PATH=$HOME/tools/xtensa-esp-elf/bin:$PATH' >> ~/.bashrc
source ~/.bashrc

# 5) configurar e compilar
cd ~/nuttx-workspace/nuttx
./tools/configure.sh esp32-devkitc:nsh
make menuconfig      # habilitar os itens da seção 4.2
make -j$(nproc)
```

### 4.2 Opções de configuração necessárias

`make menuconfig` → caminhos:

| Opção | Onde | Para quê |
|---|---|---|
| `CONFIG_EXAMPLES_TP2INFO=y` | `Application Configuration → Examples → TP2: teste da chamada de sistema` | compila a aplicação de teste |
| `CONFIG_STACK_COLORATION=y` | `RTOS Features → Memory Management → Stack coloration` (ou `Library Routines`/`Debug`) | habilita a medição do pico de pilha |
| `CONFIG_SCHED_CPULOAD_ONESHOT=y` | `RTOS Features → Clocks and Timers → CPU load measurements` | habilita `clock_cpuload()` |
| `CONFIG_TASK_NAME_SIZE=32` | `RTOS Features → Tasks and Scheduling → Max task name size` | nomes de tarefa legíveis |
| `CONFIG_SCHED_HAVE_PARENT=y` | `RTOS Features → Tasks and Scheduling → Support parent/child` | habilita `tg_ppid` |

Alternativa não interativa (o pacote já traz o `defconfig` pronto):

```bash
cp configs_tp2/defconfig_tp2 .config
make olddefconfig
```

### 4.3 Gravação na placa

```bash
# a imagem já é gerada no build: nuttx.bin
make flash ESPTOOL_PORT=/dev/ttyUSB0
```

Equivalente manual (o `make flash` do NuttX faz exatamente isto):

```bash
esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000  bootloader.bin \
  0x8000  partition_table.bin \
  0x10000 nuttx.bin
```

### 4.4 Console serial

```bash
sudo usermod -aG dialout $USER     # uma vez; depois relogar
minicom -D /dev/ttyUSB0 -b 115200
# ou: picocom -b 115200 /dev/ttyUSB0
```

Ao ligar/resetar a placa aparece o shell do NuttX (`nsh>`). Digite:

```
nsh> tp2info
```

---

## 5. Descrição dos testes realizados e resultados obtidos

A aplicação `tp2info` executa 6 cenários: 2 de **funcionamento normal** e 4 de
**erro/entrada inválida**.

### 5.1 Cenários

| # | Cenário | Entrada | Resultado esperado |
|---|---|---|---|
| 1 | consulta da própria tarefa | `pid = 0` | `OK`, dados da tarefa `tp2info` |
| 2 | consulta de outra thread do mesmo grupo | `pid = gettid()` da thread auxiliar criada com `pthread_create()` | `OK`, estado/prioridade/pilha da thread auxiliar |
| 3 | pid inexistente | `pid = 4242` | `-ENOENT` |
| 4 | pid negativo | `pid = -1` | `-EINVAL` |
| 5 | ponteiro de saída nulo | `info = NULL` | `-EINVAL` |
| 6 | `tamanho` do buffer inválido | `info->tamanho = 0` | `-EINVAL` |
| 7 | TCB de outro grupo de tarefas | `pid = 1` (init) | `-EACCES` |

### 5.2 Saída esperada na console

```
============================================================
  TP2 - Chamada de sistema tp2_info_tarefa() no NuttX/ESP32
============================================================

--- TESTE 1: consultando a propria tarefa (pid = 0) ---
  versao da estrutura : 1
  pid da thread (tid) : 3
  pid do processo     : 3
  pid do processo pai : 1
  nome da tarefa      : tp2info
  tipo                : TAREFA
  estado              : 4 (Running)
  prioridade atual    : 100
  prioridade base     : 100
  pilha total         : 2048 bytes
  pilha usada (pico)  : 1184 bytes (57,81%)
  uso de CPU          : 0,00%

Criando thread auxiliar...
--- TESTE 2: consultando a thread auxiliar ---
  pid da thread (tid) : 4
  pid do processo     : 3
  nome da tarefa      : tp2info
  tipo                : PTHREAD
  estado              : 5 (Inactive)
  pilha total         : 2048 bytes
  pilha usada (pico)  : 832 bytes (40,62%)

--- TESTES DE ERRO / ENTRADA INVALIDA ---
  [OK] pid inexistente (pid = 4242)      -> erro -2 (No such file or directory)
  [OK] pid negativo (pid = -1)           -> erro -22 (Invalid argument)
  [OK] ponteiro de saida nulo (NULL)     -> erro -22 (Invalid argument)
  [OK] campo 'tamanho' invalido (0)      -> erro -22 (Invalid argument)
  [OK] tarefa de outro grupo (pid 1)     -> erro -13 (Permission denied)

============================================================
  Fim dos testes da chamada de sistema tp2_info_tarefa()
============================================================
```

> Os números de `pid`, de bytes de pilha e de % de CPU variam a cada execução —
> são valores reais medidos no momento da chamada, não constantes.

### 5.3 Resultado da compilação (verificado)

O pacote foi validado compilando o NuttX de verdade com a toolchain oficial
`xtensa-esp-elf` (Espressif, gcc 14.2) para `esp32-devkitc:nsh`, build FLAT:

* `sched/tp2_info_tarefa.c` compilado e ligado no binário (`sched/tp2_info_tarefa.o`);
* `apps/examples/tp2info/tp2info_main.c` compilado e registrado como aplicação embutida;
* imagem final gerada: `nuttx` (ELF) + `nuttx.bin` (imagem para flash).

> **Limitação honesta:** este pacote foi compilado/ligado com sucesso, mas **não
> foi gravado em uma placa física** no ambiente em que ele foi preparado (sem
> hardware ESP32 acessível). A gravação e a sessão de console descritas em 4.3 e
> 4.4 devem ser executadas na sua VM/placa; a saída de 5.2 é o formato esperado.

---

## 6. Organização do repositório entregue

```
tp2-nuttx-syscall/
├── README.md                          <- este arquivo
├── GUIA_PASSO_A_PASSO.md              <- do zero até a placa rodando (VirtualBox/Linux Mint)
├── aplicar_tp2.sh                     <- copia arquivos e aplica o patch no clone do NuttX
├── patch/
│   └── nuttx_tp2.patch                <- diff das modificações em arquivos do NuttX
├── kernel/
│   ├── include/nuttx/tp2_info_tarefa.h
│   └── sched/sched/tp2_info_tarefa.c
├── apps/
│   └── examples/tp2info/
│       ├── Kconfig
│       ├── Make.defs
│       ├── Makefile
│       ├── CMakeLists.txt
│       └── tp2info_main.c
├── configs_tp2/
│   └── defconfig_tp2                <- configuração pronta (opções do TP2 já ligadas)
└── docs/
    └── diagrama_chamada.txt         <- fluxo da chamada, passo a passo
```

O professor pediu **apenas o que foi efetivamente feito**, não o NuttX inteiro —
por isso o repositório contém somente os arquivos novos, o patch das alterações e
a aplicação de teste.

---

## 7. Decisões de implementação (para sustentar a arguição)

1. **Subsistema escolhido:** `sched/sched/` — é onde vivem as funções que
   manipulam TCB, exatamente o “diretório do subsistema” citado em aula.
2. **Arquivo `.h` no `include/nuttx/`** e não em `include/sys/`: o NuttX reserva
   `include/nuttx/` para interfaces internas estendidas e `include/sys/` para a
   API POSIX. Como a função é uma extensão do SO (não é POSIX), `include/nuttx/`
   é o lugar convencional — o mesmo usado por `nuttx/sched.h`.
3. **`struct` de saída com campo `tamanho`**: evita *structs* incompatíveis entre
   app e kernel e valida o parâmetro (requisito “validação dos parâmetros e
   tratamento de erros”).
4. **Reutilizar `nxsched_get_tcb()`, `nxsched_get_stateinfo()` e
   `up_check_tcbstack()`** em vez de reimplementar: menor risco, menos código
   duplicado, e mostra que as funções internas foram compreendidas.
5. **Seção crítica só no trecho de cópia**: medições demoradas ficam fora para
   não aumentar a latência de interrupção.
6. **Sem `syscall.csv` obrigatório**: no FLAT não há *stub*; o `syscall.csv` é
   mantido apenas como documentação/registro e para builds KERNEL futuros.
7. **Percentuais em “por mil” (`*_permil`)**: o NuttX pode ser compilado sem
   suporte a ponto flutuante; inteiros evitam *soft-float* no kernel.
