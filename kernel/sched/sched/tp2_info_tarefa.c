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

#include <nuttx/config.h> /* Inclui as configurações gerais do NuttX. */

#include <sys/types.h> /* Define tipos utilizados pelo sistema, como pid_t. */
#include <stdint.h> /* Disponibiliza tipos inteiros com tamanho definido, como uint8_t, uint16_t e uint32_t. */
#include <string.h> /* Disponibiliza funções para manipulação de memória, como memset(). */
#include <errno.h>/* Disponibiliza os códigos de erro, como EINVAL, ENOENT e EACCES. */

#include <nuttx/irq.h>          /* enter_critical_section / leave_...        */
#include <nuttx/arch.h>         /* Disponibiliza funções específicas da arquitetura. verifica a utilização da pilha da tarefa */
#include <nuttx/clock.h>        /* Disponibiliza recursos relacionados ao relógio e à medição da utilização da CPU.  */
#include <nuttx/sched.h>        /* Disponibiliza estruturas e funções do escalonador   */
#include <nuttx/tp2_info_tarefa.h>

#include "sched/sched.h"     /* cabeçalho interno do subsistema sched Permite acessar definições internas do escalonador */

/* Verifica se o NuttX foi configurado para utilizar
 * a técnica de Stack Coloration.
 *
 * CONFIG_STACK_COLORATION permite acompanhar o uso
 * da pilha por meio da análise da memória preenchida
 * pelo sistema.
 *
 * Se estiver habilitado, ativamos nossa medição. */

#undef TP2_TEM_MEDICAO_PILHA
#ifdef CONFIG_STACK_COLORATION
#  define TP2_TEM_MEDICAO_PILHA 1
#endif

/* Verifica se a medição de carga da CPU está disponível.
 *
 * CONFIG_SCHED_CPULOAD_NONE indica que o monitoramento
 * da utilização da CPU está desabilitado.
 *
 * Se essa configuração NÃO estiver definida, habilitamos
 * a medição da CPU neste trabalho. */

#undef TP2_TEM_MEDICAO_CPU
#ifndef CONFIG_SCHED_CPULOAD_NONE
#  define TP2_TEM_MEDICAO_CPU 1
#endif

/*
 * OBJETIVO:
 * Identificar qual é o tipo da tarefa consultada.
 *
 * RECEBE:
 * Um ponteiro para a estrutura TCB da tarefa.
 *
 * TCB significa Task Control Block.
 * É a estrutura interna do NuttX que guarda informações
 * sobre uma tarefa ou thread.
 *
 * RETORNA:
 * TP2_TIPO_TAREFA, TP2_TIPO_PTHREAD ou TP2_TIPO_KERNEL.
 */

static uint8_t tp2_tipo_da_tarefa(FAR const struct tcb_s *tcb)
{
  switch (tcb->flags & TCB_FLAG_TTYPE_MASK)
    {
      case TCB_FLAG_TTYPE_PTHREAD: /* Se for uma thread criada com pthread_create(). */
        return TP2_TIPO_PTHREAD;

      case TCB_FLAG_TTYPE_KERNEL:   /* Se for uma thread pertencente ao kernel. */
        return TP2_TIPO_KERNEL;

      case TCB_FLAG_TTYPE_TASK:    /* Se for uma tarefa comum (task) */
      default:
        return TP2_TIPO_TAREFA;
    }
}

int tp2_info_tarefa(pid_t pid, FAR struct tp2_info_tarefa_s *info)
{
  FAR struct tcb_s *rtcb;       /* TCB da tarefa que fez a chamada        */
  FAR struct tcb_s *qtcb;       /* TCB da tarefa consultada               */
  irqstate_t flags;             /* estado das interrupções (seção crítica)*/
  uint16_t tamanho_pedido;      /* tamanho informado pelo chamador        */
  size_t pilha_total;           /* cópia do tamanho da pilha              */

  if (info == NULL)  /* ponteiro nulo: não há onde escrever o resultado */
    {
      return -EINVAL;   /* Parâmetro inválido */
    }

  tamanho_pedido = info->tamanho;    /* Lê o tamanho informado pelo chamador */
  if (tamanho_pedido < sizeof(struct tp2_info_tarefa_s))    /* tamanho insuficiente para armazenar o resultado */
    {
      return -EINVAL;   /* Parâmetro inválido */
    }

  if (pid < 0)  /* pid: identificador da tarefa que queremos consultar, não pode ser negativo*/
    {
      return -EINVAL;    /* Parâmetro inválido */
    }

  memset(info, 0, sizeof(struct tp2_info_tarefa_s));  /* evita que campos não preenchidos contenham lixo de memória*/
  info->tamanho = tamanho_pedido;                    /* Restaura o tamanho informado pelo chamador*/
  info->versao  = TP2_VERSAO_ATUAL;                 /* Informa qual é a versão da estrutura utilizada. */

  flags = enter_critical_section();

  rtcb = this_task();  /* tarefa atualmente em execução e retorna a estrutura de controle da tarefa atualmente em execução    */
  if (rtcb == NULL)   /* Verifica se foi possível obter o TCB atual. */
    {
      leave_critical_section(flags);   /* Sai da seção crítica e restaura as interrupções. */
      return -ENOENT;    /* Tarefa não encontrada */
    }

  if (pid == 0)
    {
      qtcb = rtcb;   /* Se o pid for 0, significa que queremos consultar a própria tarefa. */
    }
  else       /* Se não procuramos no escalonador a tarefa que corresponde a esse PID.*/
    {
      qtcb = nxsched_get_tcb(pid);   /* Retorna o TCB da tarefa encontrada */ 

      /* Se o resultado for NULL, significa que não encontramos uma tarefa com esse PID. */
      if (qtcb == NULL)
        {
          leave_critical_section(flags);
          return -ENOENT;    /* Tarefa não encontrada */
        }

      /* Verifica se a tarefa que está fazendo a consulta
       * pertence ao kernel.
       *
       * Se NÃO for uma tarefa do kernel, verificamos
       * se a tarefa consultada pertence ao mesmo grupo.
       *
       * rtcb->group representa o grupo da tarefa atual.
       * qtcb->group representa o grupo da tarefa consultada.
       *
       * Se os grupos forem diferentes, a consulta é negada. */
      if ((rtcb->flags & TCB_FLAG_TTYPE_MASK) != TCB_FLAG_TTYPE_KERNEL &&
          rtcb->group != qtcb->group)
        {
          leave_critical_section(flags);   /* Restaura as interrupções antes de sair. */
          return -EACCES;                /* Sem permissão para consultar a tarefa */
        }
    }

  info->pid       = qtcb->pid;               /* identificador individual da tarefa. */
  info->pid_grupo = qtcb->group->tg_pid;   /* identificador do grupo de tarefas ao qual a tarefa pertence. */

#if defined(CONFIG_SCHED_HAVE_PARENT)  /* Verifica se NuttX possui suporte à informação do processo pai. */
  info->pid_pai   = qtcb->group->tg_ppid;  /* Se sim, obtém o PID do processo pai. */
#else
  info->pid_pai   = (pid_t)-1;  /* Se não, utiliza -1 para indicar que a informação não está disponível. */
#endif

  /* Chama a função tp2_tipo_da_tarefa e analisa as flags do TCB se a tarefa é TASK, PTHREAD ou KERNEL*/
  info->tipo      = tp2_tipo_da_tarefa(qtcb);

  info->prioridade      = qtcb->sched_priority;  /* Obtem a prioridade atual da tarefa consultada atraves de sched_priority*/

  /* Configuração de NuttX para utilizar o mecanismo de Priority Inheritance. */
#ifdef CONFIG_PRIORITY_INHERITANCE
  info->prioridade_base = qtcb->base_priority != 0 ?  /* Se base_priority for diferente de zero, utiliza-a como prioridade base. */
                          qtcb->base_priority : qtcb->sched_priority;                        
  if (qtcb->base_priority != 0 && qtcb->base_priority != qtcb->sched_priority)  /* Se a prioridade herdada for diferente da prioridade atual*/
    {
      info->sinalizadores |= TP2_SINAL_PRIO_HERDADA;  /* Sinaliza que a prioridade herdada está ativa. */
    }
#else
  /* Se o mecanismo de Priority Inheritance não estiver habilitado, utiliza init_priority como prioridade base. */
  info->prioridade_base = qtcb->init_priority;  
#endif

  /* Obtem o estado atual da tarefa consultada atraves de task_state*/
  /* task_state valor numérico que representa estado da tarefa no escalonador.
   * Por exemplo: READY, RUNNING, WAITING ou INACTIVE */
  info->estado = (uint8_t)qtcb->task_state; 

  /* Obtém uma descrição textual do estado da tarefa consultada. */
  nxsched_get_stateinfo(qtcb, info->estado_texto,
                        sizeof(info->estado_texto)); 

#if CONFIG_TASK_NAME_SIZE > 0  /* Verifica se o NuttX foi configurado para armazenar nomes de tarefas. */
  /* strlcpy() limita a quantidade de caracteres copiados, evitando ultrapassar o tamanho do vetor de destino*/
  strlcpy(info->nome, qtcb->name, sizeof(info->nome));
#else   /* Se o armazenamento de nomes estiver desabilitado, colocamos um ponto de interrogação. */
  strlcpy(info->nome, "?", sizeof(info->nome));
#endif

  /* adj_stack_size representa o tamanho ajustado da pilha em bytes*/
  pilha_total        = qtcb->adj_stack_size;       /* Obtém o tamanho total da pilha da tarefa consultada. */
  info->pilha_total  = pilha_total;               /* Armazena o tamanho total da pilha da tarefa consultada. */

  leave_critical_section(flags);  /* Sai da seção crítica e restaura o estado das interrupções. */

#if defined(TP2_TEM_MEDICAO_PILHA)  /* Se a medição da pilha estiver habilitada */
  if (pilha_total > 0) /* Verifica se a tarefa possui uma pilha com tamanho maior que zero*/
    {
      /* up_check_tcbstack() analisa a pilha da tarefa e estima o pico de utilização.*/
      info->pilha_usada = up_check_tcbstack(qtcb, pilha_total); /*O resultado é armazenado em pilha_usada.*/

      if (info->pilha_usada > pilha_total) /* Se o pico de utilização for maior que o tamanho total da pilha*/
        {
          info->pilha_usada = pilha_total;      /* saturação defensiva      */
        }

      /* Calcula a utilização da pilha em milésimos.
       *
       * Exemplo:
       * Pilha total = 1000 bytes
       * Pilha usada = 500 bytes
       *
       * (500 * 1000) / 1000 = 500
       *
       * O resultado 500 representa 50,0%.
       *
       * Utilizei uint64_t na multiplicação para
       * reduzir o risco de estouro numérico. */
      info->pilha_uso_permil = (uint32_t)                                   /* Calcula a utilização da pilha em milésimos. */
        (((uint64_t)info->pilha_usada * 1000u) / (uint64_t)pilha_total);  /* Armazena o resultado em pilha_uso_permil. */

      /* Informa que a medição da pilha foi realizada e que o campo pilha_usada contém um valor válido. */
      info->sinalizadores |= TP2_SINAL_PILHA_MEDIDA;
    }
#endif

  /* Se o monitoramento de carga da CPU estiver habilitado. */
#if defined(TP2_TEM_MEDICAO_CPU) /* Obtemos a utilização da CPU da tarefa consultada. */
  {
    struct cpuload_s cpuload; /* Estrutura que armazena informações sobre a carga da CPU fornecidos pelo NuttX*/
    pid_t pid_alvo = qtcb->pid;   /* PID da tarefa consultada */

    /* Se a função clock_cpuload() retornar OK e o total de ciclos for maior que zero, significa que a medição foi bem-sucedida */
    /* Calcula a porcentagem de utilização da CPU.
     *
     * active = tempo em que a tarefa esteve ativa.
     * total  = tempo total considerado na medição.
     *
     * Multiplicamos por 1000 para representar
     * o resultado em milésimos.
     *
     * Exemplo:
     * active = 250
     * total  = 1000
     *
     * Resultado = 250
     * Isso representa 25,0%. */
    if (clock_cpuload(pid_alvo, &cpuload) == OK && cpuload.total > 0) 
      {
        info->cpu_permil = (uint32_t) /* Calcula a porcentagem de utilização da CPU em milésimos. */
          (((uint64_t)cpuload.active * 1000u) / (uint64_t)cpuload.total);  /* Armazena o resultado em cpu_permil. */

        /* Informa que a medição da CPU foi realizada e que campo cpu_permil contém um valor válido. */
        info->sinalizadores |= TP2_SINAL_CPU_MEDIDA; 
      }
  }
#endif

  return OK; /* consulta foi realizada com sucesso. */
}
