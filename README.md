# ejer_matriz_esparsa

Multiplicación de matrices esparsas (95 % ceros) en C, formato COO, gestionada con archivos de texto y binarios. SW303 - EPIS - UNI.

## Compilar y ejecutar

```
gcc -Wall -O2 -o matriz main.c
./matriz              # pide M N P por teclado
./matriz 1000 1000 1000
```

A es de M×N y B es de N×P (máximo 1000). El resultado es M×P.

## Flujo

| Paso | Archivo(s) | Descripción |
|------|------------|-------------|
| 1 | `matriz_a.txt`, `matriz_b.txt` | Generación aleatoria: 5 % de no nulos, sin posiciones repetidas |
| 2 | `matriz_a.bin`, `matriz_b.bin` | Conversión a binario con `fwrite` |
| 3 | `acumulado.bin` | Resultados parciales `(fila, col, a*b)`: cada A[i][k] se cruza con los B[k][j] |
| 3b | `matriz_axb.bin` | Consolidación: `qsort` + suma de celdas repetidas |
| 4 | `matriz_axb.txt` | Traducción del binario final a texto |

## Formatos

- **.txt**: primera línea `filas columnas`; luego una línea `fila columna valor` por cada elemento no nulo (índices desde 0).
- **.bin**: una cabecera `{int filas; int cols;}` seguida de estructuras `ElementoEsparso {int fila; int col; double valor;}`.
