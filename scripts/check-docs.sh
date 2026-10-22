#!/usr/bin/env bash
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"
fail=0

# 1) Markdown permitido fuera de docs/
allowed='^(README|AGENTS|CONTRIBUTING|CHANGELOG|SECURITY|CODE_OF_CONDUCT)\.md$'
while IFS= read -r f; do
  [[ "$f" == docs/* ]] && continue
  [[ "$f" =~ $allowed ]] && continue
  echo "❌ Markdown fuera de docs/: $f"
  fail=1
done < <(git ls-files '*.md' '*.markdown')

# 2) Índice obligatorio
if [ ! -f docs/README.md ]; then
  echo "❌ Falta docs/README.md"
  fail=1
fi

# 3) Enlaces relativos a .md rotos
while IFS= read -r f; do
  [[ "$f" == docs/_archivo/* ]] && continue
  while IFS= read -r link; do
    case "$link" in
      http://*|https://*|mailto:*|/*|'#') continue ;;
    esac
    target="$(dirname "$f")/$link"
    if [ ! -e "$target" ]; then
      echo "❌ Enlace roto: $f -> $link"
      fail=1
    fi
  done < <(grep -oE '\]\([^)]+\.md(#[^)]*)?\)' "$f" 2>/dev/null \
           | sed -E 's/^\]\(([^)#]+).*/\1/')
done < <(git ls-files '*.md')

# 4) Secretos de alta señal
if git grep -n -I -E 'sk-or-v1-|ghp_|github_pat_|AKIA[0-9A-Z]{16}|BEGIN (RSA|OPENSSH|EC) PRIVATE KEY' -- . ':!scripts/check-docs.sh' >/tmp/check-docs-secrets 2>/dev/null; then
  echo "❌ Posible secreto en el repositorio:"
  cat /tmp/check-docs-secrets
  fail=1
fi

exit "$fail"
