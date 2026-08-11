# Synastra

Landing site for [synastra.castalia.institute](https://synastra.castalia.institute) — relational awareness for modern households, from [Castalia Institute](https://castalia.institute).

## Site

Static pages live in `docs/`. GitHub Actions deploys to GitHub Pages on push to `main`.

## DNS (Cloudflare)

`synastra.castalia.institute` → `castaliainstitute.github.io` (DNS only, not proxied).

```bash
export CLOUDFLARE_API_TOKEN='...'   # Zone DNS Edit + Zone Read
./scripts/cf-dns-synastra-github-pages.sh
```

Or run the **Sync synastra DNS** workflow in GitHub Actions (uses repo/org `CLOUDFLARE_API_TOKEN` secret).

After DNS propagates, confirm the custom domain under **Settings → Pages** in this repository.
