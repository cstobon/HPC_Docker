// --- Compilación y ejecución ---

//@Autor: Edgar Tobon Sosa
//@Compilación: mpicc -fopenmp -o buscador_hibrido app.c 
//@Ejecución: mpirun -np 2 ./buscador_hibrido <archivo> <cadena>

// --- Bibliotecas ---  
#include <mpi.h>        // ** Libreria para computacion distribuida (Maestro/Esclavo)
#include <stdio.h>      // ** Libreria estandar para entrada y salida de datos  
#include <stdlib.h>     // ** Libreria para utilizar atoll
#include <fcntl.h>      // ** Libreria para trabajar con archivos 
#include <sys/mman.h>   // ** Libreria para mapear el archivo en memoria RAM
#include <sys/stat.h>   // ** Libreria para obtener informacion sobre archivos
#include <string.h>     // ** Libreria para la maniupulacion de cadenas
#include <unistd.h>     // ** Libreria para operar sobre archivos (Lectura)
#include <time.h>
#include <omp.h>        // ** Libreria para hilos en CPU local

#define NUM_HILOS 20  //Define el numero de hilos locales que generará cada Nodo

int main(int argc, char *argv[]) {
    // --- 1. Inicialización de MPI ---
    MPI_Init(&argc, &argv);

    int mpi_rank, mpi_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank); // 0 = Maestro, >0 = Esclavos
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size); // Total de nodos en el clúster

    // Ahora solo recibimos el archivo y la cadena
    if (argc != 3) {
        if (mpi_rank == 0) printf("Uso: mpirun -np <nodos> %s <archivo> <cadena>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    const char *archivo_ruta_1 = argv[1];
    const char *cadena = argv[2];       
    size_t len_cadena = strlen(cadena);

    struct timespec inicio, fin;    
    if (mpi_rank == 0) clock_gettime(CLOCK_MONOTONIC, &inicio); // Solo el maestro toma el tiempo total

    // Arreglo para almacenar el Offset (índice 0) y el Tamaño (índice 1) que procesará este nodo
    long long limites[2]; 

    // --- 2. Lógica del Maestro (Repartir el trabajo) ---
    if (mpi_rank == 0) {
        struct stat st;
        if (stat(archivo_ruta_1, &st) == -1) {
            perror("El Maestro no pudo leer el archivo");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        long long total_size = st.st_size;
        long long chunk_base = total_size / mpi_size;
        long long residuo = total_size % mpi_size;

        for (int i = 0; i < mpi_size; i++) {
            long long chunk_offset = i * chunk_base;
            long long chunk_size = chunk_base;

            // El último nodo se lleva el residuo de la división
            if (i == mpi_size - 1) {
                chunk_size += residuo;
            } else {
                // Truco de Solapamiento: Si no es el último nodo, lee 'len_cadena - 1' bytes extras
                // para encontrar palabras que queden justo a la mitad del corte.
                chunk_size += (len_cadena - 1);
            }

            if (i == 0) {
                // El Maestro se auto-asigna su parte
                limites[0] = chunk_offset;
                limites[1] = chunk_size;
            } else {
                // El Maestro le envía los límites a los esclavos
                long long msj[2] = {chunk_offset, chunk_size};
                MPI_Send(msj, 2, MPI_LONG_LONG, i, 0, MPI_COMM_WORLD);
            }
        }
        printf("[Maestro] Archivo de %lld bytes repartido entre %d nodos.\n", total_size, mpi_size);
    } 
    // --- 3. Lógica del Esclavo (Recibir instrucciones) ---
    else {
        // Los esclavos esperan a recibir sus límites de memoria desde el Maestro
        MPI_Recv(limites, 2, MPI_LONG_LONG, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // --- 4. Procesamiento Local (Todos los nodos hacen esto con su fragmento asignado) ---
    size_t offset_1 = limites[0];
    size_t tamano_1 = limites[1];
    int repetition = 0; // Contador local de este nodo

    int fd_1 = open(archivo_ruta_1, O_RDONLY); 
    if (fd_1 != -1) {
        long page_size = sysconf(_SC_PAGE_SIZE); 
        size_t offset_1_aligned = (offset_1 / page_size) * page_size;  
        size_t offset_1_diff = offset_1 - offset_1_aligned; 
        size_t tamano_1_adjusted = tamano_1 + offset_1_diff; 

        char *mapa_base_1 = mmap(NULL, tamano_1_adjusted, PROT_READ, MAP_PRIVATE, fd_1, offset_1_aligned);
        
        if (mapa_base_1 != MAP_FAILED) {
            char *mapa_archivo_1 = mapa_base_1 + offset_1_diff;
            size_t limite_busqueda = tamano_1 < len_cadena ? 0 : tamano_1 - len_cadena;

            omp_set_num_threads(NUM_HILOS);

            #pragma omp parallel for schedule(static) reduction(+:repetition)
            for(size_t i = 0; i <= limite_busqueda ; i++){
                if( memcmp(mapa_archivo_1 + i, cadena, len_cadena) == 0 ){
                    repetition += 1;
                }
            }

            munmap(mapa_base_1, tamano_1_adjusted);
        } else {
            printf("[Nodo %d] Error en mmap.\n", mpi_rank);
        }
        close(fd_1);
    }

    printf("[Nodo %d] Fragmento analizado. Encontradas: %d\n", mpi_rank, repetition);

    // --- 5. Reducción Global MPI (Sumar los resultados de todo el clúster) ---
    int total_repetition_cluster = 0;
    
    // MPI_Reduce recolecta la variable 'repetition' de todos los nodos, 
    // las suma (MPI_SUM) y guarda el total en 'total_repetition_cluster' dentro del Rank 0.
    MPI_Reduce(&repetition, &total_repetition_cluster, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    // --- 6. Salida de Resultados (Solo el Maestro) ---
    if (mpi_rank == 0) {
        clock_gettime(CLOCK_MONOTONIC, &fin);
        double total = (fin.tv_sec - inicio.tv_sec) + (fin.tv_nsec - inicio.tv_nsec) / 1e9; 
        
        printf("\n===================================================\n");
        printf("RESULTADO CLUSTER: Cadena encontrada %d veces\n", total_repetition_cluster);
        printf("Tiempo total de procesamiento en LAN: %.6f s\n", total);
        printf("===================================================\n");
    }

    // Finalizar red MPI
    MPI_Finalize();
    return 0;
}
