#!/bin/bash
# Prepares the machine for timing runs, runs a command, and puts everything
# back afterwards:
#
#   ./bench-cpus.sh ./run-gtp.sh all --compare --chart
#   ./bench-cpus.sh ./run-benchmark.sh sort --compare
#   ./bench-cpus.sh --status        (no root needed)
#   ./bench-cpus.sh --restore       (after a run that was killed)
#
# Run it as yourself, not with sudo: it calls sudo itself and hands your PATH
# and LD_LIBRARY_PATH to the command (sudo drops them, and nom needs
# libLLVM-12.so, racket and the python with pandas/plotly from them).
#
# While the command runs:
# - the benchmark CPUs (BENCH_CPUS, default 0,1, two P-cores) use the
#   performance governor, with their minimum and maximum frequency both at
#   BENCH_FREQ_MHZ (default 2400, the cores' base frequency, which they can
#   hold whatever the temperature and power budget). The fixed maximum keeps
#   them from boosting, so turbo is left alone for the other CPUs.
# - every other process (system.slice, user.slice, init.scope: services, the
#   desktop, editors) may only use the other CPUs, and device interrupts go to
#   the other CPUs. The command runs as the user who called sudo, in its own
#   monnom-bench.slice that keeps every CPU, with RUNGTP_CPUS and
#   RUNBENCH_CPUS set to BENCH_CPUS so the run scripts pin nom there.
# - kernel threads are not moved; that needs isolcpus= and nohz_full= on the
#   kernel command line and a reboot.
# The previous settings are saved as a script in /run/monnom-bench-cpus and
# run again when the command exits, also on Ctrl-C.
CPUS="${BENCH_CPUS:-0,1}"
FREQ="${BENCH_FREQ_MHZ:-2400}"
STATE=/run/monnom-bench-cpus
SLICES=(system.slice user.slice init.scope)
SYS=/sys/devices/system/cpu

# prints each CPU of the list $1 (e.g. 0-3,8) on its own line
benchcpus_expand() {
  local part
  for part in ${1//,/ }; do
    if [[ "$part" == *-* ]]; then seq "${part%-*}" "${part#*-}"; else echo "$part"; fi
  done
}

ONLINE="$(cat $SYS/online)"
mapfile -t BENCH < <(benchcpus_expand "$CPUS")
OTHERS="$(comm -23 <(benchcpus_expand "$ONLINE" | sort) \
                   <(printf '%s\n' "${BENCH[@]}" | sort) | sort -n | paste -sd,)"

benchcpus_status() {
  local c p irq n=0 total=0 cpu names
  for c in "${BENCH[@]}"; do
    p=$SYS/cpu$c/cpufreq
    echo "cpu$c: $(cat $p/scaling_governor), $(( $(cat $p/scaling_min_freq) / 1000 ))-$(( $(cat $p/scaling_max_freq) / 1000 )) MHz, now $(( $(cat $p/scaling_cur_freq) / 1000 )) MHz"
  done
  echo "turbo disabled: $(cat $SYS/intel_pstate/no_turbo 2>/dev/null || echo n/a)"
  for p in "${SLICES[@]}"; do
    # the kernel's cpuset, which can differ from what systemd reports
    echo "$p may use CPUs: $(cat /sys/fs/cgroup/$p/cpuset.cpus.effective)$(systemctl show -p AllowedCPUs --value "$p" | sed 's/^$/ (systemd: all)/; s/^[^ (].*/ (systemd: &)/')"
  done
  # only interrupts with a device behind them (a handler directory); unused
  # legacy lines claim every CPU but never fire
  names=()
  for irq in /proc/irq/[0-9]*; do
    compgen -G "$irq/*/" > /dev/null || continue
    total=$((total + 1))
    for cpu in $(benchcpus_expand "$(cat "$irq/effective_affinity_list" 2>/dev/null)"); do
      if printf '%s\n' "${BENCH[@]}" | grep -qx "$cpu"; then
        n=$((n + 1))
        names+=("$(basename "$(compgen -G "$irq/*/" | head -1)")")
        break
      fi
    done
  done
  echo "device interrupts handled on CPUs $CPUS: $n of $total${names[*]:+ (${names[*]})}"
  if [ -e "$STATE" ]; then echo "benchmark settings are in place"
  else echo "benchmark settings are not in place"; fi
}

# writes the commands that put the current settings back into $STATE
benchcpus_save() {
  local c p s v irq
  {
    for c in "${BENCH[@]}"; do
      p=$SYS/cpu$c/cpufreq
      # the maximum first, so it never drops below the minimum
      echo "echo $(cat $p/scaling_max_freq) > $p/scaling_max_freq"
      echo "echo $(cat $p/scaling_min_freq) > $p/scaling_min_freq"
      echo "echo $(cat $p/scaling_governor) > $p/scaling_governor"
    done
    for s in "${SLICES[@]}"; do
      v="$(systemctl show -p AllowedCPUs --value "$s")"
      echo "systemctl set-property --runtime $s AllowedCPUs=$v"
      # systemd does not write an empty AllowedCPUs= back to the kernel, so
      # the slice would stay off the benchmark CPUs; empty means the parent's
      echo "echo '$v' > /sys/fs/cgroup/$s/cpuset.cpus"
    done
    echo "echo $(cat /proc/irq/default_smp_affinity) > /proc/irq/default_smp_affinity"
    for irq in /proc/irq/[0-9]*; do
      echo "echo $(cat "$irq/smp_affinity_list") > $irq/smp_affinity_list 2> /dev/null"
    done
    echo "rm -f $STATE"
  } > "$STATE"
}

benchcpus_apply() {
  local c p s irq mask=0
  for c in "${BENCH[@]}"; do
    p=$SYS/cpu$c/cpufreq
    if [ $((FREQ * 1000)) -lt "$(cat $p/cpuinfo_min_freq)" ] ||
       [ $((FREQ * 1000)) -gt "$(cat $p/cpuinfo_max_freq)" ]; then
      echo "ERROR: cpu$c cannot run at $FREQ MHz"
      return 1
    fi
    echo performance > $p/scaling_governor
    echo $((FREQ * 1000)) > $p/scaling_max_freq
    echo $((FREQ * 1000)) > $p/scaling_min_freq
  done
  for s in "${SLICES[@]}"; do
    systemctl set-property --runtime "$s" AllowedCPUs="$OTHERS"
  done
  for c in $(benchcpus_expand "$OTHERS"); do mask=$((mask | 1 << c)); done
  printf '%x\n' $mask > /proc/irq/default_smp_affinity
  # some interrupts (e.g. per-CPU timers) cannot move; they are skipped
  for irq in /proc/irq/[0-9]*; do
    echo "$OTHERS" > "$irq/smp_affinity_list" 2> /dev/null
  done
  return 0
}

case "$1" in
  --status) benchcpus_status; exit 0 ;;
  --restore)
    if [ ! -e "$STATE" ]; then
      # a slice systemd leaves unrestricted can still be restricted in the
      # kernel, left behind by an older version of this script
      stuck=()
      for s in "${SLICES[@]}"; do
        [ -z "$(systemctl show -p AllowedCPUs --value "$s")" ] &&
          [ -n "$(cat /sys/fs/cgroup/$s/cpuset.cpus)" ] && stuck+=("$s")
      done
      [ ${#stuck[@]} -eq 0 ] && { echo "nothing to restore"; exit 0; }
      echo "== giving every CPU back to ${stuck[*]}"
      for s in "${stuck[@]}"; do
        echo | sudo tee /sys/fs/cgroup/$s/cpuset.cpus > /dev/null
      done
      benchcpus_status; exit 0
    fi
    [ "$EUID" -eq 0 ] || exec sudo "$(realpath "$0")" --restore
    bash "$STATE"; benchcpus_status; exit 0 ;;
  ""|-h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 1 ;;
esac

# as yourself: go through sudo, passing the environment the command needs
if [ "$EUID" -ne 0 ]; then
  exec sudo env BENCHCPUS_PATH="$PATH" BENCHCPUS_LD="$LD_LIBRARY_PATH" \
    BENCH_CPUS="$CPUS" BENCH_FREQ_MHZ="$FREQ" "$(realpath "$0")" "$@"
fi
if [ -z "$SUDO_USER" ] || [ -z "${BENCHCPUS_PATH+x}" ]; then
  echo "ERROR: run this as yourself, without sudo (it calls sudo itself and" \
       "keeps your PATH and LD_LIBRARY_PATH): $0 $*"
  exit 1
fi
if [ -z "$OTHERS" ]; then
  echo "ERROR: BENCH_CPUS=$CPUS leaves no CPU for everything else"
  exit 1
fi
if [ -e "$STATE" ]; then
  echo "ERROR: the settings of an earlier run are still in place;" \
       "put them back with: $0 --restore"
  exit 1
fi

benchcpus_save
trap 'bash "$STATE"; echo "== settings restored"' EXIT
trap 'exit 130' INT TERM
benchcpus_apply || exit 1
echo "== CPUs $CPUS fixed at $FREQ MHz and kept for the benchmark; everything else on CPUs $OTHERS"
benchcpus_status
echo "== running as $SUDO_USER: $*"
systemd-run --scope --quiet --slice=monnom-bench \
  runuser -u "$SUDO_USER" -- env PATH="$BENCHCPUS_PATH" \
    LD_LIBRARY_PATH="$BENCHCPUS_LD" RUNGTP_CPUS="$CPUS" RUNBENCH_CPUS="$CPUS" \
    "$@"
