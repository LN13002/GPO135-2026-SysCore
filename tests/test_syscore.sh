
#!/bin/sh
set -eu

# Seleccionar el ejecutable según el sistema operativo
if [ -n "${1:-}" ]; then
    program="$1"
elif [ -x "build/windows/x64/release/SysCore.exe" ]; then
    program="build/windows/x64/release/SysCore.exe"
else
    program="build/linux/x86_64/release/SysCore"
fi

# Verificar que el ejecutable exista y pueda ejecutarse
if [ ! -x "$program" ]; then
    printf 'No se encuentra el ejecutable: %s\n' "$program" >&2
    exit 1
fi

# Prueba 1: contador con mutex
counter_output=$("$program" contador)
printf '%s\n' "$counter_output" | grep -Fqx 'Contador final: 500000 (esperado: 500000)'
counter_workers=$(printf '%s\n' "$counter_output" | grep -Ec '^Hilo [1-5] terminó sus incrementos\.$')
[ "$counter_workers" -eq 5 ]
printf 'OK: contador, cinco hilos y valor final 500000.\n'

# Prueba 2: sincronización con variable de condición
condition_output=$("$program" condicion)
printf '%s\n' "$condition_output" | grep -Fqx 'Los cinco hilos están listos; se envía la señal de inicio.'

for id in 1 2 3 4 5; do
    printf '%s\n' "$condition_output" | grep -Fqx "Hilo $id recibió la señal de inicio."
done

printf 'OK: los cinco trabajadores recibieron la señal de inicio.\n'

# Prueba 3: ejecución conjunta
combined_output=$("$program" todo)
printf '%s\n' "$combined_output" | grep -Fqx '=== Ejercicio 1: contador con mutex ==='
printf '%s\n' "$combined_output" | grep -Fqx 'Contador final: 500000 (esperado: 500000)'
printf '%s\n' "$combined_output" | grep -Fqx '=== Ejercicio 2: sincronización con condición ==='
printf '%s\n' "$combined_output" | grep -Fqx 'Los cinco hilos están listos; se envía la señal de inicio.'

printf 'OK: ejecución conjunta de ambos ejercicios.\n'
printf '\nTodas las pruebas finalizaron correctamente.\n'
