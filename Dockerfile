# Imagen base ligera
FROM alpine:latest

# 1. Instalar dependencias necesarias
# build-base: gcc y herramientas de compilación
# openmpi y openmpi-dev: para la comunicación entre contenedores
# libgomp: librería necesaria para ejecutar OpenMP
# openssh: necesario para que mpirun se comunique entre nodos
RUN apk add --no-cache \
    build-base \
    openmpi \
    openmpi-dev \
    libgomp \
    openssh \
    bash

# 2. Configurar SSH para que los contenedores se hablen sin contraseña
# (Esencial para que el Maestro controle a los Esclavos)
RUN ssh-keygen -A && \
    ssh-keygen -t rsa -N "" -f /root/.ssh/id_rsa && \
    cp /root/.ssh/id_rsa.pub /root/.ssh/authorized_keys && \
    echo "StrictHostKeyChecking no" >> /etc/ssh/ssh_config

# 3. Directorio de trabajo
WORKDIR /app

# 4. Copiar tus programas
COPY app.c .
COPY cadenas.txt .

# 5. Compilar
# Compilamos el programa híbrido (MPI + OpenMP)
RUN mpicc -fopenmp app.c -o buscador_cadena

# 6. Exponer puerto SSH
EXPOSE 22

# 7. Script de inicio: Inicia el servidor SSH y se queda esperando
CMD ["/usr/sbin/sshd", "-D"]
