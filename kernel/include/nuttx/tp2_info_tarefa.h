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
 * GERENCIAMENTO DE TAREFAS / ESCALONAMENTO / GERENCIAMENTO DE MEMÓRIA (pilha) / MONITORAMENTO DE RECURSOS: a chamada
 * lê o TCB (Task Control Block) da tarefa alvo e reporta ao espaço de
 * usuário o que o kernel sabe sobre ela.
 * ========================================================================== */

#ifndef __INCLUDE_NUTTX_TP2_INFO_TAREFA_H /* evita que as mesmas definições sejam processadas várias vezes durante a compilação. */
#define __INCLUDE_NUTTX_TP2_INFO_TAREFA_H /* Define o identificador de proteção. */

#include <nuttx/config.h> /* Inclui as configurações gerais do NuttX. */

#include <sys/types.h>   /* Define tipos utilizados pelo sistema, como pid_t. */
#include <stdint.h>     /* Define tipos inteiros com tamanho fixo, como uint16_t e uint32_t. */

#define TP2_NOME_MAX     16  /* tamanho máximo do nome da tarefa (incluindo o caractere nulo de terminação) */

#define TP2_ESTADO_TEXTO_MAX    24  /* tamanho máximo da string que descreve o estado da tarefa (incluindo o caractere nulo de terminação) */

#define TP2_VERSAO_ATUAL     1  /* Versão atual da estrutura tp2_info_tarefa_s. Se a versão da estrutura fornecida pelo chamador for diferente, a chamada de sistema retornará um erro. */

#define TP2_TIPO_TAREFA         0   /* Identifica uma tarefa comum do NuttX                 */
#define TP2_TIPO_PTHREAD        1   /* Identifica uma thread criada utilizando pthread_create().      */
#define TP2_TIPO_KERNEL         2   /* Identifica uma thread pertencente ao kernel          */

#define TP2_SINAL_PILHA_MEDIDA  (1 << 0)  /* Bit 0 pilha_usada é válido        */
#define TP2_SINAL_CPU_MEDIDA    (1 << 1)  /* Bit 1 cpu_permil é válido         */
#define TP2_SINAL_PRIO_HERDADA  (1 << 2)  /* Bit 2 prioridade herdada ativa    */

struct tp2_info_tarefa_s   /* Estrutura que armazena as informações da tarefa consultada. */
{
  uint16_t tamanho;                 /* verificar se o espaço de memória disponível é suficiente para receber os dados.  */
  uint16_t versao;                  /* Armazena a versão da estrutura utilizada e preenchido pelo kernel               */
  pid_t    pid;                     /* PID da thread  (tcb->pid)  */
  pid_t    pid_grupo;               /* PID do grupo ou processo ao qual a tarefa pertence. */
  pid_t    pid_pai;                 /* PID do processo pai, Quando a informação não estiver disponível, poderá receber o valor -1. */
  uint8_t  tipo;                    /* TP2_TIPO_* Armazena o tipo da tarefa  */
  uint8_t  prioridade;              /* prioridade efetiva atual, prioridade utilizada pelo escalonador  */
  uint8_t  prioridade_base;         /* prioridade original (sem herança)   */
  uint8_t  estado;                  /* Armazena o estado numérico da tarefa (valor do enum tstate_e) */
  char     nome[TP2_NOME_MAX];      /* Armazena o nome da tarefa, vetor de caracteres com tamanho máximo  */
  char     estado_texto[TP2_ESTADO_TEXTO_MAX]; /* Armazena o estado da tarefa em formato textual (READY, RUNNING ou WAITING)   */
  size_t   pilha_total;             /* Armazena o tamanho total da pilha em bytes  */
  size_t   pilha_usada;             /* Armazena pico de uso da pilha (bytes), representa a quantidade máxima de pilha utilizada */
  uint32_t pilha_uso_permil;        /* percentual * 1000 (0..1000)         */
  uint32_t cpu_permil;              /* uso de CPU * 1000 (0..1000)         */
  uint32_t sinalizadores;           /* Armazena os sinalizadores TP2_SINAL_* Permite identificar quais medições foram realizadas
   * e se existe herança de prioridade*/
};

#undef EXTERN                           /* Remove a definição anterior de EXTERN para evitar conflitos. */
#if defined(__cplusplus)              /* Se o código estiver sendo compilado como C++, define EXTERN como extern "C" para evitar name mangling. */
#define EXTERN extern "C"            /* Define EXTERN como extern "C" para evitar name mangling em C++. */
extern "C"                          /* Indica que o código a seguir é compatível com C. */
{
#else
#define EXTERN extern    /* Define EXTERN como extern para C. */
#endif

/* Declaração da função tp2_info_tarefa(), que consulta informações sobre uma tarefa específica. A função recebe o PID da tarefa e um ponteiro para a estrutura tp2_info_tarefa_s onde as informações serão armazenadas. Retorna 0 em caso de sucesso ou um valor negativo em caso de erro. */
int tp2_info_tarefa(pid_t pid, FAR struct tp2_info_tarefa_s *info); 

#undef EXTERN  /* Remove a definição de EXTERN para evitar conflitos. */
#if defined(__cplusplus)   /* Se o código estiver sendo compilado como C++, fecha o bloco extern "C". */
}
#endif    /* Fecha o bloco extern "C" para C++. */

#endif /* __INCLUDE_NUTTX_TP2_INFO_TAREFA_H */
