#!/system/bin/sh
# Bounded read-only observer. No signals, native APIs, or application delay.
[ "$#" -ge 4 ] && [ "$#" -le 5 ] || exit 2
run=$1
target_pid=$2
root=$3
side=$4
main_member=${5:-public_socket_dds_experiment}
[ "${#run}" -eq 16 ] || exit 2
case "$run" in ''|*[!0-9a-f]*) exit 2 ;; esac
case "$target_pid" in ''|0*|*[!0-9]*) exit 2 ;; esac
[ "${#target_pid}" -le 10 ] || exit 2
case "$side" in A|B) ;; *) exit 2 ;; esac
[ "$root" = "/data/local/tmp/mdds_dds_${run}_${side}" ] || exit 2
[ -d "$root" ] && [ -w "$root" ] || exit 2
case "$main_member" in public_socket_dds_experiment|mdds_installed_consumer|ordinary_ros2_transport_consumer|fastdds_dsoftbus_dds214_baseline) ;; *) exit 2 ;; esac
set -f
read_starttime() {
    IFS= read -r stat_line < "$1" || return 1
    stat_fields=${stat_line##*) }
    [ "$stat_fields" != "$stat_line" ] || return 1
    set -- $stat_fields
    [ "$#" -ge 20 ] || return 1
    shift 19
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    printf '%s' "$1"
}
status=ATTEMPT_LIMIT
attempt=0
starttime=UNOBSERVED
while [ "$attempt" -lt 128 ]; do
    attempt=$((attempt + 1))
    if [ ! -r "/proc/$target_pid/stat" ]; then status=PROCESS_EXITED; break; fi
    cat "/proc/$target_pid/stat" > "$root/load-stat-before.txt" || break
    readlink "/proc/$target_pid/exe" > "$root/load-exe.txt" || continue
    readlink "/proc/$target_pid/cwd" > "$root/load-cwd.txt" || continue
    cat "/proc/$target_pid/cmdline" > "$root/load-cmdline.txt" || continue
    cat "/proc/$target_pid/environ" > "$root/load-environ.txt" || continue
    cat "/proc/$target_pid/maps" > "$root/load-maps.txt" || continue
    cat /proc/uptime > "$root/load-uptime.txt" || continue
    cat "/proc/$target_pid/stat" > "$root/load-stat-after.txt" || continue
    starttime=$(read_starttime "$root/load-stat-before.txt") || continue
    endtime=$(read_starttime "$root/load-stat-after.txt") || continue
    [ "$starttime" = "$endtime" ] || continue
    [ "$(cat "$root/load-exe.txt")" = "$root/$main_member" ] || continue
    complete=1
    while IFS= read -r required; do
        found=0
        while read -r map_address map_permissions map_offset map_device map_inode map_path; do
            if [ "$map_path" = "$required" ]; then found=1; break; fi
        done < "$root/load-maps.txt"
        if [ "$found" -ne 1 ]; then complete=0; break; fi
    done < "$root/required-load-paths.txt"
    if [ "$complete" -eq 1 ]; then status=COMPLETE; break; fi
done
printf 'status=%s\nrun=%s\nside=%s\npid=%s\nstarttime=%s\nattempts=%s\nvector_format=NUL_V1\n' "$status" "$run" "$side" "$target_pid" "$starttime" "$attempt" > "$root/load-status.txt"
[ "$status" = COMPLETE ]
