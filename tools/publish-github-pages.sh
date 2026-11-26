#!/bin/bash
# 把 MYDOS 放上 GitHub Pages。
#   ./tools/publish-github-pages.sh [repo 名稱]     預設 mydos-online
#
# 會做的事：
#   1. 確認有 GitHub CLI（gh）並已登入，沒有就安裝／帶你登入
#   2. 建立一個「公開」的 GitHub repo（GitHub Pages 免費版需要公開 repo）
#   3. 把 Pages 的來源設成 GitHub Actions
#   4. push 上去 → .github/workflows/pages.yml 自動建置並部署
set -euo pipefail
REPO="${1:-mydos-online}"
cd "$(dirname "$0")/.."

if ! command -v gh >/dev/null; then
    echo ">>> Installing GitHub CLI (type your Ubuntu password if asked)"
    sudo apt-get update && sudo apt-get install -y gh
fi
if ! gh auth status >/dev/null 2>&1; then
    echo ">>> Log in to GitHub: follow the instructions below (a code + a web page)"
    gh auth login --web --git-protocol https
fi
gh auth setup-git >/dev/null 2>&1 || true
OWNER=$(gh api user --jq .login)

git branch -M main
if ! git remote get-url origin >/dev/null 2>&1; then
    echo ">>> Creating public repo $OWNER/$REPO"
    gh repo create "$REPO" --public --source=. --remote=origin \
        --description "MYDOS - a DOS-style OS written from scratch, running in your browser"
fi

echo ">>> Turning on GitHub Pages (source: GitHub Actions)"
gh api -X POST "repos/$OWNER/$REPO/pages" -f build_type=workflow >/dev/null 2>&1 ||
gh api -X PUT  "repos/$OWNER/$REPO/pages" -f build_type=workflow >/dev/null 2>&1 || true

echo ">>> Pushing"
git push -u origin main

echo
echo ">>> Building and deploying (about 2-3 minutes)..."
sleep 8
RUN_ID=$(gh run list --workflow pages.yml --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || true)
if [ -n "$RUN_ID" ]; then gh run watch "$RUN_ID" --exit-status || true; fi

echo
echo "=============================================================="
echo "  MYDOS is online:  https://$OWNER.github.io/$REPO/"
echo "  (first load downloads ~40 MB of QEMU; please use Chrome/Edge)"
echo "=============================================================="
