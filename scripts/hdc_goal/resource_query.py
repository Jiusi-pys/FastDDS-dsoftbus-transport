"""Offline command construction; never executes a shell or reads device files."""
import shlex

MEMINFO_AWK = "awk '/^(MemTotal|MemAvailable):/ {print}' /proc/meminfo"


def meminfo_records(path='/proc/meminfo'):
    """Emit two original ordered records, only after a complete successful read.

    The target mksh distinguishes line-mode EOF (1) from read errors (2).
    An unterminated last line is rejected, including an unrelated trailing row.
    This is a kernel text-file contract, not an arbitrary-byte/NUL transport.
    Numeric values/units and all resource thresholds remain the receipt
    parser's responsibility. printf availability is already evidenced by the
    existing remote status markers; it need not be compiled into mksh.
    """
    return """(
seen=0
total=
available=
while :; do
    line=
    IFS= read -r line
    read_rc=$?
    case "$read_rc" in
        0) ;;
        1) case "$line" in '') break ;; *) exit 1 ;; esac ;;
        *) exit "$read_rc" ;;
    esac
    case "$line" in
        MemTotal:*) case "$seen" in 0) total=$line; seen=1 ;; *) exit 1 ;; esac ;;
        MemAvailable:*) case "$seen" in 1) available=$line; seen=2 ;; *) exit 1 ;; esac ;;
    esac
done
case "$seen" in
    2) printf '%s\\n' "$total" "$available" ;;
    *) exit 1 ;;
esac
) < """ + shlex.quote(str(path))


def replace_meminfo_query(command):
    """Change only the historical first AND-list command; reject other shapes."""
    if not command.startswith(MEMINFO_AWK+' && ') or command.count(MEMINFO_AWK) != 1:
        raise ValueError('FIXED_MEMINFO_QUERY_REQUIRED')
    return meminfo_records()+command[len(MEMINFO_AWK):]
