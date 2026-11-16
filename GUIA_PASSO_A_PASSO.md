# GUIA PASSO A PASSO — do zero até a placa rodando
### Segundo Trabalho Prático (chamada de sistema no Apache NuttX / ESP32-DevKitC, build FLAT)
**Ambiente:** Linux Mint dentro do VirtualBox · **Placa:** ESP32-DevKitC

> Todas as sequências abaixo foram validadas de fato na geração do binário
> (`nuttx` + `nuttx.bin`), usando a toolchain oficial `xtensa-esp-elf` 14.2 da
> Espressif e o branch `master` do NuttX. A gravação na placa depende do seu
> hardware/cabo e deve ser executada por você.

---

## PARTE 0 — Preparar a VM VirtualBox

1. VirtualBox → **Nova** → Tipo *Linux*, Versão *Ubuntu (64-bit)*.
2. Memória: **4096 MB** · CPUs: **2 ou mais** · Disco: **25 GB** (VDI, dinâmico).
3. Instale o **Linux Mint** (ISO). Durante a instalação, marque para instalar
   codecs/multimídia se quiser.
4. Depois de instalado, dentro do Mint:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential git curl wget
```

5. **Habilitar a USB no VirtualBox** (necessário para a placa):
   - Desligue a VM → *Configurações* → **USB** → marque *Controlador USB 2.0 (EHCI)* (ou 3.0).
   - Instale o **Guest Additions**: menu *Dispositivos* → *Inserir imagem de CD do Guest Additions*,
     depois `sudo apt install -y virtualbox-guest-dkms virtualbox-guest-utils && sudo reboot`.
   - Com a placa plugada, menu *Dispositivos → USB → CP210x / CH340* para
     “passar” a porta serial para dentro da VM.

---

## PARTE 1 — Dependências do NuttX

```bash
sudo apt install -y git make gcc g++ flex bison libncurses5-dev libncursesw5-dev \
                    python3 python3-pip python3-setuptools \
                    minicom picocom
```

### 1.1 kconfig-frontends (necessário para `configure.sh` e `menuconfig`)

O NuttX usa as ferramentas `kconfig-tweak` / `kconfig-mconf`, do pacote
**kconfig-frontends**:

```bash
sudo apt install -y kconfig-frontends
kconfig-mconf --version        # deve responder algo como 4.11.0
```

* **Se o pacote não existir** (acontece no Mint 22 / base Ubuntu 24.04, onde ele
  foi removido dos repositórios): use o **Linux Mint 21.3** (base Ubuntu 22.04),
  onde `kconfig-frontends` está disponível — é o caminho mais rápido — ou
  compile o `kconfig-frontends` a partir do fonte.
* **Plano B sem menuconfig:** este pacote já traz uma configuração pronta em
  `configs_tp2/defconfig_tp2` com todas as opções do TP2 ligadas. Nesse caso
  você só precisa das ferramentas `kconfig-*` para o passo 3 — sem elas o
  `make` também não roda (`make olddefconfig` usa `kconfig-conf`).

### 1.2 esptool (para gerar o `nuttx.bin` e gravar na placa)

O build do ESP32 chama `esptool.py`. Use a versão 4.x:

```bash
python3 -m pip install --user "esptool<5"
echo 'export PATH=$HOME/.local/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
esptool.py version        # deve mostrar 4.x
```

> Se aparecer `esptool.py not found` no build, é só instalar como acima e rodar
> `make` novamente.

---

## PARTE 2 — Baixar os fontes (branch master)

O professor pediu explicitamente a versão do **branch master**:

```bash
mkdir -p ~/nuttx-workspace && cd ~/nuttx-workspace
git clone https://github.com/apache/nuttx.git nuttx
git clone https://github.com/apache/nuttx-apps.git apps
git -C nuttx log -1 --date=short --format='%h %ad'   # confirma a versão
```

---

## PARTE 3 — Toolchain Xtensa (ESP32)

```bash
mkdir -p ~/tools && cd ~/tools
wget https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20241119/xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz
tar xf xtensa-esp-elf-14.2.0_20241119-x86_64-linux-gnu.tar.xz
echo 'export PATH=$HOME/tools/xtensa-esp-elf/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
xtensa-esp32-elf-gcc --version       # deve mostrar 14.2.0
```

---

## PARTE 4 — Aplicar o trabalho ao NuttX

```bash
cd ~/nuttx-workspace
unzip tp2-nuttx-syscall.zip -d tp2
cd tp2
./aplicar_tp2.sh ~/nuttx-workspace/nuttx ~/nuttx-workspace/apps
```

O script:
1. copia `kernel/include/nuttx/tp2_info_tarefa.h` → `nuttx/include/nuttx/`;
2. copia `kernel/sched/sched/tp2_info_tarefa.c` → `nuttx/sched/sched/`;
3. copia `apps/examples/tp2info/` → `apps/examples/tp2info/`;
4. aplica `patch/nuttx_tp2.patch` (edições em `sched/sched/Make.defs`,
   `sched/sched/CMakeLists.txt` e `syscall/syscall.csv`).

Confira depois:

```bash
cd ~/nuttx-workspace/nuttx
git status --short
grep -n "tp2_info_tarefa" sched/sched/Make.defs syscall/syscall.csv
```

---

## PARTE 5 — Configurar o build (FLAT, esp32-devkitc)

```bash
cd ~/nuttx-workspace/nuttx
./tools/configure.sh esp32-devkitc:nsh
```

Agora ligue as opções que a chamada de sistema usa:

```bash
make menuconfig
```

| Menu | Opção | Valor |
|---|---|---|
| `Application Configuration → Examples` | `TP2: teste da chamada de sistema tp2_info_tarefa()` | `y` |
| `RTOS Features → Tasks and Scheduling` | `Max task name size` (`CONFIG_TASK_NAME_SIZE`) | `32` |
| `RTOS Features → Tasks and Scheduling` | `Support parent/child task relationships` (`CONFIG_SCHED_HAVE_PARENT`) | `y` |
| `RTOS Features → Memory Management` | `Stack coloration` (`CONFIG_STACK_COLORATION`) | `y` |
| `RTOS Features → Clocks and Timer Support` | `CPU load measurements` (`CONFIG_SCHED_CPULOAD_ONESHOT`) | `y` |

> **Atalho sem menuconfig:** copie a configuração já pronta do pacote:
> ```bash
> cp ~/nuttx-workspace/tp2/configs_tp2/defconfig_tp2 .config
> make olddefconfig
> ```

✅ O build é **FLAT** por padrão nesse board (`CONFIG_BUILD_FLAT=y`). Confirme:

```bash
grep -E "CONFIG_BUILD_FLAT|CONFIG_STACK_COLORATION|CONFIG_EXAMPLES_TP2INFO" .config
```

---

## PARTE 6 — Compilar

```bash
cd ~/nuttx-workspace/nuttx
make -j$(nproc) 2>&1 | tee build.log
```

No fim devem existir:

```bash
ls -lh nuttx nuttx.bin nuttx.hex
# nuttx      -> ELF com símbolos
# nuttx.bin  -> imagem pronta para gravar
```

E o módulo novo tem de estar no binário:

```bash
ls -l sched/tp2_info_tarefa.o
xtensa-esp32-elf-nm nuttx | grep tp2_info_tarefa
```

---

## PARTE 7 — Gravar na ESP32-DevKitC

```bash
sudo usermod -aG dialout $USER      # 1x apenas; depois saia e entre de novo
ls /dev/ttyUSB*                     # com a placa plugada
```

### 7.1 Pelo próprio NuttX (mais simples)

```bash
cd ~/nuttx-workspace/nuttx
make flash ESPTOOL_PORT=/dev/ttyUSB0 ESPTOOL_BAUD=460800
```

### 7.2 Manual, com esptool.py

O NuttX gera `nuttx.bin` com os *headers* de boot. Para gravar o bootloader e a
tabela de partições, use os binários do pacote ESP-IDF (ou os gerados em
`arch/xtensa/src/esp32/` após o build):

```bash
esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 40m --flash_size detect \
  0x1000  bootloader.bin \
  0x8000  partition_table.bin \
  0x10000 nuttx.bin
```

> Dica: se a placa não entrar em modo de gravação, segure o botão **BOOT** e
> aperte **EN**, depois solte o BOOT.

---

## PARTE 8 — Console serial e execução

```bash
minicom -D /dev/ttyUSB0 -b 115200
# ou
picocom -b 115200 /dev/ttyUSB0
```

Aperte **EN** (reset) na placa. Deve aparecer o shell do NuttX:

```
NuttX 12.x ... esp32-devkitc
nsh>
```

Rode a aplicação de teste:

```
nsh> tp2info
```

Comandos que ajudam na demonstração:

```
nsh> help
nsh> ps            # lista de tarefas (para comparar com o que a syscall mostra)
nsh> free
nsh> tp2info
```

Saída esperada: os 7 cenários descritos na seção 5 do `README.md`
(2 de sucesso + 5 de erro).

---

## PARTE 9 — Publicar o repositório e entregar

```bash
cd ~/nuttx-workspace/tp2
git init
git add .
git commit -m "TP2: nova chamada de sistema tp2_info_tarefa no NuttX (ESP32, FLAT)"
git branch -M main
git remote add origin https://github.com/SEU_USUARIO/tp2-nuttx-syscall.git
git push -u origin main
```

Depois, no Moodle: postar **um PDF** contendo o **link do repositório** + o
`README.md` (o professor pediu só o que você fez, não o NuttX inteiro).

---

## PROBLEMAS COMUNS

| Sintoma | Causa / solução |
|---|---|
| `kconfig-tweak: command not found` | falta `kconfig-frontends` → Parte 1.1 |
| `gcc: error: unrecognized command-line option '-mlongcalls'` | você está usando o gcc do sistema, não o Xtensa → confira `which xtensa-esp32-elf-gcc` e o `PATH` |
| `esptool.py not found` | `python3 -m pip install --user "esptool<5"` e rodar `make` de novo |
| `Permission denied: /dev/ttyUSB0` | `sudo usermod -aG dialout $USER` + relogar; ou `sudo chmod 666 /dev/ttyUSB0` |
| `undefined symbol tp2_info_tarefa` | a aplicação entrou mas `sched/sched/Make.defs` não foi editado → reaplique o patch (Parte 4) |
| `pilha usada` ou `uso de CPU` aparecem como “nao medido” | faltam `CONFIG_STACK_COLORATION=y` / `CONFIG_SCHED_CPULOAD_ONESHOT=y` → Parte 5 |
| `-EACCES` em todos os pids | normal para tarefa de aplicação olhando outro grupo; é o controle de acesso funcionando |
| build termina com `nuttx` mas sem `.bin` | instale o esptool (Parte 1.2) e rode `make` novamente |
