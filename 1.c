/* =============================================================================
 * Trabalho de Projeto e Analise de Algoritmos
 * Tema sorteado: SELECAO DE ATIVIDADES (Activity Selection Problem)
 * Comparacao: Estrategia Gulosa  x  Tentativa e Erro (Backtracking)
 *
 * Aluno: Matheus de Sousa Moura
 * Professor: Rai Araujo de Miranda
 * UFPI - Campus Senador Helvidio Nunes de Barros (Picos)
 *
 * -----------------------------------------------------------------------------
 * O QUE ESTE PROGRAMA FAZ
 * -----------------------------------------------------------------------------
 * 1) Mostra um caso ILUSTRATIVO (contraexemplo) em que uma heuristica gulosa
 *    "ingenua" (escolher pela MENOR DURACAO) falha em achar o otimo, enquanto
 *    a heuristica gulosa CORRETA (escolher pelo MENOR TEMPO DE TERMINO) acerta
 *    o otimo - o mesmo otimo encontrado pelo Backtracking exato.
 *    -> Essa e literalmente a pergunta-exemplo do enunciado:
 *       "Mostre um caso onde o guloso falha."
 *
 * 2) Roda uma BATERIA DE BENCHMARKS com casos aleatorios de tamanhos
 *    crescentes, medindo para cada algoritmo:
 *       - resultado obtido (quantidade de atividades selecionadas)
 *       - tempo de execucao (segundos, alta resolucao)
 *       - numero de decisoes (comparacoes de ordenacao + passos do laco /
 *         nos explorados na recursao do backtracking)
 *       - memoria (KB, pico de memoria residente do processo)
 *    Para tamanhos grandes (n >= 50) o Backtracking exato e PULADO de
 *    propósito, pois sua complexidade é exponencial (O(2^n)) e travaria a
 *    execucao. Isso por si só já é um resultado para o relatorio.
 *
 * 3) Grava todos os resultados em "resultados.csv" (separado por virgula),
 *    para ser facilmente colado em uma planilha/tabela do relatorio.
 *
 * -----------------------------------------------------------------------------
 * COMO COMPILAR
 * -----------------------------------------------------------------------------
 * Linux / Ubuntu (gcc):
 *     gcc -O2 -Wall -Wextra -o selecao_atividades selecao_atividades.c
 *     ./selecao_atividades
 *
 * Windows (MinGW-w64, gcc):
 *     gcc -O2 -Wall -Wextra -o selecao_atividades.exe selecao_atividades.c -lpsapi
 *     selecao_atividades.exe
 *
 * Windows (MSVC, no "Developer Command Prompt"):
 *     cl /O2 selecao_atividades.c psapi.lib
 *     selecao_atividades.exe
 *
 * -----------------------------------------------------------------------------
 * O QUE ME MANDAR DE VOLTA
 * -----------------------------------------------------------------------------
 * - A SAIDA COMPLETA do terminal (copiar e colar tudo que aparecer);
 * - O CONTEUDO do arquivo "resultados.csv" gerado na mesma pasta;
 * - Se rodar em Windows e em Linux, mande as duas saidas separadamente
 *   (isso vira uma comparacao extra legal no relatorio: o algoritmo se
 *   comporta igual nos dois SOs? o tempo/memoria reportados mudam?).
 * ========================================================================= */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <sys/resource.h>
#endif

/* ---------------------------------------------------------------------------
 * Estrutura basica: uma atividade tem identificador, inicio e fim.
 * ------------------------------------------------------------------------- */
typedef struct {
    int    id;
    double inicio;
    double fim;
} Atividade;

/* ---------------------------------------------------------------------------
 * Cronometro de alta resolucao, multiplataforma.
 * ------------------------------------------------------------------------- */
static double tempo_atual_seg(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int inicializado = 0;
    LARGE_INTEGER contagem;
    if (!inicializado) {
        QueryPerformanceFrequency(&freq);
        inicializado = 1;
    }
    QueryPerformanceCounter(&contagem);
    return (double)contagem.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/* ---------------------------------------------------------------------------
 * Pico de memoria residente do processo, em KB, multiplataforma.
 *
 * OBS. IMPORTANTE PARA O RELATORIO:
 * Esse valor e o "high-water mark" (maior valor já atingido) de memoria do
 * PROCESSO TODO, desde que ele iniciou - tanto no Linux (getrusage) quanto
 * no Windows (GetProcessMemoryInfo). Ele NUNCA diminui durante a execucao.
 * Por isso, para estimar a memoria gasta especificamente por UM algoritmo,
 * o programa mede o valor ANTES e DEPOIS de cada algoritmo e reporta a
 * DIFERENCA (delta). Se o delta for 0, significa que aquele algoritmo nao
 * precisou de mais memoria do que o pico ja atingido anteriormente (comum
 * quando os tamanhos de entrada sao pequenos, ja que o SO trabalha em
 * paginas de 4KB).
 * ------------------------------------------------------------------------- */
static long memoria_pico_kb(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (long)(pmc.PeakWorkingSetSize / 1024L);
    return -1;
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (long)ru.ru_maxrss; /* no Linux, ru_maxrss ja vem em KB */
#endif
}

/* ---------------------------------------------------------------------------
 * Resultado padronizado de qualquer um dos algoritmos, para podermos
 * comparar todos com os mesmos campos.
 * ------------------------------------------------------------------------- */
typedef struct {
    int       qtd_selecionadas;
    int      *ids_selecionados;   /* alocado dinamicamente, tamanho = qtd_selecionadas */
    long long decisoes;
    double    tempo_seg;
    long      memoria_kb_antes;
    long      memoria_kb_depois;
    long      memoria_kb_delta;
} ResultadoAlgoritmo;

static void liberar_resultado(ResultadoAlgoritmo *r) {
    free(r->ids_selecionados);
    r->ids_selecionados = NULL;
}

/* ---------------------------------------------------------------------------
 * Comparadores para qsort. Um contador global de comparacoes e incrementado
 * a cada chamada, para podermos contar quantas "decisoes" a ordenacao tomou.
 * ------------------------------------------------------------------------- */
static long long g_comparacoes = 0;

static int cmp_por_fim(const void *a, const void *b) {
    g_comparacoes++;
    double fa = ((const Atividade *)a)->fim;
    double fb = ((const Atividade *)b)->fim;
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

static int cmp_por_duracao(const void *a, const void *b) {
    g_comparacoes++;
    double da = ((const Atividade *)a)->fim - ((const Atividade *)a)->inicio;
    double db = ((const Atividade *)b)->fim - ((const Atividade *)b)->inicio;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static int cmp_por_inicio(const void *a, const void *b) {
    g_comparacoes++;
    double ia = ((const Atividade *)a)->inicio;
    double ib = ((const Atividade *)b)->inicio;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * ALGORITMO GULOSO GENERICO.
 * Recebe um criterio de ordenacao (comparador) e aplica a regra classica:
 * ordena pelo criterio, depois percorre e escolhe cada atividade compativel
 * com a ultima escolhida (inicio >= fim da ultima escolhida).
 *
 * Se "comparador" for cmp_por_fim  -> e o guloso CORRETO (provadamente
 *    otimo para maximizar a QUANTIDADE de atividades selecionadas).
 * Se "comparador" for cmp_por_duracao -> e o guloso "ingenuo" (menor
 *    duracao primeiro), que PODE FALHAR (nao ha garantia de otimalidade).
 * ------------------------------------------------------------------------- */
static ResultadoAlgoritmo guloso_generico(const Atividade *atividades, int n,
                                           int (*comparador)(const void *, const void *)) {
    ResultadoAlgoritmo r;
    memset(&r, 0, sizeof(r));

    Atividade *copia = (Atividade *)malloc(sizeof(Atividade) * (size_t)n);
    memcpy(copia, atividades, sizeof(Atividade) * (size_t)n);

    int *sel = (int *)malloc(sizeof(int) * (size_t)n);
    int qtd = 0;

    g_comparacoes = 0;
    long mem_antes = memoria_pico_kb();
    double t0 = tempo_atual_seg();

    qsort(copia, (size_t)n, sizeof(Atividade), comparador);

    long long decisoes_laco = 0;
    double fim_ultima = -1e300; /* "menos infinito" pratico */
    for (int i = 0; i < n; i++) {
        decisoes_laco++; /* uma decisao "incluo ou nao incluo" por atividade avaliada */
        if (copia[i].inicio >= fim_ultima) {
            sel[qtd++] = copia[i].id;
            fim_ultima = copia[i].fim;
        }
    }

    double t1 = tempo_atual_seg();
    long mem_depois = memoria_pico_kb();

    r.qtd_selecionadas  = qtd;
    r.ids_selecionados  = sel;
    r.decisoes          = g_comparacoes + decisoes_laco;
    r.tempo_seg         = t1 - t0;
    r.memoria_kb_antes  = mem_antes;
    r.memoria_kb_depois = mem_depois;
    r.memoria_kb_delta  = mem_depois - mem_antes;

    free(copia);
    return r;
}

/* ---------------------------------------------------------------------------
 * BACKTRACKING EXATO (Tentativa e Erro).
 * Explora a arvore de decisao "incluir / nao incluir" cada atividade (na
 * ordem de inicio), com PODA por limite superior: se nem incluindo TODAS as
 * atividades restantes for possivel superar a melhor solucao já encontrada,
 * o ramo e cortado. Isso preserva a EXATIDAO (sempre acha o otimo de
 * verdade), só evita explorar ramos inuteis.
 * ------------------------------------------------------------------------- */
static long long g_decisoes_bt   = 0;
static int      *g_melhor_sel    = NULL;
static int       g_melhor_qtd    = 0;
static int      *g_atual_sel     = NULL;
static int       g_atual_qtd     = 0;

static void backtracking_rec(const Atividade *atv, int n, int idx, double fim_ultima) {
    g_decisoes_bt++; /* cada chamada = um no explorado na arvore de decisao */

    /* poda: mesmo pegando TODAS as restantes, nao supera o melhor atual */
    if (g_atual_qtd + (n - idx) <= g_melhor_qtd) {
        return;
    }

    if (idx == n) {
        if (g_atual_qtd > g_melhor_qtd) {
            g_melhor_qtd = g_atual_qtd;
            memcpy(g_melhor_sel, g_atual_sel, sizeof(int) * (size_t)g_atual_qtd);
        }
        return;
    }

    /* ramo 1: tenta incluir atv[idx], se for compativel com a ultima escolhida */
    if (atv[idx].inicio >= fim_ultima) {
        g_atual_sel[g_atual_qtd++] = atv[idx].id;
        backtracking_rec(atv, n, idx + 1, atv[idx].fim);
        g_atual_qtd--; /* desfaz a escolha (o "backtrack" propriamente) */
    }

    /* ramo 2: nao incluir atv[idx] */
    backtracking_rec(atv, n, idx + 1, fim_ultima);
}

static ResultadoAlgoritmo backtracking_exato(const Atividade *atividades, int n) {
    ResultadoAlgoritmo r;
    memset(&r, 0, sizeof(r));

    Atividade *copia = (Atividade *)malloc(sizeof(Atividade) * (size_t)n);
    memcpy(copia, atividades, sizeof(Atividade) * (size_t)n);
    qsort(copia, (size_t)n, sizeof(Atividade), cmp_por_inicio);

    g_decisoes_bt  = 0;
    g_melhor_qtd   = 0;
    g_melhor_sel   = (int *)malloc(sizeof(int) * (size_t)n);
    g_atual_sel    = (int *)malloc(sizeof(int) * (size_t)n);
    g_atual_qtd    = 0;

    long mem_antes = memoria_pico_kb();
    double t0 = tempo_atual_seg();

    backtracking_rec(copia, n, 0, -1e300);

    double t1 = tempo_atual_seg();
    long mem_depois = memoria_pico_kb();

    r.qtd_selecionadas  = g_melhor_qtd;
    r.ids_selecionados  = g_melhor_sel; /* repassa a posse do ponteiro */
    r.decisoes          = g_decisoes_bt;
    r.tempo_seg         = t1 - t0;
    r.memoria_kb_antes  = mem_antes;
    r.memoria_kb_depois = mem_depois;
    r.memoria_kb_delta  = mem_depois - mem_antes;

    free(copia);
    free(g_atual_sel);
    g_atual_sel = NULL;
    return r;
}

/* ---------------------------------------------------------------------------
 * Gerador de casos de teste aleatorios (reprodutivel via semente).
 * O horizonte de tempo cresce com n para manter uma densidade de
 * sobreposicao parecida entre os varios tamanhos testados.
 * ------------------------------------------------------------------------- */
static void gerar_atividades_aleatorias(Atividade *atv, int n, unsigned int semente) {
    srand(semente);
    double horizonte = (n < 20) ? 40.0 : (double)n * 2.0;
    for (int i = 0; i < n; i++) {
        double inicio  = ((double)rand() / (double)RAND_MAX) * horizonte;
        double duracao = 0.5 + ((double)rand() / (double)RAND_MAX) * (horizonte * 0.12);
        atv[i].id     = i + 1;
        atv[i].inicio = inicio;
        atv[i].fim    = inicio + duracao;
    }
}

/* ---------------------------------------------------------------------------
 * Impressao auxiliar: lista de atividades selecionadas (id) numa linha.
 * ------------------------------------------------------------------------- */
static void imprimir_ids(const int *ids, int qtd) {
    printf("{");
    for (int i = 0; i < qtd; i++) {
        printf("%d%s", ids[i], (i + 1 < qtd) ? ", " : "");
    }
    printf("}");
}

/* arquivo CSV global para os benchmarks */
static FILE *g_csv = NULL;

static void csv_linha(const char *caso, int n, unsigned int semente, const char *algoritmo,
                       const ResultadoAlgoritmo *r) {
    if (!g_csv) return;
    fprintf(g_csv, "%s,%d,%u,%s,%d,%.9f,%lld,%ld,%ld,%ld\n",
            caso, n, semente, algoritmo,
            r->qtd_selecionadas, r->tempo_seg, r->decisoes,
            r->memoria_kb_antes, r->memoria_kb_depois, r->memoria_kb_delta);
}

/* =============================================================================
 * PARTE 1 - CASO ILUSTRATIVO (CONTRAEXEMPLO)
 *
 * Tres atividades:
 *   L1 = (0, 4)   duracao 4
 *   X  = (3, 5)   duracao 2   <- mais curta de todas
 *   L2 = (4, 8)   duracao 4
 *
 * X sobrepoe L1 e L2. L1 e L2 NAO se sobrepoem entre si (L1 termina em 4,
 * L2 comeca em 4).
 *
 * - Guloso por DURACAO (ingenuo) escolhe X primeiro (a mais curta) e com
 *   isso BLOQUEIA L1 e L2 -> resultado = {X}, tamanho 1.
 * - Guloso por TERMINO (correto) escolhe L1, depois L2 -> {L1, L2},
 *   tamanho 2.
 * - Backtracking exato confirma que o OTIMO verdadeiro e 2.
 *
 * Ou seja: o guloso "errado" falha; o guloso "certo" empata com o exato.
 * ========================================================================= */
static void rodar_caso_ilustrativo(void) {
    printf("=============================================================\n");
    printf(" PARTE 1: CASO ILUSTRATIVO (CONTRAEXEMPLO)\n");
    printf(" \"Mostre um caso onde o guloso falha.\"\n");
    printf("=============================================================\n\n");

    Atividade atv[3] = {
        {1, 0.0, 4.0},  /* L1 */
        {2, 3.0, 5.0},  /* X  - a mais curta, sobrepoe as outras duas */
        {3, 4.0, 8.0}   /* L2 */
    };
    int n = 3;

    printf("Atividades de entrada (id: inicio - fim | duracao):\n");
    for (int i = 0; i < n; i++) {
        printf("  id %d: %.1f - %.1f | duracao %.1f\n",
               atv[i].id, atv[i].inicio, atv[i].fim, atv[i].fim - atv[i].inicio);
    }
    printf("\n");

    ResultadoAlgoritmo r_dur = guloso_generico(atv, n, cmp_por_duracao);
    ResultadoAlgoritmo r_fim = guloso_generico(atv, n, cmp_por_fim);
    ResultadoAlgoritmo r_bt  = backtracking_exato(atv, n);

    printf("%-28s | %-22s | %-22s | %s\n",
           "Criterio", "Guloso (por duracao)", "Guloso (por termino)", "Backtracking");
    printf("---------------------------------------------------------------------------------------------------\n");

    printf("%-28s | ", "Resultado obtido (ids)");
    imprimir_ids(r_dur.ids_selecionados, r_dur.qtd_selecionadas); printf("%18s", "");
    printf(" | "); imprimir_ids(r_fim.ids_selecionados, r_fim.qtd_selecionadas); printf("%18s", "");
    printf(" | "); imprimir_ids(r_bt.ids_selecionados, r_bt.qtd_selecionadas);
    printf("\n");

    printf("%-28s | %-22d | %-22d | %d\n", "Melhor solucao encontrada",
           r_dur.qtd_selecionadas, r_fim.qtd_selecionadas, r_bt.qtd_selecionadas);

    printf("%-28s | %-22.9f | %-22.9f | %.9f\n", "Tempo de execucao (s)",
           r_dur.tempo_seg, r_fim.tempo_seg, r_bt.tempo_seg);

    printf("%-28s | %-22lld | %-22lld | %lld\n", "Numero de decisoes",
           r_dur.decisoes, r_fim.decisoes, r_bt.decisoes);

    printf("\n>> O guloso por DURACAO escolheu so %d atividade(s): falhou em achar o otimo.\n",
           r_dur.qtd_selecionadas);
    printf(">> O guloso por TERMINO escolheu %d atividade(s): empatou com o Backtracking (otimo = %d).\n\n",
           r_fim.qtd_selecionadas, r_bt.qtd_selecionadas);

    csv_linha("ilustrativo", n, 0, "guloso_duracao", &r_dur);
    csv_linha("ilustrativo", n, 0, "guloso_termino", &r_fim);
    csv_linha("ilustrativo", n, 0, "backtracking",   &r_bt);

    liberar_resultado(&r_dur);
    liberar_resultado(&r_fim);
    liberar_resultado(&r_bt);
}

/* =============================================================================
 * PARTE 2 - BENCHMARK COM CASOS ALEATORIOS (n PEQUENO/MEDIO)
 * Roda os 3 algoritmos (guloso por termino, guloso por duracao, backtracking
 * exato) e imprime a tabela no mesmo formato pedido no enunciado.
 * ========================================================================= */
static void rodar_benchmark_com_backtracking(int n, unsigned int semente) {
    Atividade *atv = (Atividade *)malloc(sizeof(Atividade) * (size_t)n);
    gerar_atividades_aleatorias(atv, n, semente);

    ResultadoAlgoritmo r_dur = guloso_generico(atv, n, cmp_por_duracao);
    ResultadoAlgoritmo r_fim = guloso_generico(atv, n, cmp_por_fim);
    ResultadoAlgoritmo r_bt  = backtracking_exato(atv, n);

    printf("--- n = %d (semente = %u) -----------------------------------------------\n", n, semente);
    printf("%-28s | %-18s | %-18s | %s\n",
           "Criterio", "Guloso (duracao)", "Guloso (termino)", "Backtracking");
    printf("%-28s | %-18d | %-18d | %d\n", "Melhor solucao encontrada",
           r_dur.qtd_selecionadas, r_fim.qtd_selecionadas, r_bt.qtd_selecionadas);
    printf("%-28s | %-18.6f | %-18.6f | %.6f\n", "Tempo de execucao (s)",
           r_dur.tempo_seg, r_fim.tempo_seg, r_bt.tempo_seg);
    printf("%-28s | %-18lld | %-18lld | %lld\n", "Numero de decisoes",
           r_dur.decisoes, r_fim.decisoes, r_bt.decisoes);
    printf("%-28s | %-18ld | %-18ld | %ld\n", "Delta memoria (KB)",
           r_dur.memoria_kb_delta, r_fim.memoria_kb_delta, r_bt.memoria_kb_delta);
    printf("\n");

    csv_linha("benchmark_pequeno", n, semente, "guloso_duracao", &r_dur);
    csv_linha("benchmark_pequeno", n, semente, "guloso_termino", &r_fim);
    csv_linha("benchmark_pequeno", n, semente, "backtracking",   &r_bt);

    liberar_resultado(&r_dur);
    liberar_resultado(&r_fim);
    liberar_resultado(&r_bt);
    free(atv);
}

/* =============================================================================
 * PARTE 3 - BENCHMARK COM CASOS GRANDES (SO OS GULOSOS)
 * Backtracking exato e O(2^n): para n >= 50 a busca exaustiva travaria a
 * execucao por tempo praticamente inviavel, entao aqui comparamos so os
 * dois gulosos, para mostrar como o tempo deles cresce (suavemente, de
 * forma polinomial) mesmo para entradas bem maiores.
 * ========================================================================= */
static void rodar_benchmark_so_guloso(int n, unsigned int semente) {
    Atividade *atv = (Atividade *)malloc(sizeof(Atividade) * (size_t)n);
    gerar_atividades_aleatorias(atv, n, semente);

    ResultadoAlgoritmo r_dur = guloso_generico(atv, n, cmp_por_duracao);
    ResultadoAlgoritmo r_fim = guloso_generico(atv, n, cmp_por_fim);

    printf("--- n = %-6d (semente = %u) ---------------------------------------\n", n, semente);
    printf("%-28s | %-18s | %s\n", "Criterio", "Guloso (duracao)", "Guloso (termino)");
    printf("%-28s | %-18d | %d\n", "Melhor solucao encontrada",
           r_dur.qtd_selecionadas, r_fim.qtd_selecionadas);
    printf("%-28s | %-18.6f | %.6f\n", "Tempo de execucao (s)",
           r_dur.tempo_seg, r_fim.tempo_seg);
    printf("%-28s | %-18lld | %lld\n", "Numero de decisoes",
           r_dur.decisoes, r_fim.decisoes);
    printf("%-28s | %-18ld | %ld\n", "Delta memoria (KB)",
           r_dur.memoria_kb_delta, r_fim.memoria_kb_delta);
    printf("(Backtracking exato PULADO neste tamanho: custo exponencial O(2^n) inviavel.)\n\n");

    csv_linha("benchmark_grande", n, semente, "guloso_duracao", &r_dur);
    csv_linha("benchmark_grande", n, semente, "guloso_termino", &r_fim);

    liberar_resultado(&r_dur);
    liberar_resultado(&r_fim);
    free(atv);
}

int main(void) {
    g_csv = fopen("resultados.csv", "w");
    if (!g_csv) {
        fprintf(stderr, "AVISO: nao foi possivel criar resultados.csv no diretorio atual.\n");
    } else {
        fprintf(g_csv, "caso,n,semente,algoritmo,qtd_selecionadas,tempo_segundos,decisoes,"
                       "memoria_kb_antes,memoria_kb_depois,memoria_kb_delta\n");
    }

    printf("#############################################################\n");
    printf("# SELECAO DE ATIVIDADES - GULOSO x BACKTRACKING\n");
    printf("# Aluno: Matheus de Sousa Moura\n");
    printf("#############################################################\n\n");

    /* ---- Parte 1: contraexemplo ilustrativo ---- */
    rodar_caso_ilustrativo();

    /* ---- Parte 2: casos pequenos/medios com os 3 algoritmos ---- */
    printf("=============================================================\n");
    printf(" PARTE 2: CASOS ALEATORIOS (com Backtracking exato)\n");
    printf("=============================================================\n\n");
    int tamanhos_pequenos[] = {10, 15, 18, 20, 22, 24};
    int qtd_pequenos = (int)(sizeof(tamanhos_pequenos) / sizeof(tamanhos_pequenos[0]));
    for (int i = 0; i < qtd_pequenos; i++) {
        unsigned int semente = 1000u + (unsigned int)tamanhos_pequenos[i];
        rodar_benchmark_com_backtracking(tamanhos_pequenos[i], semente);
    }

    /* ---- Parte 3: casos grandes, so os gulosos ---- */
    printf("=============================================================\n");
    printf(" PARTE 3: CASOS GRANDES (somente os gulosos - Backtracking inviavel)\n");
    printf("=============================================================\n\n");
    int tamanhos_grandes[] = {50, 100, 500, 1000, 5000, 10000};
    int qtd_grandes = (int)(sizeof(tamanhos_grandes) / sizeof(tamanhos_grandes[0]));
    for (int i = 0; i < qtd_grandes; i++) {
        unsigned int semente = 2000u + (unsigned int)tamanhos_grandes[i];
        rodar_benchmark_so_guloso(tamanhos_grandes[i], semente);
    }

    if (g_csv) {
        fclose(g_csv);
        printf("=============================================================\n");
        printf(" Resultados completos tambem foram salvos em: resultados.csv\n");
        printf(" Mande para o Claude TODA a saida acima + o conteudo desse CSV.\n");
        printf("=============================================================\n");
    }

    return 0;
}