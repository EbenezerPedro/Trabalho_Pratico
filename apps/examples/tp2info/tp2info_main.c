/****************************************************************************
 * apps/examples/tp2info/tp2info_main.c
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
 * ARQUIVO NOVO: aplicação de teste da chamada de sistema tp2_info_tarefa().
 *
 * Este arquivo NÃO implementa nada no kernel. Ele existe apenas para
 * comprovar, no ESP32-DevKitC, que a nova chamada de sistema incorporada ao
 * NuttX funciona tanto em condições normais quanto em situações de erro.
 *
 * Roteiro executado pelo programa (5 testes):
 *   [1] Consulta da própria tarefa  -> pid 0 (self)
 *   [2] Criação de uma thread auxiliar e consulta pelo pid dela
 *   [3] Erro: pid inexistente                              -> -ENOENT
 *   [4] Erro: pid negativo                                 -> -EINVAL
 *   [5] Erro: ponteiro nulo / 'tamanho' inválido           -> -EINVAL
 *   [6] Erro: tentativa de ler tarefa de outro grupo       -> -EACCES
 *
 * Compilação: entra no NuttX como aplicação embutida (built-in) e é
 * executada pelo shell NSH com o comando: tp2info
 * ========================================================================== */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <nuttx/tp2_info_tarefa.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Quantas vezes a thread auxiliar roda o laço de trabalho. Serve apenas
 * para gerar algum uso de pilha/CPU que possa ser observado pela syscall.
 */

#define CARGA_ITERACOES   200000

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Guarda o pid da thread auxiliar para que a thread principal possa
 * consultá-la depois.
 */

static volatile pid_t g_pid_auxiliar;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: imprimir_info
 *
 * Description:
 *   Mostra na console serial, de forma tabular, todos os campos devolvidos
 *   pela chamada de sistema.
 *
 ****************************************************************************/

static void imprimir_info(FAR const char *titulo,
                          FAR const struct tp2_info_tarefa_s *info)
{
  printf("\n--- %s ---\n", titulo);

  printf("  versao da estrutura : %u\n", (unsigned)info->versao);
  printf("  pid da thread (tid) : %d\n", (int)info->pid);
  printf("  pid do processo     : %d\n", (int)info->pid_grupo);
  printf("  pid do processo pai : %d\n", (int)info->pid_pai);
  printf("  nome da tarefa      : %s\n", info->nome);

  printf("  tipo                : ");
  switch (info->tipo)
    {
      case TP2_TIPO_TAREFA:
        printf("TAREFA\n");
        break;
      case TP2_TIPO_PTHREAD:
        printf("PTHREAD\n");
        break;
      case TP2_TIPO_KERNEL:
        printf("KERNEL\n");
        break;
      default:
        printf("DESCONHECIDO\n");
        break;
    }

  printf("  estado              : %u (%s)\n",
         (unsigned)info->estado, info->estado_texto);

  printf("  prioridade atual    : %u%s\n", (unsigned)info->prioridade,
         (info->sinalizadores & TP2_SINAL_PRIO_HERDADA) ?
         " (herdada)" : "");

  printf("  prioridade base     : %u\n", (unsigned)info->prioridade_base);

  printf("  pilha total         : %lu bytes\n",
         (unsigned long)info->pilha_total);

  if (info->sinalizadores & TP2_SINAL_PILHA_MEDIDA)
    {
      printf("  pilha usada (pico)  : %lu bytes (%lu,%02lu%%)\n",
             (unsigned long)info->pilha_usada,
             (unsigned long)(info->pilha_uso_permil / 10),
             (unsigned long)(info->pilha_uso_permil % 10));
    }
  else
    {
      printf("  pilha usada (pico)  : nao medido "
             "(CONFIG_STACK_COLORATION desligado)\n");
    }

  if (info->sinalizadores & TP2_SINAL_CPU_MEDIDA)
    {
      printf("  uso de CPU          : %lu,%02lu%%\n",
             (unsigned long)(info->cpu_permil / 10),
             (unsigned long)(info->cpu_permil % 10));
    }
  else
    {
      printf("  uso de CPU          : nao medido "
             "(monitor de carga de CPU desligado)\n");
    }
}

/****************************************************************************
 * Name: mostrar_erro
 *
 * Description:
 *   Auxiliar para imprimir o resultado de um teste que DEVE falhar.
 *
 ****************************************************************************/

static void mostrar_erro(FAR const char *descricao, int ret)
{
  if (ret < 0)
    {
      printf("  [OK] %-38s -> erro %d (%s)\n",
             descricao, ret, strerror(-ret));
    }
  else
    {
      printf("  [FALHA] %-35s -> retornou %d, esperado erro\n",
             descricao, ret);
    }
}

/****************************************************************************
 * Name: thread_auxiliar
 *
 * Description:
 *   Thread criada apenas para que exista uma segunda tarefa, do mesmo grupo
 *   de tarefas, cujo TCB possa ser consultado pela chamada de sistema.
 *
 ****************************************************************************/

static FAR void *thread_auxiliar(FAR void *arg)
{
  volatile uint32_t soma = 0;
  int i;

  (void)arg;

  /* Publica o próprio pid (tid) para a thread principal */

  g_pid_auxiliar = gettid();

  /* Laço de trabalho: consome CPU e pilha de forma controlada */

  for (i = 0; i < CARGA_ITERACOES; i++)
    {
      soma += (uint32_t)i;
    }

  printf("[auxiliar] thread %d terminando o trabalho (soma=%lu)\n",
         (int)gettid(), (unsigned long)soma);

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   Ponto de entrada da aplicação. O nome "main" é o esperado pelo
 *   Application.mk do NuttX para aplicações built-in.
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct tp2_info_tarefa_s info;
  pthread_t th;
  int ret;

  (void)argc;
  (void)argv;

  printf("\n============================================================\n");
  printf("  TP2 - Chamada de sistema tp2_info_tarefa() no NuttX/ESP32\n");
  printf("============================================================\n");

  /* ------------------------------------------------------------------------
   * TESTE 1 - Consulta da própria tarefa (pid == 0 -> self)
   * ---------------------------------------------------------------------- */

  memset(&info, 0, sizeof(info));
  info.tamanho = sizeof(info);          /* contrato de ABI obrigatório */

  ret = tp2_info_tarefa(0, &info);
  if (ret < 0)
    {
      printf("ERRO no teste 1: %d (%s)\n", ret, strerror(-ret));
      return 1;
    }

  imprimir_info("TESTE 1: consultando a propria tarefa (pid = 0)", &info);

  /* ------------------------------------------------------------------------
   * TESTE 2 - Criar uma thread auxiliar e consultar o TCB dela
   * ---------------------------------------------------------------------- */

#ifndef CONFIG_DISABLE_PTHREAD
  printf("\nCriando thread auxiliar...\n");

  ret = pthread_create(&th, NULL, thread_auxiliar, NULL);
  if (ret == 0)
    {
      /* Dá tempo para a thread publicar o próprio pid */

      usleep(50000);

      memset(&info, 0, sizeof(info));
      info.tamanho = sizeof(info);

      ret = tp2_info_tarefa(g_pid_auxiliar, &info);
      if (ret == 0)
        {
          imprimir_info("TESTE 2: consultando a thread auxiliar", &info);
        }
      else
        {
          printf("ERRO no teste 2: %d (%s)\n", ret, strerror(-ret));
        }

      pthread_join(th, NULL);
    }
  else
    {
      printf("Nao foi possivel criar a thread auxiliar (%d)\n", ret);
    }
#else
  printf("\n[teste 2 ignorado: CONFIG_DISABLE_PTHREAD=y]\n");
#endif

  /* ------------------------------------------------------------------------
   * TESTES DE ERRO (validação de parâmetros)
   * ---------------------------------------------------------------------- */

  printf("\n--- TESTES DE ERRO / ENTRADA INVALIDA ---\n");

  memset(&info, 0, sizeof(info));
  info.tamanho = sizeof(info);
  mostrar_erro("pid inexistente (pid = 4242)",
               tp2_info_tarefa(4242, &info));

  memset(&info, 0, sizeof(info));
  info.tamanho = sizeof(info);
  mostrar_erro("pid negativo (pid = -1)",
               tp2_info_tarefa(-1, &info));

  mostrar_erro("ponteiro de saida nulo (NULL)",
               tp2_info_tarefa(0, NULL));

  memset(&info, 0, sizeof(info));
  info.tamanho = 0;                     /* buffer declarado inválido */
  mostrar_erro("campo 'tamanho' invalido (0)",
               tp2_info_tarefa(0, &info));

  memset(&info, 0, sizeof(info));
  info.tamanho = sizeof(info);
  mostrar_erro("tarefa de outro grupo (pid 1 = init)",
               tp2_info_tarefa(1, &info));

  printf("\n============================================================\n");
  printf("  Fim dos testes da chamada de sistema tp2_info_tarefa()\n");
  printf("============================================================\n\n");

  return 0;
}
