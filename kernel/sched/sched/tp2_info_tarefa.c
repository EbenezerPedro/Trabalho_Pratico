/****************************************************************************
 * sched/sched/tp2_info_tarefa.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* ==========================================================================
 * SEGUNDO TRABALHO PRÁTICO - SISTEMAS OPERACIONAIS
 * --------------------------------------------------------------------------
 * ARQUIVO NOVO: implementação da chamada de sistema tp2_info_tarefa().
 *
 * ONDE ESTE ARQUIVO FICA: sched/sched/ -> é o diretório do subsistema de
 * escalonamento do NuttX. É aqui que vivem as funções que manipulam o TCB
 * (sched_gettcb.c, sched_get_stackinfo.c, sched_get_stateinfo.c, ...), logo
 * a nova funcionalidade foi incorporada ao subsistema apropriado, seguindo
 * a organização e as convenções de codificação do projeto.
 *
 * O QUE A FUNÇÃO FAZ (resumo):
 *   1) valida os parâmetros recebidos do espaço de usuário;
 *   2) localiza o TCB da tarefa alvo (a própria tarefa se pid == 0);
 *   3) aplica o controle de acesso (tarefa de kernel x tarefa de aplicação);
 *   4) entra em SEÇÃO CRÍTICA (desabilita interrupções) e copia os campos do
 *      TCB para a estrutura de saída -> evita leitura inconsistente;
 *   5) fora da seção crítica, mede o pico de uso da pilha e a carga de CPU
 *      (essas medições são demoradas; mantê-las dentro da seção crítica
 *      deixaria o sistema com interrupções desabilitadas por muito tempo);
 *   6) devolve OK ou um errno negativo.
 * ========================================================================== */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <nuttx/irq.h>          /* enter_critical_section / leave_...        */
#include <nuttx/arch.h>         /* up_check_tcbstack (medição da pilha)      */
#include <nuttx/clock.h>        /* clock_cpuload / struct cpuload_s          */
#include <nuttx/sched.h>        /* struct tcb_s, nxsched_get_tcb, ...        */
#include <nuttx/tp2_info_tarefa.h>

#include "sched/sched.h"        /* cabeçalho interno do subsistema sched     */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* O NuttX só compila a medição de pilha por "coloração" quando
 * CONFIG_STACK_COLORATION=y (no ESP32 isso é feito em
 * arch/xtensa/src/common/xtensa_checkstack.c). Sem esse recurso, o campo
 * pilha_usada volta zerado e o sinalizador TP2_SINAL_PILHA_MEDIDA fica
 * desligado, para que a aplicação saiba que o dado não foi medido.
 */

#undef TP2_TEM_MEDICAO_PILHA
#ifdef CONFIG_STACK_COLORATION
#  define TP2_TEM_MEDICAO_PILHA 1
#endif

/* O mesmo raciocínio vale para a carga de CPU: a função clock_cpuload()
 * só é declarada quando CONFIG_SCHED_CPULOAD_NONE NÃO está definido.
 */

#undef TP2_TEM_MEDICAO_CPU
#ifndef CONFIG_SCHED_CPULOAD_NONE
#  define TP2_TEM_MEDICAO_CPU 1
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tp2_tipo_da_tarefa
 *
 * Description:
 *   Decodifica os dois bits de tipo de thread do campo 'flags' do TCB
 *   (TCB_FLAG_TTYPE_MASK) em um dos valores TP2_TIPO_* da interface pública.
 *
 ****************************************************************************/

static uint8_t tp2_tipo_da_tarefa(FAR const struct tcb_s *tcb)
{
  switch (tcb->flags & TCB_FLAG_TTYPE_MASK)
    {
      case TCB_FLAG_TTYPE_PTHREAD:
        return TP2_TIPO_PTHREAD;

      case TCB_FLAG_TTYPE_KERNEL:
        return TP2_TIPO_KERNEL;

      case TCB_FLAG_TTYPE_TASK:
      default:
        return TP2_TIPO_TAREFA;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tp2_info_tarefa
 *
 * Description:
 *   Consulta o TCB de uma tarefa e devolve o instantâneo de suas
 *   informações (ver documentação completa no cabeçalho público
 *   include/nuttx/tp2_info_tarefa.h).
 *
 * Input Parameters:
 *   pid  - 0 = a própria tarefa; >0 = pid/tid alvo.
 *   info - estrutura de saída (não nula, com o campo 'tamanho' preenchido).
 *
 * Returned Value:
 *   OK (0) ou errno negativo (-EINVAL, -ENOENT, -EACCES).
 *
 ****************************************************************************/

int tp2_info_tarefa(pid_t pid, FAR struct tp2_info_tarefa_s *info)
{
  FAR struct tcb_s *rtcb;       /* TCB da tarefa que fez a chamada        */
  FAR struct tcb_s *qtcb;       /* TCB da tarefa consultada               */
  irqstate_t flags;             /* estado das interrupções (seção crítica)*/
  uint16_t tamanho_pedido;      /* tamanho informado pelo chamador        */
  size_t pilha_total;           /* cópia do tamanho da pilha              */

  /* ------------------------------------------------------------------------
   * 1) VALIDAÇÃO DOS PARÂMETROS RECEBIDOS DO ESPAÇO DE USUÁRIO
   *    Nunca se deve confiar em ponteiros/valores vindos da aplicação.
   * ---------------------------------------------------------------------- */

  if (info == NULL)
    {
      /* Ponteiro nulo: não há onde escrever o resultado */

      return -EINVAL;
    }

  /* O chamador é obrigado a declarar o tamanho do seu buffer. Isso permite
   * validar a compatibilidade entre a aplicação e o kernel (ABI) e evita
   * que o kernel escreva além do buffer do usuário caso a estrutura mude
   * de tamanho no futuro.
   */

  tamanho_pedido = info->tamanho;
  if (tamanho_pedido < sizeof(struct tp2_info_tarefa_s))
    {
      return -EINVAL;
    }

  if (pid < 0)
    {
      return -EINVAL;
    }

  /* Zera a estrutura para que nenhum campo fique com "lixo" da pilha do
   * chamador (informação não inicializada não deve vazar para o usuário).
   * O campo 'tamanho' é preservado.
   */

  memset(info, 0, sizeof(struct tp2_info_tarefa_s));
  info->tamanho = tamanho_pedido;
  info->versao  = TP2_VERSAO_ATUAL;

  /* ------------------------------------------------------------------------
   * 2) ENTRADA NA SEÇÃO CRÍTICA
   *    Vamos manipular as estruturas internas do sistema operacional (o TCB
   *    e o grupo de tarefas). Sem proteção, uma interrupção poderia alterar
   *    o estado da tarefa no meio da leitura, gerando uma cópia
   *    inconsistente (por exemplo: prioridade nova com estado antigo).
   *    A chamada é aninhável: enter_critical_section() pode ser chamada
   *    dentro de outra seção crítica, como fazem as funções internas do
   *    NuttX.
   * ---------------------------------------------------------------------- */

  flags = enter_critical_section();

  rtcb = this_task();                 /* tarefa atualmente em execução     */
  if (rtcb == NULL)
    {
      leave_critical_section(flags);
      return -ENOENT;
    }

  if (pid == 0)
    {
      /* pid 0 significa "eu mesmo" (mesma convenção usada em
       * nxsched_get_stackinfo()).
       */

      qtcb = rtcb;
    }
  else
    {
      /* nxsched_get_tcb() percorre a lista de tarefas do sistema e devolve o
       * TCB correspondente ao pid informado (NULL se não existir).
       */

      qtcb = nxsched_get_tcb(pid);
      if (qtcb == NULL)
        {
          leave_critical_section(flags);
          return -ENOENT;
        }

      /* --------------------------------------------------------------------
       * 3) CONTROLE DE ACESSO
       *    Uma tarefa de aplicação só pode inspecionar tarefas do próprio
       *    grupo de tarefas. Somente threads de kernel podem olhar qualquer
       *    TCB (mesma regra adotada por nxsched_get_stackinfo()).
       * ------------------------------------------------------------------ */

      if ((rtcb->flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_KERNEL &&
          rtcb->group != qtcb->group)
        {
          leave_critical_section(flags);
          return -EACCES;
        }
    }

  /* ------------------------------------------------------------------------
   * 4) CÓPIA DOS CAMPOS INTERNOS DO NUTTX (ainda dentro da seção crítica)
   * ---------------------------------------------------------------------- */

  /* ID da thread (campo 'pid' do TCB) e ID do processo/grupo de tarefas
   * (campo tg_pid da struct task_group_s). Esta é exatamente a distinção
   * "ID da tarefa x ID do processo" discutida em sala.
   */

  info->pid       = qtcb->pid;
  info->pid_grupo = qtcb->group->tg_pid;

#if defined(CONFIG_SCHED_HAVE_PARENT)
  info->pid_pai   = qtcb->group->tg_ppid;
#else
  info->pid_pai   = (pid_t)-1;
#endif

  info->tipo      = tp2_tipo_da_tarefa(qtcb);

  /* Prioridade: 'sched_priority' é a prioridade efetiva (pode ter sido
   * elevada por herança de prioridade). 'base_priority' guarda a prioridade
   * original; sem CONFIG_PRIORITY_INHERITANCE o equivalente é
   * 'init_priority'.
   */

  info->prioridade      = qtcb->sched_priority;

#ifdef CONFIG_PRIORITY_INHERITANCE
  info->prioridade_base = qtcb->base_priority != 0 ?
                          qtcb->base_priority : qtcb->sched_priority;
  if (qtcb->base_priority != 0 && qtcb->base_priority != qtcb->sched_priority)
    {
      info->sinalizadores |= TP2_SINAL_PRIO_HERDADA;
    }
#else
  info->prioridade_base = qtcb->init_priority;
#endif

  /* Estado do escalonamento (enum tstate_e: RUNNING, READYTORUN,
   * WAIT_SEM, WAIT_SIG, INACTIVE, ...).
   */

  info->estado = (uint8_t)qtcb->task_state;

  /* Versão textual do estado: reutiliza a função interna do NuttX em vez de
   * duplicar a tabela de nomes de estado no nosso código.
   */

  nxsched_get_stateinfo(qtcb, info->estado_texto,
                        sizeof(info->estado_texto));

  /* Nome da tarefa (opcional na configuração: CONFIG_TASK_NAME_SIZE). */

#if CONFIG_TASK_NAME_SIZE > 0
  strlcpy(info->nome, qtcb->name, sizeof(info->nome));
#else
  strlcpy(info->nome, "?", sizeof(info->nome));
#endif

  /* Dados da pilha: campos mantidos pelo gerenciamento de memória do NuttX
   * dentro do TCB (adj_stack_size, stack_alloc_ptr, stack_base_ptr).
   */

  pilha_total        = qtcb->adj_stack_size;
  info->pilha_total  = pilha_total;

  /* ------------------------------------------------------------------------
   * 5) SAÍDA DA SEÇÃO CRÍTICA
   *    A partir daqui o TCB pode mudar. As duas medições seguintes são
   *    demoradas (varredura de toda a pilha) e por isso são feitas com as
   *    interrupções já reabilitadas.
   * ---------------------------------------------------------------------- */

  leave_critical_section(flags);

  /* ------------------------------------------------------------------------
   * 5.1) PICO DE USO DA PILHA (gerenciamento de memória)
   *      up_check_tcbstack() varre a pilha procurando a marca de coloração
   *      mais profunda que ainda está intacta e devolve quantos bytes já
   *      foram efetivamente usados. É a mesma função usada pelo procfs
   *      (campo "StackUsed") e pelo despejo de falhas (assert/coredump).
   * ---------------------------------------------------------------------- */

#if defined(TP2_TEM_MEDICAO_PILHA)
  if (pilha_total > 0)
    {
      info->pilha_usada = up_check_tcbstack(qtcb, pilha_total);

      if (info->pilha_usada > pilha_total)
        {
          info->pilha_usada = pilha_total;      /* saturação defensiva      */
        }

      /* Percentual em "por mil" evita ponto flutuante no kernel (o NuttX
       * pode ser compilado sem suporte a float).
       */

      info->pilha_uso_permil = (uint32_t)
        (((uint64_t)info->pilha_usada * 1000u) / (uint64_t)pilha_total);

      info->sinalizadores |= TP2_SINAL_PILHA_MEDIDA;
    }
#endif

  /* ------------------------------------------------------------------------
   * 5.2) CARGA DE CPU DA TAREFA (escalonamento / monitoramento)
   *      clock_cpuload() informa quantos tiques de clock a tarefa ficou
   *      ativa em relação ao total de tiques contabilizados.
   * ---------------------------------------------------------------------- */

#if defined(TP2_TEM_MEDICAO_CPU)
  {
    struct cpuload_s cpuload;
    pid_t pid_alvo = qtcb->pid;   /* pid real (pid == 0 = "eu mesmo")     */

    if (clock_cpuload(pid_alvo, &cpuload) == OK && cpuload.total > 0)
      {
        info->cpu_permil = (uint32_t)
          (((uint64_t)cpuload.active * 1000u) / (uint64_t)cpuload.total);

        info->sinalizadores |= TP2_SINAL_CPU_MEDIDA;
      }
  }
#endif

  return OK;
}
