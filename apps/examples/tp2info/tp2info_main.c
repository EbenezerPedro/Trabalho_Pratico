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

#include <nuttx/config.h> /* Inclui as configurações gerais do NuttX. */

#include <sys/types.h>  // Define tipos utilizados pelo sistema, como pid_t.
#include <stdint.h>
#include <stdio.h>
#include <string.h> /* Disponibiliza funções de manipulação de memória, como memset(). */
#include <errno.h>
#include <unistd.h> /* Disponibiliza funções relacionadas ao sistema, como gettid() e usleep(). */
#include <pthread.h> /* Permite criar e gerenciar threads utilizando pthread. */
#include <nuttx/tp2_info_tarefa.h> /* Inclui a estrutura e a declaração da função tp2_info_tarefa(). */

/* Define quantas vezes o laço da thread auxiliar
 * será executado.
 *
 * Quanto maior esse valor, mais tempo a thread
 * permanecerá executando o trabalho. */

#define CARGA_ITERACOES   200000 

static volatile pid_t g_pid_auxiliar; /* Variável global para armazenar o PID da thread auxiliar. */

/* Apresenta na tela todas as informações recebidas pela estrutura tp2_info_tarefa_s */
static void imprimir_info(FAR const char *titulo,
                         FAR const struct tp2_info_tarefa_s *info) 
{
  printf("\n--- %s ---\n", titulo); /* Imprime o título do teste ou da consulta. */ 

  printf("  versao da estrutura : %u\n", (unsigned)info->versao); /* Imprime a versão da estrutura tp2_info_tarefa_s. */
  printf("  pid da thread (tid) : %d\n", (int)info->pid);         /* Imprime o PID da thread consultada (tid). */
  printf("  pid do processo     : %d\n", (int)info->pid_grupo); /* Imprime o PID do processo ao qual a thread pertence. */
  printf("  pid do processo pai : %d\n", (int)info->pid_pai);  /* Imprime o PID do processo pai da thread consultada. */
  printf("  nome da tarefa      : %s\n", info->nome);         /* Imprime o nome da tarefa consultada. */

  printf("  tipo                : "); /* Imprime o tipo da tarefa consultada (TP2_TIPO_TAREFA, TP2_TIPO_PTHREAD ou TP2_TIPO_KERNEL). */
  switch (info->tipo) /* Verifica o tipo da tarefa consultada */
    {
      case TP2_TIPO_TAREFA:  /* Se for uma tarefa comum (task) */
        printf("TAREFA\n");
        break;
      case TP2_TIPO_PTHREAD: /* Se for uma thread criada com pthread_create() */
        printf("PTHREAD\n");
        break;
      case TP2_TIPO_KERNEL:  /* Se for uma thread pertencente ao kernel */
        printf("KERNEL\n");
        break;
      default:
        printf("DESCONHECIDO\n"); /* Se o tipo não corresponder a nenhum dos casos anteriores, imprime "DESCONHECIDO". */
        break;
    }

  /* Imprime o estado da tarefa consultada, tanto em valor numérico quanto em texto descritivo. */
  printf("  estado              : %u (%s)\n", 
         (unsigned)info->estado, info->estado_texto); 

  /* Imprime a prioridade atual da tarefa consultada. Se a prioridade for herdada, adiciona "(herdada)" ao final da linha. */
  printf("  prioridade atual    : %u%s\n", (unsigned)info->prioridade,
         (info->sinalizadores & TP2_SINAL_PRIO_HERDADA) ?
         " (herdada)" : ""); 

  printf("  prioridade base     : %u\n", (unsigned)info->prioridade_base); /* Imprime a prioridade base da tarefa consultada. */

  printf("  pilha total         : %lu bytes\n",
         (unsigned long)info->pilha_total); /* Imprime o tamanho total da pilha da tarefa consultada em bytes. */

  if (info->sinalizadores & TP2_SINAL_PILHA_MEDIDA) /* Verifica se a medição da pilha foi realizada e se o campo pilha_usada contém um valor válido. */
    {
      printf("  pilha usada (pico)  : %lu bytes (%lu,%02lu%%)\n", 
             (unsigned long)info->pilha_usada,             /* Imprime o pico de uso da pilha da tarefa consultada em bytes. */
             (unsigned long)(info->pilha_uso_permil / 10), /* Imprime o pico de uso da pilha da tarefa consultada em percentual (milésimos). */
             (unsigned long)(info->pilha_uso_permil % 10)); /* Imprime o pico de uso da pilha da tarefa consultada em bytes e em percentual (milésimos). */
    }
  else
    {
      /* Se a medição da pilha não foi realizada, informa que a medição não está disponível devido à configuração CONFIG_STACK_COLORATION estar desligada. */
      printf("  pilha usada (pico)  : nao medido "
             "(CONFIG_STACK_COLORATION desligado)\n"); 
    }

  if (info->sinalizadores & TP2_SINAL_CPU_MEDIDA) /* Verifica se a medição da CPU foi realizada e se o campo cpu_permil contém um valor válido. */
    {
      printf("  uso de CPU          : %lu,%02lu%%\n",
             (unsigned long)(info->cpu_permil / 10),
             (unsigned long)(info->cpu_permil % 10)); /* Imprime a utilização da CPU da tarefa consultada em percentual (milésimos). */
    }
  else
    {
      /* Se a medição da CPU não foi realizada, informa que a medição não está disponível devido à configuração CONFIG_CPU_MONITOR estar desligada. */
      printf("  uso de CPU          : nao medido "
             "(monitor de carga de CPU desligado)\n");
    }
}

/* Função auxiliar para exibir mensagens de erro durante os testes. */
static void mostrar_erro(FAR const char *descricao, int ret) 
{
  if (ret < 0)
    {
      /* Imprime a mensagem de erro correspondente ao código de erro retornado pela função tp2_info_tarefa(). */
      printf("  [OK] %-38s -> erro %d (%s)\n",
             descricao, ret, strerror(-ret)); 
    }
  else
    {
      /* Se a função tp2_info_tarefa() não retornar um erro, indica que houve uma falha no teste. */
      printf("  [FALHA] %-35s -> retornou %d, esperado erro\n",
             descricao, ret); 
    }
}

/* Função que será executada pela thread auxiliar criada durante os testes. */
static FAR void *thread_auxiliar(FAR void *arg) 
{
  volatile uint32_t soma = 0; /* Variável para acumular a soma durante o trabalho da thread auxiliar. */
  int i;

  (void)arg;

  g_pid_auxiliar = gettid(); /* Armazena o PID da thread auxiliar na variável global g_pid_auxiliar. */

  for (i = 0; i < CARGA_ITERACOES; i++) /* Executa um laço de trabalho para simular carga na thread auxiliar. */
    {
      soma += (uint32_t)i; /* Acumula a soma dos valores de i na variável soma. */
    }

  /* Imprime uma mensagem indicando que a thread auxiliar terminou o trabalho e exibe o valor final da soma acumulada. */
  printf("[auxiliar] thread %d terminando o trabalho (soma=%lu)\n",
         (int)gettid(), (unsigned long)soma); 

  return NULL; /* Retorna NULL para indicar que a thread auxiliar terminou sua execução. */
} 

int main(int argc, FAR char *argv[]) /* Função principal do programa de teste da chamada de sistema tp2_info_tarefa(). */
{
  struct tp2_info_tarefa_s info; /* Estrutura para armazenar as informações da tarefa consultada. */
  pthread_t th;  /* Variável para armazenar o identificador da thread auxiliar criada durante os testes. */
  int ret;      /* Variável para armazenar o valor de retorno das chamadas de função, incluindo a chamada tp2_info_tarefa(). */

  (void)argc;           /* Ignora o argumento argc, pois não é utilizado na função main(). */
  (void)argv;          /* Ignora o argumento argv, pois não é utilizado na função main(). */

  printf("\n============================================================\n");
  printf("  TP2 - Chamada de sistema tp2_info_tarefa() no NuttX/ESP32\n");
  printf("============================================================\n");

  memset(&info, 0, sizeof(info)); /* Inicializa a estrutura info com zeros para evitar lixo de memória. */
  info.tamanho = sizeof(info);          

  /* Consulta as informações da própria tarefa (pid = 0) e armazena os resultados na estrutura info. */
  ret = tp2_info_tarefa(0, &info); 
  if (ret < 0) /* Se a chamada tp2_info_tarefa() retornar um valor negativo, indica que ocorreu um erro durante a consulta. */
    {
      printf("ERRO no teste 1: %d (%s)\n", ret, strerror(-ret)); /* Imprime a mensagem de erro retornado pela função tp2_info_tarefa(). */
      return 1; /* Retorna 1 para indicar que ocorreu um erro durante o teste. */
    }

  imprimir_info("TESTE 1: consultando a propria tarefa (pid = 0)", &info);

  /* Se a configuração CONFIG_DISABLE_PTHREAD não estiver definida, cria uma thread auxiliar para testar a consulta de informações de uma thread diferente. */
#ifndef CONFIG_DISABLE_PTHREAD 
  printf("\nCriando thread auxiliar...\n");

  ret = pthread_create(&th, NULL, thread_auxiliar, NULL); /* Cria a thread auxiliar e armazena o identificador da thread em th. */
  if (ret == 0) /* Se a criação da thread auxiliar for bem-sucedida, ret será igual a 0. */
    {
      usleep(50000); /* Aguarda 50 milissegundos para garantir que a thread auxiliar tenha tempo de iniciar e armazenar seu PID na variável global g_pid_auxiliar. */

      memset(&info, 0, sizeof(info)); /* Inicializa a estrutura info com zeros para evitar lixo de memória. */
      info.tamanho = sizeof(info);

      /* Consulta as informações da thread auxiliar usando o PID armazenado em g_pid_auxiliar e armazena os resultados na estrutura info. */
      ret = tp2_info_tarefa(g_pid_auxiliar, &info); 
      if (ret == 0) /* Se a chamada tp2_info_tarefa() retornar 0, indica que a consulta foi bem-sucedida. */
        {
          /* Imprime as informações da thread auxiliar consultada usando a função imprimir_info(). */
          imprimir_info("TESTE 2: consultando a thread auxiliar", &info);
        }
      else
        {
          printf("ERRO no teste 2: %d (%s)\n", ret, strerror(-ret));
        }

      pthread_join(th, NULL); /* Aguarda a thread auxiliar terminar sua execução antes de prosseguir. */
    }
  else
    {
      printf("Nao foi possivel criar a thread auxiliar (%d)\n", ret);
    }
#else
  printf("\n[teste 2 ignorado: CONFIG_DISABLE_PTHREAD=y]\n");
#endif

  /* Início dos testes de erro e entrada inválida para a chamada tp2_info_tarefa().*/
  printf("\n--- TESTES DE ERRO / ENTRADA INVALIDA ---\n");

  memset(&info, 0, sizeof(info)); /* Inicializa a estrutura info com zeros para evitar lixo de memória. */
  info.tamanho = sizeof(info);
  mostrar_erro("pid inexistente (pid = 4242)",
               tp2_info_tarefa(4242, &info));

  memset(&info, 0, sizeof(info)); /* Inicializa a estrutura info com zeros para evitar lixo de memória. */
  info.tamanho = sizeof(info);
  mostrar_erro("pid negativo (pid = -1)",
               tp2_info_tarefa(-1, &info));

  /* Testa a chamada tp2_info_tarefa() com um ponteiro de saída nulo (NULL) e espera que retorne um erro.*/
  mostrar_erro("ponteiro de saida nulo (NULL)", 
               tp2_info_tarefa(0, NULL));

  memset(&info, 0, sizeof(info));  /* Inicializa a estrutura info com zeros para evitar lixo de memória. */
  info.tamanho = 0;  
  
  /* O campo 'tamanho' da estrutura info é definido como 0, o que é inválido, pois não há espaço suficiente para armazenar as informações da tarefa */
  mostrar_erro("campo 'tamanho' invalido (0)",
               tp2_info_tarefa(0, &info)); 

  memset(&info, 0, sizeof(info)); /* Inicializa a estrutura info com zeros para evitar lixo de memória. */
  info.tamanho = sizeof(info);
  /* A tarefa init (pid 1) pertence a outro grupo, portanto, a consulta deve ser negada. */
  mostrar_erro("tarefa de outro grupo (pid 1 = init)",
               tp2_info_tarefa(1, &info)); 

  printf("\n============================================================\n");
  printf("  Fim dos testes da chamada de sistema tp2_info_tarefa()\n");
  printf("============================================================\n\n");

  return 0;
}
