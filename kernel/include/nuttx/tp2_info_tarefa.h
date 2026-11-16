/****************************************************************************
 * include/nuttx/tp2_info_tarefa.h
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
 * ARQUIVO NOVO: interface pública (cabeçalho) da chamada de sistema
 *               tp2_info_tarefa().
 *
 * Este é o "arquivo .h" citado no enunciado: ele declara a interface pública
 * da nova funcionalidade do kernel, o tipo de dado devolvido ao usuário e os
 * códigos de erro/constantes que a aplicação pode utilizar.
 *
 * O subsistema escolhido foi o de GERENCIAMENTO DE TAREFAS / ESCALONAMENTO /
 * GERENCIAMENTO DE MEMÓRIA (pilha) / MONITORAMENTO DE RECURSOS: a chamada
 * lê o TCB (Task Control Block) da tarefa alvo e reporta ao espaço de
 * usuário o que o kernel sabe sobre ela.
 * ========================================================================== */

#ifndef __INCLUDE_NUTTX_TP2_INFO_TAREFA_H
#define __INCLUDE_NUTTX_TP2_INFO_TAREFA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Tamanho máximo (incluindo o terminador NUL) do nome da tarefa copiado
 * para a estrutura de saída. O nome interno do TCB tem
 * CONFIG_TASK_NAME_SIZE + 1 bytes; aqui usamos um valor fixo e pequeno para
 * manter a estrutura de tamanho constante entre kernel e aplicação.
 */

#define TP2_NOME_MAX            16

/* Tamanho do buffer de texto que recebe o estado da tarefa por extenso
 * (ex.: "Running", "Waiting,Semaphore"). O texto é produzido pela função
 * interna nxsched_get_stateinfo().
 */

#define TP2_ESTADO_TEXTO_MAX    24

/* Versão da estrutura de saída. Permite evolução futura do layout sem
 * quebrar aplicações antigas.
 */

#define TP2_VERSAO_ATUAL        1

/* Tipos de tarefa (decodificação do campo flags do TCB,
 * TCB_FLAG_TTYPE_MASK).
 */

#define TP2_TIPO_TAREFA         0   /* Tarefa "task" (thread principal)   */
#define TP2_TIPO_PTHREAD        1   /* Thread criada com pthread_create() */
#define TP2_TIPO_KERNEL         2   /* Thread de kernel (kthread)         */

/* Sinalizadores: informam QUAIS informações puderam ser realmente medidas.
 * Nem toda configuração do NuttX habilita a medição de pilha
 * (CONFIG_STACK_COLORATION) nem a de carga de CPU
 * (CONFIG_SCHED_CPULOAD_*); quando o recurso está desligado o campo
 * correspondente volta zerado e o sinalizador NÃO é ligado.
 */

#define TP2_SINAL_PILHA_MEDIDA  (1 << 0)  /* pilha_usada é válido        */
#define TP2_SINAL_CPU_MEDIDA    (1 << 1)  /* cpu_permil é válido         */
#define TP2_SINAL_PRIO_HERDADA  (1 << 2)  /* prioridade herdada ativa    */

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* Estrutura devolvida por tp2_info_tarefa().
 *
 * ATENÇÃO (contrato de ABI): o chamador DEVE preencher o campo 'tamanho'
 * com sizeof(struct tp2_info_tarefa_s) antes da chamada. É assim que o
 * kernel valida o parâmetro e garante que está escrevendo apenas dentro do
 * buffer que o usuário realmente alocou.
 */

struct tp2_info_tarefa_s
{
  uint16_t tamanho;                 /* sizeof() informado pelo chamador    */
  uint16_t versao;                  /* preenchido pelo kernel              */
  pid_t    pid;                     /* ID da thread  (tcb->pid)            */
  pid_t    pid_grupo;               /* ID do processo (group->tg_pid)      */
  pid_t    pid_pai;                 /* ID do processo pai                  */
  uint8_t  tipo;                    /* TP2_TIPO_*                          */
  uint8_t  prioridade;              /* prioridade efetiva atual            */
  uint8_t  prioridade_base;         /* prioridade original (sem herança)   */
  uint8_t  estado;                  /* valor do enum tstate_e              */
  char     nome[TP2_NOME_MAX];      /* nome da tarefa                      */
  char     estado_texto[TP2_ESTADO_TEXTO_MAX]; /* estado por extenso       */
  size_t   pilha_total;             /* tamanho da pilha (bytes)            */
  size_t   pilha_usada;             /* pico de uso da pilha (bytes)        */
  uint32_t pilha_uso_permil;        /* percentual * 1000 (0..1000)         */
  uint32_t cpu_permil;              /* uso de CPU * 1000 (0..1000)         */
  uint32_t sinalizadores;           /* bitmask TP2_SINAL_*                 */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: tp2_info_tarefa
 *
 * Description:
 *   Nova chamada de sistema do Apache NuttX. Consulta o TCB (Task Control
 *   Block) de uma tarefa e devolve ao chamador um instantâneo de suas
 *   informações de escalonamento, de estado e de uso de pilha.
 *
 *   A função é uma "extensão de kernel": ela não existe na API original do
 *   NuttX, foi implementada junto ao subsistema de escalonamento
 *   (sched/sched/) e acessa diretamente estruturas internas do sistema
 *   operacional (struct tcb_s, struct task_group_s, listas de tarefas).
 *
 * Input Parameters:
 *   pid  - Identificador da tarefa a consultar:
 *            0  => a própria tarefa que faz a chamada (self);
 *            >0 => o pid/tid informado.
 *          Valores negativos são rejeitados com -EINVAL.
 *   info - Ponteiro para a estrutura de saída. Não pode ser NULL e o campo
 *          'tamanho' deve ser >= sizeof(struct tp2_info_tarefa_s).
 *
 * Returned Value:
 *   OK (0) em caso de sucesso. Em caso de erro, um valor de errno NEGATIVO
 *   (convenção interna do NuttX, pois esta é uma função de kernel que pode
 *   ser chamada como syscall):
 *
 *     -EINVAL  Ponteiro nulo, 'tamanho' inválido ou pid negativo.
 *     -ENOENT  O pid informado não corresponde a nenhuma tarefa ativa.
 *     -EACCES  Uma tarefa de aplicação tentou consultar tarefa de outro
 *              grupo de tarefas (ou uma thread de kernel). Somente o kernel
 *              pode inspecionar qualquer tarefa.
 *     -ENOSYS  Funcionalidade desabilitada na configuração (reservado).
 *
 * Assumptions/Limitations:
 *   As medições de pilha e de CPU são "best effort": o TCB pode mudar de
 *   estado logo após a leitura, exatamente como acontece nas funções
 *   internas do NuttX que fazem o mesmo tipo de consulta.
 *
 ****************************************************************************/

int tp2_info_tarefa(pid_t pid, FAR struct tp2_info_tarefa_s *info);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_TP2_INFO_TAREFA_H */
