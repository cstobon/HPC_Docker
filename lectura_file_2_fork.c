//Edgar Tobon Sosa
//Compilación: gcc -o lectura_file_2_fork lectura_file_2_fork.c
//Ejecución: ./lectura_file_2 <archivo_1.txt> <archivo_2.txt> <cadena>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define NUM_PROCESOS 4   // Puedes modificarlo

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Uso: %s <archivo1> <archivo2> <cadena>\n", argv[0]);
        return 1;
    }

    // Obtener tamaño de ambos archivos
    struct stat sb1, sb2;
    if (stat(argv[1], &sb1) == -1) {
        perror("Error al obtener stats del archivo 1");
        return 1;
    }
    if (stat(argv[2], &sb2) == -1) {
        perror("Error al obtener stats del archivo 2");
        return 1;
    }
    
    size_t tamano_archivo_1 = sb1.st_size;
    size_t tamano_archivo_2 = sb2.st_size;
    
    printf("[PADRE] Archivo 1: %ld bytes, Archivo 2: %ld bytes\n", tamano_archivo_1, tamano_archivo_2);
    printf("[PADRE] Creando %d procesos\n", NUM_PROCESOS);

    // Pipe para comunicación
    int pipefd[2];
    pipe(pipefd);

    pid_t hijos[NUM_PROCESOS];

    for (int i = 0; i < NUM_PROCESOS; i++) {
        
        pid_t pid = fork();

        if (pid == 0) {
            // HIJO

            // Cerramos el extremo de lectura
            close(pipefd[0]);

            // Redirigimos la escritura del pipe al descriptor 3
            dup2(pipefd[1], 3);

            // Calcular la porción de cada archivo para este proceso
            size_t chunk_size_1 = tamano_archivo_1 / NUM_PROCESOS;
            size_t chunk_size_2 = tamano_archivo_2 / NUM_PROCESOS;
            
            size_t offset_1 = i * chunk_size_1;
            size_t offset_2 = i * chunk_size_2;
            
            // El último proceso toma el resto
            if (i == NUM_PROCESOS - 1) {
                chunk_size_1 = tamano_archivo_1 - offset_1;
                chunk_size_2 = tamano_archivo_2 - offset_2;
            }
            
            // Convertir offsets y tamaños a strings
            char offset_1_str[32], size_1_str[32];
            char offset_2_str[32], size_2_str[32];
            snprintf(offset_1_str, sizeof(offset_1_str), "%ld", offset_1);
            snprintf(size_1_str, sizeof(size_1_str), "%ld", chunk_size_1);
            snprintf(offset_2_str, sizeof(offset_2_str), "%ld", offset_2);
            snprintf(size_2_str, sizeof(size_2_str), "%ld", chunk_size_2);
            
            printf("[PROCESO %d] Archivo1: offset=%ld, size=%ld | Archivo2: offset=%ld, size=%ld\n",
                   i, offset_1, chunk_size_1, offset_2, chunk_size_2);

            // Pasamos al programa hijo: archivo1, archivo2, cadena, offset1, size1, offset2, size2
            execl("./lectura_file_2", "lectura_file_2", 
                  argv[1], argv[2], argv[3],
                  offset_1_str, size_1_str, offset_2_str, size_2_str, NULL);

            perror("Error ejecutando execl");
            exit(1);
        }

        // PADRE
        hijos[i] = pid;
    }

    // Cerramos extremo de escritura del pipe
    close(pipefd[1]);

    // PADRE ESPERA MENSAJE DEL PIPE
    char buffer[10] = {0};
    int leido = read(pipefd[0], buffer, sizeof(buffer));

    if (leido > 0) {
        printf("\n[PADRE] Mensaje recibido: %s\n", buffer);

        printf("[PADRE] Terminando procesos...\n");
        for (int i = 0; i < NUM_PROCESOS; i++) {
            kill(hijos[i], SIGTERM);
        }
    }

    // Esperar a los hijos
    for (int i = 0; i < NUM_PROCESOS; i++) {
        waitpid(hijos[i], NULL, 0);
    }

    printf("\n[PADRE] Finalizado\n");
    return 0;
}

