# Git hooks

Подключить hooks проекта (pre-push запускает `ci-local.sh` перед каждым пушем):

```bash
git config core.hooksPath .githooks
```

Отключить на один push: `git push --no-verify`.
См. также `.github/CI-CHECKLIST_RU.md` (копия вне репо) — чеклист перед выпуском.
