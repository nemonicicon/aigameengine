#!/bin/bash
# agent.sh -- your AI's command-line tool, inside a cap. usage: agent.sh <command> [args]. A 4 GB Pi holds the game, the agent and a compile with very little to spare, and these tools have been reported to grow without limit,
# so this gives it a hard ceiling and a name that play.sh can freeze. AGENT_MEM and AGENT_CPU change the cap.
# The numbers: the game leaves about 1.9 GB and an agent takes 1 to 1.5 GB in use (reports/, section 11).
if [ "$#" -eq 0 ]; then echo "usage: agent.sh <your AI's command> [args]"; exit 1; fi
exec sudo systemd-run --scope --unit=gameagent --uid="$(id -u)" --gid="$(id -g)" --same-dir --setenv=HOME="$HOME" --setenv=PATH="$PATH" --setenv=TERM="$TERM" --setenv=COLUMNS="${COLUMNS:-80}" \
    -p MemoryMax="${AGENT_MEM:-1500M}" -p MemorySwapMax=256M -p CPUQuota="${AGENT_CPU:-200%}" -p Nice=10 "$@"
