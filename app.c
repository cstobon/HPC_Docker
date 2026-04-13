// --- Compilación y ejecución ---

//@Autor: Edgar Tobon Sosa
//@Compilación: gcc -o lectura_file_2 lectura_file_2.c -lpthread 
//@Ejecución: ./lectura_file_2 <archivo_1.txt> <archivo_2.txt> <cadena>

// --- Bibliotecas ---  

#include <stdio.h>		// ** Libreria estandar para entrada y salida de datos  
#include <stdlib.h>		// ** Libreria para utilizar atoll
#include <pthread.h> 	// ** Libreria para trabajar con hilos
#include <fcntl.h>		// ** Libreria para trabajar con archivos 
#include <sys/mman.h>   // ** Libreria para mapear el archivo en memoria RAM
#include <sys/stat.h>   // ** Libreria para obtener informacion sobre archivos
#include <string.h>		// ** Libreria para la maniupulacion de cadenas
#include <unistd.h>		// ** Libreria para operar sobre archivos (Lectura)
#include <signal.h>     // ** Libreria para que un proceso envie señales a otros procesos
// -- Variables globales

#include<omp.h>			// ** Libreria par

#define NUM_HILOS 20  //Define el numero de hilos que se generarán



// -- Estructura que contiene los parámetos requeridos en la función que trabajara cada hilo --
//	- id_hilo: identificador del hilo
//	- id_archivo: identificador de archivo
//	- inicio_bloque: Puntero donde comezara la lectura el hilo
//	- tamano_bloque: Cuantos bytes le corresponde leer el hilo
//	- cadena_buscar: Cadena buscada dentro de los archivos 

typedef struct {
    int id_hilo;
    int id_archivo;             
    char *inicio_bloque;        
    size_t tamano_bloque;      
    const char *cadena_buscar;
} ThreadArgs;

// -- SECCIÓN CRITICA --
// ** Varible que indica cuando la cadena ya ha sido encontrada
// 0: FALSE
// 1: TRUE 
volatile int ya_encontrado = 0; //Volatile asegura que siempre se lea el valor actualizado

// -- Mecanismo de sincronización --
pthread_mutex_t mutex_bandera; //Proporna exclusión mutua 

// --- Función del Hilo ---
void* buscar_cadena(void* arg) {
    ThreadArgs *datos = (ThreadArgs*) arg; //Casting a la estructura ThreadArgs
    size_t len_cadena = strlen(datos->cadena_buscar); //Leemos la longitud de la cadena que buscamos
    
    // Recorremos el bloque asignado byte por byte
    for (size_t i = 0; i < datos->tamano_bloque; i++) {
        
        // Verificación de la Sección Crítica 
        if (ya_encontrado) {
            // Si otro hilo ya lo encontró, terminamos este hilo inmediatamente
            pthread_exit(NULL); 
        }

        // Lógica de búsqueda (comparación de memoria)
        // Verificamos si no nos salimos del bloque al comparar
        if (i + len_cadena <= datos->tamano_bloque) {
			// memcmp nos ayuda a comparar la cadena de busqueda con el mapeado del archivo que tenemos en memoria
            if (memcmp(datos->inicio_bloque + i, datos->cadena_buscar, len_cadena) == 0) {
                
                // --- INICIO SECCIÓN CRÍTICA ---
                pthread_mutex_lock(&mutex_bandera);
                if (!ya_encontrado) { 
                    ya_encontrado = 1; 
					write (3, "FOUND", 5); //Mecanismo de sincronización para finalizar la busqueda de los demás procesos
                    printf("\n[!!!] ENCONTRADO por Hilo %d (Archivo %d)\n", datos->id_hilo, datos->id_archivo);
                }
                pthread_mutex_unlock(&mutex_bandera);
                // --- FIN SECCIÓN CRÍTICA ---

				kill (getpid(), SIGTERM); //Finalizar el proceso               
                pthread_exit(NULL);
            }
        }
    }
    
    pthread_exit(NULL);
}

// --- Main ---
int main(int argc, char *argv[]) {
    // Debe recibir 3 argumentos: ./programa <ruta_arch1> <ruta_arch2> <cadena_a_buscar>
    if (argc != 8) {
        printf("Uso: %s <archivo_1> <archivo_2> <cadena> <offset_1> <size_1> <offset_2> <size_2>\n", argv[0]);
        return 1;
    }
    // Debe asignar numeros pares de hilos a partir de dos 
    if (NUM_HILOS < 2 || NUM_HILOS % 2 != 0) {
        fprintf(stderr, "Error: NUM_HILOS debe ser al menos 2 y un número par para dividir los archivos.\n");
        return 1;
    }

    const char *archivo_ruta_1 = argv[1];   // ** Nombre del archivo dos
    const char *archivo_ruta_2 = argv[2];   // ** Nombre del archivo uno
    const char *cadena = argv[3];		    // ** Cadena de busqueda	
	
	// --- Variables para medir el tiempo de ejecución
	struct timespec inicio, fin;    

	size_t offset_1 = atoll(argv[4]); // Cast String -> long long int
 	size_t tamano_1 = atoll(argv[5]); // Cast String -> long long int
 	size_t offset_2 = atoll(argv[6]); // Cast String -> long long int
 	size_t tamano_2 = atoll(argv[7]); // Cast String -> long long int

    // Variables para ambos archivos
    int fd_1, fd_2; //Variables que que almaceneran el identificador del archivo
    char *mapa_archivo_1 = NULL; //Mapeo de archivo al espacio de direcciones del proceso
    char *mapa_archivo_2 = NULL; //Mapeo de archivo al espacio de direcciones del proceso



    // --- Archivo 1 ---
    fd_1 = open(archivo_ruta_1, O_RDONLY);  //Asignación de valor entero con la operacion open para fd_1
    if (fd_1 == -1) {
        perror("Error al abrir el Archivo 1");
        return 1;
    }
	
	 // -- Alinear offset a la página del sistema --
     long page_size = sysconf(_SC_PAGE_SIZE); //Obtiene el tamaño de una pagina de memoria
     size_t offset_1_aligned = (offset_1 / page_size) * page_size;  //Alineación con el inicio de la pagina de memoria 
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

// -- Se replican las instrucciones para el archivo 2 --

    // --- Archivo 2 ---
    fd_2 = open(archivo_ruta_2, O_RDONLY);
    if (fd_2 == -1) {
        perror("Error al abrir el Archivo 2");
		munmap(mapa_base_1, tamano_1_adjusted); //Desmapear región de memoria
        close(fd_1);
        return 1;
    }

// Alinear offset a la página del sistema
    size_t offset_2_aligned = (offset_2 / page_size) * page_size;
    size_t offset_2_diff = offset_2 - offset_2_aligned;
	size_t tamano_2_adjusted = tamano_2 + offset_2_diff;


  char *mapa_base_2 = mmap(NULL, tamano_2_adjusted, PROT_READ, MAP_PRIVATE, fd_2, offset_2_aligned);
     if (mapa_base_2 == MAP_FAILED) {
         perror("Error al mapear memoria (mmap) para Archivo 2");
         close(fd_2);
         munmap(mapa_base_1, tamano_1_adjusted);
         close(fd_1);
         return 1;
     }

	mapa_archivo_2 = mapa_base_2 + offset_2_diff;

    // -- Hilos --
    pthread_t hilos[NUM_HILOS]; // Se define un vector de hilos 
    ThreadArgs args[NUM_HILOS]; // Se dfine un vecto para los argumentos de la función de cada hilo
    pthread_mutex_init(&mutex_bandera, NULL); // Se iniciliza el mutex (Mecanismo de sincronización)
    
    // La mitad de los hilos para cada archivo
    const int HILOS_POR_ARCHIVO = NUM_HILOS / 2; 

    printf("Iniciando %d hilos de búsqueda: %d para Archivo 1 y %d para Archivo 2.\n", 
           NUM_HILOS, HILOS_POR_ARCHIVO, HILOS_POR_ARCHIVO);

	clock_gettime(CLOCK_MONOTONIC, &inicio); // Medición del tiempo de ejecucion

    // --- Lógica para Archivo 1 (Hilos 0 a HILOS_POR_ARCHIVO - 1) ---
    size_t tamano_chunk_1 = tamano_1 / HILOS_POR_ARCHIVO; //Definimos e inicializamos el tamaño del archivo que leera cada hilo
    for (int i = 0; i < HILOS_POR_ARCHIVO; i++) {
        args[i].id_hilo = i;  // argumento id del hilo 
        args[i].id_archivo = 1; // Archivo 1 
        args[i].cadena_buscar = cadena; // argumento de cadena de busqueda
        
        args[i].inicio_bloque = mapa_archivo_1 + (i * tamano_chunk_1); // argumento de inicio de bloque para cada hilo
        
        // El último hilo del Archivo 1 toma el resto
        if (i == HILOS_POR_ARCHIVO - 1) {
            args[i].tamano_bloque = tamano_1 - (i * tamano_chunk_1);
        } else {
            args[i].tamano_bloque = tamano_chunk_1;
        }

		// -- Creación de los hilos --
        if (pthread_create(&hilos[i], NULL, buscar_cadena, (void*)&args[i]) != 0) {
            perror("Error creando hilo para Archivo 1");
            return 1;
        }
    }
	/*
		Repetimos las instrucciones para la creación de hilos correspondientes al archivo 2
	*/
	
    // --- Lógica para Archivo 2 (Hilos HILOS_POR_ARCHIVO a NUM_HILOS - 1) ---
    size_t tamano_chunk_2 = tamano_2 / HILOS_POR_ARCHIVO;
    for (int i = 0; i < HILOS_POR_ARCHIVO; i++) {
        // El índice del hilo en el array global (continúa desde el último del Archivo 1)
        int indice_hilo = i + HILOS_POR_ARCHIVO; 
        
        args[indice_hilo].id_hilo = indice_hilo;
        args[indice_hilo].id_archivo = 2; // Archivo 2
        args[indice_hilo].cadena_buscar = cadena;
        
        args[indice_hilo].inicio_bloque = mapa_archivo_2 + (i * tamano_chunk_2);
        
        // El último hilo del Archivo 2 toma el resto
        if (i == HILOS_POR_ARCHIVO - 1) {
            args[indice_hilo].tamano_bloque = tamano_2 - (i * tamano_chunk_2);
        } else {
            args[indice_hilo].tamano_bloque = tamano_chunk_2;
        }
	
		// -- Creación de los hilos --
        if (pthread_create(&hilos[indice_hilo], NULL, buscar_cadena, (void*)&args[indice_hilo]) != 0) {
            perror("Error creando hilo para Archivo 2");
            return 1;
        }
    }

    //Esperar a todos los hilos (Join)
    for (int i = 0; i < NUM_HILOS; i++) {
        pthread_join(hilos[i], NULL);
    }

    // 6. Limpieza
    pthread_mutex_destroy(&mutex_bandera);
    
    // Desmapear y cerrar el primer archivo
	munmap(mapa_base_1, tamano_1_adjusted);
    close(fd_1);
    
    // Desmapear y cerrar el segundo archivo
    munmap(mapa_base_2, tamano_2_adjusted);
	close(fd_2);

	// Salida para indicar si la cadena no se encuentra en ninguno de los archivos
    if (!ya_encontrado) {
        printf("\nBúsqueda finalizada: La cadena NO se encontró en ninguno de los archivos.\n");
    }

	//Asignación de tiempo final en la variable fin
	clock_gettime(CLOCK_MONOTONIC, &fin);
	double total = (fin.tv_sec - inicio.tv_sec) + (fin.tv_nsec - inicio.tv_nsec) / 1e9; //Tiempo total
	
	//Salida del proces
 	printf("\n=========================\n");
    printf("Tiempo total: %.6f s\n", total);
    printf("=========================\n");

    return 0;
}
