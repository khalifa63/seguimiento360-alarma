# Pruebas de firmware que corren en el PC

Comprobaciones de logica pura que no necesitan un ESP32 ni el entorno de
ESP-IDF. Se compilan con `gcc` y se ejecutan directamente.

```bash
cd tests/firmware
./run.sh
```

## Por que existen

Un compilador de C acepta muchas cosas que no hacen lo que parece. El
validador de claim codes es el ejemplo: compilaba sin avisos, y su version en
Python del mismo algoritmo era correcta, pero la traduccion a C sumaba un
caracter de mas y **rechazaba todos los codigos validos**. Eso no se ve hasta
que se prueba con datos reales.

Pruebas de este tipo conviene escribirlas cuando la logica se puede aislar:
cuentan mas que compilar, porque compilar no dice si el resultado es correcto.
