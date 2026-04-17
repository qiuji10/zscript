#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  tools/actions_cache_tool.sh list [--repo owner/repo] [--ref refs/heads/main] [--key prefix]
  tools/actions_cache_tool.sh delete-all [--repo owner/repo] [--ref refs/heads/main] [--key prefix] [--confirm]
  tools/actions_cache_tool.sh delete-before [--repo owner/repo] [--ref refs/heads/main] [--key prefix] \
    --before 2026-04-15T00:00:00Z [--field createdAt|lastAccessedAt] [--confirm]

Examples:
  tools/actions_cache_tool.sh list --repo qiuji10/zscript
  tools/actions_cache_tool.sh delete-all --repo qiuji10/zscript --confirm
  tools/actions_cache_tool.sh delete-before --repo qiuji10/zscript \
    --before 2026-04-15T00:00:00Z --field lastAccessedAt --confirm

Notes:
  - Requires GitHub CLI: gh
  - Requires prior authentication: gh auth login
  - delete-before compares ISO-8601 timestamps lexicographically, so use UTC
    timestamps like 2026-04-15T00:00:00Z.
EOF
}

require_gh() {
  if ! command -v gh >/dev/null 2>&1; then
    echo "Missing required command: gh" >&2
    exit 1
  fi
}

require_auth() {
  if ! gh auth status >/dev/null 2>&1; then
    echo "GitHub CLI is not authenticated. Run: gh auth login" >&2
    exit 1
  fi
}

infer_repo() {
  local url repo
  if ! url="$(git remote get-url origin 2>/dev/null)"; then
    return 1
  fi
  if [[ "$url" == git@github.com:* ]]; then
    repo="${url#git@github.com:}"
  elif [[ "$url" == *github.com/* ]]; then
    repo="${url#*github.com/}"
  else
    return 1
  fi
  repo="${repo%.git}"
  printf '%s\n' "$repo"
}

COMMAND="${1:-}"
if [[ -z "$COMMAND" ]]; then
  usage
  exit 1
fi
if [[ "$COMMAND" == "-h" || "$COMMAND" == "--help" ]]; then
  usage
  exit 0
fi
shift

REPO=""
REF=""
KEY=""
BEFORE=""
FIELD="lastAccessedAt"
CONFIRM=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo)
      REPO="${2:-}"
      shift 2
      ;;
    --ref)
      REF="${2:-}"
      shift 2
      ;;
    --key)
      KEY="${2:-}"
      shift 2
      ;;
    --before)
      BEFORE="${2:-}"
      shift 2
      ;;
    --field)
      FIELD="${2:-}"
      shift 2
      ;;
    --confirm)
      CONFIRM=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

require_gh
require_auth

if [[ -z "$REPO" ]]; then
  REPO="$(infer_repo || true)"
fi
if [[ -z "$REPO" ]]; then
  echo "Unable to infer repository. Pass --repo owner/repo." >&2
  exit 1
fi

if [[ "$COMMAND" == "delete-before" ]]; then
  if [[ -z "$BEFORE" ]]; then
    echo "delete-before requires --before" >&2
    exit 1
  fi
  if [[ "$FIELD" != "createdAt" && "$FIELD" != "lastAccessedAt" ]]; then
    echo "--field must be one of: createdAt, lastAccessedAt" >&2
    exit 1
  fi
fi

LIST_ARGS=(cache list --repo "$REPO" --limit 1000 --json id,key,ref,createdAt,lastAccessedAt,sizeInBytes)
if [[ -n "$REF" ]]; then
  LIST_ARGS+=(--ref "$REF")
fi
if [[ -n "$KEY" ]]; then
  LIST_ARGS+=(--key "$KEY")
fi

ROWS="$(
  gh "${LIST_ARGS[@]}" --template \
    '{{range .}}{{printf "%.0f\t%s\t%s\t%s\t%s\t%.0f\n" .id .key .ref .createdAt .lastAccessedAt .sizeInBytes}}{{end}}'
)"

print_rows() {
  if [[ -z "$1" ]]; then
    echo "No caches found."
    return
  fi
  while IFS=$'\t' read -r id key ref created last_accessed size; do
    [[ -z "$id" ]] && continue
    echo "id=$id key=$key ref=$ref created=$created last_accessed=$last_accessed size=$size"
  done <<< "$1"
}

case "$COMMAND" in
  list)
    print_rows "$ROWS"
    ;;
  delete-all)
    print_rows "$ROWS"
    MATCHED_COUNT="$(printf '%s\n' "$ROWS" | awk 'NF > 0 {count++} END {print count+0}')"
    echo "Matched $MATCHED_COUNT cache(s)."
    if [[ "$CONFIRM" -ne 1 ]]; then
      echo "Dry run only. Re-run with --confirm to delete."
      exit 0
    fi
    while IFS=$'\t' read -r id key _rest; do
      [[ -z "$id" ]] && continue
      gh cache delete "$id" --repo "$REPO"
      echo "Deleted cache id=$id key=$key"
    done <<< "$ROWS"
    ;;
  delete-before)
    FILTERED="$(
      printf '%s\n' "$ROWS" | awk -F '\t' -v before="$BEFORE" -v field="$FIELD" '
        NF == 0 { next }
        {
          ts = (field == "createdAt") ? $4 : $5
          if (ts != "" && ts < before) print
        }
      '
    )"
    print_rows "$FILTERED"
    MATCHED_COUNT="$(printf '%s\n' "$FILTERED" | awk 'NF > 0 {count++} END {print count+0}')"
    echo "Matched $MATCHED_COUNT cache(s)."
    if [[ "$CONFIRM" -ne 1 ]]; then
      echo "Dry run only. Re-run with --confirm to delete."
      exit 0
    fi
    while IFS=$'\t' read -r id key _rest; do
      [[ -z "$id" ]] && continue
      gh cache delete "$id" --repo "$REPO"
      echo "Deleted cache id=$id key=$key"
    done <<< "$FILTERED"
    ;;
  *)
    echo "Unknown command: $COMMAND" >&2
    usage
    exit 1
    ;;
esac
