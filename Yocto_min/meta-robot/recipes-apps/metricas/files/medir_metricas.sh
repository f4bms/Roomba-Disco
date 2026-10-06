#!/bin/sh
# - Genera las métricas de recursos del proyecto Roomba-Disco -
#
# Corre en la Rasp. Usa busybox, /proc, /sys y systemd.
#
# Como usarlo: medir_metricas.sh [-d segundos] [-i intervalo] [-o archivo] [-p "proceso1 proceso2"]
#   -d  duración del muestreo de CPU/RAM            (default: 10)
#   -i  intervalo entre muestras                    (default: 2)
#   -o  archivo .txt de salida                      (default: /home/root/metricas_<fecha>.txt)
#   -p  procesos a medir (nombre en /proc/PID/comm) (default: "logica servidor mpg123")

DURACION=10
INTERVALO=2
SALIDA=""
PROCS="logica servidor mpg123"
SERVICIO_CONTROL="servidor.service"   # servicio que define SO
LIMITE_ROOTFS_MB=200
LIMITE_ARRANQUE_S=15

uso() {
    echo "Uso: $0 [-d segundos] [-i intervalo] [-o archivo] [-p \"proc1 proc2\"]"
}

while getopts "d:i:o:p:h" op; do
    case $op in
        d) DURACION=$OPTARG ;;
        i) INTERVALO=$OPTARG ;;
        o) SALIDA=$OPTARG ;;
        p) PROCS=$OPTARG ;;
        *) uso; exit 1 ;;
    esac
done

if [ -z "$SALIDA" ]; then
    DIR=/home/root
    [ -w "$DIR" ] || DIR=/tmp
    SALIDA="$DIR/metricas_$(date +%Y%m%d_%H%M%S).txt"
fi

# ------------------ utilidades ---------------------------------------------- 
# PAsa de kB -> MB con 2 decimales
mb() { printf '%d.%02d' $(($1 / 1024)) $((($1 % 1024) * 100 / 1024)); }
# Pasa el valor*10 -> "X.Y"
dec() { printf '%d.%d' $(($1 / 10)) $(($1 % 10)); }
# convierte los ms y los escribe: milisegundos -> "S.mmm" (Ejemplo: 4213ms -> 4.213)
seg() { printf '%d.%03d' $(($1 / 1000)) $(($1 % 1000)); }
# así se vería: campo_kb <archivo> <clave>  (p. ej. /proc/meminfo MemTotal)
campo_kb() {
    while read -r k v u; do
        if [ "$k" = "$2:" ]; then echo "$v"; return; fi
    done < "$1"
}
# pid_de <nombre>: primer PID cuyo comm coincida
#busca el PID de un proceso por el nombre
pid_de() {
    for d in /proc/[0-9]*; do
        read -r c < "$d/comm" 2>/dev/null || continue
        if [ "$c" = "$1" ]; then echo "${d#/proc/}"; return; fi
    done
}
# En el caso de la rasp por su reloj: 1 jiffy = 10 ms
# jiffies_pid <pid>: utime + stime
#devuelve cuantos jiffies de CPU consumió un proceso
jiffies_pid() {
    s=$(cat "/proc/$1/stat" 2>/dev/null) || { echo 0; return; }
    resto=${s#*) }
    set -- $resto
    echo $((${12} + ${13}))
}
# leer_cpu: deja CPU_TOTAL y CPU_BUSY (jiffies de todos los núcleos)
leer_cpu() {
    read -r _ c_u c_n c_s c_i c_w c_irq c_sirq c_st _ < /proc/stat
    CPU_BUSY=$((c_u + c_n + c_s + c_irq + c_sirq + c_st))
    CPU_TOTAL=$((CPU_BUSY + c_i + c_w))
}
#lee la temperatura del SoC en milésimas de grado (Ej 51300 = 51.3°C)
temperatura10() {
    t=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null) || { echo ""; return; }
    echo $((t / 100))
}

# ----------- secciones -------------------------------------------------------- 
sec_sistema() {
    echo "=============================================================="
    echo " ~~~~~   MÉTRICAS - Roomba-Disco   ~~~~~"
    echo "=============================================================="
	echo "--------------------------------------------------------------"
    echo " SISTEMA ACTUAL:"
    echo "--------------------------------------------------------------"
    echo "Fecha (de la rasp):  $(date)"
    modelo=n/d
    [ -r /proc/device-tree/model ] && modelo=$(tr -d '\0' < /proc/device-tree/model)
    echo "Placa:           $modelo"
    echo "Kernel:          $(uname -sr) ($(uname -m))"
    echo "Núcleos:         $(grep -c '^processor' /proc/cpuinfo)"
    echo "Uptime:          $(cut -d' ' -f1 /proc/uptime) s"
    echo "Carga (1/5/15):  $(cut -d' ' -f1-3 /proc/loadavg)"
    echo
}

sec_rootfs() {
    echo "--------------------------------------------------------------"
    echo " 1. TAMAÑO DEL ROOTFS"
    echo "--------------------------------------------------------------"
    set -- $(df -k / | tail -n 1)
    total=$2; usado=$3
	#uso en kb de la partición root
    echo "Partición raíz (df -k /): total $(mb "$total") MB, usado $(mb "$usado") MB"
    du_total=$(du -xsk / 2>/dev/null | cut -f1)
    echo "Contenido real (du -xsk /): $(mb "$du_total") MB"
    if [ "$((usado / 1024))" -le "$LIMITE_ROOTFS_MB" ]; then
        echo "Referencia <= ${LIMITE_ROOTFS_MB} MB: CUMPLE EL LIMITE"
    else
        echo "Referencia <= ${LIMITE_ROOTFS_MB} MB: EXCEDE EL LIMITE (ver README)"
    fi
    echo
    echo "Desglose por directorio (MB):"
    for d in /usr /etc /var /opt /home /lib /bin /sbin; do
        [ -d "$d" ] && [ ! -L "$d" ] || continue
        k=$(du -xsk "$d" 2>/dev/null | cut -f1)
        printf '  %-8s %s\n' "$d" "$(mb "$k")"
    done
    echo
    echo "10 subdirectorios más grandes de /usr (kB):"
    du -xk /usr 2>/dev/null | sort -rn | head -n 10 | sed 's/^/  /'
    if mountpoint -q /boot 2>/dev/null || grep -q ' /boot ' /proc/mounts; then
        set -- $(df -k /boot | tail -n 1)
        echo
        echo "Partición /boot (aparte, no cuenta como rootfs): usado $(mb "$3") MB de $(mb "$2") MB"
    fi
    echo
}

sec_arranque() {
    echo "--------------------------------------------------------------"
    echo " 2. TIEMPO DE ARRANQUE"
    echo "--------------------------------------------------------------"
    if ! command -v systemctl >/dev/null 2>&1; then
        echo "systemctl no disponible: no se puede medir el arranque."
        echo
        return
    fi
    echo "Estado de systemd: $(systemctl is-system-running 2>/dev/null)"
    echo "Unidades fallidas: $(systemctl --failed --no-legend 2>/dev/null | wc -l)"
    echo
    if command -v systemd-analyze >/dev/null 2>&1; then
        echo "\$ systemd-analyze time"
        systemd-analyze time 2>&1
        echo
        echo "\$ systemd-analyze critical-chain $SERVICIO_CONTROL"
        systemd-analyze critical-chain "$SERVICIO_CONTROL" --no-pager 2>&1
        echo
        echo "\$ systemd-analyze blame (10 más lentas)"
        systemd-analyze blame --no-pager 2>&1 | head -n 10
        echo
    else
        echo "(systemd-analyze no está en la imagen; se usan solo timestamps de systemctl)"
        echo
    fi
    # Timestamps: microsegundos desde que arrancó el kernel
    userspace=$(systemctl show -p UserspaceTimestampMonotonic --value 2>/dev/null)
    finish=$(systemctl show -p FinishTimestampMonotonic --value 2>/dev/null)
    echo "Inicio de userspace (desde kernel):     $(seg $((${userspace:-0} / 1000))) s"
    echo "Fin de arranque de systemd:             $(seg $((${finish:-0} / 1000))) s"
    listo_ms=""
    for u in logica.service "$SERVICIO_CONTROL"; do
        estado=$(systemctl is-active "$u" 2>/dev/null)
        ts=$(systemctl show -p ActiveEnterTimestampMonotonic --value "$u" 2>/dev/null)
        if [ "$estado" = "active" ] && [ -n "$ts" ] && [ "$ts" -gt 0 ]; then
            echo "$u activo a los: $(seg $((ts / 1000))) s desde el inicio del kernel"
            [ "$u" = "$SERVICIO_CONTROL" ] && listo_ms=$((ts / 1000))
        else
            echo "$u: estado '$estado' (no se puede calcular)"
        fi
    done
    echo
    if [ -n "$listo_ms" ]; then
        if [ "$listo_ms" -le $((LIMITE_ARRANQUE_S * 1000)) ]; then
            echo "Kernel -> $SERVICIO_CONTROL operativo: $(seg "$listo_ms") s. Referencia <= ${LIMITE_ARRANQUE_S} s: CUMPLE"
        else
            echo "Kernel -> $SERVICIO_CONTROL operativo: $(seg "$listo_ms") s. Referencia <= ${LIMITE_ARRANQUE_S} s: EXCEDE (justificar)"
        fi
    fi
    echo "NOTA: no incluye el tiempo de firmware/bootloader de la rasp antes del kernel"
    echo
}

sec_recursos() {
    echo "--------------------------------------------------------------"
    echo " 3. RAM Y CPU EN OPERACIÓN (${DURACION} s, cada ${INTERVALO} s)"
    echo "--------------------------------------------------------------"
    echo "Procesos objetivo: $PROCS"
#    medir con navegación autónoma, música y servidor web activos a la vez
    echo

    ncpu=$(grep -c '^processor' /proc/cpuinfo)
    mem_total=$(campo_kb /proc/meminfo MemTotal)

    # PIDs y jiffies iniciales por proceso
    for p in $PROCS; do
        pid=$(pid_de "$p")
        eval "PID0_$p=\$pid"
        if [ -n "$pid" ]; then eval "J0_$p=\$(jiffies_pid \$pid)"; fi
    done

    leer_cpu
    T0=$CPU_TOTAL
    prevT=$CPU_TOTAL; prevB=$CPU_BUSY
    elapsed=0; muestras=0
    sum_cpu=0; max_cpu=0; sum_mem=0; max_mem=0

    printf '%8s %9s %16s %9s\n' "t(s)" "CPU(%)" "RAM usada(MB)" "Temp(C)"
    while [ "$elapsed" -lt "$DURACION" ]; do
        sleep "$INTERVALO"
        elapsed=$((elapsed + INTERVALO))
        leer_cpu
        dT=$((CPU_TOTAL - prevT)); dB=$((CPU_BUSY - prevB))
        prevT=$CPU_TOTAL; prevB=$CPU_BUSY
        if [ "$dT" -gt 0 ]; then c10=$((dB * 1000 / dT)); else c10=0; fi
        disp=$(campo_kb /proc/meminfo MemAvailable)
        usada=$((mem_total - disp))
        temp10=$(temperatura10)
        printf '%8d %9s %16s %9s\n' "$elapsed" "$(dec "$c10")" "$(mb "$usada")" \
            "$([ -n "$temp10" ] && dec "$temp10" || echo n/d)"
        muestras=$((muestras + 1))
        sum_cpu=$((sum_cpu + c10)); sum_mem=$((sum_mem + usada))
        [ "$c10" -gt "$max_cpu" ] && max_cpu=$c10
        [ "$usada" -gt "$max_mem" ] && max_mem=$usada
    done
    T1=$CPU_TOTAL
    echo
    if [ "$muestras" -gt 0 ]; then
        echo "CPU del sistema: promedio $(dec $((sum_cpu / muestras))) %, máximo $(dec "$max_cpu") %"
        echo "RAM usada (Total - MemAvailable): promedio $(mb $((sum_mem / muestras))) MB, máximo $(mb "$max_mem") MB, de $(mb "$mem_total") MB totales"
    fi
    echo
    echo "Por proceso (CPU como % de UN núcleo; RSS/PSS en MB):"
    printf '  %-10s %6s %9s %9s %9s %9s\n' "proceso" "PID" "CPU(%)" "RSS" "RSS pico" "PSS"
    for p in $PROCS; do
        pid0=$(eval "echo \$PID0_$p")
        pid1=$(pid_de "$p")
        if [ -z "$pid1" ]; then
            printf '  %-10s %s\n' "$p" "no está corriendo"
            continue
        fi
        if [ "$pid0" != "$pid1" ]; then
            printf '  %-10s %6s  (arrancó/reinició durante la medición; CPU no calculable)' "$p" "$pid1"
            cpu_txt="n/d"
        else
            j0=$(eval "echo \$J0_$p"); j1=$(jiffies_pid "$pid1")
            dt=$((T1 - T0))
            if [ "$dt" -gt 0 ]; then
                cpu_txt=$(dec $(((j1 - j0) * 1000 * ncpu / dt)))
            else
                cpu_txt="n/d"
            fi
            printf '  %-10s %6s %9s' "$p" "$pid1" "$cpu_txt"
        fi
        rss=$(campo_kb "/proc/$pid1/status" VmRSS)
        hwm=$(campo_kb "/proc/$pid1/status" VmHWM)
        pss=$(campo_kb "/proc/$pid1/smaps_rollup" Pss)
        printf ' %9s %9s %9s\n' "$(mb "${rss:-0}")" "$(mb "${hwm:-0}")" "$(mb "${pss:-0}")"
    done
    echo
}

# ----------------------------------------------------------------------- main
main() {
    sec_sistema
    sec_rootfs
    sec_arranque
    sec_recursos
    echo "~~~~~ Fin de las métricas calculadas ~~~~~."
}

main 2>&1 | tee "$SALIDA"
echo
echo "Resultados guardados en: $SALIDA"
