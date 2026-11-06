#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_DIM    1000
#define DENSIDAD   0.05      // 5% de elementos no nulos 
#define TAM_BUFFER 4096      // elementos de B leídos por bloque 

// Una celda no nula de la matriz (formato COO) 
typedef struct {
    int    fila;
    int    col;
    double valor;
} ElementoEsparso;

// Cabecera al inicio de cada .bin: dimensiones de la matriz 
typedef struct {
    int filas;
    int cols;
} Cabecera;

static FILE *abrir(const char *nombre, const char *modo)
{
    FILE *f = fopen(nombre, modo);
    if (f == NULL) {
        perror(nombre);
        exit(1);
    }
    return f;
}

// Paso 1: generación aleatoria en texto 

/* Formato del .txt:
 *   primera línea: filas columnas
 *   siguientes   : fila columna valor   (solo no nulos, base 0)
 */
static long generar_matriz_txt(const char *nombre, int filas, int cols)
{
    long total = (long)filas * cols;
    long nnz   = (long)(total * DENSIDAD);
    if (nnz < 1) nnz = 1;

    // marca de posiciones ya usadas, para no repetir (i, j) 
    char *ocupado = calloc((size_t)total, 1);
    if (ocupado == NULL) {
        fprintf(stderr, "Sin memoria\n");
        exit(1);
    }

    FILE *f = abrir(nombre, "w");
    fprintf(f, "%d %d\n", filas, cols);

    long generados = 0;
    while (generados < nnz) {
        /* i y j por separado */
        int i = rand() % filas;
        int j = rand() % cols;
        long pos = (long)i * cols + j;

        if (ocupado[pos]) continue;          // posición repetida 
        ocupado[pos] = 1;

        double v = 1.0 + (rand() % 900) / 100.0;   // 1.00 .. 9.99 (nunca 0) 
        fprintf(f, "%d %d %.2f\n", i, j, v);
        generados++;
    }

    fclose(f);
    free(ocupado);
    return nnz;
}

//  Paso 2: texto -> binario

static long txt_a_bin(const char *txt, const char *bin)
{
    FILE *ft = abrir(txt, "r");
    FILE *fb = abrir(bin, "wb");

    Cabecera cab;
    if (fscanf(ft, "%d %d", &cab.filas, &cab.cols) != 2) {
        fprintf(stderr, "%s: cabecera inválida\n", txt);
        exit(1);
    }
    fwrite(&cab, sizeof(Cabecera), 1, fb);

    ElementoEsparso e;
    long n = 0;
    while (fscanf(ft, "%d %d %lf", &e.fila, &e.col, &e.valor) == 3) {
        fwrite(&e, sizeof(ElementoEsparso), 1, fb);
        n++;
    }

    fclose(ft);
    fclose(fb);
    return n;
}

// Paso 3: multiplicación -> acumulado.bin  

/* -Por cada A[i][k] recorre todo B buscando filas == k.
 -Cada coincidencia genera un parcial (i, j, a*b) que se escribe en acumulado.
  -Devuelve la cantidad de parciales; en *res deja las dimensiones de AxB. */
static long multiplicar(const char *bin_a, const char *bin_b,
                        const char *acumulado, Cabecera *res)
{
    FILE *fa = abrir(bin_a, "rb");
    FILE *fb = abrir(bin_b, "rb");
    FILE *fo = abrir(acumulado, "wb");

    Cabecera ca, cb;
    fread(&ca, sizeof(Cabecera), 1, fa);
    fread(&cb, sizeof(Cabecera), 1, fb);

    if (ca.cols != cb.filas) {
        fprintf(stderr, "Dimensiones incompatibles: A es %dx%d y B es %dx%d\n",
                ca.filas, ca.cols, cb.filas, cb.cols);
        exit(1);
    }
    res->filas = ca.filas;
    res->cols  = cb.cols;

    ElementoEsparso a;
    ElementoEsparso buffer[TAM_BUFFER];   
    long parciales = 0;

    while (fread(&a, sizeof(ElementoEsparso), 1, fa) == 1) {
        /* volver al primer elemento de B (saltando la cabecera) */
        fseek(fb, (long)sizeof(Cabecera), SEEK_SET);

        size_t n;
        while ((n = fread(buffer, sizeof(ElementoEsparso), TAM_BUFFER, fb)) > 0) {
            for (size_t k = 0; k < n; k++) {
                if (buffer[k].fila == a.col) {
                    ElementoEsparso p;
                    p.fila  = a.fila;
                    p.col   = buffer[k].col;
                    p.valor = a.valor * buffer[k].valor;
                    fwrite(&p, sizeof(ElementoEsparso), 1, fo);
                    parciales++;
                }
            }
        }
    }

    fclose(fa);
    fclose(fb);
    fclose(fo);
    return parciales;
}

/* Consolidación: sumar celdas repetidas */

/* Orden por fila y luego por columna */
static int comparar(const void *pa, const void *pb)
{
    const ElementoEsparso *x = pa;
    const ElementoEsparso *y = pb;
    if (x->fila != y->fila) return x->fila - y->fila;
    return x->col - y->col;
}

/* -Lee acumulado.bin en RAM, lo ordena y suma los que comparten (fila, col).
   -Escribe el resultado final en bin_final (con cabecera). */
static long consolidar(const char *acumulado, const char *bin_final,
                       Cabecera res)
{
    FILE *fi = abrir(acumulado, "rb");
    FILE *fo = abrir(bin_final, "wb");
    fwrite(&res, sizeof(Cabecera), 1, fo);

    /* cantidad de parciales = tamaño del archivo / tamaño de cada elemento */
    fseek(fi, 0, SEEK_END);
    long bytes = ftell(fi);
    rewind(fi);
    size_t n = (size_t)bytes / sizeof(ElementoEsparso);

    long resultantes = 0;
    if (n > 0) {
        ElementoEsparso *datos = malloc(n * sizeof(ElementoEsparso));
        if (datos == NULL) {
            fprintf(stderr, "No cabe acumulado.bin en RAM\n");
            exit(1);
        }
        fread(datos, sizeof(ElementoEsparso), n, fi);
        qsort(datos, n, sizeof(ElementoEsparso), comparar);

        size_t i = 0;
        while (i < n) {
            ElementoEsparso suma = datos[i];
            i++;
            while (i < n && datos[i].fila == suma.fila &&
                            datos[i].col  == suma.col) {
                suma.valor += datos[i].valor;
                i++;
            }
            fwrite(&suma, sizeof(ElementoEsparso), 1, fo);
            resultantes++;
        }
        free(datos);
    }

    fclose(fi);
    fclose(fo);
    return resultantes;
}

/* Paso 4: binario final -> matriz_axb.txt  */

static void bin_a_txt(const char *bin, const char *txt)
{
    FILE *fb = abrir(bin, "rb");
    FILE *ft = abrir(txt, "w");

    Cabecera cab;
    fread(&cab, sizeof(Cabecera), 1, fb);
    fprintf(ft, "%d %d\n", cab.filas, cab.cols);

    ElementoEsparso res;
    while (fread(&res, sizeof(ElementoEsparso), 1, fb) == 1)
        fprintf(ft, "%d %d %.2f\n", res.fila, res.col, res.valor);

    fclose(fb);
    fclose(ft);
}

// MAIN 

int main(int argc, char *argv[])
{
    int M, N, P;

    if (argc == 4) {
        M = atoi(argv[1]);
        N = atoi(argv[2]);
        P = atoi(argv[3]);
    } else {
        printf("Matriz A es MxN y matriz B es NxP (maximo %d)\n", MAX_DIM);
        printf("Ingrese M N P: ");
        if (scanf("%d %d %d", &M, &N, &P) != 3) {
            fprintf(stderr, "Entrada invalida\n");
            return 1;
        }
    }

    if (M < 1 || N < 1 || P < 1 || M > MAX_DIM || N > MAX_DIM || P > MAX_DIM) {
        fprintf(stderr, "M, N y P deben estar entre 1 y %d\n", MAX_DIM);
        return 1;
    }

    srand((unsigned)time(NULL));
    clock_t t0 = clock();

    // Paso 1
    long nnz_a = generar_matriz_txt("matriz_a.txt", M, N);
    long nnz_b = generar_matriz_txt("matriz_b.txt", N, P);
    printf("Paso 1: matriz_a.txt (%dx%d, %ld no nulos) y matriz_b.txt (%dx%d, %ld no nulos)\n",
           M, N, nnz_a, N, P, nnz_b);

    // Paso 2 
    txt_a_bin("matriz_a.txt", "matriz_a.bin");
    txt_a_bin("matriz_b.txt", "matriz_b.bin");
    printf("Paso 2: matriz_a.bin y matriz_b.bin creados\n");

    //Paso 3 
    Cabecera res;
    long parciales = multiplicar("matriz_a.bin", "matriz_b.bin",
                                 "acumulado.bin", &res);
    printf("Paso 3: %ld resultados parciales en acumulado.bin\n", parciales);

    long finales = consolidar("acumulado.bin", "matriz_axb.bin", res);
    printf("        consolidado: %ld celdas no nulas en matriz_axb.bin\n", finales);

    // Paso 4 
    bin_a_txt("matriz_axb.bin", "matriz_axb.txt");
    printf("Paso 4: matriz_axb.txt (%dx%d) escrito\n", res.filas, res.cols);

    printf("Tiempo total: %.2f s\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}