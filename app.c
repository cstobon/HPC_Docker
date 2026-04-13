// --- Compilación y ejecución ---

//@Autor: Edgar Tobon Sosa
//@Compilación: gcc -o lectura_file_2 lectura_file_2.c -lpthread 
//@Ejecución: ./lectura_file_2 <archivo_1.txt> <archivo_2.txt> <cadena>

// --- Bibliotecas ---  

#include <stdio.h>		// ** Libreria estandar para entrada y salida de datos  
#include <stdlib.h>		// ** Libreria para utilizar atoll
#include <fcntl.h>		// ** Libreria para trabajar con archivos 
#include <sys/mman.h>   // ** Libreria para mapear el archivo en memoria RAM
#include <sys/stat.h>   // ** Libreria para obtener informacion sobre archivos
#include <string.h>		// ** Libreria para la maniupulacion de cadenas
#include <unistd.h>		// ** Libreria para operar sobre archivos (Lectura)
#include <signal.h>     // ** Libreria para que un proceso envie señales a otros procesos
#include <time.h>
// -- Variables globales

#include<omp.h>			// ** Libreria par

#define NUM_HILOS 20  //Define el numero de hilos que se generarán


// -- SECCIÓN CRITICA --
// ** Varible que indica cuando la cadena ya ha sido encontrada
// 0: FALSE
// 1: TRUE 

int repetition = 0;
// --- Main ---
int main(int argc, char *argv[]) {
    // Debe recibir 3 argumentos: ./programa <ruta_arch1> <ruta_arch2> <cadena_a_buscar>
    if (argc != 5) {
        printf("Uso: %s <archivo_1> <cadena> <offset_1> <size_1>\n", argv[0]);
        return 1;
    }

    const char *archivo_ruta_1 = argv[1];   // ** Nombre del archivo
    const char *cadena = argv[2];		    // ** Cadena de busqueda	
	
	// --- Variables para medir el tiempo de ejecución
	struct timespec inicio, fin;    

	size_t offset_1 = atoll(argv[3]); // Cast String -> long long int
 	size_t tamano_1 = atoll(argv[4]); // Cast String -> long long int
    
	// Variables para ambos archivos
    int fd_1; //Variables que que almaceneran el identificador del archivo
    char *mapa_archivo_1 = NULL; //Mapeo de archivo al espacio de direcciones del proceso



    // --- Archivo 1 ---
    fd_1 = open(archivo_ruta_1, O_RDONLY);  //Asignación de valor entero con la operacion open para fd_1
    if (fd_1 == -1) {
        perror("Error: No puedo abrir el archivo");
        return( 1 );
    }
	
	 // -- Alinear offset a la página del sistema --
     long page_size = sysconf(_SC_PAGE_SIZE); //Obtiene el tamaño de una pagina de memoria
     size_t offset_1_aligned =(offset_1 / page_size) * page_size;  //Alineación con el inicio de la pagina de memoria 
     size_t offset_1_diff = offset_1 - offset_1_aligned; //Define donde empezar a comparar la cadena
     size_t tamano_1_adjusted = tamano_1 + offset_1_diff; //Ajusta los bytes leido ocasionado por la alineación de la pagina

	// -- Función clave mmap, para mapear el archivo al espacio de direcciones el proces --
	// NULL: Permite que el kernel decida donde ubicar el mapa en memoria
	// Tamaño de archivo: Cantidad de bytes a mapear
	// PROT_READ: Define el permiso de solo lectura al proceso
	// MAP_PRIVATE: Permite que el archivo original pemanezca inmutable
	// fd_1: descriptor del archivo, el archivo que se va a mapear
	// offset_1_alignment: offset desde que posición se va a mapear

	// Mapear solo la porción asignada del Archivo 1
     char *mapa_base_1 = mmap(NULL, tamano_1_adjusted, PROT_READ, MAP_PRIVATE, fd_1, offset_1_aligned);
     if (mapa_base_1 == MAP_FAILED) {
         perror("Error al mapear memoria (mmap) para Archivo 1");
         close(fd_1);
         return 1;
    }
	mapa_archivo_1 = mapa_base_1 + offset_1_diff;

	omp_set_num_threads(NUM_HILOS);

	size_t len_cadena = strlen(cadena);
	size_t limite_busqueda = tamano_1 < len_cadena ? 0 : tamano_1 - len_cadena;
	clock_gettime(CLOCK_MONOTONIC, &inicio); // Medición del tiempo de ejecucion

	#pragma omp parallel for schedule(static) reduction(+:repetition)
	for( size_t i = 0; i <= limite_busqueda ; i++){
		if( memcmp(mapa_archivo_1 + i, cadena, len_cadena) == 0 ){
			repetition += 1;
			
			#pragma omp critical
            {
                // %zu imprime el byte exacto
                // %.*s imprime exactamente 'len_cadena' caracteres, evitando que se desborde
                printf("[Hilo %d] Coincidencia en el byte %zu -> Texto: %.*s\n", 
                       omp_get_thread_num(), 
                       i, 
                       (int)len_cadena, 
                       mapa_archivo_1 + i);
            }

		}
	}
 
    // Desmapear y cerrar el primer archivo
	munmap(mapa_base_1, tamano_1_adjusted);
    close(fd_1);
    

	//Asignación de tiempo final en la variable fin
	clock_gettime(CLOCK_MONOTONIC, &fin);
	double total = (fin.tv_sec - inicio.tv_sec) + (fin.tv_nsec - inicio.tv_nsec) / 1e9; //Tiempo total

	
 	printf("\n=========================\n");
	printf("\nNumero total de coincidencias: %d\n", repetition);
    printf("Tiempo total: %.6f s\n", total);
    printf("=========================\n");

    return 0;
}
