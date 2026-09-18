# Publishing Jaguar on GitHub

How to host this project on GitHub so `get-jaguar.sh` works as a
one-line install, and so `docs/index.html` can be served as a real
documentation site. Neither needs anything beyond a normal public repo —
no separate hosting account, no build pipeline required for the basics.

## 1. Create the repository

```sh
cd jaguar
git init
git add -A
git commit -m "Jaguar v2: OOP, static typing, cross-platform"
```

On GitHub: **New repository** → name it (`jaguar` is assumed throughout
this doc and in `get-jaguar.sh`'s default) → **Public** (required for the
plain `curl` install command below to work without a token) → don't
initialize with a README, since you already have one → create.

Then push:

```sh
git remote add origin https://github.com/joemrnice/jaguar.git
git branch -M main
git push -u origin main
```

## 2. Wire up the install one-liner

`get-jaguar.sh` at the repo root has one line to edit:

```sh
JAGUAR_REPO="${JAGUAR_REPO:-joemrnice/jaguar}"
```

Change `joemrnice/jaguar` to your actual `owner/repo` (e.g.
`ada/jaguar`), commit, and push. That's the entire setup — GitHub serves
every file in a public repo at a stable `raw.githubusercontent.com` URL
automatically, no configuration needed:

```
https://raw.githubusercontent.com/joemrnice/jaguar/main/get-jaguar.sh
```

The install command developers run is that URL piped into `bash`:

```sh
curl -fsSL https://raw.githubusercontent.com/joemrnice/jaguar/main/get-jaguar.sh | bash
```

Update the same placeholder in `README.md` and `docs/index.html` to
match (both currently say `YOUR_GITHUB_USERNAME` in the same spot — a
project-wide find-and-replace of `YOUR_GITHUB_USERNAME/jaguar` with your
real `owner/repo` after pushing covers all three).

**Test it before telling anyone else to run it** — from a different
machine/directory than your working copy:

```sh
curl -fsSL https://raw.githubusercontent.com/joemrnice/jaguar/main/get-jaguar.sh | bash
```

If that raw URL 404s, double check the repo is public and the path
(`owner/repo/branch/filename`) matches exactly — a private repo needs a
token and isn't compatible with a bare `curl` one-liner; keep the repo
public, or switch to distributing prebuilt release binaries instead (see
below) if you need it private.

### Pinning a version instead of tracking `main`

Once you cut a release (next section), anyone can install that exact
version instead of whatever's currently on `main`:

```sh
JAGUAR_REF=v0.2.0 curl -fsSL https://raw.githubusercontent.com/joemrnice/jaguar/main/get-jaguar.sh | bash
```

`get-jaguar.sh` clones/downloads `JAGUAR_REF` (a branch **or** a tag —
GitHub's archive URLs treat them the same way), so this works with no
changes to the script itself. Recommended once the project has a first
tagged release, so `main` can keep moving without breaking anyone who
already installed.

## 3. (Optional) cut a GitHub Release

Gives users a changelog and a stable version to pin (`JAGUAR_REF=v0.2.0`
above), and is the more typical distribution point once the project is
past its earliest days:

```sh
git tag v0.2.0
git push origin v0.2.0
```

Then on GitHub: **Releases** → **Draft a new release** → pick the tag →
write release notes → publish. Source archives (`.zip`/`.tar.gz`) are
attached automatically by GitHub for every release; you don't need to
build or upload anything yourself unless you later want to attach
prebuilt binaries too (a separate, bigger undertaking — cross-compiling
or running CI on both Linux and macOS runners — not needed for the
curl-install flow, which always builds from source on the user's own
machine).

## 4. Serve the documentation site with GitHub Pages

`docs/index.html` is a single, self-contained static file — GitHub Pages
can serve it with no build step:

1. Repo **Settings** → **Pages**.
2. **Source**: Deploy from a branch.
3. **Branch**: `main`, folder `/docs`.
4. Save.

GitHub publishes it (usually within a minute or two) at:

```
https://joemrnice.github.io/jaguar/
```

(That's `/docs/index.html` in the repo becoming the site's root `index.html`
— GitHub Pages does that mapping for you when you pick the `/docs` folder
as the source, which is why the file lives at `docs/index.html` rather
than the repo root.)

No CI, no separate hosting account, no cost — this is exactly what
GitHub Pages is for. If you'd rather serve it from the repo root or a
custom domain, GitHub's own Pages documentation covers both; neither
requires changing anything in this project.

## Summary of what to edit after creating the repo

| File | What to change |
|---|---|
| `get-jaguar.sh` | `JAGUAR_REPO` default at the top |
| `README.md` | the `curl \| bash` example in the Install section |
| `docs/index.html` | the install command shown in the hero section |

All three currently say `joemrnice/jaguar` in the matching
spot, so it's one find-and-replace across the project.
